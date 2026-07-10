/**
 * @file ButtonManager.h
 * @brief 按键管理模块 — 支持去抖处理与回调注册
 *
 * 管理三个物理按键（上/下/确认），提供去抖逻辑和
 * 按键事件回调机制，支持短按与长按分别注册回调。
 */

#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include <functional>
#include <vector>

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
     * @brief 初始化按键 GPIO 与内部状态
     * @return true 成功
     */
    bool init();

    /**
     * @brief 轮询按键状态（应高频调用）
     *
     * 替代 readButtons()，在 loop() 中定期调用。
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

    /**
     * @brief 处理单个按键的去抖与事件分发
     * @param btn 按键 ID
     */
    void processButton(ButtonId btn);
};

#endif // BUTTON_MANAGER_H
