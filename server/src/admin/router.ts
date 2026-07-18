import { Router, Request, Response } from 'express';
import path from 'path';
import fs from 'fs';
import { loginHandler, authMiddleware } from './auth';
import { getDatabase } from '../db/database';
import consoleRouter from './console';

const router = Router();

// Login endpoint (no auth required)
router.post('/login', loginHandler);

// Protected data routes
const dataRouter = Router();
dataRouter.use(authMiddleware);

dataRouter.get('/devices', (_req: Request, res: Response) => {
  const db = getDatabase();
  const devices = db.prepare('SELECT device_id, name, public_key, created_at FROM devices ORDER BY created_at DESC').all();
  res.json({ devices });
});

dataRouter.get('/challenges', (req: Request, res: Response) => {
  const db = getDatabase();
  const limit = Math.min(parseInt(req.query.limit as string) || 50, 200);
  const challenges = db.prepare(`
    SELECT request_id, device_id, website, source, status, signature, created_at
    FROM challenges ORDER BY created_at DESC LIMIT ?
  `).all(limit);
  res.json({ challenges });
});

dataRouter.get('/stats', (_req: Request, res: Response) => {
  const db = getDatabase();
  const deviceCount = (db.prepare('SELECT COUNT(*) as count FROM devices').get() as any).count;
  const pubkeyCount = (db.prepare('SELECT COUNT(*) as count FROM devices WHERE public_key IS NOT NULL').get() as any).count;
  const todayChals = (db.prepare("SELECT COUNT(*) as count FROM challenges WHERE date(created_at) = date('now')").get() as any).count;
  res.json({ devices: deviceCount, pubkeys: pubkeyCount, todayChallenges: todayChals });
});

router.use('/data', dataRouter);

// 控制台（MQTT 命令发送 + SSE 实时流）
router.use('/console', consoleRouter);

// Serve admin panel HTML
router.get('/', (_req: Request, res: Response) => {
  const htmlPath = path.join(__dirname, 'index.html');
  if (fs.existsSync(htmlPath)) {
    res.sendFile(htmlPath);
  } else {
    const devPath = path.join(__dirname, '../../src/admin/index.html');
    res.sendFile(devPath);
  }
});

export default router;
