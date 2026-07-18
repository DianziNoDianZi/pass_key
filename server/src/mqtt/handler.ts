import { AedesPublishPacket } from 'aedes';
import { broker } from './broker';
import { AuthResponseMessage, MqttMessage, TotpSyncResponse, ConfigUpdateResponse } from './types';
import { getDatabase } from '../db/database';
import { publishToDevice } from './broker';

export function setupMessageHandler(): void {
  // Subscribe to all device response topics
  // broker.on('publish') 和 broker.subscribe 都会收到设备的消息，
  // 为避免消息重复处理，统一使用 broker.subscribe 方式。
  broker.subscribe('passkey/+/resp', (packet: AedesPublishPacket, cb: () => void) => {
    try {
      const payload = packet.payload.toString();
      const message: MqttMessage = JSON.parse(payload);

      console.log(`[MQTT Handler] Received message on ${packet.topic}:`, (message as any).type);

      switch (message.type) {
        case 'auth_response':
          handleAuthResponse(message as AuthResponseMessage);
          break;
        case 'totp_sync_ack':
          handleTotpSyncAck(message as TotpSyncResponse);
          break;
        case 'config_update_ack':
          handleConfigUpdateAck(message as ConfigUpdateResponse);
          break;
        case 'device_register':
          handleDeviceRegister(packet.topic, message as { publicKey?: string });
          break;
        default:
          console.log(`[MQTT Handler] Unknown message type: ${(message as any).type}`);
      }
    } catch (err) {
      console.error('[MQTT Handler] Error processing message:', err);
    }
    cb();
  }, () => {
    console.log('[MQTT Handler] Subscribed to passkey/+/resp');
  });
}

async function handleAuthResponse(message: AuthResponseMessage): Promise<void> {
  const db = getDatabase();

  // Update challenge status
  const result = db.prepare(`
    UPDATE challenges
    SET status = ?, signature = ?, responded_at = datetime('now')
    WHERE request_id = ? AND status = 'pending'
  `).run(message.status, message.signature || null, message.requestId);

  if (result.changes === 0) {
    console.warn(`[MQTT Handler] No pending challenge found for requestId: ${message.requestId}`);
    return;
  }

  console.log(`[MQTT Handler] Challenge ${message.requestId} updated to status: ${message.status}`);

  // If there's a callback URL, send notification
  const challenge = db.prepare('SELECT callback_url, device_id FROM challenges WHERE request_id = ?')
    .get(message.requestId) as { callback_url: string | null; device_id: string } | undefined;

  if (challenge && challenge.callback_url) {
    await notifyCallback(challenge.callback_url, {
      requestId: message.requestId,
      status: message.status,
      signature: message.signature,
      publicKey: message.publicKey,
    });
  }
}

function handleTotpSyncAck(message: TotpSyncResponse): void {
  console.log(`[MQTT Handler] TOTP sync acknowledged: ${message.accountCount} accounts, status: ${message.status}`);
}

function handleConfigUpdateAck(message: ConfigUpdateResponse): void {
  console.log(`[MQTT Handler] Config update acknowledged, status: ${message.status}`);
}

/**
 * Send TOTP sync to a device
 */
export function sendTotpSync(deviceId: string): void {
  const db = getDatabase();
  const accounts = db.prepare(
    'SELECT id, issuer, account_name, encrypted_seed FROM totp_accounts WHERE device_id = ? ORDER BY created_at ASC'
  ).all(deviceId) as Array<{ id: number; issuer: string; account_name: string | null; encrypted_seed: string }>;

  // Decrypt seeds
  const key = require('crypto').createHash('sha256').update('passkey-server-totp-key').digest();
  const decrypted = accounts.map(a => {
    const parts = a.encrypted_seed.split(':');
    const iv = Buffer.from(parts[0], 'hex');
    const encryptedData = parts[1];
    const decipher = require('crypto').createDecipheriv('aes-256-cbc', key, iv);
    let secret = decipher.update(encryptedData, 'hex', 'utf8');
    secret += decipher.final('utf8');
    return {
      id: a.id,
      issuer: a.issuer,
      accountName: a.account_name || a.issuer,
      secret,
    };
  });

  publishToDevice(deviceId, {
    type: 'totp_sync',
    accounts: decrypted,
    timestamp: Date.now(),
  });
}

