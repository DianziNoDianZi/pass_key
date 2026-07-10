/**
 * @file FIDO2Manager.cpp
 * @brief FIDO2/CTAP2 over BLE 安全密钥管理器实现
 */

#include "FIDO2Manager.h"
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/error.h>
#include <Preferences.h>
#include <cstring>

// 调试输出宏
#define FIDO_LOG(fmt, ...) Serial.printf("[FIDO2] " fmt "\n", ##__VA_ARGS__)

// Preferences 命名空间
static const char *FIDO_NS = "fido";
static const char *KEY_COUNTER = "counter";
static const char *KEY_CRED_COUNT = "cred_cnt";

// ---- 构造/析构 ----

FIDO2Manager::FIDO2Manager() {}

FIDO2Manager::~FIDO2Manager() {
    stop();
}

// ---- 初始化 ----

bool FIDO2Manager::init(CryptoEngine *crypto) {
    _crypto = crypto;

    Preferences prefs;
    prefs.begin(FIDO_NS, true);
    _signCounter = prefs.getUInt(KEY_COUNTER, 0);
    _credentialCount = prefs.getUInt(KEY_CRED_COUNT, 0);
    prefs.end();

    _initialized = true;
    FIDO_LOG("FIDO2 管理器初始化完成, 凭证数: %d, 签名计数: %u", _credentialCount, _signCounter);
    return true;
}

// ---- 配置 ----

void FIDO2Manager::setConfig(const FIDO2Config &cfg) {
    _cfg = cfg;
    if (_cfg.enabled && _initialized && !_server) {
        start();
    } else if (!_cfg.enabled && _server) {
        stop();
    }
}

// ---- 启动/停止 BLE ----

