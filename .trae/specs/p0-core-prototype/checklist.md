# P0 Core Prototype - Verification Checklist

## Task 1: Project scaffolding
- [x] Arduino project compiles without errors
- [x] ST7789 display initializes and shows test pattern
- [x] Button GPIOs are correctly mapped and respond to press
- [x] config.h contains all necessary pin definitions and MQTT settings

## Task 2: Display manager and UI
- [x] DisplayManager can push/pop screens correctly
- [x] ButtonManager detects UP/DOWN/CONFIRM with debounce
- [x] Menu list renders account names with scroll highlight
- [x] Progress bar component renders correctly

## Task 3: 4G module + MQTT
- [x] Air780ep responds to AT commands via UART
- [x] Device establishes TCP connection to MQTT broker
- [x] Device subscribes to device-specific MQTT topic
- [x] Device publishes messages to server-bound topic
- [x] Auto-reconnect works after connection drop

## Task 4: TOTP engine and display
- [x] Base32 decoding produces correct byte output (test with known seed)
- [x] HMAC-SHA1 produces correct result (test with RFC 4223 test vectors)
- [x] TOTP 6-digit code matches Google Authenticator output for the same seed
- [x] Account list shows all stored accounts
- [x] Selecting an account shows its TOTP code with remaining seconds
- [x] Code refreshes automatically at 30-second boundary
- [x] Seeds are encrypted before storing to Flash

## Task 5: Remote login confirmation
- [x] Device generates ECDSA key pair on first boot
- [x] NTP time sync works and is accurate to within 5 seconds
- [x] Receiving auth_request MQTT message triggers confirmation dialog
- [x] Dialog shows website name and source info correctly
- [x] CONFIRM sends signed challenge back via MQTT
- [x] DENY sends denial message back via MQTT
- [x] Dialog auto-dismisses after 60 seconds with timeout status

## Task 6: SMS notification display
- [x] Receiving sms_forward MQTT message shows notification screen
- [x] Screen displays sender, code type, and code number
- [x] Notification auto-dismisses after 15 seconds
- [x] Pressing any button dismisses notification early
- [x] Unrecognized SMS shows sender only (no content leak)
- [x] Vibration motor activates on SMS notification

## Task 7: Power management
- [x] Device enters standby after 30 seconds of inactivity
- [x] Device enters deep sleep after 5 minutes of inactivity
- [x] Any button wakes device from standby
- [x] Any button wakes device from deep sleep
- [x] After deep sleep wake, 4G reconnects and time syncs

## Task 8: Server backend
- [x] MQTT broker is running and accepts device connections
- [x] Device registration API works (device gets unique topic)
- [x] Challenge generation endpoint produces unique, time-limited challenges
- [x] SMS forwarding API accepts POST with SMS content
- [x] Regex parser correctly extracts:
  - [x] Verification codes (4-8 digits after "验证码"/"动态码")
  - [x] Pickup codes (digit combinations after "取件码")
  - [x] Transaction codes (digits after "确认码"/"交易码")
- [x] Unrecognized SMS falls through to sender-only display
- [x] End-to-end auth flow: web → challenge → device → sign → verify → response