/**
 * Send config update to a device
 */
export function sendConfigUpdate(deviceId: string): void {
  const db = getDatabase();
  const config = db.prepare('SELECT * FROM device_config WHERE device_id = ?').get(deviceId) as any;
  if (!config) return;

  publishToDevice(deviceId, {
    type: 'config_update',
    config: {
      standbyTimeout: config.standby_timeout,
      deepSleepTimeout: config.deep_sleep_timeout,
      vibrationEnabled: config.vibration_enabled === 1,
      screenBrightness: config.screen_brightness,
      fido2Enabled: config.fido2_enabled === 1,
      fido2BleName: config.fido2_ble_name || 'PassKey',
    },
    timestamp: Date.now(),
  });
}

async function notifyCallback(callbackUrl: string, data: Record<string, any>): Promise<void> {
  try {
    // Use fetch if available (Node 18+), otherwise fallback to http/https
    if (typeof fetch === 'function') {
      const response = await fetch(callbackUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
      });
      if (!response.ok) {
        console.warn(`[MQTT Handler] Callback ${callbackUrl} returned ${response.status}`);
      }
    } else {
      const http = require(callbackUrl.startsWith('https') ? 'https' : 'http');
      return new Promise<void>((resolve, reject) => {
        const postData = JSON.stringify(data);
        const urlObj = new URL(callbackUrl);
        const options = {
          hostname: urlObj.hostname,
          port: urlObj.port,
          path: urlObj.pathname + urlObj.search,
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(postData),
          },
        };
        const req = http.request(options, (res: any) => {
          if (res.statusCode && res.statusCode >= 200 && res.statusCode < 300) {
            resolve();
          } else {
            reject(new Error(`Callback returned ${res.statusCode}`));
          }
        });
        req.on('error', reject);
        req.write(postData);
        req.end();
      });
    }
  } catch (err) {
    console.error(`[MQTT Handler] Failed to notify callback ${callbackUrl}:`, (err as Error).message);
  }
}

/**
 * Handle device registration message from device
 * A device sends its public key after MQTT connection is established
 */
function handleDeviceRegister(topic: string, message: { publicKey?: string }): void {
  if (!message.publicKey) {
    console.warn('[MQTT Handler] device_register missing publicKey');
    return;
  }

  // Extract deviceId from topic: passkey/{deviceId}/resp
  const parts = topic.split('/');
  if (parts.length < 2) return;
  const deviceId = parts[1];

  const db = getDatabase();
  const existing = db.prepare('SELECT public_key FROM devices WHERE device_id = ?').get(deviceId) as { public_key: string | null } | undefined;

  if (existing) {
    if (!existing.public_key) {
      // 首次注册：保存公钥
      db.prepare('UPDATE devices SET public_key = ? WHERE device_id = ?').run(message.publicKey, deviceId);
      console.log(`[MQTT Handler] Device ${deviceId} public key registered (len=${message.publicKey.length})`);
    } else if (existing.public_key !== message.publicKey) {
      // 公钥变更：客户端可能重置了密钥，允许覆盖
      console.log(`[MQTT Handler] Device ${deviceId} public key UPDATED (old differs from new)`);
      db.prepare('UPDATE devices SET public_key = ? WHERE device_id = ?').run(message.publicKey, deviceId);
    } else {
      console.log(`[MQTT Handler] Device ${deviceId} already has this public key, skipping`);
    }
  } else {
    console.warn(`[MQTT Handler] Device ${deviceId} not found in database. Add device via admin panel first.`);
  }
}
