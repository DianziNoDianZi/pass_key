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

CREATE TABLE IF NOT EXISTS totp_accounts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id TEXT NOT NULL,
  issuer TEXT NOT NULL,
  account_name TEXT,
  encrypted_seed TEXT NOT NULL,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (device_id) REFERENCES devices(device_id)
);

CREATE TABLE IF NOT EXISTS device_config (
  device_id TEXT PRIMARY KEY,
  standby_timeout INTEGER DEFAULT 30,
  deep_sleep_timeout INTEGER DEFAULT 300,
  vibration_enabled INTEGER DEFAULT 1,
  screen_brightness INTEGER DEFAULT 255,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (device_id) REFERENCES devices(device_id)
);

CREATE INDEX IF NOT EXISTS idx_totp_device_id ON totp_accounts(device_id);
`;
