/**
 * @file CryptoEngine.h
 * @brief 加密引擎模块 — AES-256-GCM 加密/解密 & ECDSA P-256 签名/验签
 *
 * 使用 mbedTLS 实现：
 * - AES-256-GCM 对称加解密
 * - ECDSA P-256 密钥生成、签名和验签
 * - 主密钥和 ECDSA 密钥对存储于 NVS
 */

#ifndef CRYPTO_ENGINE_H
#define CRYPTO_ENGINE_H

#include <Arduino.h>
#include <mbedtls/pk.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

class SecureStorage;

class CryptoEngine
{
public:
    CryptoEngine();
    ~CryptoEngine();

    /**
     * @brief 初始化加密引擎
     * - 加载或生成主密钥
     * - 加载或生成 ECDSA 密钥对
     * @return true 成功
     */
    bool init();

    // ==================== 密钥生成 ====================

    /**
     * @brief 生成 256 位 AES 主密钥并存入 NVS
     * @return true 成功
     */
    bool generateMasterKey();

    /**
     * @brief 生成 ECDSA P-256 密钥对并存入 NVS
     * @return true 成功
     */
    bool generateECDSAKeyPair();

    // ==================== 加密/解密 ====================

    /**
     * @brief AES-256-GCM 加密
     * @param plain     明文
     * @param plainLen  明文长度
     * @param key       密钥（32 字节）
     * @param cipher    输出密文（含 IV + tag）
     * @param cipherLen 输入：缓冲区大小，输出：实际密文长度
     * @return true 加密成功
     */
    bool encryptAESGCM(const uint8_t *plain, size_t plainLen,
                       const uint8_t *key,
                       uint8_t *cipher, size_t &cipherLen);

    /**
     * @brief AES-256-GCM 解密
     * @param cipher    密文（含 IV + tag）
     * @param cipherLen 密文长度
     * @param key       密钥（32 字节）
     * @param plain     输出明文
     * @param plainLen  输入：缓冲区大小，输出：实际明文长度
     * @return true 解密成功
     */
    bool decryptAESGCM(const uint8_t *cipher, size_t cipherLen,
                       const uint8_t *key,
                       uint8_t *plain, size_t &plainLen);

    // ==================== 签名 ====================

    /**
     * @brief 使用 ECDSA P-256 对挑战数据签名
     * @param challenge   挑战数据
     * @param challengeLen 挑战数据长度
     * @param signature   输出签名（DER 编码）
     * @param sigLen      输入：缓冲区大小，输出：实际签名长度
     * @return true 签名成功
     */
    bool signChallenge(const uint8_t *challenge, size_t challengeLen,
                       uint8_t *signature, size_t &sigLen);

    /**
     * @brief 验证 ECDSA P-256 签名
     * @param challenge   挑战数据
     * @param challengeLen 挑战数据长度
     * @param signature   签名（DER 编码）
     * @param sigLen      签名长度
     * @param pubKey      公钥（DER 编码 SubjectPublicKeyInfo）
     * @param pubKeyLen   公钥长度
     * @return true 验签通过
     */
    bool verifySignature(const uint8_t *challenge, size_t challengeLen,
                         const uint8_t *signature, size_t sigLen,
                         const uint8_t *pubKey, size_t pubKeyLen);

    // ==================== 工具方法 ====================

    /**
     * @brief 导出公钥的 Base64 编码（用于设备注册）
     * @return Base64 编码的公钥字符串
     */
    String getPublicKeyBase64();

    /**
     * @brief 获取主密钥
     * @return 指向 32 字节主密钥的指针，若未初始化返回 nullptr
     */
    const uint8_t *getMasterKey() const { return masterKey; }

    /**
     * @brief 检查 ECDSA 密钥是否已生成
     */
    bool hasECDSAKey() const { return ecdsaReady; }

private:
    bool    ready;
    bool    ecdsaReady;
    uint8_t masterKey[32];              // AES-256 主密钥

    // mbedTLS 上下文
    mbedtls_pk_context    pkContext;    // ECDSA 密钥对
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_entropy_context entropy;

    // 持久化密钥
    bool loadKeys();
    bool saveMasterKey();
    bool saveECDSAKeyPair();
};

#endif // CRYPTO_ENGINE_H