void FIDO2Manager::start() {
    if (!_initialized || !_cfg.enabled) return;
    if (_server) return;

    FIDO_LOG("启动 BLE FIDO2...");

    // 初始化 BLE 设备
    BLEDevice::init(_cfg.deviceName);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);

    // 创建服务器
    _server = BLEDevice::createServer();
    _server->setCallbacks(new FIDOServerCallbacks(this));

    // 创建 FIDO2 服务
    _service = _server->createService(FIDO2_SERVICE_UUID);

    // Control Point (FFF1) - Write/Indicate
    _controlPoint = _service->createCharacteristic(
        FIDO2_CONTROL_POINT_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE
    );
    _controlPoint->setCallbacks(new FIDOCharacteristicCallbacks(this));
    _controlPoint->addDescriptor(new BLE2902());

    // Status (FFF2) - Notify
    _status = _service->createCharacteristic(
        FIDO2_STATUS_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _status->addDescriptor(new BLE2902());

    // Control Point Length (FFF3) - Read
    _controlPointLen = _service->createCharacteristic(
        FIDO2_CONTROL_POINT_LEN,
        BLECharacteristic::PROPERTY_READ
    );
    uint16_t maxLen = 1024;
    _controlPointLen->setValue((uint8_t *)&maxLen, 2);

    // Service Revision Bitmap (FFF4) - Read
    _serviceRevision = _service->createCharacteristic(
        FIDO2_SERVICE_REVISION,
        BLECharacteristic::PROPERTY_READ
    );
    uint8_t revisionBitmap[] = {0x80, 0x00, 0x00, 0x00}; // BLE service revision 1.0
    _serviceRevision->setValue(revisionBitmap, 4);

    // 启动服务
    _service->start();

    // 广播设置
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->setAppearance(0x0540); // FIDO2 HID appearance
    pAdv->addServiceUUID(FIDO2_SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    pAdv->setMinPreferred(0x12);

    BLEDevice::startAdvertising();

    FIDO_LOG("BLE FIDO2 广播已启动: %s", _cfg.deviceName);
}

void FIDO2Manager::stop() {
    if (_server) {
        BLEDevice::stopAdvertising();
        _server->removeService(_service);
        _server = nullptr;
        _service = nullptr;
        _controlPoint = nullptr;
        _status = nullptr;
        _connected = false;
        FIDO_LOG("BLE FIDO2 已停止");
    }
}

void FIDO2Manager::setEnabled(bool en) {
    _cfg.enabled = en;
    if (en && _initialized) {
        start();
    } else {
        stop();
    }
}

// ---- 电源管理 ----

void FIDO2Manager::prepareSleep() {
    stop();
}

void FIDO2Manager::wakeFromSleep() {
    start();
}

// ---- 主循环 ----

void FIDO2Manager::update() {
    if (_connected && _userActionPending) {
        if (millis() >= _userActionTimeout) {
            FIDO_LOG("用户操作超时");
            _userActionPending = false;
            sendError(_pendingCmdCode, CTAP2_ERR_USER_ACTION_PENDING);
        }
    }
}

// ---- 用户存在处理 ----

void FIDO2Manager::confirmUserPresence(bool approved) {
    if (!_userActionPending) return;
    _userActionApproved = approved;
    _userActionPending = false;

    if (!approved) {
        sendError(_pendingCmdCode, CTAP2_ERR_OPERATION_DENIED);
        FIDO_LOG("用户拒绝了操作");
        return;
    }

    FIDO_LOG("用户确认操作");

    // 处理待处理命令
    switch (_pendingCmdCode) {
        case CTAP_MAKE_CREDENTIAL:
            handleMakeCredential(_pendingCmdBuffer, _pendingCmdLen);
            break;
        case CTAP_GET_ASSERTION:
            handleGetAssertion(_pendingCmdBuffer, _pendingCmdLen);
            break;
        case CTAP_RESET:
            handleReset();
            break;
        default:
            break;
    }
}

// ---- BLE 回调 ----

void FIDO2Manager::FIDOServerCallbacks::onConnect(BLEServer *pServer) {
    parent->_connected = true;
    BLEDevice::stopAdvertising();
    FIDO_LOG("BLE 已连接");
}

void FIDO2Manager::FIDOServerCallbacks::onDisconnect(BLEServer *pServer) {
    parent->_connected = false;
    parent->_userActionPending = false;
    BLEDevice::startAdvertising();
    FIDO_LOG("BLE 已断开, 恢复广播");
}

void FIDO2Manager::FIDOCharacteristicCallbacks::onWrite(BLECharacteristic *pChar) {
    if (!parent) return;
    std::string value = pChar->getValue();
    if (value.empty()) return;
    parent->handleCTAP2Command((const uint8_t *)value.data(), value.length());
}

// ---- CTAP2 命令分发 ----

void FIDO2Manager::handleCTAP2Command(const uint8_t *data, size_t len) {
    if (len < 1) return;

    uint8_t cmd = data[0];
    FIDO_LOG("收到 CTAP2 命令: 0x%02X, 长度: %d", cmd, len - 1);

    switch (cmd) {
        case CTAP_GET_INFO:
            handleGetInfo();
            break;
        case CTAP_MAKE_CREDENTIAL:
            if (len > 1) {
                // 需要用户确认
                _pendingCmdCode = CTAP_MAKE_CREDENTIAL;
                _pendingCmdLen = len - 1;
                memcpy(_pendingCmdBuffer, data + 1, len - 1);
                _userActionPending = true;
                _userActionApproved = false;
                _userActionTimeout = millis() + 30000; // 30秒超时
                sendKeepAlive(0x01); // User Presence required
                if (_userPresenceCb) _userPresenceCb();
            }
            break;
        case CTAP_GET_ASSERTION:
            if (len > 1) {
                // 需要用户确认
                _pendingCmdCode = CTAP_GET_ASSERTION;
                _pendingCmdLen = len - 1;
                memcpy(_pendingCmdBuffer, data + 1, len - 1);
                _userActionPending = true;
                _userActionApproved = false;
                _userActionTimeout = millis() + 30000;
                sendKeepAlive(0x01);
                if (_userPresenceCb) _userPresenceCb();
            }
            break;
        case CTAP_CLIENT_PIN:
            if (len > 1) {
                handleClientPIN(data + 1, len - 1);
            }
            break;
        case CTAP_RESET:
            _pendingCmdCode = CTAP_RESET;
            _userActionPending = true;
            _userActionApproved = false;
            _userActionTimeout = millis() + 30000;
            sendKeepAlive(0x01);
            if (_userPresenceCb) _userPresenceCb();
            break;
        case CTAP_CANCEL:
            _userActionPending = false;
            FIDO_LOG("操作已取消");
            break;
        default:
            FIDO_LOG("未知命令: 0x%02X", cmd);
            sendError(cmd, CTAP1_ERR_INVALID_COMMAND);
            break;
    }
}

// ---- CTAP2 命令处理 ----

// authenticatorGetInfo
void FIDO2Manager::handleGetInfo() {
    FIDO_LOG("处理 GetInfo");

    uint8_t buf[512];
    size_t offset = 0;

    // 构建 CBOR map (共 10 个字段)
    offset += encodeCBORMap(buf + offset, sizeof(buf) - offset, 10);

    // 1: versions (array of text strings)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 1);
    const char *versions[] = {"FIDO_2_0", "FIDO_2_1_PRE", "FIDO_2_1"};
    offset += encodeCBORArray(buf + offset, sizeof(buf) - offset, 3);
    for (auto &v : versions) {
        offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, v);
    }

    // 2: extensions (array, empty for now)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 2);
    offset += encodeCBORArray(buf + offset, sizeof(buf) - offset, 0);

    // 3: AAGUID (byte string)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 3);
    offset += encodeCBORByteString(buf + offset, sizeof(buf) - offset, _aaguid, 16);

    // 4: options (map)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 4);
    offset += encodeCBORMap(buf + offset, sizeof(buf) - offset, 6);
    //   rk (resident key): true
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "rk");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, true);
    //   up (user presence): true
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "up");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, true);
    //   uv (user verification): false
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "uv");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, false);
    //   clientPin: false
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "clientPin");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, false);
    //   credentialMgmtPreview: true
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "credentialMgmtPreview");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, true);
    //   ep (enterprise attestation): false
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "ep");
    offset += encodeCBORBool(buf + offset, sizeof(buf) - offset, false);

    // 5: maxMsgSize (unsigned int)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 5);
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 1024);

    // 6: all pinUvAuthProtocols (array)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 6);
    offset += encodeCBORArray(buf + offset, sizeof(buf) - offset, 0);

    // 7: maxCredentialCountInList (unsigned int)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 7);
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, _credentialMax);

    // 8: maxCredentialIdLength (unsigned int)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 8);
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 64);

    // 9: transports (array)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 9);
    const char *transports[] = {"ble", "internal"};
    offset += encodeCBORArray(buf + offset, sizeof(buf) - offset, 2);
    for (auto &t : transports) {
        offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, t);
    }

    // 10: algorithms (array)
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, 10);
    offset += encodeCBORArray(buf + offset, sizeof(buf) - offset, 1);
    offset += encodeCBORMap(buf + offset, sizeof(buf) - offset, 2);
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "type");
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "public-key");
    offset += encodeCBORTextString(buf + offset, sizeof(buf) - offset, "alg");
    offset += encodeCBORInt(buf + offset, sizeof(buf) - offset, -7); // ES256 (ECDSA w/ SHA256)

    sendResponse(CTAP_GET_INFO, buf, offset);
}

