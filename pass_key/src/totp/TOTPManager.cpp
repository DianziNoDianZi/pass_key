/**
 * @file TOTPManager.cpp
 * @brief TOTP 动态口令管理模块实现
 *
 * 遵循 RFC 6238 (TOTP) 与 RFC 4226 (HOTP) 规范。
 * 使用 mbedTLS 计算 HMAC-SHA1 和 AES-256-GCM 加密。
 */

#include "TOTPManager.h"
#include "SecureStorage.h"
#include "TimeManager.h"
#include "config.h"
#include <cstring>
#include <cstdio>

// ArduinoJson
#include <ArduinoJson.h>

// mbedTLS 头文件（ESP32-S3 内置）
#include <mbedtls/md.h>
#include <mbedtls/gcm.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

// ==================== Base32 字母表（RFC 4648） ====================
static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/**
 * @brief 将 Base32 字符映射回数值，-1 表示非法
 */
static int base32CharToValue(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

// ==================== 外部全局实例引用 ====================
extern TimeManager timeManager;
extern SecureStorage secureStorage;

// ==================== 构造 / 析构 ====================

TOTPManager::TOTPManager()
    : storage(nullptr)
    , initialized(false)
{
}

TOTPManager::~TOTPManager()
{
}

// ==================== 初始化 ====================

bool TOTPManager::init()
{
    if (initialized) return true;

    storage = &secureStorage;

    // 1. 加载或创建主密钥
    if (!loadOrCreateMasterKey()) {
        Serial.println(F("[TOTP] 主密钥加载/创建失败"));
        return false;
    }

    // 2. 从存储加载已有账户
    loadAccounts();

    // 3. 首次运行时添加默认测试账户
    if (accounts.empty()) {
        // RFC 6238 测试种子："12345678901234567890" 的 Base32 编码
        const char *testSecret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
        addAccount("Google", testSecret);
        addAccount("GitHub", testSecret);
        addAccount("Steam",  testSecret);
        Serial.println(F("[TOTP] 已添加 3 个默认测试账户"));
    }

    initialized = true;
    Serial.printf("[TOTP] 初始化完成，共 %d 个账户\n", accounts.size());
    return true;
}

// ==================== 账户管理 ====================

bool TOTPManager::addAccount(const char *name, const char *base32Secret)
{
    if (!name || !base32Secret || strlen(name) == 0) {
        return false;
    }

    // 检查是否已存在同名账户
    for (const auto &acc : accounts) {
        if (acc.name.equalsIgnoreCase(name)) {
            return false; // 已存在
        }
    }

    // 验证 Base32 密钥是否可解码
    std::vector<uint8_t> decoded = base32Decode(base32Secret);
    if (decoded.empty()) {
        Serial.printf("[TOTP] 警告：账户 '%s' 的 Base32 密钥无效\n", name);
        // 仍然允许添加，但记录警告
    }

    Account acc;
    acc.name         = String(name);
    acc.base32Secret = String(base32Secret);
    accounts.push_back(acc);

    // 持久化
    saveAccounts();
    Serial.printf("[TOTP] 已添加账户: %s\n", name);
    return true;
}

bool TOTPManager::removeAccount(const char *name)
{
    if (!name) return false;

    for (auto it = accounts.begin(); it != accounts.end(); ++it) {
        if (it->name.equalsIgnoreCase(name)) {
            accounts.erase(it);
            saveAccounts();
            Serial.printf("[TOTP] 已删除账户: %s\n", name);
            return true;
        }
    }
    return false;
}

void TOTPManager::clearAllAccounts()
{
    accounts.clear();
    saveAccounts();
    Serial.println("[TOTP] 已清空所有账户");
}

bool TOTPManager::hasAccount(const char *name) const
{
    if (!name) return false;
    for (const auto &acc : accounts) {
        if (acc.name.equalsIgnoreCase(name)) return true;
    }
    return false;
}

int TOTPManager::syncFromServer(const char *jsonArray)
{
    if (!jsonArray) return 0;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonArray);
    if (error) {
        Serial.printf("[TOTP] 同步数据 JSON 解析失败: %s\n", error.c_str());
        return 0;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) return 0;

    accounts.clear();

    for (JsonObject obj : arr) {
        const char *issuer = obj["issuer"].as<const char *>();
        const char *accountName = obj["accountName"].as<const char *>();
        const char *secret = obj["secret"].as<const char *>();

        if (!issuer || !secret) continue;

        // Use accountName if available, otherwise use issuer
        const char *name = (accountName && strlen(accountName) > 0) ? accountName : issuer;

        Account acc;
        acc.name = String(name);
        acc.base32Secret = String(secret);
        accounts.push_back(acc);
    }

    saveAccounts();
    Serial.printf("[TOTP] 从服务器同步了 %d 个账户\n", accounts.size());
    return accounts.size();
}

