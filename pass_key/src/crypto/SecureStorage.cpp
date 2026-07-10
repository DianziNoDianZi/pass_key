/**
 * @file SecureStorage.cpp
 * @brief 安全存储模块实现
 *
 * 基于 ESP32 NVS（非易失性存储），使用 "passkey" 命名空间。
 * 提供原始二进制数据的持久化存储能力。
 */

#include "SecureStorage.h"
#include "config.h"

#include <nvs_flash.h>
#include <vector>

#define NVS_NAMESPACE "passkey"

SecureStorage::SecureStorage()
    : initialized(false)
    , nvsHandle(0)
{
}

SecureStorage::~SecureStorage()
{
    if (initialized) {
        nvs_close(nvsHandle);
    }
}

bool SecureStorage::init()
{
    // 初始化 NVS 闪存分区（首次调用时执行）
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS 分区需要擦除重建
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        Serial.printf("[Storage] NVS 初始化失败: %d\n", err);
        return false;
    }

    // 打开 "passkey" 命名空间
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("[Storage] NVS 打开命名空间失败: %d\n", err);
        return false;
    }

    initialized = true;
    return true;
}

// ==================== std::vector 重载 ====================

bool SecureStorage::save(const char *key, const std::vector<uint8_t> &data)
{
    return save(key, data.data(), data.size());
}

std::vector<uint8_t> SecureStorage::load(const char *key)
{
    if (!initialized) {
        return {};
    }

    // 先获取数据长度
    size_t len = 0;
    esp_err_t err = nvs_get_blob(nvsHandle, key, NULL, &len);
    if (err != ESP_OK || len == 0) {
        return {};
    }

    // 分配缓冲区并读取数据
    std::vector<uint8_t> data(len);
    err = nvs_get_blob(nvsHandle, key, data.data(), &len);
    if (err != ESP_OK) {
        return {};
    }
    data.resize(len);
    return data;
}

bool SecureStorage::save(const char *key, const uint8_t *data, size_t len)
{
    if (!initialized) {
        Serial.println("[Storage] 未初始化");
        return false;
    }

    esp_err_t err = nvs_set_blob(nvsHandle, key, data, len);
    if (err != ESP_OK) {
        Serial.printf("[Storage] 保存 '%s' 失败: %d\n", key, err);
        return false;
    }

    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("[Storage] 提交 '%s' 失败: %d\n", key, err);
        return false;
    }

    return true;
}

bool SecureStorage::load(const char *key, uint8_t *data, size_t &len)
{
    if (!initialized) {
        return false;
    }

    esp_err_t err = nvs_get_blob(nvsHandle, key, data, &len);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            Serial.printf("[Storage] 加载 '%s' 失败: %d\n", key, err);
        }
        return false;
    }

    return true;
}

bool SecureStorage::erase(const char *key)
{
    if (!initialized) {
        return false;
    }

    esp_err_t err = nvs_erase_key(nvsHandle, key);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            Serial.printf("[Storage] 擦除 '%s' 失败: %d\n", key, err);
        }
        return false;
    }

    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        Serial.printf("[Storage] 提交擦除 '%s' 失败: %d\n", key, err);
        return false;
    }

    return true;
}

bool SecureStorage::exists(const char *key)
{
    if (!initialized) {
        return false;
    }

    size_t len = 0;
    esp_err_t err = nvs_get_blob(nvsHandle, key, NULL, &len);
    return (err == ESP_OK);
}
