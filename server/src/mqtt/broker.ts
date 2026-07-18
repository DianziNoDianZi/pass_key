import Aedes, { AedesOptions, Client, PublishPacket, Subscription, AuthenticateError } from 'aedes';
import { createServer } from 'net';
import { config } from '../config';
import { getDatabase } from '../db/database';
import { MqttMessage } from './types';

const options: AedesOptions = {
  // Pre-shared key authentication
  authenticate: (client: Client, username: Readonly<string | undefined>, password: Readonly<Buffer | undefined>, callback: (error: AuthenticateError | null, success: boolean | null) => void) => {
    if (!username || !password) {
      const err = new Error('Authentication failed: missing credentials') as AuthenticateError;
      callback(err, false);
      return;
    }

    const deviceId = username.toString();
    const psk = password.toString();

    try {
      const db = getDatabase();
      const device = db.prepare('SELECT psk FROM devices WHERE device_id = ?').get(deviceId) as { psk: string } | undefined;

      if (device && device.psk === psk) {
        callback(null, true);
      } else if (!device) {
        // 设备不存在时自动创建（首次连接自动注册）
        db.prepare('INSERT INTO devices (device_id, name, psk) VALUES (?, ?, ?)').run(deviceId, deviceId, psk);
        // 同时创建设备配置默认值
        db.prepare('INSERT OR IGNORE INTO device_config (device_id) VALUES (?)').run(deviceId);
        console.log(`[MQTT Broker] 设备自动注册: ${deviceId}`);
        callback(null, true);
      } else {
        const err = new Error('Authentication failed: invalid credentials') as AuthenticateError;
        callback(err, false);
      }
    } catch (err) {
      callback(err as AuthenticateError, false);
    }
  },

  // Authorize publish
  // - Server-internal publishes (client=null) are always allowed
  // - Devices may only publish to their own response topic: passkey/{deviceId}/resp
  authorizePublish: (client: Client | null, packet: PublishPacket, callback: (error?: Error | null) => void) => {
    // Allow server-internal publishes (from publishToDevice, sendTotpSync, etc.)
    if (!client) {
      callback(null);
      return;
    }
    if (packet.topic) {
      const deviceId = client.id || '';
      const respTopicPattern = new RegExp(`^passkey/${escapeRegex(deviceId)}/resp$`);
      if (respTopicPattern.test(packet.topic)) {
        callback(null);
        return;
      }
    }
    // Deny all other publishes from clients
    console.warn(`[MQTT Broker] Denied publish from=${client?.id} topic=${packet.topic}`);
    callback(new Error('Not authorized to publish to this topic'));
  },

  // Authorize subscribe - only allow devices to subscribe to their own command topic
  authorizeSubscribe: (client: Client, subscription: Subscription, callback: (error: Error | null, subscription?: Subscription | null) => void) => {
    if (client) {
      const deviceId = client.id || '';
      const cmdTopicPattern = new RegExp(`^passkey/${escapeRegex(deviceId)}/cmd$`);
      const respTopicPattern = new RegExp(`^passkey/${escapeRegex(deviceId)}/resp$`);

      if (cmdTopicPattern.test(subscription.topic) || respTopicPattern.test(subscription.topic)) {
        callback(null, subscription);
        return;
      }
    }
    callback(new Error('Not authorized to subscribe to this topic'));
  },
};

const broker = new Aedes(options);

// 用 Map 记录客户端连接时间
const clientConnectTimes = new Map<string, number>();

// ===== 诊断事件监听 =====
broker.on('client', (client: Client) => {
  console.log(`[MQTT Broker] Client connected: ${client.id}`);
  clientConnectTimes.set(client.id, Date.now());

  // 启用 TCP keepalive（使用系统默认延迟，Linux 默认 2 小时）
  // 设备每 3 秒发送应用层心跳，实际已足够维持 NAT 映射。
  // TCP keepalive 作为兜底机制，避免极端情况下的死连接。
  try {
    const conn = (client as any).conn;
    if (conn && typeof conn.setKeepAlive === 'function') {
      conn.setKeepAlive(true);
    }
  } catch (err) {
    // setKeepAlive 不是关键功能，失败不影响运行
  }
});
broker.on('clientReady', () => {
  // MQTT keepalive 超时由设备端的 MQTT_KEEPALIVE 值决定（已设为 65535，约 27 小时）。
  // 设备每 3 秒发送应用层心跳 PUBLISH，确保 _lastIncomingActivity 持续更新。
  // 因此 Aedes 的 keepalive 检查永远不会触发超时断开。
  // 无需操作 Aedes 内部定时器。
});
broker.on('clientDisconnect', (client: Client) => {
  // 计算连接时长
  const startTime = clientConnectTimes.get(client.id);
  let duration = '';
  if (startTime) {
    const seconds = Math.round((Date.now() - startTime) / 1000);
    duration = ` (持续 ${seconds}s)`;
    clientConnectTimes.delete(client.id);
  }
  console.log(`[MQTT Broker] Client disconnected: ${client.id}${duration}`);
});
broker.on('publish', (packet: PublishPacket, client: Client | null) => {
  // 不记录心跳，避免刷屏
  if (packet.topic && !packet.topic.startsWith('$SYS/')) {
    console.log(`[MQTT Broker] Publish from=${client?.id || '(server)'} topic=${packet.topic} len=${packet.payload?.length || 0}`);
  }
});
// =========================

function escapeRegex(str: string): string {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// Create TCP server
const tcpServer = createServer(broker.handle);

// Track connection state
let isRunning = false;

export async function startBroker(): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    if (isRunning) {
      resolve();
      return;
    }

    tcpServer.listen(config.mqttTcpPort, config.host, () => {
      isRunning = true;
      console.log(`[MQTT Broker] TCP listening on ${config.host}:${config.mqttTcpPort}`);
      resolve();
    });

    tcpServer.on('error', (err: Error) => {
      console.error('[MQTT Broker] TCP server error:', err);
      reject(err);
    });
  });
}

export async function stopBroker(): Promise<void> {
  return new Promise<void>((resolve) => {
    if (!isRunning) {
      resolve();
      return;
    }

    tcpServer.close(() => {
      isRunning = false;
      console.log('[MQTT Broker] stopped');
      resolve();
    });

    broker.close(() => {
      console.log('[MQTT Broker] aedes engine closed');
    });
  });
}

/**
 * Publish a message to a specific device's command topic
 */
export function publishToDevice(deviceId: string, message: MqttMessage): void {
  const topic = `passkey/${deviceId}/cmd`;
  const payload = JSON.stringify(message);

  broker.publish({
    topic,
    payload: Buffer.from(payload),
    qos: 1,
    retain: false,
  } as PublishPacket, (err?: Error) => {
    if (err) {
      console.error(`[MQTT Broker] Failed to publish to ${topic}:`, err.message);
    } else {
      console.log(`[MQTT Broker] Published to ${topic}:`, message.type);
    }
  });
}

export { broker };
