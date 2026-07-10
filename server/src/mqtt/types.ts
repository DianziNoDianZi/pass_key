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

export type MqttMessage = AuthRequestMessage | AuthResponseMessage | SmsForwardMessage;

export interface DeviceAuth {
  deviceId: string;
  psk: string;
}
