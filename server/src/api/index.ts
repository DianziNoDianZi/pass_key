import { Router } from 'express';
import deviceRouter from './device';
import authRouter from './auth';
import smsRouter from './sms';
import { getDatabase } from '../db/database';

const router = Router();

// Device management
router.use('/device', deviceRouter);

// Auth challenges
router.use('/auth', authRouter);

// SMS forwarding
router.use('/sms', smsRouter);

// List all devices (separate from /auth/devices)
router.get('/devices', (_req, res) => {
  const db = getDatabase();
  const devices = db.prepare('SELECT device_id, name, public_key, created_at FROM devices ORDER BY created_at DESC').all();
  res.json({ devices });
});

// List all challenges
router.get('/challenges', (req, res) => {
  const db = getDatabase();
  const limit = Math.min(parseInt(req.query.limit as string) || 50, 200);
  const challenges = db.prepare(`
    SELECT request_id, device_id, website, source, status, signature, created_at
    FROM challenges ORDER BY created_at DESC LIMIT ?
  `).all(limit);
  res.json({ challenges });
});

// Weather proxy (placeholder - requires HeFeng API key)
router.get('/weather/:deviceId', (_req, res) => {
  res.json({ error: 'Weather API key not configured' });
});

export default router;
