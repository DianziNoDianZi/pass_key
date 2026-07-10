import { AedesPublishPacket, Client } from 'aedes';
import { broker } from './broker';
import { AuthResponseMessage, MqttMessage } from './types';
import { getDatabase } from '../db/database';

export function setupMessageHandler(): void {
  // Subscribe to all device response topics using internal handler
  broker.subscribe('passkey/+/resp', (packet: AedesPublishPacket, cb: () => void) => {
    try {
      const payload = packet.payload.toString();
      const message: MqttMessage = JSON.parse(payload);

      console.log(`[MQTT Handler] Received message on ${packet.topic}:`, message.type);

      switch (message.type) {
        case 'auth_response':
          handleAuthResponse(message as AuthResponseMessage);
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

  // Also listen to published events for direct client messages
  broker.on('publish', (packet: AedesPublishPacket, client: Client | null) => {
    // Only process messages from clients (devices), not from the server itself
    if (!client) return;

    const topic = packet.topic;
    if (!topic.startsWith('passkey/')) return;

    try {
      const payload = packet.payload.toString();
      const message: MqttMessage = JSON.parse(payload);

      console.log(`[MQTT Handler] Received message from ${client.id} on ${topic}:`, message.type);

      switch (message.type) {
        case 'auth_response':
          handleAuthResponse(message as AuthResponseMessage);
          break;
        default:
          console.log(`[MQTT Handler] Unknown message type: ${(message as any).type}`);
      }
    } catch (err) {
      console.error('[MQTT Handler] Error processing message:', err);
    }
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
