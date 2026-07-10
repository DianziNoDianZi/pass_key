/**
 * @file SecureStorage.h
 * @brief 安全存储模块 — 基于 NVS (Non-Volatile Storage)
 *
 * 提供在 NVS "passkey" 命名空间中的安全数据存储能力。
 * 支持原始二进制数据的保存、加载、擦除和存在性检查。
 */

#ifndef SECURE_STORAGE_H
#define SECURE_STORAGE_H

#include <Arduino.h>
#include <nvs.h>
#include <vector>

class SecureStorage
{
public:
    SecureStorage();
    ~SecureStorage();

    /**
     * @brief 初始化安全存储（打开 NVS "passkey" 分区）
     * @return true 成功
     */
    bool init();

    /**
     * @brief 保存数据到 NVS（std::vector 版本）
     * @param key  数据键名
     * @param data 数据向量
     * @return true 保存成功
     */
    bool save(const char *key, const std::vector<uint8_t> &data);

    /**
     * @brief 从 NVS 加载数据（std::vector 版本）
     * @param key  数据键名
     * @return 数据向量，若失败返回空向量
     */
    std::vector<uint8_t> load(const char *key);

    /**
     * @brief 保存数据到 NVS
     * @param key  数据键名
     * @param data 数据指针
     * @param len  数据长度
     * @return true 保存成功
     */
    bool save(const char *key, const uint8_t *data, size_t len);

    /**
     * @brief 从 NVS 加载数据
     * @param key  数据键名
     * @param data 输出缓冲区
     * @param len  输入：缓冲区大小，输出：实际数据长度
     * @return true 加载成功
     */
    bool load(const char *key, uint8_t *data, size_t &len);

    /**
     * @brief 擦除指定键的数据
     * @param key 数据键名
     * @return true 擦除成功
     */
    bool erase(const char *key);

    /**
     * @brief 检查键是否存在
     * @param key 数据键名
     * @return true 存在
     */
    bool exists(const char *key);

private:
    bool    initialized;
    nvs_handle nvsHandle;
};

#endif // SECURE_STORAGE_H
