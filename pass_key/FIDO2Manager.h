/**
 * @file FIDO2Manager.h
 * @brief FIDO2/CTAP2 over BLE 安全密钥管理器
 *
 * 实现 BLE FIDO2 认证器，支持 WebAuthn 标准。
 * 电脑/手机通过蓝牙发现设备作为安全密钥使用。
 */

#ifndef FIDO2MANAGER_H
#define FIDO2MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <functional>
#include <vector>
#include "CryptoEngine.h"

// FIDO2 BLE 服务 UUID（FIDO Alliance 定义）
#define FIDO2_SERVICE_UUID        "0000FFF0-0000-1000-8000-00805F9B34FB"
#define FIDO2_CONTROL_POINT_UUID  "0000FFF1-0000-1000-8000-00805F9B34FB"
#define FIDO2_STATUS_UUID         "0000FFF2-0000-1000-8000-00805F9B34FB"
#define FIDO2_CONTROL_POINT_LEN   "0000FFF3-0000-1000-8000-00805F9B34FB"
#define FIDO2_SERVICE_REVISION    "0000FFF4-0000-1000-8000-00805F9B34FB"
#define FIDO2_AA_GUID             "0000FFF5-0000-1000-8000-00805F9B34FB"

// CTAP2 命令码
enum CTAP2Command : uint8_t {
    CTAP_MAKE_CREDENTIAL  = 0x01,
    CTAP_GET_ASSERTION    = 0x02,
    CTAP_GET_INFO         = 0x04,
    CTAP_CLIENT_PIN       = 0x06,
    CTAP_RESET            = 0x07,
    CTAP_NEXT_ASSERTION   = 0x08,
    CTAP_CANCEL           = 0x09,
    CTAP_ERROR            = 0x3F
};

// CTAP2 状态码
enum CTAP2Status : uint8_t {
    CTAP2_OK                     = 0x00,
    CTAP1_ERR_INVALID_COMMAND    = 0x01,
    CTAP1_ERR_INVALID_PARAMETER  = 0x02,
    CTAP1_ERR_INVALID_LENGTH     = 0x03,
    CTAP1_ERR_INVALID_SEQ        = 0x04,
    CTAP1_ERR_TIMEOUT            = 0x05,
    CTAP1_ERR_OTHER              = 0x7F,
    CTAP2_ERR_CBOR_UNEXPECTED_TYPE = 0x11,
    CTAP2_ERR_CREDENTIAL_EXCLUDED   = 0x19,
    CTAP2_ERR_PUAT_REQUIRED      = 0x24,
    CTAP2_ERR_PIN_REQUIRED       = 0x25,
    CTAP2_ERR_OPERATION_DENIED   = 0x27,
    CTAP2_ERR_KEEPALIVE_CANCEL   = 0x2D,
    CTAP2_ERR_NO_CREDENTIALS     = 0x2E,
    CTAP2_ERR_USER_ACTION_PENDING = 0x2F
};

// FIDO2 设备配置
struct FIDO2Config {
    bool enabled = true;             // BLE FIDO2 功能开关
    char deviceName[32] = "PassKey"; // BLE 广播名称
    bool usePIN = false;             // 是否启用 PIN 保护
};

// 凭证存储结构
struct FIDO2Credential {
    uint8_t credentialID[64];    // 凭证 ID (SHA256 hash of public key)
    uint8_t privateKey[32];      // ECDSA P-256 私钥
    uint8_t publicKey[64];       // ECDSA P-256 公钥 (X || Y)
    uint8_t rpIDHash[32];        // RP ID SHA256 hash
    char rpID[128];              // 依赖方 ID (例如 "webauthn.io")
    char userName[128];          // 用户名
    uint8_t userID[32];          // 用户 ID
    uint8_t userIDLen;           // 用户 ID 长度
    uint16_t rpIDHashLen_full;   // 实际上总是 32
    bool isValid;                // 是否有效
};

// 认证器数据（authenticatorData）
struct AuthData {
    uint8_t rpIDHash[32];
    uint8_t flags;
    uint32_t counter;
    // 以下仅在 makeCredential 的响应中包含
    uint8_t aaguid[16];
    uint16_t credentialIDLen;
    uint8_t credentialID[64];
    uint8_t credentialPublicKey[64];
};

// 回调类型
typedef std::function<void()> UserPresenceCallback;

class FIDO2Manager {
public:
    FIDO2Manager();
    ~FIDO2Manager();

    // 初始化
    bool init(CryptoEngine *crypto);

    // 启动/停止 BLE 广播
    void start();
    void stop();

    // 更新循环（处理待处理操作）
    void update();

    // 配置
    void setConfig(const FIDO2Config &cfg);
    FIDO2Config getConfig() const { return _cfg; }

    // 设置用户存在回调
    void setUserPresenceCallback(UserPresenceCallback cb) { _userPresenceCb = cb; }

    // 确认用户存在（从 BTN_CONFIRM 回调调用）
    void confirmUserPresence(bool approved);

