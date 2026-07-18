export interface AuthRequestMessage {
  type: 'auth_request';
  requestId: string;
  website: string;
  source?: string;
  challenge: string;
  expiresAt: string;   // ISO 时间戳，发送给设备端的超时时间
  timestamp: number;
}

export interface AuthResponseMessage {
  type: 'auth_response';
  requestId: string;
  status: 'approved' | 'denied' | 'timeout';
  signature?: string;
  publicKey?: string;
  timestamp: number;
}

export interface SmsForwardMessage {
  type: 'sms_forward';
  sender: string;
  content: string;
  parsedCode?: string;
  codeType?: string;
  timestamp: number;
}

// TOTP management messages
export interface TotpSyncCommand {
  type: 'totp_sync';
  accounts: Array<{
    id: number;
    issuer: string;
    accountName: string;
    secret: string;
  }>;
  timestamp: number;
}

export interface TotpAddCommand {
  type: 'totp_add';
  account: {
    id: number;
    issuer: string;
    accountName: string;
    secret: string;
  };
  timestamp: number;
}

export interface TotpDeleteCommand {
  type: 'totp_delete';
  accountId: number;
  timestamp: number;
}

export interface TotpSyncResponse {
  type: 'totp_sync_ack';
  status: 'ok' | 'error';
  accountCount: number;
  timestamp: number;
}

// Device configuration messages
export interface ConfigUpdateCommand {
  type: 'config_update';
  config: {
    standbyTimeout?: number;
    deepSleepTimeout?: number;
    vibrationEnabled?: boolean;
    screenBrightness?: number;
    fido2Enabled?: boolean;
    fido2BleName?: string;
  };
  timestamp: number;
}

export interface ConfigUpdateResponse {
  type: 'config_update_ack';
  status: 'ok' | 'error';
  timestamp: number;
}

export interface DeviceRegisterMessage {
  type: 'device_register';
  publicKey: string;
}

export interface EarthquakeAlertMessage {
  type: 'earthquake_alert';
  epicenter: string;       // 震中位置名称（拼音/英文，如 "Luding"）
  magnitude: number;        // 震级（如 6.8）
  intensity: string;        // 本地预估烈度（如 "V度"）
  countdown: number;        // 地震波到达倒计时（秒）
  depth?: number;           // 震源深度（公里）
  timestamp: number;
}

export interface HeartbeatMessage {
  type: 'heartbeat';
  t: number;                // 时间戳（millis）
}

export type MqttMessage = AuthRequestMessage | AuthResponseMessage | SmsForwardMessage
  | TotpSyncCommand | TotpAddCommand | TotpDeleteCommand | TotpSyncResponse
  | ConfigUpdateCommand | ConfigUpdateResponse | DeviceRegisterMessage
  | EarthquakeAlertMessage | HeartbeatMessage;

export interface DeviceAuth {
  deviceId: string;
  psk: string;
}
