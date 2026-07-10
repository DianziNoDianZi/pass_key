/**
 * @file PowerManager.h
 * @brief 电源管理模块 — 三级电源管理（活跃/待机/深度睡眠）
 *
 * 负责控制设备的电源状态，实现三级电源管理模式：
 * - 活跃模式 (Active)：正常工作，全速运行
 * - 待机模式 (Standby)：30秒无操作，背光降低，4G保持连接
 * - 深度睡眠 (Deep Sleep)：5分钟无操作，进入深度睡眠，GPIO唤醒
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "config.h"

// 电源状态枚举
enum PowerState {
    POWER_ACTIVE,      // 活跃模式：屏幕亮起，4G全速运行
    POWER_STANDBY,     // 待机模式：背光降低，4G保持连接
    POWER_DEEP_SLEEP   // 深度睡眠：关闭背光，4G低功耗
};

class PowerManager
{
public:
    PowerManager();
    ~PowerManager();

    /**
     * @brief 初始化电源管理，检测唤醒原因
     * @return true 表示从深度睡眠唤醒，false 表示首次冷启动
     */
    bool init();

    /**
     * @brief 获取当前电源状态
     * @return PowerState 当前状态
     */
    PowerState getState() const;

    /**
     * @brief 手动设置为活跃模式（恢复背光等）
     */
    void setActive();

    /**
     * @brief 手动设置为待机模式
     */
    void setStandby();

    /**
     * @brief 进入深度睡眠模式
     * @return false（实际不会返回，进入深度睡眠后程序重启）
     */
    bool goToDeepSleep();

    /**
     * @brief 重置空闲计时器（每次按键操作时调用）
     */
    void resetIdleTimer();

    /**
     * @brief 设置待机超时时间
     * @param seconds 秒数
     */
    void setStandbyTimeout(uint32_t seconds);

    /**
     * @brief 设置深度睡眠超时时间
     * @param seconds 秒数
     */
    void setDeepSleepTimeout(uint32_t seconds);

    /**
     * @brief 每帧调用，检查空闲时间并自动切换状态
     */
    void update();

private:
    PowerState  currentState;
    uint32_t    lastActivityTime;   // 最后一次活动时间戳 (ms)
    bool        initialized;

    // 超时配置（可通过 MQTT 远程修改）
    uint32_t standbyTimeoutMs;      // 待机超时 (ms)
    uint32_t deepSleepTimeoutMs;    // 深度睡眠超时 (ms)

    /**
     * @brief 配置 TFT 背光 PWM（5000Hz，8位分辨率）
     */
    void setupBacklightPWM();

    /**
     * @brief 设置背光亮度
     * @param brightness 亮度值 0-255
     */
    void setBacklightBrightness(uint8_t brightness);
};

#endif // POWER_MANAGER_H
