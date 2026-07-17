/**
 * @file ButtonManager.h
 * @brief 按键管理模块 — 支持去抖处理与回调注册
 *
 * 管理三个物理按键（上/下/确认），提供去抖逻辑和
 * 按键事件回调机制，支持短按与长按分别注册回调。
 *
 * 使用 ESP32 硬件定时器中断扫描按键引脚，
 * ISR 内部维护 20ms 滑动窗口多数决 + 事件队列，
 * 确保主循环长时间阻塞也不会丢失按键事件。
 */

#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include "esp_timer.h"

// 按键事件类型
enum ButtonEvent {
    BTN_PRESSED,
    BTN_RELEASED,
    BTN_LONG_PRESS
};

// 按键 ID
enum ButtonId {
    BTN_ID_UP,
    BTN_ID_DOWN,
    BTN_ID_CONFIRM
};

// 按键事件回调类型（保留向后兼容）
typedef std::function<void(ButtonId id, ButtonEvent event)> ButtonCallback;

// 短按回调类型（参数为 GPIO 引脚号）
typedef std::function<void(uint8_t pin)> ShortPressCallback;

// 长按回调类型（参数为 GPIO 引脚号）
typedef std::function<void(uint8_t pin)> LongPressCallback;

class ButtonManager
{
public:
    ButtonManager();
    ~ButtonManager();

    /**
     * @brief 初始化按键 GPIO 与内部状态，启动定时器中断
     * @return true 成功
     */
    bool init();

    /**
     * @brief 轮询按键状态（在 loop() 中定期调用）
     *
     * 从 ISR 事件队列取出所有待处理按键事件，
     * 执行去抖和事件分发。即使 loop() 长时间阻塞也不会丢失事件。
     */
    void update();

    /**
     * @brief 读取并处理按键状态（旧接口，保留兼容）
     */
    void readButtons();

    /**
     * @brief 注册按键事件回调（旧接口，保留兼容）
     * @param callback 回调函数
     */
    void setCallback(ButtonCallback callback);

    /**
     * @brief 注册短按回调（支持多个）
     * @param cb 回调函数，参数为 GPIO 引脚号
     */
    void addShortPressCallback(ShortPressCallback cb);

    /**
     * @brief 注册长按回调（支持多个）
     * @param cb 回调函数，参数为 GPIO 引脚号
     */
    void addLongPressCallback(LongPressCallback cb);

    /**
     * @brief 清除所有短按与长按回调
     */
    void clearPressCallbacks();

private:
    static const int SCAN_INTERVAL_US = 2000;   // 定时器周期 2ms
    static const int ISR_WINDOW_SIZE = 10;       // 20ms 滑动窗口
    static const int EVENT_QUEUE_SIZE = 16;      // 事件队列深度

    // ISR 事件队列条目
    struct ISREvent {
        uint8_t btnId;   // ButtonId
        uint8_t state;   // LOW or HIGH
    };

    struct ButtonState {
        uint8_t  pin;
        bool     lastState;
        bool     currentState;
        uint32_t lastDebounceTime;
        bool     stableState;
    };

    ButtonState           buttons[3];
    ButtonCallback        userCallback;
    std::vector<ShortPressCallback> shortPressCbs;
    std::vector<LongPressCallback>  longPressCbs;
    uint32_t              pressStartTime[3];
    uint32_t              longPressThreshold;

    // ===== 定时器中断相关 =====
    esp_timer_handle_t    scanTimer;

    // ISR 写入的事件队列 + 标志
    static volatile bool  hasEvents;
    static volatile ISREvent eventQueue[EVENT_QUEUE_SIZE];
    static volatile uint8_t eventWriteIdx;    // ISR 写入
    static volatile uint8_t eventReadIdx;     // 主循环读取

    // ISR 内部状态追踪（静态变量在 ISR 中）
    static int            isrLastState[3];     // ISR 上次输出的 processedState

    /**
     * @brief 定时器中断服务函数（IRAM 加速）
     * @param arg 指向 ButtonManager 实例的指针
     */
    static void IRAM_ATTR timerISR(void *arg);

    /**
     * @brief 处理单个按键按下/释放事件
     * @param btn 按键 ID
     * @param state 新状态 (LOW=按下, HIGH=释放)
     * @param timestamp 事件发生时的 millis 值
     */
    void handleButtonEvent(ButtonId btn, int state, unsigned long timestamp);
};

#endif // BUTTON_MANAGER_H
