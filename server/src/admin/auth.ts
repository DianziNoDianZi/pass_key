import crypto from 'crypto';
import { Request, Response, NextFunction } from 'express';
import { config } from '../config';

// In-memory token store: token -> expiry timestamp
const tokenStore = new Map<string, number>();

// Clean up expired tokens every hour
setInterval(() => {
  const now = Date.now();
  for (const [token, expiry] of tokenStore.entries()) {
    if (now > expiry) tokenStore.delete(token);
  }
}, 3600_000);

function generateToken(password: string): string {
  const expiry = Date.now() + 24 * 3600_000; // 24h
  const payload = `${password}|${expiry}`;
  const hmac = crypto.createHmac('sha256', 'passkey-admin-secret').update(payload).digest('hex');
  const token = Buffer.from(`${hmac}|${expiry}`).toString('base64');
  tokenStore.set(token, expiry);
  return token;
}

function validateToken(token: string): boolean {
  const stored = tokenStore.get(token);
  if (!stored) return false;
  if (Date.now() > stored) {
    tokenStore.delete(token);
    return false;
  }
  return true;
}

// POST /api/admin/login
export function loginHandler(req: Request, res: Response): void {
  const { password } = req.body;
  if (!password || password !== config.adminPassword) {
    res.status(401).json({ error: '密码错误' });
    return;
  }
  const token = generateToken(password);
  res.json({ token });
}

// Middleware: verify token from Authorization header
export function authMiddleware(req: Request, res: Response, next: NextFunction): void {
  // Only protect /api routes when called from admin panel
  // The token is optional for device API calls, required for admin UI calls
  const auth = req.headers.authorization;
  if (!auth || !auth.startsWith('Bearer ')) {
    res.status(401).json({ error: '未登录，请先登录管理面板' });
    return;
  }
  const token = auth.slice(7);
  if (!validateToken(token)) {
    res.status(401).json({ error: '登录已过期，请重新登录' });
    return;
  }
  next();
}

export { validateToken };
