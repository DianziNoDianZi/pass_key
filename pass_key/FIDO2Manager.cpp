/**
 * @file FIDO2Manager.cpp
 * @brief FIDO2/CTAP2 over BLE - mbedTLS 3.x compatible implementation
 */

#include "FIDO2Manager.h"
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <Preferences.h>
#include <cstring>

#define FIDO_LOG(fmt, ...) Serial.printf("[FIDO2] " fmt "\n", ##__VA_ARGS__)
static const char *FIDO_NS = "fido";
static const char *KEY_COUNTER = "counter";
static const char *KEY_CRED_COUNT = "cred_cnt";

// -------------------------------------------------------------------
// mbedTLS 3.x helpers (avoids direct ecp_keypair struct access)
// -------------------------------------------------------------------

/** Reconstruct a pk_context from stored DER key */
static bool pkFromDER(mbedtls_pk_context *pk, const uint8_t *der, size_t len) {
    mbedtls_pk_init(pk);
    return mbedtls_pk_parse_key(pk, der, len, NULL, 0, NULL, NULL) == 0;
}

/** Export pk_context private key to DER (caller owns buf, returns length) */
static int pkToDER(mbedtls_pk_context *pk, uint8_t *buf, size_t bufSize) {
    return mbedtls_pk_write_key_der(pk, buf, bufSize);
}

/** Extract raw X,Y (32 bytes each) from pk_context public key */
static bool getPubKeyXY(mbedtls_pk_context *pk, uint8_t *x, uint8_t *y) {
    uint8_t der[512];
    int len = mbedtls_pk_write_pubkey_der(pk, der, sizeof(der));
    if (len <= 0) return false;

    // DER SubjectPublicKeyInfo: SEQUENCE { SEQUENCE { OID }, BIT STRING point }
    uint8_t *p = der + sizeof(der) - len;
    int rem = len;

    // Search for BIT STRING tag (0x03) near the end
    for (int i = 0; i < rem - 66; i++) {
        if (p[i] == 0x03) {
            int off = i + 2;                // skip tag + first length byte
            int blen = p[i + 1];
            if (blen & 0x80) off += blen & 0x7F;   // long-form length
            while (off < rem && p[off] == 0x00) off++; // unused bits byte(s)
            if (off + 65 <= rem && p[off] == 0x04) { // uncompressed point
                memcpy(x, p + off + 1, 32);
                memcpy(y, p + off + 33, 32);
                return true;
            }
        }
    }
    return false;
}

/** Parse DER ECDSA signature to raw R||S (64 bytes) */
static bool derSigToRS(const uint8_t *der, size_t derLen, uint8_t *rs, size_t rsLen) {
    if (!der || derLen < 8 || rsLen < 64) return false;
    // DER: SEQUENCE { INTEGER r, INTEGER s }
    size_t off = 0;
    if (der[off++] != 0x30) return false;
    if (der[off] & 0x80) off += (der[off] & 0x7F) + 1; else off++; // length

    auto readInt = [&](uint8_t *out, size_t outLen) -> bool {
        if (off >= derLen || der[off] != 0x02) return false;
        off++; // INTEGER tag
        size_t ilen = der[off++];
        if (ilen + off > derLen) return false;
        if (ilen > outLen) off += ilen - outLen;  // leading zeros
        else {
            size_t pad = outLen - ilen;
            memset(out, 0, pad);
            memcpy(out + pad, der + off, ilen);
        }
        off += ilen;
        return true;
    };
    return readInt(rs, 32) && readInt(rs + 32, 32);
}

// ===================================================================

FIDO2Manager::FIDO2Manager() {}
FIDO2Manager::~FIDO2Manager() { stop(); }

// ---- INIT ----

bool FIDO2Manager::init(CryptoEngine *crypto) {
    _crypto = crypto;
    Preferences prefs;
    prefs.begin(FIDO_NS, true);
    _signCounter = prefs.getUInt(KEY_COUNTER, 0);
    _credentialCount = prefs.getUInt(KEY_CRED_COUNT, 0);
    prefs.end();
    _initialized = true;
    FIDO_LOG("init OK  credentials=%d  signCounter=%u", _credentialCount, _signCounter);
    return true;
}

void FIDO2Manager::setConfig(const FIDO2Config &cfg) {
    _cfg = cfg;
    if (_cfg.enabled && _initialized && !_server) start();
    else if (!_cfg.enabled && _server) stop();
}

// ---- BLE START / STOP ----

