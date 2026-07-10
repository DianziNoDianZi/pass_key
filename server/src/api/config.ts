import { Router, Request, Response } from 'express';
import { getDatabase } from '../db/database';
import { sendConfigUpdate } from '../mqtt/handler';

const router = Router();

/**
 * GET /api/config/:deviceId - Get device configuration
 */
router.get('/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  // Ensure config row exists
  db.prepare('INSERT OR IGNORE INTO device_config (device_id) VALUES (?)').run(deviceId);

  const config = db.prepare('SELECT * FROM device_config WHERE device_id = ?').get(deviceId) as any;
  if (!config) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  res.json({
    deviceId: config.device_id,
    standbyTimeout: config.standby_timeout,
    deepSleepTimeout: config.deep_sleep_timeout,
    vibrationEnabled: config.vibration_enabled === 1,
    screenBrightness: config.screen_brightness,
  });
});

/**
 * PUT /api/config/:deviceId - Update device configuration
 */
router.put('/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const db = getDatabase();

  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  // Validate fields
  const updates: string[] = [];
  const params: any[] = [];

  if (req.body.standbyTimeout !== undefined) {
    const val = parseInt(req.body.standbyTimeout);
    if (val < 5 || val > 600) { res.status(400).json({ error: 'standbyTimeout must be 5-600' }); return; }
    updates.push('standby_timeout = ?');
    params.push(val);
  }

  if (req.body.deepSleepTimeout !== undefined) {
    const val = parseInt(req.body.deepSleepTimeout);
    if (val < 60 || val > 3600) { res.status(400).json({ error: 'deepSleepTimeout must be 60-3600' }); return; }
    updates.push('deep_sleep_timeout = ?');
    params.push(val);
  }

  if (req.body.vibrationEnabled !== undefined) {
    updates.push('vibration_enabled = ?');
    params.push(req.body.vibrationEnabled ? 1 : 0);
  }

  if (req.body.screenBrightness !== undefined) {
    const val = parseInt(req.body.screenBrightness);
    if (val < 0 || val > 255) { res.status(400).json({ error: 'screenBrightness must be 0-255' }); return; }
    updates.push('screen_brightness = ?');
    params.push(val);
  }

  if (updates.length === 0) {
    res.status(400).json({ error: 'No valid fields to update' });
    return;
  }

  updates.push("updated_at = datetime('now')");
  params.push(deviceId);

  db.prepare(`UPDATE device_config SET ${updates.join(', ')} WHERE device_id = ?`).run(...params);

  res.json({ success: true });
});

/**
 * POST /api/config/:deviceId/push - Push config to device via MQTT
 */
router.post('/:deviceId/push', (req: Request, res: Response) => {
  const deviceId = req.params.deviceId as string;
  sendConfigUpdate(deviceId);
  res.json({ success: true });
});

export default router;