// authenticatorMakeCredential
void FIDO2Manager::handleMakeCredential(const uint8_t *cborData, size_t cborLen) {
    FIDO_LOG("处理 MakeCredential");

    if (!_crypto) {
        sendError(CTAP_MAKE_CREDENTIAL, CTAP1_ERR_OTHER);
        return;
    }

    // 解析 CBOR 参数
    size_t offset = 0;
    uint8_t majorType;
    uint64_t mapCount;
    if (!decodeCBORHeader(cborData, cborLen, offset, majorType, mapCount) || majorType != 0xA0) {
        sendError(CTAP_MAKE_CREDENTIAL, CTAP1_ERR_INVALID_PARAMETER);
        return;
    }

    // 提取 RP ID 和 User
    char rpID[128] = {0};
    char rpName[128] = {0};
    char userName[128] = {0};
    char userDisplayName[128] = {0};
    uint8_t userID[32] = {0};
    size_t userIDLen = 0;

    for (uint64_t i = 0; i < mapCount && offset < cborLen; i++) {
        int64_t key;
        if (!decodeCBORInt(cborData, cborLen, offset, key)) break;

        switch (key) {
            case 1: { // rp
                size_t rpOffset = offset;
                uint8_t rpMT;
                uint64_t rpMapLen;
                if (decodeCBORHeader(cborData, cborLen, offset, rpMT, rpMapLen) && rpMT == 0xA0) {
                    for (uint64_t j = 0; j < rpMapLen; j++) {
                        int64_t rpKey;
                        if (!decodeCBORInt(cborData, cborLen, offset, rpKey)) break;
                        if (rpKey == 1) { // rp.id
                            decodeCBORTextString(cborData, cborLen, offset, rpID, sizeof(rpID));
                        } else if (rpKey == 2) { // rp.name
                            decodeCBORTextString(cborData, cborLen, offset, rpName, sizeof(rpName));
                        } else {
                            skipCBORValue(cborData, cborLen, offset);
                        }
                    }
                } else {
                    offset = rpOffset;
                    skipCBORValue(cborData, cborLen, offset);
                }
                break;
            }
            case 2: { // user
                size_t userOffset = offset;
                uint8_t userMT;
                uint64_t userMapLen;
                if (decodeCBORHeader(cborData, cborLen, offset, userMT, userMapLen) && userMT == 0xA0) {
                    for (uint64_t j = 0; j < userMapLen; j++) {
                        int64_t userKey;
                        if (!decodeCBORInt(cborData, cborLen, offset, userKey)) break;
                        if (userKey == 1) { // user.id
                            const uint8_t *idData;
                            size_t idLen;
                            decodeCBORByteString(cborData, cborLen, offset, idData, idLen);
                            userIDLen = min(idLen, sizeof(userID));
                            memcpy(userID, idData, userIDLen);
                        } else if (userKey == 2) { // user.name
                            decodeCBORTextString(cborData, cborLen, offset, userName, sizeof(userName));
                        } else if (userKey == 3) { // user.displayName
                            decodeCBORTextString(cborData, cborLen, offset, userDisplayName, sizeof(userDisplayName));
                        } else {
                            skipCBORValue(cborData, cborLen, offset);
                        }
                    }
                } else {
                    offset = userOffset;
                    skipCBORValue(cborData, cborLen, offset);
                }
                break;
            }
            default:
                skipCBORValue(cborData, cborLen, offset);
                break;
        }
    }

    FIDO_LOG("  RP ID: %s, User: %s", rpID, userName);

    // 生成 ECDSA P-256 密钥对
    mbedtls_pk_context pkCtx;
    mbedtls_pk_init(&pkCtx);
    int ret = mbedtls_pk_setup(&pkCtx, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) {
        mbedtls_pk_free(&pkCtx);
        sendError(CTAP_MAKE_CREDENTIAL, CTAP1_ERR_OTHER);
        return;
    }

    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                               mbedtls_pk_ec(pkCtx),
                               mbedtls_ctr_drbg_random, &_crypto->getCTRDRBG());
    if (ret != 0) {
        mbedtls_pk_free(&pkCtx);
        sendError(CTAP_MAKE_CREDENTIAL, CTAP1_ERR_OTHER);
        return;
    }

    // 提取私钥和公钥
    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecdsa_init(&ecdsa);
    mbedtls_ecdsa_from_keypair(&ecdsa, mbedtls_pk_ec(pkCtx));

    uint8_t privateKey[32] = {0};
    uint8_t publicKeyX[32] = {0};
    uint8_t publicKeyY[32] = {0};

    mbedtls_mpi_write_binary(&ecdsa.d, privateKey, 32);
    mbedtls_mpi_write_binary(&ecdsa.Q.X, publicKeyX, 32);
    mbedtls_mpi_write_binary(&ecdsa.Q.Y, publicKeyY, 32);

    // 计算凭证 ID (SHA256 of public key)
    uint8_t credID[32];
    uint8_t pubKeyRaw[64];
    memcpy(pubKeyRaw, publicKeyX, 32);
    memcpy(pubKeyRaw + 32, publicKeyY, 32);

    mbedtls_sha256(pubKeyRaw, 64, credID, 0);

    // 计算 RP ID hash
    uint8_t rpIDHash[32];
    mbedtls_sha256((const uint8_t *)rpID, strlen(rpID), rpIDHash, 0);

    // 存储凭证
    FIDO2Credential cred;
    memset(&cred, 0, sizeof(cred));
    memcpy(cred.credentialID, credID, 32);
    memcpy(cred.privateKey, privateKey, 32);
    memcpy(cred.publicKey, publicKeyX, 32);  // X only
    memcpy(cred.publicKey + 32, publicKeyY, 32);  // Y
    memcpy(cred.rpIDHash, rpIDHash, 32);
    strncpy(cred.rpID, rpID, sizeof(cred.rpID) - 1);
    strncpy(cred.userName, userName, sizeof(cred.userName) - 1);
    memcpy(cred.userID, userID, min(userIDLen, (size_t)32));
    cred.userIDLen = userIDLen;
    cred.isValid = true;

    if (!storeCredential(cred)) {
        mbedtls_ecdsa_free(&ecdsa);
        mbedtls_pk_free(&pkCtx);
        sendError(CTAP_MAKE_CREDENTIAL, CTAP1_ERR_OTHER);
        return;
    }

    // 构建响应 CBOR
    uint8_t resp[512];
    size_t respOff = 0;
    respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 3);

    // 1: fmt = "packed"
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 1);
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "packed");

    // 2: authData
    uint8_t authData[256];
    size_t authDataLen = buildAuthDataMakeCred(rpIDHash, credID, 32,
                                                publicKeyX, publicKeyY,
                                                authData, sizeof(authData));
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 2);
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, authData, authDataLen);

    // 3: attStmt
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 3);
    respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 2);
    //   alg: -7 (ES256)
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "alg");
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, -7);
    //   sig: ECDSA signature of authData
    uint8_t sig[64] = {0};
    size_t sigLen = 0;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    ret = mbedtls_ecdsa_sign(&ecdsa.grp, &r, &s,
                              &ecdsa.d,
                              authData, authDataLen,
                              mbedtls_ctr_drbg_random, &_crypto->getCTRDRBG());
    if (ret == 0) {
        mbedtls_mpi_write_binary(&r, sig, 32);
        mbedtls_mpi_write_binary(&s, sig + 32, 32);
        sigLen = 64;
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "sig");
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, sig, sigLen);

    mbedtls_ecdsa_free(&ecdsa);
    mbedtls_pk_free(&pkCtx);

    sendResponse(CTAP_MAKE_CREDENTIAL, resp, respOff);
}

