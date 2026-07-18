import { Router, Request, Response } from 'express';
import { getDatabase } from '../db/database';
import { publishToDevice } from '../mqtt/broker';
import { EarthquakeAlertMessage } from '../mqtt/types';

const router = Router();

/**
 * POST /api/earthquake/alert - 接收地震预警并推送至设备
 *
 * Body:
 *   deviceId    - 目标设备 ID
 *   epicenter   - 震中位置名称（拼音/英文，如 "Luding"）
 *   magnitude   - 震级（如 6.8）
 *   intensity   - 本地预估烈度（如 "V" 或 "V-degree"）
 *   countdown   - 地震波到达倒计时（秒）
 *   depth       - 震源深度（公里，可选）
 *
 * 设备端收到后会全屏红底白字闪烁报警，
 * 蜂鸣器长鸣 + 震动马达持续震动。
 */
router.post('/alert', (req: Request, res: Response) => {
  const { deviceId, epicenter, magnitude, intensity, countdown, depth } = req.body;

  if (!deviceId || !epicenter || magnitude === undefined || !intensity || countdown === undefined) {
    res.status(400).json({
      error: 'deviceId, epicenter, magnitude, intensity, and countdown are required',
    });
    return;
  }

  // Validate reasonable values
  if (typeof magnitude !== 'number' || magnitude < 0 || magnitude > 10) {
    res.status(400).json({ error: 'magnitude must be a number between 0 and 10' });
    return;
  }
  if (typeof countdown !== 'number' || countdown < 0 || countdown > 300) {
    res.status(400).json({ error: 'countdown must be between 0 and 300 seconds' });
    return;
  }

  const db = getDatabase();

  // Verify device exists
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId) as
    { device_id: string } | undefined;
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  // Log to database
  db.prepare(`
    INSERT INTO earthquake_events (device_id, epicenter, magnitude, intensity, countdown, depth)
    VALUES (?, ?, ?, ?, ?, ?)
  `).run(deviceId, epicenter, magnitude, intensity, countdown, depth || null);

  // Push to device via MQTT (QoS 1, highest priority)
  const alertMessage: EarthquakeAlertMessage = {
    type: 'earthquake_alert',
    epicenter,
    magnitude,
    intensity,
    countdown,
    depth: depth || undefined,
    timestamp: Date.now(),
  };

  publishToDevice(deviceId, alertMessage);

  res.json({
    success: true,
    message: `Earthquake alert pushed to ${deviceId}`,
  });
});

/**
 * GET /api/earthquake/history/:deviceId - 查询设备的地震预警历史
 */
router.get('/history/:deviceId', (req: Request, res: Response) => {
  const { deviceId } = req.params;
  const limit = Math.min(parseInt(req.query.limit as string) || 20, 100);

  const db = getDatabase();
  const events = db.prepare(`
    SELECT id, epicenter, magnitude, intensity, countdown, depth, created_at
    FROM earthquake_events
    WHERE device_id = ?
    ORDER BY created_at DESC
    LIMIT ?
  `).all(deviceId, limit);

  res.json({ events });
});

export default router;
