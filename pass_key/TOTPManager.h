/**
 * @file TOTPManager.h
 * @brief TOTP 动态口令管理模块 — 基于 RFC 6238
 *
 * 支持多账户管理、Base32 密钥解码、HMAC-SHA1 运算、
 * 动态截断，以及种子密钥的 AES-256-GCM 加密存储。
 */

#ifndef TOTP_MANAGER_H
#define TOTP_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <cstdint>

class SecureStorage;

class TOTPManager
{
public:
    TOTPManager();
    ~TOTPManager();

    /**
     * @brief 初始化 TOTP 管理器
     * - 加载或创建主密钥（AES-256 密钥）
     * - 从 SecureStorage 加载已保存的账户列表
     * - 添加 3 个默认测试账户（首次运行）
     * @return true 成功
     */
    bool init();

    // ==================== 账户管理 ====================

    /**
     * @brief 添加一个 TOTP 账户
     * @param name         账户名称（如 "Google"）
     * @param base32Secret Base32 编码的种子密钥
     * @return true 添加成功
     */
    bool addAccount(const char *name, const char *base32Secret);

    /**
     * @brief 删除指定账户
     * @param name 账户名称
     * @return true 删除成功
     */
    bool removeAccount(const char *name);

    /**
     * @brief 清空所有账户（用于远程同步）
     */
    void clearAllAccounts();

    /**
     * @brief 从服务器同步数据替换全部账户
     * @param jsonArray JSON 数组字符串: [{"issuer":"...","accountName":"...","secret":"..."}]
     * @return 成功同步的账户数
     */
    int syncFromServer(const char *jsonArray);

    /**
     * @brief 检查是否存在同名账户
     */
    bool hasAccount(const char *name) const;

    /**
     * @brief 获取已注册的账户数量
     */
    int getAccountCount();

    /**
     * @brief 获取第 index 个账户的名称
     * @param index 索引（从 0 开始）
     * @return 账户名称，为空表示索引无效
     */
    String getAccountName(int index);

    /**
     * @brief 获取所有账户名（逗号分隔）
     */
    String getAccountList();

    // ==================== TOTP 码生成 ====================

    /**
     * @brief 为指定账户生成当前 6 位 TOTP 验证码
     * @param name 账户名称
     * @return 6 位数字字符串，空表示未找到
     */
    String generateCode(const char *name);

    /**
     * @brief 为第 index 个账户生成当前 6 位 TOTP 验证码
     * @param index 账户索引
     * @return 6 位数字字符串，空表示无效索引
     */
    String generateCodeAtIndex(int index);

    /**
     * @brief 获取当前 30 秒窗口的剩余秒数
     */
    uint32_t getRemainingSeconds();

    /**
     * @brief 计算当前时间计数器（UnixTime / 30）
     */
    uint32_t getTimeCounter();

private:
    // ==================== 内部数据结构 ====================
    struct Account {
        String name;
        String base32Secret;  // Base32 编码的种子密钥（内存中为明文）
    };

    std::vector<Account> accounts;
    SecureStorage       *storage;
    std::vector<uint8_t> masterKey;   // AES-256-GCM 主密钥（32 字节）
    bool                 initialized;

    // ==================== TOTP 算法 ====================

    /**
     * @brief Base32 解码（RFC 4648）
     * @param base32 Base32 编码字符串
     * @return 解码后的字节数组，为空表示解码失败
     */
    static std::vector<uint8_t> base32Decode(const String &base32);

    /**
     * @brief 计算 HMAC-SHA1（使用 mbedTLS）
     * @param key  密钥
     * @param data 数据
     * @return 20 字节 HMAC-SHA1 结果
     */
    static std::vector<uint8_t> hmacSha1(const std::vector<uint8_t> &key,
                                         const std::vector<uint8_t> &data);

    /**
     * @brief 动态截断（RFC 4226 Section 5.3）
     * @param hmacResult HMAC-SHA1 结果（20 字节）
     * @return 31 位整数
     */
    static uint32_t dynamicTruncation(const std::vector<uint8_t> &hmacResult);

    /**
     * @brief 计算 TOTP 验证码
     * @param secretBytes 解码后的种子密钥字节
     * @param timeCounter 时间计数器
     * @return 6 位数字字符串
     */
    String computeTOTP(const std::vector<uint8_t> &secretBytes,
                       uint64_t timeCounter);

    // ==================== 持久化与加密 ====================

    /**
     * @brief 从 SecureStorage 加载账户列表
     * @return true 加载成功
     */
    bool loadAccounts();

    /**
     * @brief 保存账户列表到 SecureStorage
     * @return true 保存成功
     */
    bool saveAccounts();

    /**
     * @brief 加载或创建 AES-256-GCM 主密钥
     * @return true 成功
     */
    bool loadOrCreateMasterKey();

    /**
     * @brief 使用主密钥加密数据（AES-256-GCM）
     * @param plaintext 明文
     * @return 密文（格式：12 字节 IV + 加密数据 + 16 字节认证标签）
     */
    std::vector<uint8_t> encryptData(const std::vector<uint8_t> &plaintext);

    /**
     * @brief 使用主密钥解密数据（AES-256-GCM）
     * @param ciphertext 密文（格式：12 字节 IV + 加密数据 + 16 字节认证标签）
     * @return 明文，为空表示解密失败
     */
    std::vector<uint8_t> decryptData(const std::vector<uint8_t> &ciphertext);
};

#endif // TOTP_MANAGER_H