int TOTPManager::getAccountCount()
{
    return (int)accounts.size();
}

String TOTPManager::getAccountName(int index)
{
    if (index < 0 || index >= (int)accounts.size()) {
        return String();
    }
    return accounts[index].name;
}

String TOTPManager::getAccountList()
{
    String list;
    for (size_t i = 0; i < accounts.size(); i++) {
        if (i > 0) list += ",";
        list += accounts[i].name;
    }
    return list;
}

// ==================== TOTP 码生成 ====================

String TOTPManager::generateCode(const char *name)
{
    if (!name || !initialized) return String();

    for (const auto &acc : accounts) {
        if (acc.name.equalsIgnoreCase(name)) {
            std::vector<uint8_t> secretBytes = base32Decode(acc.base32Secret);
            if (secretBytes.empty()) return String("------");

            uint64_t tc = getTimeCounter();
            return computeTOTP(secretBytes, tc);
        }
    }
    return String();
}

String TOTPManager::generateCodeAtIndex(int index)
{
    if (!initialized || index < 0 || index >= (int)accounts.size()) {
        return String();
    }

    const auto &acc = accounts[index];
    std::vector<uint8_t> secretBytes = base32Decode(acc.base32Secret);
    if (secretBytes.empty()) return String("------");

    uint64_t tc = getTimeCounter();
    return computeTOTP(secretBytes, tc);
}

uint32_t TOTPManager::getRemainingSeconds()
{
    time_t now = timeManager.getUnixTime();
    if (now == 0) {
        // 时间未同步，使用 millis() 模拟（仅供测试）
        now = (time_t)(millis() / 1000);
    }
    return TOTP_PERIOD - (now % TOTP_PERIOD);
}

uint32_t TOTPManager::getTimeCounter()
{
    time_t now = timeManager.getUnixTime();
    if (now == 0) {
        now = (time_t)(millis() / 1000);
    }
    return (uint32_t)(now / TOTP_PERIOD);
}

// ==================== TOTP 算法实现 ====================

std::vector<uint8_t> TOTPManager::base32Decode(const String &base32)
{
    // 去除空格和连字符
    String cleaned;
    for (size_t i = 0; i < base32.length(); i++) {
        char c = base32[i];
        if (c == ' ' || c == '-' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        cleaned += c;
    }

    // 处理填充字符 '='
    size_t padding = 0;
    size_t len = cleaned.length();
    if (len == 0) return {};

    // 从末尾数 '='
    while (padding < len && cleaned[len - 1 - padding] == '=') {
        padding++;
    }
    size_t encodedLen = len - padding;  // 有效字符数
    if (encodedLen == 0) return {};

    // 计算输出字节数：(有效字符数 * 5) / 8
    size_t outLen = (encodedLen * 5) / 8;
    std::vector<uint8_t> result(outLen, 0);

    int buffer   = 0;
    int bitsLeft = 0;
    size_t outIdx = 0;

    for (size_t i = 0; i < encodedLen; i++) {
        int val = base32CharToValue(cleaned[i]);
        if (val < 0) {
            // 非法字符
            return {};
        }

        buffer = (buffer << 5) | val;
        bitsLeft += 5;

        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            if (outIdx < outLen) {
                result[outIdx++] = (uint8_t)((buffer >> bitsLeft) & 0xFF);
            }
        }
    }

    return result;
}

std::vector<uint8_t> TOTPManager::hmacSha1(const std::vector<uint8_t> &key,
                                            const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> result(20, 0); // SHA1 = 20 字节

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    int ret = mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return {};
    }

    ret = mbedtls_md_hmac_starts(&ctx, key.data(), key.size());
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return {};
    }

    ret = mbedtls_md_hmac_update(&ctx, data.data(), data.size());
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return {};
    }

    ret = mbedtls_md_hmac_finish(&ctx, result.data());
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return {};
    }

    mbedtls_md_free(&ctx);
    return result;
}