// authenticatorGetAssertion
void FIDO2Manager::handleGetAssertion(const uint8_t *cborData, size_t cborLen) {
    FIDO_LOG("处理 GetAssertion");

    if (!_crypto) {
        sendError(CTAP_GET_ASSERTION, CTAP1_ERR_OTHER);
        return;
    }

    // 解析 CBOR
    size_t offset = 0;
    uint8_t majorType;
    uint64_t mapCount;
    if (!decodeCBORHeader(cborData, cborLen, offset, majorType, mapCount) || majorType != 0xA0) {
        sendError(CTAP_GET_ASSERTION, CTAP1_ERR_INVALID_PARAMETER);
        return;
    }

    char rpID[128] = {0};
    uint8_t clientDataHash[32] = {0};
    bool hasCDHash = false;
    uint8_t allowCredID[64] = {0};
    size_t allowCredIDLen = 0;
    bool hasAllowList = false;

    for (uint64_t i = 0; i < mapCount && offset < cborLen; i++) {
        int64_t key;
        if (!decodeCBORInt(cborData, cborLen, offset, key)) break;

        switch (key) {
            case 1: // rpId
                decodeCBORTextString(cborData, cborLen, offset, rpID, sizeof(rpID));
                break;
            case 2: { // clientDataHash
                const uint8_t *hash;
                size_t hashLen;
                decodeCBORByteString(cborData, cborLen, offset, hash, hashLen);
                if (hashLen == 32) {
                    memcpy(clientDataHash, hash, 32);
                    hasCDHash = true;
                }
                break;
            }
            case 3: { // allowList
                size_t listOffset = offset;
                uint8_t listMT;
                uint64_t listCount;
                if (decodeCBORHeader(cborData, cborLen, offset, listMT, listCount) && listMT == 0x80 && listCount > 0) {
                    // 只取第一个凭证
                    uint64_t innerMapLen;
                    decodeCBORHeader(cborData, cborLen, offset, majorType, innerMapLen);
                    for (uint64_t j = 0; j < innerMapLen; j++) {
                        int64_t allowKey;
                        decodeCBORInt(cborData, cborLen, offset, allowKey);
                        if (allowKey == 2) { // id (credential ID)
                            const uint8_t *credData;
                            size_t credLen;
                            decodeCBORByteString(cborData, cborLen, offset, credData, credLen);
                            if (credLen <= 64) {
                                memcpy(allowCredID, credData, credLen);
                                allowCredIDLen = credLen;
                                hasAllowList = true;
                            }
                        } else {
                            skipCBORValue(cborData, cborLen, offset);
                        }
                    }
                } else {
                    offset = listOffset;
                    skipCBORValue(cborData, cborLen, offset);
                }
                break;
            }
            case 7: // options
                skipCBORValue(cborData, cborLen, offset);
                break;
            case 5: // extensions
                skipCBORValue(cborData, cborLen, offset);
                break;
            default:
                skipCBORValue(cborData, cborLen, offset);
                break;
        }
    }

    // 计算 RP ID hash
    uint8_t rpIDHash[32];
    mbedtls_sha256((const uint8_t *)rpID, strlen(rpID), rpIDHash, 0);

    // 查找凭证
    std::vector<FIDO2Credential> creds;
    findCredentialsByRP(rpIDHash, creds);

    if (creds.empty()) {
        sendError(CTAP_GET_ASSERTION, CTAP2_ERR_NO_CREDENTIALS);
        return;
    }

    // 如果传了凭证列表，匹配
    FIDO2Credential *matched = nullptr;
    for (auto &c : creds) {
        if (hasAllowList && allowCredIDLen > 0) {
            if (memcmp(c.credentialID, allowCredID, min(allowCredIDLen, (size_t)32)) == 0) {
                matched = &c;
                break;
            }
        } else {
            matched = &c;
            break;
        }
    }

    if (!matched) {
        sendError(CTAP_GET_ASSERTION, CTAP2_ERR_NO_CREDENTIALS);
        return;
    }

    // 自增签名计数器
    _signCounter++;
    {
        Preferences prefs;
        prefs.begin(FIDO_NS, false);
        prefs.putUInt(KEY_COUNTER, _signCounter);
        prefs.end();
    }

    // 构建 authData
    uint8_t authData[128];
    memset(authData, 0, sizeof(authData));
    memcpy(authData, rpIDHash, 32);
    authData[32] = 0x05; // UP + AT flags
    authData[33] = (_signCounter >> 24) & 0xFF;
    authData[34] = (_signCounter >> 16) & 0xFF;
    authData[35] = (_signCounter >> 8) & 0xFF;
    authData[36] = _signCounter & 0xFF;

    // 构建签名消息: authData + clientDataHash
    uint8_t sigMsg[160];
    memcpy(sigMsg, authData, 37);
    memcpy(sigMsg + 37, clientDataHash, 32);
    size_t sigMsgLen = hasCDHash ? 69 : 37;

    // 用匹配的私钥签名
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    mbedtls_ecp_group_load(&mbedtls_pk_ec(pk)->grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&mbedtls_pk_ec(pk)->d, matched->privateKey, 32);

    // 构建完整公钥点 (0x04 || X || Y)
    uint8_t pubKeyFull[65];
    pubKeyFull[0] = 0x04;
    memcpy(pubKeyFull + 1, matched->publicKey, 64);
    mbedtls_ecp_point_read_binary(&mbedtls_pk_ec(pk)->grp, &mbedtls_pk_ec(pk)->Q,
                                   pubKeyFull, 65);

    uint8_t sig[64];
    size_t sigLen = 64;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecdsa_sign(&mbedtls_pk_ec(pk)->grp, &r, &s,
                        &mbedtls_pk_ec(pk)->d,
                        sigMsg, sigMsgLen,
                        mbedtls_ctr_drbg_random, &_crypto->getCTRDRBG());
    mbedtls_mpi_write_binary(&r, sig, 32);
    mbedtls_mpi_write_binary(&s, sig + 32, 32);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_pk_free(&pk);

    // 构建 CBOR 响应
    uint8_t resp[512];
    size_t respOff = 0;
    respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 4);

    // 1: credential (包含凭证 ID)
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 1);
    respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 2);
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "type");
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "public-key");
    respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "id");
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, matched->credentialID, 32);

    // 2: authData
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 2);
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, authData, 37);

    // 3: signature
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 3);
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, sig, sigLen);

    // 4: user (可选)
    if (strlen(matched->userName) > 0) {
        respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 4);
        respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 2);
        respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "name");
        respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, matched->userName);
        respOff += encodeCBORTextString(resp + respOff, sizeof(resp) - respOff, "id");
        respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, matched->userID, matched->userIDLen);
    }

    sendResponse(CTAP_GET_ASSERTION, resp, respOff);
}