void FIDO2Manager::start() {
    if (!_initialized || !_cfg.enabled || _server) return;
    FIDO_LOG("Starting BLE FIDO2...");

    BLEDevice::init(_cfg.deviceName);
    _server = BLEDevice::createServer();
    _server->setCallbacks(new FIDOServerCallbacks(this));

    _service = _server->createService(FIDO2_SERVICE_UUID);

    _controlPoint = _service->createCharacteristic(
        FIDO2_CONTROL_POINT_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE);
    _controlPoint->setCallbacks(new FIDOCharacteristicCallbacks(this));
    _controlPoint->addDescriptor(new BLE2902());

    _status = _service->createCharacteristic(
        FIDO2_STATUS_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    _status->addDescriptor(new BLE2902());

    _controlPointLen = _service->createCharacteristic(
        FIDO2_CONTROL_POINT_LEN,
        BLECharacteristic::PROPERTY_READ);
    { uint16_t v = 1024; _controlPointLen->setValue((uint8_t *)&v, 2); }

    _serviceRevision = _service->createCharacteristic(
        FIDO2_SERVICE_REVISION,
        BLECharacteristic::PROPERTY_READ);
    { uint8_t rev[] = {0x80,0,0,0}; _serviceRevision->setValue(rev,4); }

    _service->start();

    BLEAdvertising *a = BLEDevice::getAdvertising();
    a->setAppearance(0x0540);
    a->addServiceUUID(FIDO2_SERVICE_UUID);
    a->setScanResponse(true);
    BLEDevice::startAdvertising();

    FIDO_LOG("BLE FIDO2 advertising as '%s'", _cfg.deviceName);
}

void FIDO2Manager::stop() {
    if (_server) {
        BLEDevice::stopAdvertising();
        _server->removeService(_service);
        _server = nullptr; _service = nullptr;
        _controlPoint = nullptr; _status = nullptr; _connected = false;
        FIDO_LOG("BLE stopped");
    }
}

void FIDO2Manager::setEnabled(bool en) {
    _cfg.enabled = en;
    en ? start() : stop();
}

void FIDO2Manager::prepareSleep() { stop(); }
void FIDO2Manager::wakeFromSleep() { start(); }

void FIDO2Manager::update() {
    if (_connected && _userActionPending && millis() >= _userActionTimeout) {
        _userActionPending = false;
        sendError(_pendingCmdCode, CTAP2_ERR_USER_ACTION_PENDING);
        FIDO_LOG("user action timeout");
    }
}

// ---- USER PRESENCE ----

void FIDO2Manager::confirmUserPresence(bool approved) {
    if (!_userActionPending) return;
    _userActionApproved = approved;
    _userActionPending = false;
    if (!approved) {
        sendError(_pendingCmdCode, CTAP2_ERR_OPERATION_DENIED);
        return;
    }
    switch (_pendingCmdCode) {
        case CTAP_MAKE_CREDENTIAL: handleMakeCredential(_pendingCmdBuffer, _pendingCmdLen); break;
        case CTAP_GET_ASSERTION:   handleGetAssertion(_pendingCmdBuffer, _pendingCmdLen); break;
        case CTAP_RESET:           handleReset(); break;
        default: break;
    }
}

// ---- BLE CALLBACKS ----

void FIDO2Manager::FIDOServerCallbacks::onConnect(BLEServer*) {
    parent->_connected = true;
    BLEDevice::stopAdvertising();
    FIDO_LOG("BLE connected");
}
void FIDO2Manager::FIDOServerCallbacks::onDisconnect(BLEServer*) {
    parent->_connected = false;
    parent->_userActionPending = false;
    BLEDevice::startAdvertising();
    FIDO_LOG("BLE disconnected, advertising");
}

void FIDO2Manager::FIDOCharacteristicCallbacks::onWrite(BLECharacteristic *pChar) {
    if (!parent) return;
    String val = pChar->getValue();
    if (val.length() == 0) return;
    parent->handleCTAP2Command((const uint8_t *)val.c_str(), val.length());
}

// ---- CTAP2 DISPATCH ----

void FIDO2Manager::handleCTAP2Command(const uint8_t *data, size_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];
    switch (cmd) {
        case CTAP_GET_INFO:      handleGetInfo(); break;
        case CTAP_MAKE_CREDENTIAL:
        case CTAP_GET_ASSERTION:
        case CTAP_RESET:
            _pendingCmdCode = cmd;
            _pendingCmdLen = len > 1 ? len - 1 : 0;
            if (_pendingCmdLen) memcpy(_pendingCmdBuffer, data + 1, _pendingCmdLen);
            _userActionPending = true;
            _userActionApproved = false;
            _userActionTimeout = millis() + 30000;
            sendKeepAlive(0x01);
            if (_userPresenceCb) _userPresenceCb();
            break;
        case CTAP_CLIENT_PIN:    if (len>1) handleClientPIN(data+1,len-1); break;
        case CTAP_CANCEL:        _userActionPending = false; FIDO_LOG("cancel"); break;
        default:                 sendError(cmd, CTAP1_ERR_INVALID_COMMAND); break;
    }
}

