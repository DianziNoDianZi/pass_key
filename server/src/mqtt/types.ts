export interface AuthRequestMessage {
  type: 'auth_request';
  requestId: string;
  website: string;
  source?: string;
  challenge: string;
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
  };
  timestamp: number;
}

export interface ConfigUpdateResponse {
  type: 'config_update_ack';
  status: 'ok' | 'error';
  timestamp: number;
}

export type MqttMessage = AuthRequestMessage | AuthResponseMessage | SmsForwardMessage
  | TotpSyncCommand | TotpAddCommand | TotpDeleteCommand | TotpSyncResponse
  | ConfigUpdateCommand | ConfigUpdateResponse;

export interface DeviceAuth {
  deviceId: string;
  psk: string;
}
