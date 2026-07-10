# Tasks - P0 Core Prototype

## Development Dependencies Map
```
Task 1 (Project Init) ─┬─ Task 2 (Screen Driver) ──┬─ Task 4 (TOTP Display)
                        │                           └─ Task 5 (Auth Screen)
                        │                           └─ Task 6 (SMS Screen)
                        ├─ Task 3 (4G + MQTT) ──────┼─ Task 5 (MQTT receive)
                        │                           └─ Task 6 (MQTT receive)
                        └─ Task 7 (Power Mgmt) ─────┴─ depends on Task 2
Task 8 (Server) ────────┴─ MQTT broker + REST API + SMS parser (parallelizable within)
```

---

- [ ] **Task 1: Project scaffolding and hardware configuration**
  - Initialize Arduino project structure
  - Define pin mappings (display SPI, UART for Air780ep, buttons, buzzer, vibrator)
  - Set up TFT_eSPI library configuration for ST7789 240x240
  - Create config.h with all pin definitions and MQTT broker settings
  - Implement basic display test (fill screen, draw text)

- [ ] **Task 2: Display manager and UI framework**
  - Implement DisplayManager with screen navigation stack
  - Implement ButtonManager for UP/DOWN/CONFIRM input handling (debounce, long-press detection)
  - Create screen base class and screen transition system
  - Draw menu list UI with scrollable items
  - Draw progress bar component (for TOTP countdown)

- [ ] **Task 3: 4G module (Air780ep) driver and MQTT connectivity**
  - Implement Air780ep UART AT command driver
  - Implement TCP/SSL connection establishment
  - Implement MQTT client (pub/sub) over AT-command-based TCP
  - Implement auto-reconnect with exponential backoff
  - Implement MQTT message parsing and dispatch callbacks

- [ ] **Task 4: TOTP engine and display**
  - Implement Base32 decoding for TOTP seeds
  - Implement HMAC-SHA1 computation (use Arduino Crypto library or mbedTLS)
  - Implement TOTP code generation per RFC 6238 (30-second window)
  - Implement TOTPAccount storage (encrypted seed + account name in Flash)
  - Implement secure seed storage with master-key encryption (AES-GCM)
  - TOTPScreen: account list view and code display view
  - Auto-refresh countdown display

- [ ] **Task 5: Remote login confirmation flow**
  - Implement TimeManager (NTP sync over 4G)
  - Generate ECDSA key pair on first boot (using mbedTLS)
  - Implement challenge signature flow (sign with device private key)
  - AuthScreen: confirmation dialog with source info display
  - MQTT message handling for auth_request and auth_response
  - Auto-dismiss after 60s timeout

- [ ] **Task 6: SMS notification display**
  - SMSNotificationScreen: parsed SMS display with auto-dismiss (15s)
  - MQTT message handling for sms_forward messages
  - Vibration pattern for SMS notification
  - Fallback display for unrecognized SMS

- [ ] **Task 7: Power management**
  - Implement standby mode (30s idle → dimmed clock display)
  - Implement deep sleep mode (5min idle → ESP32 deep sleep)
  - Configure Air780ep low-power modes
  - Wake-on-button from deep sleep with full re-initialization

- [ ] **Task 8: Server backend**
  - Set up MQTT broker (EMQX or Mosquitto)
  - Implement device registration and authentication API
  - Implement challenge generation endpoint for auth flow
  - Implement SMS forwarding REST API endpoint
  - Implement SMS content regex parser
  - Implement device-to-server MQTT message routing

---

## Task Dependencies
- Task 4 depends on: Task 1, Task 2
- Task 5 depends on: Task 1, Task 2, Task 3
- Task 6 depends on: Task 1, Task 2, Task 3
- Task 7 depends on: Task 1, Task 2
- Task 8 has internal sub-tasks that can be parallelized
