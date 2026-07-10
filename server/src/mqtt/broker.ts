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
      } else {
        const err = new Error('Authentication failed: invalid credentials') as AuthenticateError;
        callback(err, false);
      }
    } catch (err) {
      callback(err as AuthenticateError, false);
    }
  },

  // Authorize publish - only allow devices to publish to their own response topic
  authorizePublish: (client: Client | null, packet: PublishPacket, callback: (error?: Error | null) => void) => {
    if (client && packet.topic) {
      const deviceId = client.id || '';
      const respTopicPattern = new RegExp(`^passkey/${escapeRegex(deviceId)}/resp$`);
      if (respTopicPattern.test(packet.topic)) {
        callback(null);
        return;
      }
    }
    // Deny all other publishes
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