// ---- authenticatorGetInfo ----

void FIDO2Manager::handleGetInfo() {
    uint8_t buf[512]; size_t off = 0;
    off += encodeCBORMap(buf+off,sizeof(buf)-off, 10);

    off += encodeCBORInt(buf+off,sizeof(buf)-off,1); // versions
    const char *vers[] = {"FIDO_2_0","FIDO_2_1_PRE","FIDO_2_1"};
    off += encodeCBORArray(buf+off,sizeof(buf)-off,3);
    for (auto v:vers) off += encodeCBORTextString(buf+off,sizeof(buf)-off,v);

    off += encodeCBORInt(buf+off,sizeof(buf)-off,2); off += encodeCBORArray(buf+off,sizeof(buf)-off,0); //extensions
    off += encodeCBORInt(buf+off,sizeof(buf)-off,3); off += encodeCBORByteString(buf+off,sizeof(buf)-off,_aaguid,16);
    off += encodeCBORInt(buf+off,sizeof(buf)-off,4); // options
    {   const char *k[] = {"rk","up","uv","clientPin","credentialMgmtPreview","ep"};
        bool v[] = {true,true,false,false,true,false};
        off += encodeCBORMap(buf+off,sizeof(buf)-off,6);
        for (int i=0;i<6;i++) {
            off += encodeCBORTextString(buf+off,sizeof(buf)-off,k[i]);
            off += encodeCBORBool(buf+off,sizeof(buf)-off,v[i]);
        }
    }
    off += encodeCBORInt(buf+off,sizeof(buf)-off,5); off += encodeCBORInt(buf+off,sizeof(buf)-off,1024);
    off += encodeCBORInt(buf+off,sizeof(buf)-off,6); off += encodeCBORArray(buf+off,sizeof(buf)-off,0);
    off += encodeCBORInt(buf+off,sizeof(buf)-off,7); off += encodeCBORInt(buf+off,sizeof(buf)-off,_credentialMax);
    off += encodeCBORInt(buf+off,sizeof(buf)-off,8); off += encodeCBORInt(buf+off,sizeof(buf)-off,64);
    off += encodeCBORInt(buf+off,sizeof(buf)-off,9); // transports
    {   const char *t[] = {"ble","internal"};
        off += encodeCBORArray(buf+off,sizeof(buf)-off,2);
        off += encodeCBORTextString(buf+off,sizeof(buf)-off,t[0]);
        off += encodeCBORTextString(buf+off,sizeof(buf)-off,t[1]);
    }
    off += encodeCBORInt(buf+off,sizeof(buf)-off,10); // algorithms
    off += encodeCBORArray(buf+off,sizeof(buf)-off,1);
    off += encodeCBORMap(buf+off,sizeof(buf)-off,2);
    off += encodeCBORTextString(buf+off,sizeof(buf)-off,"type");
    off += encodeCBORTextString(buf+off,sizeof(buf)-off,"public-key");
    off += encodeCBORTextString(buf+off,sizeof(buf)-off,"alg");
    off += encodeCBORInt(buf+off,sizeof(buf)-off,-7);

    sendResponse(CTAP_GET_INFO,buf,off);
}

// ---- authenticatorMakeCredential ----