uint32_t TOTPManager::dynamicTruncation(const std::vector<uint8_t> &hmacResult)
{
    if (hmacResult.size() < 20) return 0;

    // RFC 4226 Section 5.3: 取最后一个字节的低 4 位作为偏移量
    int offset = hmacResult[19] & 0x0F;

    // 提取 4 字节（大端序），将最高位清零（31 位）
    uint32_t binCode =
        ((uint32_t)(hmacResult[offset]     & 0x7F) << 24) |
        ((uint32_t) hmacResult[offset + 1]         << 16) |
        ((uint32_t) hmacResult[offset + 2]         <<  8) |
        ((uint32_t) hmacResult[offset + 3]);

    return binCode;
}

String TOTPManager::computeTOTP(const std::vector<uint8_t> &secretBytes,
                                uint64_t timeCounter)
{
    // 1. 将时间计数器转为 8 字节大端序
    std::vector<uint8_t> msg(8, 0);
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)(timeCounter & 0xFF);
        timeCounter >>= 8;
    }

    // 2. 计算 HMAC-SHA1
    std::vector<uint8_t> hmac = hmacSha1(secretBytes, msg);
    if (hmac.size() != 20) {
        return String("000000");
    }

    // 3. 动态截断
    uint32_t binary = dynamicTruncation(hmac);

    // 4. 取模 10^6 得到 6 位数字
    uint32_t otp = binary % 1000000;

    // 5. 格式化为 6 位数字字符串（前导零补全）
    char code[8];
    snprintf(code, sizeof(code), "%06u", otp);
    return String(code);
}

// ==================== 持久化 ====================

bool TOTPManager::loadAccounts()
{
    if (!storage) return false;

    std::vector<uint8_t> encrypted = storage->load("totp_accts");
    if (encrypted.empty()) {
        // 没有已保存的数据
        return true;
    }

    // 解密
    std::vector<uint8_t> plaintext = decryptData(encrypted);
    if (plaintext.empty()) {
        Serial.println(F("[TOTP] 账户数据解密失败，使用空列表"));
        return true;
    }

    // 解析格式：[count: uint16_t][name_len:uint8_t][name][secret_len:uint16_t][secret]...
    accounts.clear();
    size_t offset = 0;

    if (offset + 2 > plaintext.size()) return false;
    uint16_t count = (uint16_t)(plaintext[offset]) |
                     ((uint16_t)(plaintext[offset + 1]) << 8);
    offset += 2;

    for (uint16_t i = 0; i < count; i++) {
        // 读取名称长度
        if (offset + 1 > plaintext.size()) break;
        uint8_t nameLen = plaintext[offset++];

        // 读取名称
        if (offset + nameLen > plaintext.size()) break;
        String name = String((const char *)&plaintext[offset], nameLen);
        offset += nameLen;

        // 读取密钥长度
        if (offset + 2 > plaintext.size()) break;
        uint16_t secretLen = (uint16_t)(plaintext[offset]) |
                             ((uint16_t)(plaintext[offset + 1]) << 8);
        offset += 2;

        // 读取密钥
        if (offset + secretLen > plaintext.size()) break;
        String secret = String((const char *)&plaintext[offset], secretLen);
        offset += secretLen;

        Account acc;
        acc.name         = name;
        acc.base32Secret = secret;
        accounts.push_back(acc);
    }

    Serial.printf("[TOTP] 从存储加载了 %d 个账户\n", accounts.size());
    return true;
}

bool TOTPManager::saveAccounts()
{
    if (!storage) return false;

    // 序列化账户列表
    std::vector<uint8_t> plaintext;

    // 账户数量（2 字节，小端序）
    uint16_t count = (uint16_t)accounts.size();
    plaintext.push_back((uint8_t)(count & 0xFF));
    plaintext.push_back((uint8_t)((count >> 8) & 0xFF));

    for (const auto &acc : accounts) {
        // 名称长度（1 字节）
        uint8_t nameLen = (uint8_t)acc.name.length();
        plaintext.push_back(nameLen);

        // 名称
        for (size_t i = 0; i < nameLen; i++) {
            plaintext.push_back((uint8_t)acc.name[i]);
        }

        // 密钥长度（2 字节，小端序）
        uint16_t secretLen = (uint16_t)acc.base32Secret.length();
        plaintext.push_back((uint8_t)(secretLen & 0xFF));
        plaintext.push_back((uint8_t)((secretLen >> 8) & 0xFF));

        // 密钥
        for (size_t i = 0; i < secretLen; i++) {
            plaintext.push_back((uint8_t)acc.base32Secret[i]);
        }
    }

    // 加密后存储
    std::vector<uint8_t> encrypted = encryptData(plaintext);
    if (encrypted.empty()) {
        Serial.println(F("[TOTP] 账户数据加密失败"));
        return false;
    }

    return storage->save("totp_accts", encrypted);
}

