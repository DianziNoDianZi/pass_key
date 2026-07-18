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
    if (!driver) {
        return false;
    }

    if (driver->isModuleReady()) {
        // 模块就绪，正常发送 AT+CCLK? 查询模块 RTC 时间
        String collected;
        if (driver->sendCommand("AT+CCLK?", "+CCLK:", 5000, &collected)) {
            return parseAndSetTime(collected);
        }
        return false;
    }

    // 模块未就绪，尝试直接发送 AT+CCLK?（不检查 moduleReady 标志）
    // 这适用于模块已供电但初始化阶段未完全通过 isModuleReady 的情况
    String collected;
    if (driver->sendCommand("AT+CCLK?", "+CCLK:", 5000, &collected)) {
        return parseAndSetTime(collected);
    }

    return false;
}

// ==================== 解析 +CCLK 响应 ====================

bool TimeManager::parseAndSetTime(const String &response)
{
    // 解析 +CCLK: "yy/MM/dd,hh:mm:ss±zz"
    // 找到引号内的内容
    int quote1 = response.indexOf('"');
    int quote2 = response.indexOf('"', quote1 + 1);
    if (quote1 < 0 || quote2 < 0) {
        Serial.println(F("[Time] CCLK 格式错误（无引号）"));
        return false;
    }

    String timeStr = response.substring(quote1 + 1, quote2);
    if (timeStr.length() < 17) {
        Serial.printf("[Time] CCLK 格式错误（长度不足）: %s\n", timeStr.c_str());
        return false;
    }

    // 解析各时间分量
    int year   = 2000 + (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
    int month  = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');
    int day    = (timeStr[6] - '0') * 10 + (timeStr[7] - '0');
    int hour   = (timeStr[9] - '0') * 10 + (timeStr[10] - '0');
    int minute = (timeStr[12] - '0') * 10 + (timeStr[13] - '0');
    int second = (timeStr[15] - '0') * 10 + (timeStr[16] - '0');

    // 简单有效性检查
    if (year < 2023 || year > 2100 ||
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

    // 转换为时间戳（秒级）
    time_t epoch = mktime(&tmVal);

    // 应用时区偏移（config.h 中 TZ_OFFSET_SEC 定义）
    epoch -= TZ_OFFSET_SEC;

    // 设置 ESP32 系统时间
    struct timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }

    // 设置时区，使 getLocalTime() 能正确转换回本地时间
    char tzBuf[32];
    if (TZ_OFFSET_SEC > 0) {
        // UTC+X → POSIX 格式需要反号: UTC-X（如 UTC+8 → "UTC-08:00"）
        int tzH = TZ_OFFSET_SEC / 3600;
        int tzM = (TZ_OFFSET_SEC % 3600) / 60;
        snprintf(tzBuf, sizeof(tzBuf), "UTC-%02d:%02d", tzH, tzM);
    } else if (TZ_OFFSET_SEC < 0) {
        int tzH = -TZ_OFFSET_SEC / 3600;
        int tzM = (-TZ_OFFSET_SEC % 3600) / 60;
        snprintf(tzBuf, sizeof(tzBuf), "UTC+%02d:%02d", tzH, tzM);
    } else {
        strcpy(tzBuf, "UTC+00:00");
    }
    setenv("TZ", tzBuf, 1);
    tzset();

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

    // 执行重试
    lastRetryTime = now;
    retryCount++;

    Serial.printf("[Time] RTC 同步重试 %d/%d...\n", retryCount, MAX_RETRIES);

    if (syncRTC()) {
        Serial.println(F("[Time] RTC 同步成功（重试后）"));
        syncFailed = false;
    }
}
