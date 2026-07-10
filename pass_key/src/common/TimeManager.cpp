/**
 * @file TimeManager.cpp
 * @brief 时间管理模块实现
 *
 * 在 AT 固件模式下，ESP32 通过 Air780ep 模块上网，
 * 自身没有网络连接。因此通过 AT+CCLK? 命令读取
 * Air780ep 模块的 RTC 时间，同步到 ESP32 系统时间。
 */

#include "TimeManager.h"
#include "config.h"
#include "Air780epDriver.h"

#include <sys/time.h>

// 引用全局 Air780epDriver 实例（定义在 pass_key.ino 中）
extern Air780epDriver air780epDriver;

// ==================== CCLK 响应解析 ====================
// AT+CCLK? 返回格式: +CCLK: "yy/MM/dd,hh:mm:ss±zz"
// 其中 ±zz 是时区偏移（以一刻钟为单位）
// 例如: +CCLK: "24/12/25,14:30:00+32" 表示 UTC+8

/**
 * @brief 将 BCD 编码的两位数字转为整数
 */
static uint8_t bcdToUint8(const char *p)
{
    return (uint8_t)((p[0] - '0') * 10 + (p[1] - '0'));
}

// ==================== 构造 / 析构 ====================

TimeManager::TimeManager()
    : driver(nullptr)
    , timeSynced(false)
    , syncFailed(false)
    , retryCount(0)
    , lastRetryTime(0)
{
}

TimeManager::~TimeManager()
{
}

// ==================== 初始化 ====================

bool TimeManager::init()
{
    // 获取 Air780epDriver 实例指针
    driver = &air780epDriver;

    if (!driver || !driver->isModuleReady()) {
        Serial.println(F("[Time] Air780ep 模块未就绪，稍后重试"));
        syncFailed = true;
        retryCount = 0;
        lastRetryTime = millis();
        return false;
    }

    // 尝试首次同步
    if (syncRTC()) {
        return true;
    }

    // 首次同步失败，记录状态，后续由 update() 重试
    syncFailed = true;
    retryCount = 0;
    lastRetryTime = millis();
    return false;
}

// ==================== 从 Air780ep 获取时间 ====================

bool TimeManager::syncRTC()
{
    if (!driver || !driver->isModuleReady()) {
        return false;
    }

    // 发送 AT+CCLK? 查询模块 RTC 时间，收集完整响应
    String collected;
    if (!driver->sendCommand("AT+CCLK?", "+CCLK:", 5000, &collected)) {
        Serial.println(F("[Time] AT+CCLK? 无响应"));
        return false;
    }

    // 解析 +CCLK: "yy/MM/dd,hh:mm:ss±zz"
    // 找到引号内的内容
    int quote1 = collected.indexOf('"');
    int quote2 = collected.indexOf('"', quote1 + 1);
    if (quote1 < 0 || quote2 < 0) {
        Serial.println(F("[Time] CCLK 格式错误（无引号）"));
        return false;
    }

    String timeStr = collected.substring(quote1 + 1, quote2);
    // timeStr 格式: yy/MM/dd,hh:mm:ss±zz

    if (timeStr.length() < 17) {
        Serial.printf("[Time] CCLK 格式错误（长度不足）: %s\n", timeStr.c_str());
        return false;
    }

    // 解析各字段
    const char *p = timeStr.c_str();

    int year   = 2000 + bcdToUint8(p + 0);  // yy -> 20yy
    int month  = bcdToUint8(p + 3);          // MM
    int day    = bcdToUint8(p + 6);          // dd
    int hour   = bcdToUint8(p + 9);          // hh
    int minute = bcdToUint8(p + 12);         // mm
    int second = bcdToUint8(p + 15);         // ss

    // 检查有效性
    if (year < 2024 || year > 2099 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) {
        Serial.printf("[Time] CCLK 时间值无效: %04d-%02d-%02d %02d:%02d:%02d\n",
                       year, month, day, hour, minute, second);
        return false;
    }

    // 设置 ESP32 系统时间
    struct tm tmVal = {};
    tmVal.tm_year  = year - 1900;
    tmVal.tm_mon   = month - 1;
    tmVal.tm_mday  = day;
    tmVal.tm_hour  = hour;
    tmVal.tm_min   = minute;
    tmVal.tm_sec   = second;
    tmVal.tm_isdst = 0;

    time_t epoch = mktime(&tmVal);

    // 应用时区偏移（config.h 中 TZ_OFFSET_SEC = 28800 = UTC+8）
    // AT+CCLK? 返回的是模块的本地时间（已含时区偏移），
    // 所以转换为 UTC 存储到 ESP32 系统时间
    epoch -= TZ_OFFSET_SEC;

    // 设置 ESP32 系统时间
    struct timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        Serial.println(F("[Time] settimeofday 失败"));
        return false;
    }

    timeSynced = true;
    Serial.printf("[Time] AT+CCLK 同步成功: %04d-%02d-%02d %02d:%02d:%02d\n",
                   year, month, day, hour, minute, second);
    return true;
}

// ==================== 时间获取 ====================

time_t TimeManager::getUnixTime()
{
    if (!timeSynced) {
        return 0;
    }
    return time(nullptr);
}

String TimeManager::getFormattedTime()
{
    if (!timeSynced) {
        return "00:00:00";
    }
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "00:00:00";
    }
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    return String(buf);
}

String TimeManager::getFormattedDate()
{
    if (!timeSynced) {
        return "0000-00-00";
    }
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "0000-00-00";
    }
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
    return String(buf);
}

bool TimeManager::isTimeSynced()
{
    return timeSynced;
}

// ==================== 重试逻辑 ====================

void TimeManager::update()
{
    // 如果已同步，无需重试
    if (timeSynced || !syncFailed) {
        return;
    }

    // 检查是否达到最大重试次数
    if (retryCount >= MAX_RETRIES) {
        return;
    }

    // 检查重试间隔
    unsigned long now = millis();
    if (now - lastRetryTime < RETRY_INTERVAL_MS) {
        return;
    }

    // 检查模块是否就绪
    if (!driver || !driver->isModuleReady()) {
        return;
    }

    // 执行重试
    lastRetryTime = now;
    retryCount++;

    Serial.printf("[Time] RTC 同步重试 %d/%d...\n", retryCount, MAX_RETRIES);

    if (syncRTC()) {
        Serial.println(F("[Time] RTC 同步成功（重试后）"));
        syncFailed = false;
    }
}
