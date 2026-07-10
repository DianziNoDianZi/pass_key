import { Router, Request, Response } from 'express';
import crypto from 'crypto';
import { getDatabase } from '../db/database';

const router = Router();

/**
 * POST /api/device/register - 注册新设备
 */
router.post('/register', (req: Request, res: Response) => {
  const { deviceId, name } = req.body;

  if (!deviceId || typeof deviceId !== 'string') {
    res.status(400).json({ error: 'deviceId is required and must be a string' });
    return;
  }

  const db = getDatabase();

  // Check if device already exists
  const existing = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (existing) {
    res.status(409).json({ error: 'Device already exists' });
    return;
  }

  // Generate PSK
  const psk = crypto.randomBytes(32).toString('hex');
  const topic = `passkey/${deviceId}/cmd`;

  try {
    db.prepare('INSERT INTO devices (device_id, name, psk) VALUES (?, ?, ?)')
      .run(deviceId, name || null, psk);

    res.status(201).json({
      deviceId,
      psk,
      topic,
    });
  } catch (err) {
    console.error('[Device API] Failed to register device:', err);
    res.status(500).json({ error: 'Failed to register device' });
  }
});

/**
 * GET /api/device/:deviceId - 获取设备信息
 */
router.get('/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  const device = db.prepare('SELECT device_id, name, public_key, created_at FROM devices WHERE device_id = ?')
    .get(deviceId) as any;

  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  res.json({
    deviceId: device.device_id,
    name: device.name,
    publicKey: device.public_key,
    createdAt: device.created_at,
  });
});

export default router;
