/**
 * @file ButtonManager.cpp
 * @brief 按键管理模块实现
 */

#include "ButtonManager.h"
#include "config.h"

ButtonManager::ButtonManager()
    : userCallback(nullptr)
    , longPressThreshold(1000) // 默认长按阈值 1 秒
{
    buttons[BTN_ID_UP].pin          = BTN_UP;
    buttons[BTN_ID_DOWN].pin        = BTN_DOWN;
    buttons[BTN_ID_CONFIRM].pin     = BTN_CONFIRM;
}

ButtonManager::~ButtonManager()
{
}

bool ButtonManager::init()
{
    for (int i = 0; i < 3; i++) {
        pinMode(buttons[i].pin, INPUT_PULLUP);
        buttons[i].lastState        = HIGH;
        buttons[i].currentState     = HIGH;
        buttons[i].stableState      = HIGH;
        buttons[i].lastDebounceTime = 0;
        pressStartTime[i]           = 0;
    }
    return true;
}

void ButtonManager::update()
{
    readButtons();
}

void ButtonManager::readButtons()
{
    processButton(BTN_ID_UP);
    processButton(BTN_ID_DOWN);
    processButton(BTN_ID_CONFIRM);
}

void ButtonManager::setCallback(ButtonCallback callback)
{
    userCallback = callback;
}

void ButtonManager::addShortPressCallback(ShortPressCallback cb)
{
    shortPressCbs.push_back(cb);
}

void ButtonManager::addLongPressCallback(LongPressCallback cb)
{
    longPressCbs.push_back(cb);
}

void ButtonManager::clearPressCallbacks()
{
    shortPressCbs.clear();
    longPressCbs.clear();
}

void ButtonManager::processButton(ButtonId btn)
{
    ButtonState &bs = buttons[btn];

    // 抗 RF 噪声：快速采样 5 次取多数值
    // 4G 射频是周期性脉冲，多采样可滤除瞬时错误电平
    int highCount = 0;
    int lowCount = 0;
    for (int i = 0; i < 5; i++) {
        if (digitalRead(bs.pin) == HIGH) {
            highCount++;
        } else {
            lowCount++;
        }
        delayMicroseconds(500); // 每次间隔 500μs
    }
    bs.currentState = (highCount > lowCount) ? HIGH : LOW;

    // 去抖逻辑（50ms 消抖延时）
    if (bs.currentState != bs.lastState) {
        bs.lastDebounceTime = millis();
    }

    if ((millis() - bs.lastDebounceTime) > BTN_DEBOUNCE_MS) {
        // 状态已稳定
        if (bs.stableState != bs.currentState) {
            bs.stableState = bs.currentState;

            if (bs.stableState == LOW) {
                // 按键按下
                pressStartTime[btn] = millis();
                if (userCallback) {
                    userCallback(btn, BTN_PRESSED);
                }
            } else {
                // 按键释放
                uint32_t duration = millis() - pressStartTime[btn];
                if (duration >= longPressThreshold) {
                    // 长按（>1 秒）
                    for (auto &cb : longPressCbs) {
                        cb(bs.pin);
                    }
                    if (userCallback) {
                        userCallback(btn, BTN_LONG_PRESS);
                    }
                } else {
                    // 短按
                    for (auto &cb : shortPressCbs) {
                        cb(bs.pin);
                    }
                    if (userCallback) {
                        userCallback(btn, BTN_RELEASED);
                    }
                }
            }
        }
    }

    bs.lastState = bs.currentState;
}
