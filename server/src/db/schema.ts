export const SCHEMA_SQL = `
CREATE TABLE IF NOT EXISTS devices (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id TEXT UNIQUE NOT NULL,
  name TEXT,
  psk TEXT NOT NULL,
  public_key TEXT,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS challenges (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  request_id TEXT UNIQUE NOT NULL,
  device_id TEXT NOT NULL,
  website TEXT NOT NULL,
  source TEXT,
  challenge TEXT NOT NULL,
  status TEXT DEFAULT 'pending',
  signature TEXT,
  callback_url TEXT,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  responded_at DATETIME
);

CREATE INDEX IF NOT EXISTS idx_devices_device_id ON devices(device_id);
CREATE INDEX IF NOT EXISTS idx_challenges_request_id ON challenges(request_id);
CREATE INDEX IF NOT EXISTS idx_challenges_device_id ON challenges(device_id);
`;
