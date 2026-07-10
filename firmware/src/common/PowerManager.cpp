/**
 * @file PowerManager.cpp
 * @brief 电源管理模块实现 — 三级电源管理
 */

#include "PowerManager.h"
#include "mqtt/Air780epDriver.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

// 引用全局 Air780epDriver 实例（定义在 pass_key.ino 中）
extern Air780epDriver air780epDriver;

PowerManager::PowerManager()
    : currentState(POWER_ACTIVE)
    , lastActivityTime(0)
    , initialized(false)
{
}

PowerManager::~PowerManager()
{
}

bool PowerManager::init()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // 配置 TFT 背光 PWM 通道
    setupBacklightPWM();

    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println(F("[PM] 从深度睡眠唤醒 (GPIO 唤醒)"));
        currentState = POWER_ACTIVE;
        lastActivityTime = millis();
        setActive();
        initialized = true;
        return true;  // 深度睡眠唤醒
    }

    // 首次冷启动
    Serial.println(F("[PM] 首次冷启动"));
    currentState = POWER_ACTIVE;
    lastActivityTime = millis();
    setActive();
    initialized = true;
    return false;
}

PowerState PowerManager::getState() const
{
    return currentState;
}

void PowerManager::setActive()
{
    if (!initialized) return;
    if (currentState == POWER_ACTIVE) return;

    currentState = POWER_ACTIVE;
    lastActivityTime = millis();

    // 恢复背光到全亮度
    setBacklightBrightness(255);
    Serial.println(F("[PM] -> 活跃模式"));
}

void PowerManager::setStandby()
{
    if (!initialized) return;
    if (currentState != POWER_ACTIVE) return;

    currentState = POWER_STANDBY;

    // 背光降低到 10% 亮度 (255 * 10% ≈ 25)
    setBacklightBrightness(25);

    // 4G 模块保持连接，不断开 MQTT
    Serial.println(F("[PM] -> 待机模式 (背光 10%)"));
}

bool PowerManager::goToDeepSleep()
{
    if (!initialized) return false;

    Serial.println(F("[PM] -> 深度睡眠..."));

    currentState = POWER_DEEP_SLEEP;

    // 关闭 TFT 背光（PWM 输出 0%）
    setBacklightBrightness(0);

    // 让 4G 模块进入低功耗
    Serial.println(F("[PM] 4G 模块进入睡眠..."));
    air780epDriver.sleep();

    // 配置唤醒 GPIO：BTN_UP(GPIO4), BTN_DOWN(GPIO5), BTN_CONFIRM(GPIO6)
    // 按键均为上拉输入，按下为低电平，使用低电平唤醒
    const uint64_t wakeup_pin_mask = (1ULL << BTN_UP) |
                                     (1ULL << BTN_DOWN) |
                                     (1ULL << BTN_CONFIRM);

    // 配置唤醒引脚为上拉输入模式
    gpio_config_t wakeup_io_cfg = {};
    wakeup_io_cfg.pin_bit_mask = wakeup_pin_mask;
    wakeup_io_cfg.mode         = GPIO_MODE_INPUT;
    wakeup_io_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    wakeup_io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    wakeup_io_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&wakeup_io_cfg);

    // 使能 GPIO 唤醒（低电平触发）
    esp_deep_sleep_enable_gpio_wakeup(wakeup_pin_mask, ESP_GPIO_WAKEUP_GPIO_LOW);

    Serial.println(F("[PM] 调用 esp_deep_sleep_start()"));

    // 进入深度睡眠 —— 此函数不会返回
    esp_deep_sleep_start();

    // 不会执行到这里
    return false;
}

void PowerManager::resetIdleTimer()
{
    if (!initialized) return;
    lastActivityTime = millis();

    // 如果在待机模式下按键，恢复活跃模式
    if (currentState == POWER_STANDBY) {
        setActive();
    }
}

void PowerManager::update()
{
    if (!initialized) return;

    uint32_t now     = millis();
    uint32_t idleMs  = now - lastActivityTime;

    switch (currentState) {
        case POWER_ACTIVE:
            // 30 秒无操作 → 待机模式
            if (idleMs >= STANDBY_TIMEOUT_MS) {
                setStandby();
            }
            break;

        case POWER_STANDBY:
            // 待机持续 5 分钟无操作 → 深度睡眠
            if (idleMs >= DEEP_SLEEP_TIMEOUT_MS) {
                goToDeepSleep();   // 不会返回
            }
            break;

        case POWER_DEEP_SLEEP:
            // 深度睡眠中，不做任何事
            break;
    }
}

// ==================== 背光 PWM 控制 ====================

void PowerManager::setupBacklightPWM()
{
    // 通道 0，频率 5000Hz，8 位分辨率 (0-255)
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
}

void PowerManager::setBacklightBrightness(uint8_t brightness)
{
    ledcWrite(0, brightness);
}
