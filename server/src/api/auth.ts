import { Router, Request, Response } from 'express';
import crypto from 'crypto';
import { getDatabase } from '../db/database';
import { config } from '../config';
import { publishToDevice } from '../mqtt/broker';
import { AuthRequestMessage } from '../mqtt/types';

const router = Router();

/**
 * POST /api/auth/challenge - 创建登录挑战
 */
router.post('/challenge', (req: Request, res: Response) => {
  const { deviceId, website, source, callbackUrl } = req.body;

  if (!deviceId || !website) {
    res.status(400).json({ error: 'deviceId and website are required' });
    return;
  }

  const db = getDatabase();

  // Verify device exists
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  // Generate unique requestId and challenge
  const requestId = crypto.randomUUID();
  const challenge = crypto.randomBytes(32).toString('hex');
  const expiresAt = new Date(Date.now() + config.challengeTimeout * 1000).toISOString();

  try {
    db.prepare(`
      INSERT INTO challenges (request_id, device_id, website, source, challenge, callback_url, expires_at)
      VALUES (?, ?, ?, ?, ?, ?, ?)
    `).run(requestId, deviceId, website, source || null, challenge, callbackUrl || null, expiresAt);

    // Publish auth request to device via MQTT
    const authMessage: AuthRequestMessage = {
      type: 'auth_request',
      requestId,
      website,
      source: source || undefined,
      challenge,
      expiresAt,
      timestamp: Date.now(),
    };

    publishToDevice(deviceId, authMessage);

    res.status(201).json({
      requestId,
      expiresAt,
    });
  } catch (err) {
    console.error('[Auth API] Failed to create challenge:', err);
    res.status(500).json({ error: 'Failed to create challenge' });
  }
});

/**
 * GET /api/auth/status/:requestId - 查询挑战状态
 */
router.get('/status/:requestId', (req: Request, res: Response) => {
  const { requestId } = req.params;
  const db = getDatabase();

  const challenge = db.prepare(`
    SELECT status, signature, device_id FROM challenges WHERE request_id = ?
  `).get(requestId) as any;

  if (!challenge) {
    res.status(404).json({ error: 'Challenge not found' });
    return;
  }

  // Check timeout for pending challenges
  if (challenge.status === 'pending') {
    const challengeRow = db.prepare('SELECT expires_at FROM challenges WHERE request_id = ?')
      .get(requestId) as { expires_at: string };

    if (new Date(challengeRow.expires_at) < new Date()) {
      // Auto-timeout
      db.prepare("UPDATE challenges SET status = 'timeout', responded_at = datetime('now') WHERE request_id = ? AND status = 'pending'")
        .run(requestId);
      challenge.status = 'timeout';
    }
  }

  const response: any = {
    status: challenge.status,
  };

  if (challenge.signature) {
    response.signature = challenge.signature;
  }

  // Get device's public key if available
  if (challenge.device_id) {
    const device = db.prepare('SELECT public_key FROM devices WHERE device_id = ?')
      .get(challenge.device_id) as { public_key: string | null } | undefined;
    if (device && device.public_key) {
      response.publicKey = device.public_key;
    }
  }

  res.json(response);
});

/**
 * POST /api/auth/verify - 验证签名
 */
router.post('/verify', (req: Request, res: Response) => {
  const { requestId, signature, publicKey } = req.body;

  if (!requestId || !signature || !publicKey) {
    res.status(400).json({ error: 'requestId, signature, and publicKey are required' });
    return;
  }

  const db = getDatabase();

  const challenge = db.prepare(`
    SELECT challenge, status FROM challenges WHERE request_id = ?
  `).get(requestId) as any;

  if (!challenge) {
    res.status(404).json({ error: 'Challenge not found' });
    return;
  }

  if (challenge.status !== 'approved') {
    res.json({ valid: false, reason: `Challenge status is '${challenge.status}', not 'approved'` });
    return;
  }

  try {
    // Verify signature using the public key
    const { challenge: originalChallenge } = challenge;
    const verifier = crypto.createVerify('SHA256');
    verifier.update(Buffer.from(originalChallenge, 'utf-8'));
    verifier.end();

    // 将 Base64 编码的 DER 公钥转为 Node.js verify 可识别的格式
    const publicKeyBuf = Buffer.from(publicKey, 'base64');
    const isValid = verifier.verify(
      { key: publicKeyBuf, format: 'der', type: 'spki' },
      signature,
      'base64'
    );

    // If valid, store the public key with the device
    if (isValid) {
      const challengeRow = db.prepare('SELECT device_id FROM challenges WHERE request_id = ?')
        .get(requestId) as { device_id: string };
      db.prepare('UPDATE devices SET public_key = ? WHERE device_id = ? AND public_key IS NULL')
        .run(publicKey, challengeRow.device_id);
    }

    res.json({ valid: isValid });
  } catch (err) {
    console.error('[Auth API] Signature verification error:', err);
    res.json({ valid: false, reason: 'Verification failed' });
  }
});

/**
 * GET /api/devices - 列出所有设备
 */
router.get('/devices', (req, res) => {
  // This route is mounted at /api/auth/devices for convenience
  // The /api/devices endpoint is separately mounted in the main router
  const db = getDatabase();
  const devices = db.prepare('SELECT device_id, name, public_key, created_at FROM devices ORDER BY created_at DESC').all();
  res.json({ devices });
});

export default router;