// authenticatorClientPIN
void FIDO2Manager::handleClientPIN(const uint8_t *cborData, size_t cborLen) {
    FIDO_LOG("处理 ClientPIN (暂不支持)");

    // 如果不支持 PIN，返回错误
    uint8_t resp[16];
    size_t respOff = 0;
    respOff += encodeCBORMap(resp + respOff, sizeof(resp) - respOff, 2);
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 1);  // pinUvAuthProtocol
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 0);  // none
    respOff += encodeCBORInt(resp + respOff, sizeof(resp) - respOff, 2);  // pinUvAuthToken
    respOff += encodeCBORByteString(resp + respOff, sizeof(resp) - respOff, nullptr, 0);

    sendResponse(CTAP_CLIENT_PIN, resp, respOff);
}

// authenticatorReset
void FIDO2Manager::handleReset() {
    FIDO_LOG("处理 Reset - 清除所有凭证");

    // 清除 Preferences
    Preferences prefs;
    prefs.begin(FIDO_NS, false);
    prefs.clear();
    prefs.end();

    _credentialCount = 0;
    _signCounter = 0;

    sendResponse(CTAP_RESET, nullptr, 0);
}

void FIDO2Manager::resetCredentials() {
    handleReset();
}

// ---- 响应发送 ----

void FIDO2Manager::sendResponse(uint8_t cmd, const uint8_t *payload, size_t payloadLen) {
    if (!_connected || !_controlPoint) return;

    // U2F 响应: [0x00] [payload...]
    size_t totalLen = 1 + payloadLen;
    uint8_t *resp = new uint8_t[totalLen];
    resp[0] = 0x00; // U2F 成功状态
    if (payloadLen > 0) {
        memcpy(resp + 1, payload, payloadLen);
    }

    _controlPoint->setValue(resp, totalLen);
    _controlPoint->indicate();

    delete[] resp;
    FIDO_LOG("发送响应: cmd=0x%02X, 长度=%d", cmd, totalLen);
}

void FIDO2Manager::sendError(uint8_t cmd, uint8_t status) {
    if (!_connected || !_controlPoint) return;

    uint8_t resp[2] = {status, 0x00};
    _controlPoint->setValue(resp, 1);
    _controlPoint->indicate();
    FIDO_LOG("发送错误: cmd=0x%02X, status=0x%02X", cmd, status);
}

void FIDO2Manager::sendKeepAlive(uint8_t status) {
    if (!_connected || !_status) return;

    uint8_t ka[1] = {status};
    _status->setValue(ka, 1);
    _status->notify();
}

