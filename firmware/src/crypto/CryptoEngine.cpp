/**
 * @file CryptoEngine.cpp
 * @brief 加密引擎模块实现
 *
 * 使用 mbedTLS 提供 AES-256-GCM 加解密和 ECDSA P-256 签名/验签。
 * 密钥安全地存储在 NVS "passkey" 命名空间中。
 */

#include "CryptoEngine.h"
#include "SecureStorage.h"
#include "config.h"

#include <mbedtls/gcm.h>
#include <mbedtls/error.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/ecdsa.h>

// NVS 键名
#define NVS_KEY_MASTER_KEY   "master_key"
#define NVS_KEY_ECDSA_PRIV   "ecdsa_priv"
#define NVS_KEY_ECDSA_PUB    "ecdsa_pub"

// GCM 参数
#define GCM_IV_LEN    12
#define GCM_TAG_LEN   16

// 外部全局 SecureStorage 实例
extern SecureStorage secureStorage;

CryptoEngine::CryptoEngine()
    : ready(false)
    , ecdsaReady(false)
{
    memset(masterKey, 0, sizeof(masterKey));
}

CryptoEngine::~CryptoEngine()
{
    if (ecdsaReady) {
        mbedtls_pk_free(&pkContext);
    }
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);
}

bool CryptoEngine::init()
{
    // 初始化 RNG
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctrDrbg);

    const char *pers = "passkey_crypto";
    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                                     (const uint8_t *)pers, strlen(pers));
    if (ret != 0) {
        Serial.println("[Crypto] RNG 初始化失败");
        return false;
    }

    // 加载或生成密钥
    if (!loadKeys()) {
        Serial.println("[Crypto] 密钥加载失败");
        return false;
    }

    ready = true;
    return true;
}

// ==================== 密钥生成 ====================

bool CryptoEngine::generateMasterKey()
{
    int ret = mbedtls_ctr_drbg_random(&ctrDrbg, masterKey, sizeof(masterKey));
    if (ret != 0) {
        Serial.println("[Crypto] 主密钥生成失败");
        return false;
    }

    return saveMasterKey();
}

bool CryptoEngine::generateECDSAKeyPair()
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) {
        Serial.println("[Crypto] ECDSA 上下文设置失败");
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                              mbedtls_pk_ec(pk),
                              mbedtls_ctr_drbg_random,
                              &ctrDrbg);
    if (ret != 0) {
        Serial.println("[Crypto] ECDSA 密钥对生成失败");
        mbedtls_pk_free(&pk);
        return false;
    }

    // 复制到成员变量
    if (ecdsaReady) {
        mbedtls_pk_free(&pkContext);
    }
    mbedtls_pk_init(&pkContext);
    mbedtls_pk_copy(&pkContext, &pk);
    mbedtls_pk_free(&pk);
    ecdsaReady = true;

    return saveECDSAKeyPair();
}

// ==================== 加密/解密 ====================

bool CryptoEngine::encryptAESGCM(const uint8_t *plain, size_t plainLen,
                                  const uint8_t *key,
                                  uint8_t *cipher, size_t &cipherLen)
{
    // 输出布局: [IV(12)] + [ciphertext(plainLen)] + [tag(16)]
    size_t needed = GCM_IV_LEN + plainLen + GCM_TAG_LEN;
    if (cipherLen < needed) {
        cipherLen = needed;
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    // 生成随机 IV
    uint8_t iv[GCM_IV_LEN];
    ret = mbedtls_ctr_drbg_random(&ctrDrbg, iv, GCM_IV_LEN);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    // 加密
    uint8_t *tag = cipher + GCM_IV_LEN + plainLen;
    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     plainLen,
                                     iv, GCM_IV_LEN,
                                     NULL, 0,           // AAD
                                     plain, cipher + GCM_IV_LEN,
                                     tag, GCM_TAG_LEN);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    // 复制 IV 到输出
    memcpy(cipher, iv, GCM_IV_LEN);
    cipherLen = needed;

    mbedtls_gcm_free(&gcm);
    return true;
}

