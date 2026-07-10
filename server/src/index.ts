import express from 'express';
import cors from 'cors';
import { config } from './config';
import { getDatabase } from './db/database';
import { startBroker, stopBroker } from './mqtt/broker';
import { setupMessageHandler } from './mqtt/handler';
import apiRouter from './api/index';

const app = express();

// Middleware
app.use(cors());
app.use(express.json());

// Health check
app.get('/health', (_req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// API routes
app.use('/api', apiRouter);

async function main(): Promise<void> {
  try {
    // Initialize database
    console.log('[Server] Initializing database...');
    getDatabase(config.dbPath);

    // Start MQTT Broker
    console.log('[Server] Starting MQTT Broker...');
    await startBroker();

    // Setup MQTT message handler
    setupMessageHandler();

    // Start HTTP server
    app.listen(config.httpPort, config.host, () => {
      console.log(`[Server] HTTP server listening on ${config.host}:${config.httpPort}`);
      console.log(`[Server] MQTT Broker TCP on ${config.host}:${config.mqttTcpPort}`);
      console.log(`[Server] Ready`);
    });
  } catch (err) {
    console.error('[Server] Failed to start:', err);
    process.exit(1);
  }
}

// Graceful shutdown
process.on('SIGINT', async () => {
  console.log('\n[Server] Shutting down...');
  await stopBroker();
  process.exit(0);
});

process.on('SIGTERM', async () => {
  console.log('\n[Server] Shutting down...');
  await stopBroker();
  process.exit(0);
});

main();
