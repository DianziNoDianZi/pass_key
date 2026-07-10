import { Router, Request, Response } from 'express';
import path from 'path';
import fs from 'fs';

const router = Router();

// Serve admin panel HTML
router.get('/', (_req: Request, res: Response) => {
  const htmlPath = path.join(__dirname, 'index.html');
  if (fs.existsSync(htmlPath)) {
    res.sendFile(htmlPath);
  } else {
    // Fallback for dev mode (tsx)
    const devPath = path.join(__dirname, '../../src/admin/index.html');
    res.sendFile(devPath);
  }
});

export default router;