bool CryptoEngine::decryptAESGCM(const uint8_t *cipher, size_t cipherLen,
                                  const uint8_t *key,
                                  uint8_t *plain, size_t &plainLen)
{
    // 输入布局: [IV(12)] + [ciphertext] + [tag(16)]
    if (cipherLen < GCM_IV_LEN + GCM_TAG_LEN) {
        return false;
    }

    size_t ctLen = cipherLen - GCM_IV_LEN - GCM_TAG_LEN;
    if (plainLen < ctLen) {
        plainLen = ctLen;
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    const uint8_t *iv  = cipher;
    const uint8_t *ct  = cipher + GCM_IV_LEN;
    const uint8_t *tag = cipher + cipherLen - GCM_TAG_LEN;

    ret = mbedtls_gcm_auth_decrypt(&gcm, ctLen,
                                    iv, GCM_IV_LEN,
                                    NULL, 0,
                                    tag, GCM_TAG_LEN,
                                    ct, plain);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    plainLen = ctLen;

    mbedtls_gcm_free(&gcm);
    return true;
}

// ==================== 签名 ====================

bool CryptoEngine::signChallenge(const uint8_t *challenge, size_t challengeLen,
                                  uint8_t *signature, size_t &sigLen)
{
    if (!ecdsaReady) {
        Serial.println("[Crypto] ECDSA 密钥未就绪，无法签名");
        return false;
    }

    // 计算 SHA-256 摘要
    uint8_t hash[32];
    int ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         challenge, challengeLen, hash);
    if (ret != 0) {
        Serial.println("[Crypto] SHA-256 计算失败");
        return false;
    }

    // 签名
    size_t actualSigLen = sigLen;
    ret = mbedtls_pk_sign(&pkContext, MBEDTLS_MD_SHA256,
                          hash, sizeof(hash),
                          signature, &actualSigLen,
                          mbedtls_ctr_drbg_random, &ctrDrbg);
    if (ret != 0) {
        Serial.printf("[Crypto] 签名失败: ret=%d\n", ret);
        return false;
    }

    sigLen = actualSigLen;
    return true;
}

bool CryptoEngine::verifySignature(const uint8_t *challenge, size_t challengeLen,
                                    const uint8_t *signature, size_t sigLen,
                                    const uint8_t *pubKey, size_t pubKeyLen)
{
    // 从 DER 公钥加载 PK 上下文
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk, pubKey, pubKeyLen);
    if (ret != 0) {
        Serial.printf("[Crypto] 公钥解析失败: ret=%d\n", ret);
        mbedtls_pk_free(&pk);
        return false;
    }

    // 计算 SHA-256 摘要
    uint8_t hash[32];
    ret = mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                     challenge, challengeLen, hash);
    if (ret != 0) {
        mbedtls_pk_free(&pk);
        return false;
    }

    // 验签
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                            hash, sizeof(hash),
                            signature, sigLen);
    mbedtls_pk_free(&pk);

    return (ret == 0);
}

// ==================== 工具方法 ====================

String CryptoEngine::getPublicKeyBase64()
{
    if (!ecdsaReady) {
        return "";
    }

    // 导出公钥 DER (SubjectPublicKeyInfo)
    uint8_t derBuf[256];
    size_t derLen = sizeof(derBuf);

    // mbedtls_pk_write_pubkey_der 从尾部写入
    int ret = mbedtls_pk_write_pubkey_der(&pkContext, derBuf, sizeof(derBuf));
    if (ret < 0) {
        Serial.println("[Crypto] 公钥导出失败");
        return "";
    }
    derLen = (size_t)ret;
    // 数据在 derBuf + sizeof(derBuf) - derLen 处
    uint8_t *derStart = derBuf + sizeof(derBuf) - derLen;

    // Base64 编码
    size_t b64Len = 0;
    ret = mbedtls_base64_encode(NULL, 0, &b64Len, derStart, derLen);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return "";
    }

    size_t b64BufSize = b64Len + 1;
    uint8_t *b64Buf = (uint8_t *)malloc(b64BufSize);
    if (!b64Buf) {
        return "";
    }

    ret = mbedtls_base64_encode(b64Buf, b64BufSize, &b64Len, derStart, derLen);
    if (ret != 0) {
        free(b64Buf);
        return "";
    }

    b64Buf[b64Len] = '\0';
    String result = String((char *)b64Buf);
    free(b64Buf);

    return result;
}