// ==================== 主密钥管理 ====================

bool TOTPManager::loadOrCreateMasterKey()
{
    if (!storage) return false;

    // 尝试从 SecureStorage 加载主密钥
    std::vector<uint8_t> stored = storage->load("totp_master");

    if (stored.size() == 32) {
        masterKey = stored;
        Serial.println(F("[TOTP] 主密钥已加载"));
        return true;
    }

    // 首次启动，生成 32 字节随机主密钥
    Serial.println(F("[TOTP] 首次启动，生成主密钥..."));

    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_init(&ctrDrbg);
    mbedtls_entropy_init(&entropy);

    int ret = mbedtls_ctr_drbg_seed(&ctrDrbg,
                                     mbedtls_entropy_func,
                                     &entropy,
                                     (const uint8_t *)"TOTP_MASTER_KEY",
                                     15);
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        // 备用方案：使用弱随机数
        masterKey.resize(32);
        for (int i = 0; i < 32; i++) {
            masterKey[i] = (uint8_t)random(256);
        }
    } else {
        masterKey.resize(32);
        ret = mbedtls_ctr_drbg_random(&ctrDrbg, masterKey.data(), 32);
        mbedtls_ctr_drbg_free(&ctrDrbg);
        mbedtls_entropy_free(&entropy);
        if (ret != 0) {
            masterKey.clear();
            return false;
        }
    }

    // 保存主密钥
    if (!storage->save("totp_master", masterKey)) {
        Serial.println(F("[TOTP] 主密钥保存失败"));
        return false;
    }

    Serial.println(F("[TOTP] 主密钥已生成并保存"));
    return true;
}

// ==================== AES-256-GCM 加解密 ====================

std::vector<uint8_t> TOTPManager::encryptData(const std::vector<uint8_t> &plaintext)
{
    if (plaintext.empty() || masterKey.size() != 32) {
        return {};
    }

    // GCM 需要：密钥(32B)、IV(12B)、明文、附加认证数据(AAD)
    const size_t IV_LEN = 12;
    const size_t TAG_LEN = 16;

    // 生成随机 IV
    std::vector<uint8_t> iv(IV_LEN);
    for (size_t i = 0; i < IV_LEN; i++) {
        iv[i] = (uint8_t)random(256);
    }

    // 输出：IV + 密文 + 认证标签
    std::vector<uint8_t> result;
    result.resize(IV_LEN + plaintext.size() + TAG_LEN);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  masterKey.data(), 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return {};
    }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     plaintext.size(),
                                     iv.data(), IV_LEN,
                                     nullptr, 0,            // AAD = none
                                     plaintext.data(),
                                     result.data() + IV_LEN, // 密文输出
                                     TAG_LEN,
                                     result.data() + IV_LEN + plaintext.size());
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return {};
    }

    // 复制 IV 到开头
    memcpy(result.data(), iv.data(), IV_LEN);

    mbedtls_gcm_free(&gcm);
    return result;
}

std::vector<uint8_t> TOTPManager::decryptData(const std::vector<uint8_t> &ciphertext)
{
    const size_t IV_LEN = 12;
    const size_t TAG_LEN = 16;

    if (ciphertext.size() < IV_LEN + TAG_LEN || masterKey.size() != 32) {
        return {};
    }

    size_t dataLen = ciphertext.size() - IV_LEN - TAG_LEN;
    if (dataLen == 0) return {};

    std::vector<uint8_t> plaintext(dataLen, 0);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  masterKey.data(), 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return {};
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm,
                                    dataLen,
                                    ciphertext.data(),             // IV
                                    IV_LEN,
                                    nullptr, 0,                    // AAD = none
                                    ciphertext.data() + IV_LEN + dataLen, // 认证标签
                                    TAG_LEN,
                                    ciphertext.data() + IV_LEN,    // 密文
                                    plaintext.data());
    if (ret != 0) {
        // 认证失败或解密错误
        mbedtls_gcm_free(&gcm);
        return {};
    }

    mbedtls_gcm_free(&gcm);
    return plaintext;
}