void FIDO2Manager::handleMakeCredential(const uint8_t *cbor, size_t cborLen) {
    // Parse minimal CBOR map
    char rpID[128]={}, rpName[128]={}, userName[128]={}, userDisp[128]={};
    uint8_t userID[32]={}; size_t userIDLen=0;
    {   size_t off=0; uint8_t mt; uint64_t mapN;
        if (!decodeCBORHeader(cbor,cborLen,off,mt,mapN)||mt!=0xA0)
            { sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_INVALID_PARAMETER); return; }
        for (uint64_t i=0;i<mapN&&off<cborLen;i++) {
            int64_t k; if (!decodeCBORInt(cbor,cborLen,off,k)) break;
            if (k==1) { // rp
                size_t s=off; uint8_t mt2; uint64_t m2;
                if (decodeCBORHeader(cbor,cborLen,off,mt2,m2)&&mt2==0xA0) {
                    for (uint64_t j=0;j<m2;j++) {
                        int64_t rk; if(!decodeCBORInt(cbor,cborLen,off,rk)) break;
                        if(rk==1) decodeCBORTextString(cbor,cborLen,off,rpID,sizeof(rpID));
                        else if(rk==2) decodeCBORTextString(cbor,cborLen,off,rpName,sizeof(rpName));
                        else skipCBORValue(cbor,cborLen,off);
                    }
                } else { off=s; skipCBORValue(cbor,cborLen,off); }
            } else if (k==2) { // user
                size_t s=off; uint8_t mt2; uint64_t m2;
                if (decodeCBORHeader(cbor,cborLen,off,mt2,m2)&&mt2==0xA0) {
                    for (uint64_t j=0;j<m2;j++) {
                        int64_t uk; if(!decodeCBORInt(cbor,cborLen,off,uk)) break;
                        if (uk==1) { const uint8_t *d; size_t dl;
                            decodeCBORByteString(cbor,cborLen,off,d,dl);
                            userIDLen=min(dl,sizeof(userID)); memcpy(userID,d,userIDLen);
                        } else if (uk==2) decodeCBORTextString(cbor,cborLen,off,userName,sizeof(userName));
                        else if (uk==3) decodeCBORTextString(cbor,cborLen,off,userDisp,sizeof(userDisp));
                        else skipCBORValue(cbor,cborLen,off);
                    }
                } else { off=s; skipCBORValue(cbor,cborLen,off); }
            } else skipCBORValue(cbor,cborLen,off);
        }
    }

    // Generate ECDSA P-256 key pair
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk),
                            mbedtls_ctr_drbg_random, &_crypto->getCTRDRBG())) {
        mbedtls_pk_free(&pk);
        sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_OTHER);
        return;
    }

    // Export private key DER
    uint8_t pkDer[256]; int pkDerLen = pkToDER(&pk, pkDer, sizeof(pkDer));
    if (pkDerLen <= 0) { mbedtls_pk_free(&pk); sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_OTHER); return; }

    // Extract public key X,Y
    uint8_t pubX[32], pubY[32];
    if (!getPubKeyXY(&pk, pubX, pubY)) {
        mbedtls_pk_free(&pk);
        sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_OTHER);
        return;
    }

    // Compute credential ID = SHA256(pubX || pubY)
    {
        uint8_t raw[64]; memcpy(raw,pubX,32); memcpy(raw+32,pubY,32);
        uint8_t credID[32]; mbedtls_sha256(raw,64,credID,0);

        // RP ID hash
        uint8_t rpIDHash[32]; mbedtls_sha256((const uint8_t*)rpID,strlen(rpID),rpIDHash,0);

        // Store credential
        FIDO2Credential cred; memset(&cred,0,sizeof(cred));
        memcpy(cred.credentialID,credID,32);
        memcpy(cred.keyDER,pkDer+sizeof(pkDer)-pkDerLen,pkDerLen);
        cred.keyDERLen=pkDerLen;
        memcpy(cred.publicKey,pubX,32); memcpy(cred.publicKey+32,pubY,32);
        memcpy(cred.rpIDHash,rpIDHash,32);
        strncpy(cred.rpID,rpID,sizeof(cred.rpID)-1);
        strncpy(cred.userName,userName,sizeof(cred.userName)-1);
        memcpy(cred.userID,userID,min(userIDLen,(size_t)32));
        cred.userIDLen=userIDLen; cred.isValid=true;

        if (!storeCredential(cred)) {
            mbedtls_pk_free(&pk);
            sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_OTHER);
            return;
        }

        // Build authenticator data
        uint8_t authData[256];
        size_t authLen = buildAuthData(rpIDHash,credID,32,pubX,pubY,authData,sizeof(authData));

        // Sign authData
        uint8_t sigDer[128]; size_t sigDerLen=0;
        if (mbedtls_pk_sign(&pk,MBEDTLS_MD_SHA256,
                            authData,authLen,
                            sigDer,sizeof(sigDer),&sigDerLen,
                            mbedtls_ctr_drbg_random,&_crypto->getCTRDRBG())) {
            mbedtls_pk_free(&pk);
            sendError(CTAP_MAKE_CREDENTIAL,CTAP1_ERR_OTHER);
            return;
        }
        uint8_t sig[64]; derSigToRS(sigDer,sigDerLen,sig,64);

        // Build response CBOR
        uint8_t resp[512]; size_t ro=0;
        ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,3);
        ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,1);
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"packed");
        ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,2);
        ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,authData,authLen);
        ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,3);
        ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,2);
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"alg");
        ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,-7);
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"sig");
        ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,sig,64);

        mbedtls_pk_free(&pk);
        sendResponse(CTAP_MAKE_CREDENTIAL,resp,ro);
    }
}

// ---- authenticatorGetAssertion ----

