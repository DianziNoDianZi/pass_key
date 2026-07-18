import { Router, Request, Response } from 'express';
import { authMiddleware, validateToken } from './auth';
import { broker } from '../mqtt/broker';
import { AedesPublishPacket } from 'aedes';

const router = Router();
router.use(authMiddleware);

// ===== SSE 客户端管理 =====
interface SseClient {
  id: number;
  res: Response;
}

let sseClients: SseClient[] = [];
let sseNextId = 1;

/**
 * 向所有 SSE 客户端广播消息
 */
function broadcastToSse(topic: string, payload: string): void {
  const data = JSON.stringify({ topic, payload, time: Date.now() });
  for (const client of sseClients) {
    try {
      client.res.write(`data: ${data}\n\n`);
    } catch {
      // 客户端断开，下次清理
    }
  }
  // 清理断开的客户端
  sseClients = sseClients.filter(c => {
    try {
      return !c.res.destroyed;
    } catch {
      return false;
    }
  });
}

// ===== 订阅 MQTT 设备响应 =====
let consoleSubscribed = false;

function ensureSseSubscription(): void {
  if (consoleSubscribed) return;
  consoleSubscribed = true;

  broker.subscribe('passkey/+/resp', (packet: AedesPublishPacket, cb: () => void) => {
    try {
      const topic = packet.topic || '';
      const payload = packet.payload?.toString() || '';

      // 转发到所有 SSE 客户端
      if (sseClients.length > 0) {
        broadcastToSse(topic, payload);
      }
    } catch (err) {
      console.error('[Console] SSE broadcast error:', (err as Error).message);
    }
    cb();
  }, () => {
    console.log('[Console] Subscribed to passkey/+/resp for SSE streaming');
  });
}

// ===== POST /send — 发送命令到设备 =====
router.post('/send', (req: Request, res: Response) => {
  const { deviceId, topic, payload } = req.body;

  if (!deviceId) {
    res.status(400).json({ error: '缺少 deviceId' });
    return;
  }
  if (!payload) {
    res.status(400).json({ error: '缺少 payload' });
    return;
  }

  // 确保 SSE 订阅已启动
  ensureSseSubscription();

  // 确定目标主题
  const targetTopic = topic || `passkey/${deviceId}/cmd`;

  // 验证 payload 是否为有效 JSON
  let payloadStr: string;
  if (typeof payload === 'object') {
    payloadStr = JSON.stringify(payload);
  } else if (typeof payload === 'string') {
    // 尝试解析 JSON 字符串以确保格式正确
    try {
      JSON.parse(payload);
      payloadStr = payload;
    } catch {
      payloadStr = payload; // 纯文本，原样发送
    }
  } else {
    payloadStr = String(payload);
  }

  // 使用 broker 发送消息
  broker.publish({
    topic: targetTopic,
    payload: Buffer.from(payloadStr),
    qos: 1,
    retain: false,
  } as AedesPublishPacket, (err?: Error | null) => {
    if (err) {
      console.error(`[Console] 发送到 ${targetTopic} 失败:`, err.message);
      res.status(500).json({ error: `发送失败: ${err.message}` });
    } else {
      console.log(`[Console] 已发送到 ${targetTopic}:`, payloadStr.substring(0, 80));
      res.json({ success: true, topic: targetTopic, payload: payloadStr });
    }
  });
});

// ===== GET /stream — SSE 实时响应流 =====
router.get('/stream', (req: Request, res: Response) => {
  // 通过 query 参数验证 token
  const token = req.query.token as string;
  if (!token) {
    res.status(401).json({ error: '缺少 token' });
    return;
  }

  // 手动验证 token
  if (!validateToken(token)) {
    res.status(401).json({ error: 'token 无效或已过期' });
    return;
  }

  // 确保 SSE 订阅已启动
  ensureSseSubscription();

  // 设置 SSE 头
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
    'X-Accel-Buffering': 'no',
  });

  // 发送初始连接确认
  res.write(`data: ${JSON.stringify({ type: 'connected', time: Date.now() })}\n\n`);

  // 注册客户端
  const clientId = sseNextId++;
  const client: SseClient = { id: clientId, res };
  sseClients.push(client);

  console.log(`[Console] SSE client #${clientId} connected (total: ${sseClients.length})`);

  // 客户端断开时清理
  req.on('close', () => {
    sseClients = sseClients.filter(c => c.id !== clientId);
    console.log(`[Console] SSE client #${clientId} disconnected (total: ${sseClients.length})`);
  });
});

export default router;