    // 检查是否有待处理的用户操作
    bool isUserActionPending() const { return _userActionPending; }

    // 清除所有凭证
    void resetCredentials();

    // 电源管理
    void prepareSleep();
    void wakeFromSleep();
    void setEnabled(bool en);

private:
    // BLE 回调
    class FIDOServerCallbacks : public BLEServerCallbacks {
    public:
        FIDO2Manager *parent;
        FIDOServerCallbacks(FIDO2Manager *p) : parent(p) {}
        void onConnect(BLEServer *pServer) override;
        void onDisconnect(BLEServer *pServer) override;
    };

    class FIDOCharacteristicCallbacks : public BLECharacteristicCallbacks {
    public:
        FIDO2Manager *parent;
        FIDOCharacteristicCallbacks(FIDO2Manager *p) : parent(p) {}
        void onWrite(BLECharacteristic *pCharacteristic) override;
    };

    // CTAP2 命令处理
    void handleCTAP2Command(const uint8_t *data, size_t len);
    void handleGetInfo();
    void handleMakeCredential(const uint8_t *cborData, size_t cborLen);
    void handleGetAssertion(const uint8_t *cborData, size_t cborLen);
    void handleReset();
    void handleClientPIN(const uint8_t *cborData, size_t cborLen);

    // 发送响应
    void sendResponse(uint8_t cmd, const uint8_t *payload, size_t payloadLen);
    void sendError(uint8_t cmd, uint8_t status);
    void sendKeepAlive(uint8_t status);

    // CBOR 辅助（最小实现）
    size_t encodeCBORHeader(uint8_t *buf, size_t bufSize, uint8_t majorType, uint64_t value);
    size_t encodeCBORInt(uint8_t *buf, size_t bufSize, int64_t value);
    size_t encodeCBORByteString(uint8_t *buf, size_t bufSize, const uint8_t *data, size_t dataLen);
    size_t encodeCBORTextString(uint8_t *buf, size_t bufSize, const char *str);
    size_t encodeCBORArray(uint8_t *buf, size_t bufSize, size_t count);
    size_t encodeCBORMap(uint8_t *buf, size_t bufSize, size_t count);
    size_t encodeCBORBool(uint8_t *buf, size_t bufSize, bool value);
    size_t encodeCBORNull(uint8_t *buf, size_t bufSize);

    // CBOR 解析辅助
    bool decodeCBORHeader(const uint8_t *data, size_t len, size_t &offset, uint8_t &majorType, uint64_t &value);
    bool decodeCBORByteString(const uint8_t *data, size_t len, size_t &offset, const uint8_t *&out, size_t &outLen);
    bool decodeCBORTextString(const uint8_t *data, size_t len, size_t &offset, char *out, size_t outSize);
    bool decodeCBORInt(const uint8_t *data, size_t len, size_t &offset, int64_t &value);
    bool skipCBORValue(const uint8_t *data, size_t len, size_t &offset);

    // 构建认证器数据
    size_t buildAuthDataMakeCred(const uint8_t *rpIDHash, const uint8_t *credID, uint16_t credIDLen,
                                  const uint8_t *pubKeyX, const uint8_t *pubKeyY,
                                  uint8_t *out, size_t outLen);
    size_t buildAuthDataGetAssert(const uint8_t *rpIDHash, uint8_t *out, size_t outLen);
    size_t encodeCredentialPublicKey(uint8_t *buf, size_t bufSize, const uint8_t *pubKeyX, const uint8_t *pubKeyY);

    // 凭证存储
    bool storeCredential(const FIDO2Credential &cred);
    bool findCredential(const uint8_t *rpIDHash, const uint8_t *credID, uint16_t credIDLen, FIDO2Credential &out);
    bool findCredentialsByRP(const uint8_t *rpIDHash, std::vector<FIDO2Credential> &out);
    int loadAllCredentials(std::vector<FIDO2Credential> &out);

    // 内部状态
    FIDO2Config _cfg;
    CryptoEngine *_crypto = nullptr;
    BLEServer *_server = nullptr;
    BLEService *_service = nullptr;
    BLECharacteristic *_controlPoint = nullptr;
    BLECharacteristic *_status = nullptr;
    BLECharacteristic *_controlPointLen = nullptr;
    BLECharacteristic *_serviceRevision = nullptr;
    bool _connected = false;
    bool _initialized = false;

    // 用户存在管理
    bool _userActionPending = false;
    bool _userActionApproved = false;
    unsigned long _userActionTimeout = 0;
    UserPresenceCallback _userPresenceCb = nullptr;

    // 待处理命令缓冲区
    uint8_t _pendingCmdBuffer[1024];
    size_t _pendingCmdLen = 0;
    uint8_t _pendingCmdCode = 0;

    // 凭证计数
    uint16_t _credentialCount = 0;
    uint16_t _credentialMax = 16;
    uint32_t _signCounter = 0;

    // 设备 UUID (AAGUID)
    uint8_t _aaguid[16] = {
        0xF1, 0xD0, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    };
};

#endif // FIDO2MANAGER_H