void FIDO2Manager::handleGetAssertion(const uint8_t *cbor, size_t cborLen) {
    char rpID[128]={}; uint8_t cdHash[32]={}; bool hasCDHash=false;
    uint8_t allowCred[64]; size_t allowLen=0; bool hasAllow=false;

    {   size_t off=0; uint8_t mt; uint64_t mapN;
        if (!decodeCBORHeader(cbor,cborLen,off,mt,mapN)||mt!=0xA0)
            { sendError(CTAP_GET_ASSERTION,CTAP1_ERR_INVALID_PARAMETER); return; }
        for (uint64_t i=0;i<mapN&&off<cborLen;i++) {
            int64_t k; if(!decodeCBORInt(cbor,cborLen,off,k)) break;
            if (k==1) decodeCBORTextString(cbor,cborLen,off,rpID,sizeof(rpID));
            else if (k==2) { const uint8_t *d; size_t dl;
                decodeCBORByteString(cbor,cborLen,off,d,dl);
                if(dl==32){memcpy(cdHash,d,32);hasCDHash=true;}
            } else if (k==3) { // allowList
                uint8_t mt2; uint64_t acnt;
                if(decodeCBORHeader(cbor,cborLen,off,mt2,acnt)&&mt2==0x80&&acnt>0){
                    size_t so=off; uint64_t mm;
                    if(decodeCBORHeader(cbor,cborLen,off,mt,mm)&&mt==0xA0){
                        for(uint64_t j=0;j<mm;j++){
                            int64_t ak; decodeCBORInt(cbor,cborLen,off,ak);
                            if(ak==2){const uint8_t *d;size_t dl;
                                decodeCBORByteString(cbor,cborLen,off,d,dl);
                                allowLen=min(dl,sizeof(allowCred));
                                memcpy(allowCred,d,allowLen);hasAllow=true;
                            }else skipCBORValue(cbor,cborLen,off);
                        }
                    }else{off=so;skipCBORValue(cbor,cborLen,off);}
                } else { skipCBORValue(cbor,cborLen,off); }
            } else skipCBORValue(cbor,cborLen,off);
        }
    }

    uint8_t rpIDHash[32]; mbedtls_sha256((const uint8_t*)rpID,strlen(rpID),rpIDHash,0);
    std::vector<FIDO2Credential> creds;
    findCredentialsByRP(rpIDHash,creds);
    if (creds.empty()) { sendError(CTAP_GET_ASSERTION,CTAP2_ERR_NO_CREDENTIALS); return; }

    FIDO2Credential *match=nullptr;
    for(auto &c:creds){
        if(hasAllow&&allowLen>0){
            if(memcmp(c.credentialID,allowCred,min(allowLen,(size_t)32))==0){match=&c;break;}
        }else{match=&c;break;}
    }
    if(!match){sendError(CTAP_GET_ASSERTION,CTAP2_ERR_NO_CREDENTIALS);return;}

    _signCounter++;
    { Preferences p; p.begin(FIDO_NS,false); p.putUInt(KEY_COUNTER,_signCounter); p.end(); }

    // authData
    uint8_t authData[37]; memset(authData,0,sizeof(authData));
    memcpy(authData,rpIDHash,32);
    authData[32]=0x05; // UP+AT
    authData[33]=(_signCounter>>24)&0xFF; authData[34]=(_signCounter>>16)&0xFF;
    authData[35]=(_signCounter>>8)&0xFF; authData[36]=_signCounter&0xFF;

    // sign authData + clientDataHash
    uint8_t sigMsg[128]; size_t smLen=37;
    memcpy(sigMsg,authData,37);
    if(hasCDHash){memcpy(sigMsg+37,cdHash,32);smLen=69;}

    // Load key from DER
    mbedtls_pk_context pk;
    if (!pkFromDER(&pk, match->keyDER, match->keyDERLen)) {
        sendError(CTAP_GET_ASSERTION,CTAP1_ERR_OTHER);
        return;
    }

    uint8_t sigDer[128]; size_t sigDerLen=0;
    if (mbedtls_pk_sign(&pk,MBEDTLS_MD_SHA256,
                        sigMsg,smLen,
                        sigDer,sizeof(sigDer),&sigDerLen,
                        mbedtls_ctr_drbg_random,&_crypto->getCTRDRBG())) {
        mbedtls_pk_free(&pk);
        sendError(CTAP_GET_ASSERTION,CTAP1_ERR_OTHER);
        return;
    }
    uint8_t sigRS[64]; derSigToRS(sigDer,sigDerLen,sigRS,64);
    mbedtls_pk_free(&pk);

    // Build response
    uint8_t resp[512]; size_t ro=0;
    ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,4);
    ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,1); // credential
    ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,2);
    ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"type");
    ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"public-key");
    ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"id");
    ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,match->credentialID,32);
    ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,2); // authData
    ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,authData,37);
    ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,3); // signature
    ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,sigRS,64);
    if(strlen(match->userName)>0){
        ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,4);
        ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,2);
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"name");
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,match->userName);
        ro+=encodeCBORTextString(resp+ro,sizeof(resp)-ro,"id");
        ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,match->userID,match->userIDLen);
    }
    sendResponse(CTAP_GET_ASSERTION,resp,ro);
}

