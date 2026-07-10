# P0 Core Prototype Spec

## Why
Build a working hardware prototype that demonstrates the three core value propositions of the PassKey device: TOTP authentication code display, remote login confirmation via MQTT, and SMS verification code forwarding.

## What Changes

### Hardware Configuration
- ESP32-S3-N16R8 as main MCU
- Air780ep 4G module connected via UART (AT commands)
- 1.54" ST7789 SPI TFT display (240x240)
- 3 physical buttons: UP, DOWN, CONFIRM
- Vibration motor
- Buzzer

### New Project Structure
```
pass_key/
├── firmware/                  # Arduino platform firmware
│   ├── pass_key.ino          # Main entry
│   ├── config.h              # Pin definitions, MQTT config
│   ├── display/              # Screen rendering
│   │   ├── DisplayManager.h/cpp
│   │   ├── TOTPScreen.h/cpp
│   │   ├── AuthScreen.h/cpp
│   │   └── SMSNotificationScreen.h/cpp
│   ├── totp/                 # TOTP engine
│   │   ├── TOTPManager.h/cpp
│   │   └── TOTPAccount.h/cpp
│   ├── mqtt/                 # MQTT over 4G
│   │   ├── MQTTManager.h/cpp
│   │   └── Air780epDriver.h/cpp
│   ├── crypto/               # Crypto utilities
│   │   ├── CryptoEngine.h/cpp
│   │   └── SecureStorage.h/cpp
│   └── common/               # Shared utilities
│       ├── ButtonManager.h/cpp
│       ├── PowerManager.h/cpp
│       └── TimeManager.h/cpp
├── server/                   # Cloud server
│   ├── src/
│   │   ├── mqtt/            # MQTT broker integration
│   │   ├── auth/            # Challenge/response auth
│   │   ├── sms-parser/      # SMS regex parsing
│   │   ├── weather/         # Weather API proxy
│   │   └── api/             # REST API for device
│   └── ...
└── .trae/specs/p0-core-prototype/
```

## Impact
- New project initialization (firmware + server)
- Affected specs: N/A (first spec)
- Affected code: N/A (new project)

## Requirements

### Requirement: TOTP Code Display
The device SHALL display 6-digit TOTP codes for configured accounts, refreshing every 30 seconds.

#### Scenario: Browse and select account
- **WHEN** device is in standby mode
- **THEN** screen shows a scrollable list of configured account names (e.g., "Google", "GitHub", "Steam")
- **WHEN** user presses UP/DOWN buttons
- **THEN** list scrolls to highlight the next/previous account
- **WHEN** user presses CONFIRM on an account
- **THEN** screen switches to TOTP code view for that account

#### Scenario: Display TOTP code
- **WHEN** TOTP code view is active for an account
- **THEN** screen displays:
  - Account name (top)
  - 6-digit code (center, large font)
  - Remaining seconds as a progress bar or countdown (bottom)
- **WHEN** 30-second window expires
- **THEN** code refreshes automatically with smooth visual transition
- **WHEN** user presses any button on TOTP view
- **THEN** returns to account list

#### Scenario: TOTP computation
- **GIVEN** a stored Base32 seed and current Unix time
- **WHEN** code is requested
- **THEN** compute HMAC-SHA1(seed, time_counter) per RFC 6238
- **THEN** extract 6-digit code from the HMAC result
- **THEN** display code immediately

#### Scenario: Time synchronization
- **WHEN** device boots up
- **THEN** synchronize time via NTP over 4G
- **WHEN** device has been running for 1 hour
- **THEN** re-synchronize time via NTP

### Requirement: Remote Login Confirmation
The device SHALL receive MQTT push notifications for login challenges and allow user to approve/deny via physical buttons, signing approved challenges with a device-local private key.

#### Scenario: Receive login challenge
- **WHEN** MQTT message arrives with type "auth_request"
- **THEN** device vibrates and displays confirmation dialog
- **THEN** dialog shows:
  - Website name (e.g., "example.com")
  - Request source info (e.g., "Chrome · Chengdu")
  - Two options: [Confirm] [Deny]
- **WHEN** user presses CONFIRM
- **THEN** device signs the challenge string with device ECDSA private key
- **THEN** signed response is sent back via MQTT
- **WHEN** user presses DENY
- **THEN** denial message is sent back via MQTT

#### Scenario: Auto-dismiss
- **WHEN** confirmation dialog is shown for 60 seconds without input
- **THEN** dialog auto-dismisses with "timeout" status
- **THEN** timeout message sent via MQTT

### Requirement: SMS Verification Code Forwarding
The device SHALL display SMS verification codes pushed from the server, parsed from forwarded phone SMS messages.

#### Scenario: Receive SMS notification
- **WHEN** MQTT message arrives with type "sms_forward"
- **THEN** device vibrates briefly
- **THEN** screen displays parsed SMS info for 15 seconds:
  - Sender (e.g., "ICBC", "Cainiao", "Alipay")
  - Code type (e.g., "Confirmation Code", "Pickup Code", "Verification Code")
  - The extracted code number
- **WHEN** 15 seconds elapse OR user presses any button
- **THEN** notification dismissed, return to previous screen

#### Scenario: Unrecognized SMS
- **GIVEN** server cannot parse the SMS content
- **WHEN** SMS notification arrives
- **THEN** display only sender number, NOT full content

### Requirement: Power Management
- **WHEN** no user input for 30 seconds
- **THEN** enter low-power standby mode (display dimmed or off)
- **WHEN** any button is pressed in standby
- **THEN** wake up to main menu
- **WHEN** no user input for 5 minutes and no active notifications
- **THEN** enter deep sleep mode (4G module disconnected)
- **WHEN** button is pressed in deep sleep
- **THEN** wake up, reconnect 4G, sync time, return to main menu

### Requirement: MQTT Communication
The device SHALL maintain a persistent MQTT connection over 4G for receiving commands and sending responses.

#### Scenario: Connect
- **WHEN** device boots
- **THEN** initialize Air780ep, establish TCP/SSL connection to MQTT broker
- **THEN** subscribe to device-specific topic
- **WHEN** connection drops
- **THEN** auto-reconnect with exponential backoff (max 5 min interval)

#### Scenario: Send response
- **WHEN** device needs to send a message
- **THEN** publish to server-bound topic with QoS 1

### Requirement: Server Backend
The server SHALL provide MQTT-based push services, REST API for device management, and SMS parsing logic.

#### Scenario: SMS parsing rules
- **WHEN** server receives SMS text via REST API
- **THEN** apply regex rules:
  - Verification code: match "验证码|动态码", extract 4-8 digit number
  - Pickup code: match "取件码", extract digit combination
  - Transaction code: match "确认码|交易码", extract number
- **WHEN** no rule matches
- **THEN** forward as unrecognized (sender only)