// ---- CBOR 编码 ----

size_t FIDO2Manager::encodeCBORHeader(uint8_t *buf, size_t bufSize, uint8_t majorType, uint64_t value) {
    size_t len = 0;
    if (bufSize < 1) return 0;

    if (value <= 23) {
        buf[0] = majorType | (uint8_t)value;
        len = 1;
    } else if (value <= 0xFF) {
        if (bufSize >= 2) {
            buf[0] = majorType | 24;
            buf[1] = (uint8_t)value;
            len = 2;
        }
    } else if (value <= 0xFFFF) {
        if (bufSize >= 3) {
            buf[0] = majorType | 25;
            buf[1] = (uint8_t)(value >> 8);
            buf[2] = (uint8_t)value;
            len = 3;
        }
    } else if (value <= 0xFFFFFFFFULL) {
        if (bufSize >= 5) {
            buf[0] = majorType | 26;
            buf[1] = (uint8_t)(value >> 24);
            buf[2] = (uint8_t)(value >> 16);
            buf[3] = (uint8_t)(value >> 8);
            buf[4] = (uint8_t)value;
            len = 5;
        }
    } else {
        if (bufSize >= 9) {
            buf[0] = majorType | 27;
            for (int i = 0; i < 8; i++) {
                buf[1 + i] = (uint8_t)(value >> (56 - 8 * i));
            }
            len = 9;
        }
    }
    return len;
}

size_t FIDO2Manager::encodeCBORInt(uint8_t *buf, size_t bufSize, int64_t value) {
    if (value >= 0) {
        return encodeCBORHeader(buf, bufSize, 0x00, (uint64_t)value);
    } else {
        return encodeCBORHeader(buf, bufSize, 0x20, (uint64_t)(-1 - value));
    }
}

size_t FIDO2Manager::encodeCBORByteString(uint8_t *buf, size_t bufSize, const uint8_t *data, size_t dataLen) {
    size_t off = encodeCBORHeader(buf, bufSize, 0x40, dataLen);
    if (off == 0 || off + dataLen > bufSize) return 0;
    if (data && dataLen > 0) {
        memcpy(buf + off, data, dataLen);
    }
    return off + dataLen;
}

size_t FIDO2Manager::encodeCBORTextString(uint8_t *buf, size_t bufSize, const char *str) {
    size_t len = strlen(str);
    return encodeCBORByteString(buf, bufSize, (const uint8_t *)str, len);
}

size_t FIDO2Manager::encodeCBORArray(uint8_t *buf, size_t bufSize, size_t count) {
    return encodeCBORHeader(buf, bufSize, 0x80, count);
}

size_t FIDO2Manager::encodeCBORMap(uint8_t *buf, size_t bufSize, size_t count) {
    return encodeCBORHeader(buf, bufSize, 0xA0, count);
}

size_t FIDO2Manager::encodeCBORBool(uint8_t *buf, size_t bufSize, bool value) {
    if (bufSize < 1) return 0;
    buf[0] = value ? 0xF5 : 0xF4;
    return 1;
}

size_t FIDO2Manager::encodeCBORNull(uint8_t *buf, size_t bufSize) {
    if (bufSize < 1) return 0;
    buf[0] = 0xF6;
    return 1;
}

// ---- CBOR 解码 ----

bool FIDO2Manager::decodeCBORHeader(const uint8_t *data, size_t len, size_t &offset, uint8_t &majorType, uint64_t &value) {
    if (offset >= len) return false;

    uint8_t initial = data[offset++];
    majorType = initial & 0xE0;
    uint8_t extra = initial & 0x1F;

    if (extra <= 23) {
        value = extra;
    } else if (extra == 24) {
        if (offset + 1 > len) return false;
        value = data[offset++];
    } else if (extra == 25) {
        if (offset + 2 > len) return false;
        value = ((uint64_t)data[offset] << 8) | data[offset + 1];
        offset += 2;
    } else if (extra == 26) {
        if (offset + 4 > len) return false;
        value = ((uint64_t)data[offset] << 24) | ((uint64_t)data[offset + 1] << 16) |
                ((uint64_t)data[offset + 2] << 8) | data[offset + 3];
        offset += 4;
    } else if (extra == 27) {
        if (offset + 8 > len) return false;
        value = 0;
        for (int i = 0; i < 8; i++) {
            value = (value << 8) | data[offset + i];
        }
        offset += 8;
    } else {
        return false;
    }
    return true;
}

bool FIDO2Manager::decodeCBORInt(const uint8_t *data, size_t len, size_t &offset, int64_t &value) {
    uint8_t majorType;
    uint64_t raw;
    if (!decodeCBORHeader(data, len, offset, majorType, raw)) return false;

    if (majorType == 0x00) {
        value = (int64_t)raw;
        return true;
    } else if (majorType == 0x20) {
        value = -1 - (int64_t)raw;
        return true;
    }
    return false;
}

bool FIDO2Manager::decodeCBORByteString(const uint8_t *data, size_t len, size_t &offset, const uint8_t *&out, size_t &outLen) {
    uint8_t majorType;
    uint64_t rawLen;
    if (!decodeCBORHeader(data, len, offset, majorType, rawLen)) return false;

    if (majorType != 0x40) return false;
    if (offset + rawLen > len) return false;

    out = data + offset;
    outLen = (size_t)rawLen;
    offset += outLen;
    return true;
}

bool FIDO2Manager::decodeCBORTextString(const uint8_t *data, size_t len, size_t &offset, char *out, size_t outSize) {
    const uint8_t *str;
    size_t strLen;
    if (!decodeCBORByteString(data, len, offset, str, strLen)) return false;

    size_t copyLen = min(strLen, outSize - 1);
    memcpy(out, str, copyLen);
    out[copyLen] = '\0';
    return true;
}