// ==================== 私有方法 ====================

bool CryptoEngine::loadKeys()
{
    // 尝试加载主密钥
    size_t keyLen = sizeof(masterKey);
    bool hasMasterKey = secureStorage.exists(NVS_KEY_MASTER_KEY);

    if (hasMasterKey) {
        if (!secureStorage.load(NVS_KEY_MASTER_KEY, masterKey, keyLen)) {
            Serial.println("[Crypto] 主密钥加载失败");
            return false;
        }
        if (keyLen != sizeof(masterKey)) {
            Serial.println("[Crypto] 主密钥长度异常，重新生成");
            hasMasterKey = false;
        }
    }

    if (!hasMasterKey) {
        if (!generateMasterKey()) {
            return false;
        }
        Serial.println("[Crypto] 已生成新的主密钥");
    }

    // 尝试加载 ECDSA 密钥对
    if (secureStorage.exists(NVS_KEY_ECDSA_PRIV)) {
        size_t privLen = 0;
        // 先获取长度
        secureStorage.load(NVS_KEY_ECDSA_PRIV, NULL, privLen);
        if (privLen > 0) {
            uint8_t *privBuf = (uint8_t *)malloc(privLen);
            if (privBuf) {
                if (secureStorage.load(NVS_KEY_ECDSA_PRIV, privBuf, privLen)) {
                    mbedtls_pk_init(&pkContext);
                    int ret = mbedtls_pk_parse_key(&pkContext, privBuf, privLen, NULL, 0);
                    if (ret == 0) {
                        ecdsaReady = true;
                    } else {
                        Serial.printf("[Crypto] ECDSA 私钥解析失败: ret=%d\n", ret);
                    }
                }
                free(privBuf);
            }
        }
    }

    if (!ecdsaReady) {
        if (!generateECDSAKeyPair()) {
            Serial.println("[Crypto] ECDSA 密钥对生成失败");
            return false;
        }
        Serial.println("[Crypto] 已生成新的 ECDSA 密钥对");
    }

    return true;
}

bool CryptoEngine::saveMasterKey()
{
    return secureStorage.save(NVS_KEY_MASTER_KEY, masterKey, sizeof(masterKey));
}

bool CryptoEngine::saveECDSAKeyPair()
{
    if (!ecdsaReady) return false;

    // 导出私钥 DER
    uint8_t privBuf[512];
    int ret = mbedtls_pk_write_key_der(&pkContext, privBuf, sizeof(privBuf));
    if (ret < 0) {
        Serial.println("[Crypto] ECDSA 私钥导出失败");
        return false;
    }
    size_t privLen = (size_t)ret;
    uint8_t *privStart = privBuf + sizeof(privBuf) - privLen;

    if (!secureStorage.save(NVS_KEY_ECDSA_PRIV, privStart, privLen)) {
        return false;
    }

    // 导出公钥 DER
    uint8_t pubBuf[256];
    ret = mbedtls_pk_write_pubkey_der(&pkContext, pubBuf, sizeof(pubBuf));
    if (ret < 0) {
        Serial.println("[Crypto] ECDSA 公钥导出失败");
        return false;
    }
    size_t pubLen = (size_t)ret;
    uint8_t *pubStart = pubBuf + sizeof(pubBuf) - pubLen;

    if (!secureStorage.save(NVS_KEY_ECDSA_PUB, pubStart, pubLen)) {
        return false;
    }

    return true;
}