// ---- ClientPIN ----

void FIDO2Manager::handleClientPIN(const uint8_t*,size_t){
    uint8_t resp[16]; size_t ro=0;
    ro+=encodeCBORMap(resp+ro,sizeof(resp)-ro,2);
    ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,1); ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,0);
    ro+=encodeCBORInt(resp+ro,sizeof(resp)-ro,2); ro+=encodeCBORByteString(resp+ro,sizeof(resp)-ro,nullptr,0);
    sendResponse(CTAP_CLIENT_PIN,resp,ro);
}

// ---- Reset ----

void FIDO2Manager::handleReset(){
    { Preferences p; p.begin(FIDO_NS,false); p.clear(); p.end(); }
    _credentialCount=0; _signCounter=0;
    sendResponse(CTAP_RESET,nullptr,0);
}
void FIDO2Manager::resetCredentials(){handleReset();}

// ---- SEND RESPONSES ----

void FIDO2Manager::sendResponse(uint8_t cmd, const uint8_t *payload, size_t payloadLen){
    if(!_connected||!_controlPoint)return;
    size_t total=1+payloadLen;
    uint8_t *resp=new uint8_t[total];
    resp[0]=0x00; if(payloadLen)memcpy(resp+1,payload,payloadLen);
    _controlPoint->setValue(resp,total); _controlPoint->indicate();
    delete[] resp;
}
void FIDO2Manager::sendError(uint8_t cmd, uint8_t status){
    if(!_connected||!_controlPoint)return;
    uint8_t r[1]={status}; _controlPoint->setValue(r,1); _controlPoint->indicate();
}
void FIDO2Manager::sendKeepAlive(uint8_t status){
    if(!_connected||!_status)return;
    uint8_t ka[1]={status}; _status->setValue(ka,1); _status->notify();
}

// ---- BUILD AUTHENTICATOR DATA (MakeCredential) ----

size_t FIDO2Manager::buildAuthData(const uint8_t *rpIDHash,
                                    const uint8_t *credID, uint16_t credIDLen,
                                    const uint8_t *pubX, const uint8_t *pubY,
                                    uint8_t *out, size_t outLen){
    size_t o=0;
    if(o+32>outLen)return 0; memcpy(out+o,rpIDHash,32); o+=32;
    if(o+1>outLen)return 0; out[o++]=0x41; // UP+AT
    _signCounter++;
    if(o+4>outLen)return 0;
    out[o++]=(_signCounter>>24)&0xFF; out[o++]=(_signCounter>>16)&0xFF;
    out[o++]=(_signCounter>>8)&0xFF; out[o++]=_signCounter&0xFF;
    { Preferences p; p.begin(FIDO_NS,false); p.putUInt(KEY_COUNTER,_signCounter); p.end(); }

    if(o+16>outLen)return 0; memcpy(out+o,_aaguid,16); o+=16;
    if(o+2>outLen)return 0; out[o++]=(credIDLen>>8)&0xFF; out[o++]=credIDLen&0xFF;
    if(o+credIDLen>outLen)return 0; memcpy(out+o,credID,credIDLen); o+=credIDLen;

    // COSE_Key
    o+=encodeCOSEKey(out+o,outLen-o,pubX,pubY);
    return o;
}

size_t FIDO2Manager::encodeCOSEKey(uint8_t *buf, size_t bufSize, const uint8_t *x, const uint8_t *y){
    size_t o=0;
    o+=encodeCBORMap(buf+o,bufSize-o,5);
    o+=encodeCBORInt(buf+o,bufSize-o,1); o+=encodeCBORInt(buf+o,bufSize-o,2);  // kty=EC2
    o+=encodeCBORInt(buf+o,bufSize-o,3); o+=encodeCBORInt(buf+o,bufSize-o,-7); // alg=ES256
    o+=encodeCBORInt(buf+o,bufSize-o,-1); o+=encodeCBORInt(buf+o,bufSize-o,1); // crv=P-256
    o+=encodeCBORInt(buf+o,bufSize-o,-2); o+=encodeCBORByteString(buf+o,bufSize-o,x,32);
    o+=encodeCBORInt(buf+o,bufSize-o,-3); o+=encodeCBORByteString(buf+o,bufSize-o,y,32);
    return o;
}