bool FIDO2Manager::skipCBORValue(const uint8_t *data, size_t len, size_t &offset) {
    if (offset >= len) return false;

    uint8_t initial = data[offset];
    uint8_t majorType = initial & 0xE0;
    uint8_t extra = initial & 0x1F;

    // 简单类型 (boolean, null, undefined)
    if (majorType == 0xE0 && extra == 0x1F) {
        offset += 2; // 0xF9-0xFB (half/float/double) or 0xFF (break)
        return true;
    }

    uint64_t value;
    if (!decodeCBORHeader(data, len, offset, majorType, value)) return false;

    switch (majorType) {
        case 0x00: // unsigned int
        case 0x20: // negative int
        case 0xE0: // simple/float
            break;
        case 0x40: // byte string
        case 0x60: // text string
            if (offset + value > len) return false;
            offset += (size_t)value;
            break;
        case 0x80: // array
            for (uint64_t i = 0; i < value; i++) {
                if (!skipCBORValue(data, len, offset)) return false;
            }
            break;
        case 0xA0: // map
            for (uint64_t i = 0; i < value * 2; i++) {
                if (!skipCBORValue(data, len, offset)) return false;
            }
            break;
        default:
            return false;
    }
    return true;
}

// ---- 认证器数据构建 ----

size_t FIDO2Manager::buildAuthDataMakeCred(const uint8_t *rpIDHash, const uint8_t *credID, uint16_t credIDLen,
                                            const uint8_t *pubKeyX, const uint8_t *pubKeyY,
                                            uint8_t *out, size_t outLen) {
    size_t off = 0;
    if (off + 32 > outLen) return 0;
    memcpy(out + off, rpIDHash, 32);
    off += 32;

    if (off + 1 > outLen) return 0;
    out[off] = 0x41; // UP (0x01) + AT (0x40) + UV not set
    off += 1;

    if (off + 4 > outLen) return 0;
    _signCounter++;
    out[off] = (_signCounter >> 24) & 0xFF;
    out[off + 1] = (_signCounter >> 16) & 0xFF;
    out[off + 2] = (_signCounter >> 8) & 0xFF;
    out[off + 3] = _signCounter & 0xFF;
    off += 4;

    // AAGUID
    if (off + 16 > outLen) return 0;
    memcpy(out + off, _aaguid, 16);
    off += 16;

    // Credential ID length
    if (off + 2 > outLen) return 0;
    out[off] = (credIDLen >> 8) & 0xFF;
    out[off + 1] = credIDLen & 0xFF;
    off += 2;

    // Credential ID
    if (off + credIDLen > outLen) return 0;
    memcpy(out + off, credID, credIDLen);
    off += credIDLen;

    // CBOR-encoded credential public key
    if (off + 100 > outLen) return 0;
    off += encodeCredentialPublicKey(out + off, outLen - off, pubKeyX, pubKeyY);

    // Save counter
    {
        Preferences prefs;
        prefs.begin(FIDO_NS, false);
        prefs.putUInt(KEY_COUNTER, _signCounter);
        prefs.end();
    }

    return off;
}

size_t FIDO2Manager::encodeCredentialPublicKey(uint8_t *buf, size_t bufSize, const uint8_t *pubKeyX, const uint8_t *pubKeyY) {
    // COSE_Key format for EC2 P-256
    size_t off = 0;
    off += encodeCBORMap(buf + off, bufSize - off, 5);

    // 1: key type (2 = EC2)
    off += encodeCBORInt(buf + off, bufSize - off, 1);
    off += encodeCBORInt(buf + off, bufSize - off, 2);

    // 3: algorithm (-7 = ES256)
    off += encodeCBORInt(buf + off, bufSize - off, 3);
    off += encodeCBORInt(buf + off, bufSize - off, -7);

    // -1: curve (1 = P-256)
    off += encodeCBORInt(buf + off, bufSize - off, -1);
    off += encodeCBORInt(buf + off, bufSize - off, 1);

    // -2: x coordinate
    off += encodeCBORInt(buf + off, bufSize - off, -2);
    off += encodeCBORByteString(buf + off, bufSize - off, pubKeyX, 32);

    // -3: y coordinate
    off += encodeCBORInt(buf + off, bufSize - off, -3);
    off += encodeCBORByteString(buf + off, bufSize - off, pubKeyY, 32);

    return off;
}

// ---- 凭证存储 ----

bool FIDO2Manager::storeCredential(const FIDO2Credential &cred) {
    if (_credentialCount >= _credentialMax) return false;

    Preferences prefs;
    prefs.begin(FIDO_NS, false);

    char key[32];
    // 存储每个凭证字段
    snprintf(key, sizeof(key), "cr_%d_id", _credentialCount);
    prefs.putBytes(key, cred.credentialID, 32);
    snprintf(key, sizeof(key), "cr_%d_pk", _credentialCount);
    prefs.putBytes(key, cred.privateKey, 32);
    snprintf(key, sizeof(key), "cr_%d_pub", _credentialCount);
    prefs.putBytes(key, cred.publicKey, 64);
    snprintf(key, sizeof(key), "cr_%d_rp", _credentialCount);
    prefs.putBytes(key, cred.rpIDHash, 32);
    snprintf(key, sizeof(key), "cr_%d_rps", _credentialCount);
    prefs.putString(key, cred.rpID);
    snprintf(key, sizeof(key), "cr_%d_un", _credentialCount);
    prefs.putString(key, cred.userName);
    snprintf(key, sizeof(key), "cr_%d_uid", _credentialCount);
    prefs.putBytes(key, cred.userID, cred.userIDLen);
    snprintf(key, sizeof(key), "cr_%d_uidl", _credentialCount);
    prefs.putUChar(key, cred.userIDLen);
    snprintf(key, sizeof(key), "cr_%d_valid", _credentialCount);
    prefs.putBool(key, true);

    _credentialCount++;
    prefs.putUInt(KEY_CRED_COUNT, _credentialCount);
    prefs.end();

    FIDO_LOG("凭证已存储 #%d: %s", _credentialCount - 1, cred.rpID);
    return true;
}

