import { Router, Request, Response } from 'express';
import { parseSms } from '../sms-parser';
import { getDatabase } from '../db/database';
import { publishToDevice } from '../mqtt/broker';
import { SmsForwardMessage } from '../mqtt/types';

const router = Router();

/**
 * POST /api/sms/forward - 接收手机转发的短信
 */
router.post('/forward', (req: Request, res: Response) => {
  const { deviceId, sender, content } = req.body;

  if (!deviceId || !sender || !content) {
    res.status(400).json({ error: 'deviceId, sender, and content are required' });
    return;
  }

  const db = getDatabase();

  // Verify device exists
  const device = db.prepare('SELECT device_id FROM devices WHERE device_id = ?').get(deviceId);
  if (!device) {
    res.status(404).json({ error: 'Device not found' });
    return;
  }

  // Parse SMS
  const parseResult = parseSms(sender, content);

  // Forward to device via MQTT
  const smsMessage: SmsForwardMessage = {
    type: 'sms_forward',
    sender,
    content,
    parsedCode: parseResult.code,
    codeType: parseResult.codeType,
    timestamp: Date.now(),
  };

  publishToDevice(deviceId, smsMessage);

  res.json({
    parsed: parseResult.parsed,
    code: parseResult.code,
    codeType: parseResult.codeType,
  });
});

export default router;
