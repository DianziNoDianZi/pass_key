import { Router, Request, Response } from 'express';
import crypto from 'crypto';
import { getDatabase } from '../db/database';
import { sendTotpSync } from '../mqtt/handler';

const router = Router();

/**
 * Parse otpauth:// URI to extract TOTP parameters
 */
function parseOtpAuthUri(uri: string): { issuer: string; accountName: string; secret: string } | null {
  try {
    const url = new URL(uri);
    if (url.protocol !== 'otpauth:' || url.hostname !== 'totp') return null;

    const issuerFromPath = url.pathname.replace(/^\//, '').split(':');
    const accountName = issuerFromPath.length > 1 ? issuerFromPath.slice(1).join(':') : issuerFromPath[0];
    const issuerFromPathOnly = issuerFromPath[0];

    const secret = url.searchParams.get('secret');
    const issuer = url.searchParams.get('issuer') || issuerFromPathOnly;

    if (!secret) return null;
    return { issuer, accountName, secret: secret.toUpperCase() };
  } catch {
    return null;
  }
}

/**
 * Encrypt TOTP seed using server-side key
 */
function encryptSeed(seed: string): string {
  // Use a server-side encryption key (in production, use a proper key management)
  const key = crypto.createHash('sha256').update('passkey-server-totp-key').digest();
  const iv = crypto.randomBytes(16);
  const cipher = crypto.createCipheriv('aes-256-cbc', key, iv);
  let encrypted = cipher.update(seed, 'utf8', 'hex');
  encrypted += cipher.final('hex');
  return iv.toString('hex') + ':' + encrypted;
}

function decryptSeed(encrypted: string): string {
  const key = crypto.createHash('sha256').update('passkey-server-totp-key').digest();
  const parts = encrypted.split(':');
  const iv = Buffer.from(parts[0], 'hex');
  const encryptedData = parts[1];
  const decipher = crypto.createDecipheriv('aes-256-cbc', key, iv);
  let decrypted = decipher.update(encryptedData, 'hex', 'utf8');
  decrypted += decipher.final('utf8');
  return decrypted;
}

/**
 * GET /api/totp/:deviceId - List all TOTP accounts for a device
 */
router.get('/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  const accounts = db.prepare(
    'SELECT id, issuer, account_name, created_at FROM totp_accounts WHERE device_id = ? ORDER BY created_at ASC'
  ).all(deviceId);

  res.json({ accounts });
});

/**
 * POST /api/totp/:deviceId - Add a new TOTP account
 * Body: { issuer, account_name, secret } or { uri: "otpauth://..." }
 */
router.post('/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  let issuer: string;
  let accountName: string;
  let secret: string;

  // Support both direct fields and URI import
  if (req.body.uri) {
    const parsed = parseOtpAuthUri(req.body.uri);
    if (!parsed) {
      res.status(400).json({ error: '无效的 otpauth:// URI' });
      return;
    }
    // URI 解析值作为默认值，允许客户端覆盖
    issuer = req.body.issuer || parsed.issuer;
    accountName = req.body.account_name || parsed.accountName;
    secret = parsed.secret;
  } else {
    issuer = req.body.issuer || '';
    accountName = req.body.account_name || '';
    secret = req.body.secret || '';

    if (!issuer || !secret) {
      res.status(400).json({ error: 'issuer and secret are required' });
      return;
    }
    // Clean up secret: remove spaces, uppercase
    secret = secret.replace(/\s/g, '').toUpperCase();
  }

  // Validate Base32
  if (!/^[A-Z2-7]+=*$/.test(secret)) {
    res.status(400).json({ error: 'secret 不是有效的 Base32 编码' });
    return;
  }

  try {
    const encryptedSeed = encryptSeed(secret);
    const result = db.prepare(
      'INSERT INTO totp_accounts (device_id, issuer, account_name, encrypted_seed) VALUES (?, ?, ?, ?)'
    ).run(deviceId, issuer, accountName || null, encryptedSeed);

    const accountId = result.lastInsertRowid;

    // Return the added account info (without the encrypted seed)
    res.status(201).json({
      id: accountId,
      issuer,
      accountName: accountName || issuer,
    });

    // Push to device via MQTT if connected
    const deviceIdStr = deviceId as string;
    sendTotpSync(deviceIdStr);
  } catch (err) {
    console.error('[TOTP API] Failed to add account:', err);
    res.status(500).json({ error: 'Failed to add account' });
  }
});

/**
 * DELETE /api/totp/:deviceId/:accountId - Delete a TOTP account
 */
router.delete('/:deviceId/:accountId', (req: Request, res: Response) => {
  const { deviceId, accountId } = req.params;
  const db = getDatabase();

  const result = db.prepare(
    'DELETE FROM totp_accounts WHERE id = ? AND device_id = ?'
  ).run(accountId, deviceId);

  if (result.changes === 0) {
    res.status(404).json({ error: 'Account not found' });
    return;
  }

  res.json({ success: true });

  // Push update to device
  sendTotpSync(deviceId as string);
});

/**
 * POST /api/totp/:deviceId/sync - Get all TOTP accounts with decrypted seeds (for MQTT sync)
 */
router.post('/:deviceId/sync', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  const accounts = db.prepare(
    'SELECT id, issuer, account_name, encrypted_seed FROM totp_accounts WHERE device_id = ? ORDER BY created_at ASC'
  ).all(deviceId) as Array<{ id: number; issuer: string; account_name: string | null; encrypted_seed: string }>;

  // Decrypt seeds for sending to device
  const decrypted = accounts.map(a => ({
    id: a.id,
    issuer: a.issuer,
    accountName: a.account_name || a.issuer,
    secret: decryptSeed(a.encrypted_seed),
  }));

  res.json({ accounts: decrypted, pushed: true });

  // 同步到设备
  sendTotpSync(deviceId as string);
});

export default router;