bool FIDO2Manager::findCredential(const uint8_t *rpIDHash, const uint8_t *credID, uint16_t credIDLen, FIDO2Credential &out) {
    Preferences prefs;
    prefs.begin(FIDO_NS, true);

    for (int i = 0; i < _credentialCount; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cr_%d_valid", i);
        if (!prefs.getBool(key, false)) continue;

        snprintf(key, sizeof(key), "cr_%d_id", i);
        uint8_t storedID[32];
        size_t storedLen = prefs.getBytes(key, storedID, 32);

        if (storedLen == (size_t)credIDLen && memcmp(storedID, credID, credIDLen) == 0) {
            snprintf(key, sizeof(key), "cr_%d_rp", i);
            prefs.getBytes(key, out.rpIDHash, 32);

            snprintf(key, sizeof(key), "cr_%d_pk", i);
            prefs.getBytes(key, out.privateKey, 32);

            snprintf(key, sizeof(key), "cr_%d_pub", i);
            prefs.getBytes(key, out.publicKey, 64);

            snprintf(key, sizeof(key), "cr_%d_rps", i);
            String rp = prefs.getString(key);
            strncpy(out.rpID, rp.c_str(), sizeof(out.rpID) - 1);

            snprintf(key, sizeof(key), "cr_%d_un", i);
            String un = prefs.getString(key);
            strncpy(out.userName, un.c_str(), sizeof(out.userName) - 1);

            snprintf(key, sizeof(key), "cr_%d_uid", i);
            out.userIDLen = prefs.getBytes(key, out.userID, 32);

            memcpy(out.credentialID, credID, credIDLen);
            out.isValid = true;

            prefs.end();
            return true;
        }
    }

    prefs.end();
    return false;
}

bool FIDO2Manager::findCredentialsByRP(const uint8_t *rpIDHash, std::vector<FIDO2Credential> &out) {
    Preferences prefs;
    prefs.begin(FIDO_NS, true);

    for (int i = 0; i < _credentialCount; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cr_%d_valid", i);
        if (!prefs.getBool(key, false)) continue;

        snprintf(key, sizeof(key), "cr_%d_rp", i);
        uint8_t storedRP[32];
        prefs.getBytes(key, storedRP, 32);

        if (memcmp(storedRP, rpIDHash, 32) != 0) continue;

        FIDO2Credential cred;
        memset(&cred, 0, sizeof(cred));
        memcpy(cred.rpIDHash, storedRP, 32);

        snprintf(key, sizeof(key), "cr_%d_id", i);
        prefs.getBytes(key, cred.credentialID, 32);

        snprintf(key, sizeof(key), "cr_%d_pk", i);
        prefs.getBytes(key, cred.privateKey, 32);

        snprintf(key, sizeof(key), "cr_%d_pub", i);
        prefs.getBytes(key, cred.publicKey, 64);

        snprintf(key, sizeof(key), "cr_%d_rps", i);
        String rp = prefs.getString(key);
        strncpy(cred.rpID, rp.c_str(), sizeof(cred.rpID) - 1);

        snprintf(key, sizeof(key), "cr_%d_un", i);
        String un = prefs.getString(key);
        strncpy(cred.userName, un.c_str(), sizeof(cred.userName) - 1);

        snprintf(key, sizeof(key), "cr_%d_uid", i);
        cred.userIDLen = prefs.getBytes(key, cred.userID, 32);

        cred.isValid = true;
        out.push_back(cred);
    }

    prefs.end();
    return !out.empty();
}

int FIDO2Manager::loadAllCredentials(std::vector<FIDO2Credential> &out) {
    Preferences prefs;
    prefs.begin(FIDO_NS, true);
    int count = 0;

    for (int i = 0; i < _credentialCount; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cr_%d_valid", i);
        if (!prefs.getBool(key, false)) continue;

        FIDO2Credential cred;
        memset(&cred, 0, sizeof(cred));

        snprintf(key, sizeof(key), "cr_%d_id", i);
        prefs.getBytes(key, cred.credentialID, 32);
        snprintf(key, sizeof(key), "cr_%d_pk", i);
        prefs.getBytes(key, cred.privateKey, 32);
        snprintf(key, sizeof(key), "cr_%d_pub", i);
        prefs.getBytes(key, cred.publicKey, 64);
        snprintf(key, sizeof(key), "cr_%d_rp", i);
        prefs.getBytes(key, cred.rpIDHash, 32);
        snprintf(key, sizeof(key), "cr_%d_rps", i);
        String rp = prefs.getString(key);
        strncpy(cred.rpID, rp.c_str(), sizeof(cred.rpID) - 1);
        snprintf(key, sizeof(key), "cr_%d_un", i);
        String un = prefs.getString(key);
        strncpy(cred.userName, un.c_str(), sizeof(cred.userName) - 1);
        snprintf(key, sizeof(key), "cr_%d_uid", i);
        cred.userIDLen = prefs.getBytes(key, cred.userID, 32);
        cred.isValid = true;
        out.push_back(cred);
        count++;
    }

    prefs.end();
    return count;
}