// ---- CBOR ENCODING ----

size_t FIDO2Manager::encodeCBORHeader(uint8_t *buf, size_t bufSize, uint8_t major, uint64_t val){
    if(bufSize<1)return 0;
    if(val<=23)               { buf[0]=major|(uint8_t)val; return 1; }
    if(val<=0xFF&&bufSize>=2) { buf[0]=major|24; buf[1]=(uint8_t)val; return 2; }
    if(val<=0xFFFF&&bufSize>=3){ buf[0]=major|25; buf[1]=val>>8; buf[2]=val; return 3; }
    if(val<=0xFFFFFFFFULL&&bufSize>=5){ buf[0]=major|26;
        buf[1]=val>>24; buf[2]=val>>16; buf[3]=val>>8; buf[4]=val; return 5; }
    if(bufSize>=9){ buf[0]=major|27;
        for(int i=0;i<8;i++) buf[1+i]=(val>>(56-8*i))&0xFF; return 9; }
    return 0;
}
size_t FIDO2Manager::encodeCBORInt(uint8_t *b, size_t bs, int64_t v){
    return v>=0 ? encodeCBORHeader(b,bs,0x00,v) : encodeCBORHeader(b,bs,0x20,(uint64_t)(-1-v));
}
size_t FIDO2Manager::encodeCBORByteString(uint8_t *b, size_t bs, const uint8_t *d, size_t dl){
    size_t o=encodeCBORHeader(b,bs,0x40,dl);
    if(!o||o+dl>bs)return 0; if(d&&dl)memcpy(b+o,d,dl); return o+dl;
}
size_t FIDO2Manager::encodeCBORTextString(uint8_t *b, size_t bs, const char *s){
    return encodeCBORByteString(b,bs,(const uint8_t*)s,strlen(s));
}
size_t FIDO2Manager::encodeCBORArray(uint8_t *b, size_t bs, size_t c){ return encodeCBORHeader(b,bs,0x80,c); }
size_t FIDO2Manager::encodeCBORMap(uint8_t *b, size_t bs, size_t c){ return encodeCBORHeader(b,bs,0xA0,c); }
size_t FIDO2Manager::encodeCBORBool(uint8_t *b, size_t bs, bool v){
    if(bs<1)return 0; b[0]=v?0xF5:0xF4; return 1;
}
size_t FIDO2Manager::encodeCBORNull(uint8_t *b, size_t bs){
    if(bs<1)return 0; b[0]=0xF6; return 1;
}

// ---- CBOR DECODING ----

bool FIDO2Manager::decodeCBORHeader(const uint8_t *d, size_t len, size_t &off, uint8_t &mt, uint64_t &val){
    if(off>=len)return false;
    uint8_t init=d[off++]; mt=init&0xE0; uint8_t x=init&0x1F;
    if(x<=23) val=x;
    else if(x==24){if(off+1>len)return false; val=d[off++];}
    else if(x==25){if(off+2>len)return false; val=((uint64_t)d[off]<<8)|d[off+1]; off+=2;}
    else if(x==26){if(off+4>len)return false;
        val=((uint64_t)d[off]<<24)|((uint64_t)d[off+1]<<16)|((uint64_t)d[off+2]<<8)|d[off+3]; off+=4;}
    else if(x==27){if(off+8>len)return false; val=0;
        for(int i=0;i<8;i++)val=(val<<8)|d[off+i]; off+=8;}
    else return false;
    return true;
}
bool FIDO2Manager::decodeCBORInt(const uint8_t *d, size_t len, size_t &off, int64_t &v){
    uint8_t mt; uint64_t raw;
    if(!decodeCBORHeader(d,len,off,mt,raw))return false;
    if(mt==0x00){v=(int64_t)raw;return true;}
    if(mt==0x20){v=-1-(int64_t)raw;return true;}
    return false;
}
bool FIDO2Manager::decodeCBORByteString(const uint8_t *d, size_t len, size_t &off, const uint8_t *&out, size_t &outLen){
    uint8_t mt; uint64_t rl;
    if(!decodeCBORHeader(d,len,off,mt,rl)||mt!=0x40)return false;
    if(off+rl>len)return false; out=d+off; outLen=(size_t)rl; off+=outLen; return true;
}
bool FIDO2Manager::decodeCBORTextString(const uint8_t *d, size_t len, size_t &off, char *out, size_t outSize){
    const uint8_t *s; size_t sl;
    if(!decodeCBORByteString(d,len,off,s,sl))return false;
    size_t cp=min(sl,outSize-1); memcpy(out,s,cp); out[cp]=0; return true;
}
bool FIDO2Manager::skipCBORValue(const uint8_t *d, size_t len, size_t &off){
    if(off>=len)return false;
    uint8_t init=d[off]; (void)init; uint8_t mt; uint64_t val;
    if(!decodeCBORHeader(d,len,off,mt,val))return false;
    switch(mt){
        case 0x00: case 0x20: case 0xE0: break;
        case 0x40: case 0x60: if(off+val>len)return false; off+=(size_t)val; break;
        case 0x80: for(uint64_t i=0;i<val;i++){if(!skipCBORValue(d,len,off))return false;} break;
        case 0xA0: for(uint64_t i=0;i<val*2;i++){if(!skipCBORValue(d,len,off))return false;} break;
        default: return false;
    }
    return true;
}

