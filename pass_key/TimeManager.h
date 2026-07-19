/**
 * @file TimeManager.h
 * @brief 时间管理模块 — NTP 同步与时间格式化
 *
 * 负责通过 NTP 协议同步系统时间，并提供获取
 * Unix 时间戳与格式化时间字符串的功能。
 * 首次 NTP 同步失败时，每 5 分钟重试，最多 6 次。
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>

// 前向声明
class Air780epDriver;

class TimeManager
{
public:
    TimeManager();
    ~TimeManager();

    /**
     * @brief 初始化时间管理器
     * 通过 Air780ep 模块的 AT+CCLK? 命令获取模块 RTC 时间，
     * 并同步到 ESP32 系统时间。
     * @return true 同步成功
     */
    bool init();

    /**
     * @brief 手动触发时间同步（通过 Air780ep 模块）
     * 发送 AT+CCLK? 命令获取模块 RTC 时间。
     * @return true 同步成功
     */
    bool syncRTC();

    /**
     * @brief 获取当前 Unix 时间戳（秒）
     * @return Unix 时间戳
     */
    time_t getUnixTime();

    /**
     * @brief 获取格式化的时间字符串 "HH:MM:SS"
     * @return 时间字符串
     */
    String getFormattedTime();

    /**
     * @brief 获取格式化的日期字符串 "YYYY-MM-DD"
     * @return 日期字符串
     */
    String getFormattedDate();

    /**
     * @brief 返回是否已成功同步时间
     * @return true 已同步
     */
    bool isTimeSynced();

    /**
     * @brief 周期性更新（处理同步重试逻辑）
     * 应在主 loop 中定期调用
     */
    void update();

private:
    /**
     * @brief 解析 +CCLK 响应字符串并设置系统时间
     * @param response AT+CCLK? 的完整原始响应字符串
     * @return true 解析并设置成功
     */
    bool parseAndSetTime(const String &response);

    Air780epDriver *driver;         // Air780ep 模块驱动指针
    bool      timeSynced;
    bool      syncFailed;          // 初始同步是否失败
    int       retryCount;          // 已重试次数
    uint32_t  lastRetryTime;       // 上次重试时间戳 (ms)
    uint32_t  lastSyncTime;        // 上次成功同步时间戳 (ms)，用于定时重新同步

    static const int    MAX_RETRIES       = 6;
    static const int    RETRY_INTERVAL_MS = 300000; // 5 分钟
    static const int    RESYNC_INTERVAL_MS = 21600000; // 6 小时
};

#endif // TIME_MANAGER_H