// ---- CREDENTIAL STORAGE (NVS) ----

bool FIDO2Manager::storeCredential(const FIDO2Credential &cred){
    if(_credentialCount>=_credentialMax)return false;
    Preferences p; p.begin(FIDO_NS,false);
    char k[32];
    snprintf(k,sizeof(k),"cr_%d_id",_credentialCount); p.putBytes(k,cred.credentialID,32);
    snprintf(k,sizeof(k),"cr_%d_kd",_credentialCount); p.putBytes(k,cred.keyDER,cred.keyDERLen);
    snprintf(k,sizeof(k),"cr_%d_kl",_credentialCount); p.putUShort(k,cred.keyDERLen);
    snprintf(k,sizeof(k),"cr_%d_pub",_credentialCount); p.putBytes(k,cred.publicKey,64);
    snprintf(k,sizeof(k),"cr_%d_rp",_credentialCount); p.putBytes(k,cred.rpIDHash,32);
    snprintf(k,sizeof(k),"cr_%d_rps",_credentialCount); p.putString(k,cred.rpID);
    snprintf(k,sizeof(k),"cr_%d_un",_credentialCount); p.putString(k,cred.userName);
    snprintf(k,sizeof(k),"cr_%d_uid",_credentialCount); p.putBytes(k,cred.userID,cred.userIDLen);
    snprintf(k,sizeof(k),"cr_%d_udl",_credentialCount); p.putUChar(k,cred.userIDLen);
    snprintf(k,sizeof(k),"cr_%d_v",_credentialCount); p.putBool(k,true);
    _credentialCount++; p.putUInt(KEY_CRED_COUNT,_credentialCount);
    p.end(); FIDO_LOG("credential #%d stored: %s",_credentialCount-1,cred.rpID);
    return true;
}

bool FIDO2Manager::findCredentialsByRP(const uint8_t *rpIDHash, std::vector<FIDO2Credential> &out){
    Preferences p; p.begin(FIDO_NS,true);
    for(int i=0;i<_credentialCount;i++){
        char k[32]; snprintf(k,sizeof(k),"cr_%d_v",i);
        if(!p.getBool(k,false))continue;
        snprintf(k,sizeof(k),"cr_%d_rp",i);
        uint8_t srp[32]; p.getBytes(k,srp,32);
        if(memcmp(srp,rpIDHash,32)!=0)continue;
        FIDO2Credential c; memset(&c,0,sizeof(c));
        memcpy(c.rpIDHash,srp,32);
        snprintf(k,sizeof(k),"cr_%d_id",i); p.getBytes(k,c.credentialID,32);
        snprintf(k,sizeof(k),"cr_%d_kl",i); c.keyDERLen=p.getUShort(k,0);
        snprintf(k,sizeof(k),"cr_%d_kd",i); p.getBytes(k,c.keyDER,c.keyDERLen);
        snprintf(k,sizeof(k),"cr_%d_pub",i); p.getBytes(k,c.publicKey,64);
        snprintf(k,sizeof(k),"cr_%d_rps",i); strncpy(c.rpID,p.getString(k).c_str(),sizeof(c.rpID)-1);
        snprintf(k,sizeof(k),"cr_%d_un",i); strncpy(c.userName,p.getString(k).c_str(),sizeof(c.userName)-1);
        snprintf(k,sizeof(k),"cr_%d_uid",i); c.userIDLen=p.getBytes(k,c.userID,32);
        c.isValid=true; out.push_back(c);
    }
    p.end(); return !out.empty();
}

bool FIDO2Manager::findCredential(const uint8_t *rpIDHash, const uint8_t *credID, uint16_t credIDLen, FIDO2Credential &out){
    std::vector<FIDO2Credential> list;
    findCredentialsByRP(rpIDHash,list);
    for(auto &c:list){
        if(memcmp(c.credentialID,credID,min((size_t)credIDLen,(size_t)32))==0){
            out=c; return true;
        }
    }
    return false;
}
