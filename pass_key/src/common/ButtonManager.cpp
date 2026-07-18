/**
 * @file ButtonManager.cpp
 * @brief 按键管理模块实现
 *
 * 基于 ESP32 硬件定时器中断扫描按键引脚，
 * ISR 内部维护 20ms 滑动窗口多数决 + 事件队列，
 * 确保主循环 (loop) 长时间阻塞也不会丢失按键事件。
 */

#include "ButtonManager.h"
#include "config.h"
#include "driver/gpio.h"

// ===== 静态 volatile 成员定义 =====
volatile bool ButtonManager::hasEvents = false;
volatile ButtonManager::ISREvent ButtonManager::eventQueue[EVENT_QUEUE_SIZE];
volatile uint8_t ButtonManager::eventWriteIdx = 0;
volatile uint8_t ButtonManager::eventReadIdx = 0;
int ButtonManager::isrLastState[3] = {HIGH, HIGH, HIGH};

ButtonManager::ButtonManager()
    : userCallback(nullptr)
    , longPressThreshold(1000) // 默认长按阈值 1 秒
    , scanTimer(nullptr)
{
    buttons[BTN_ID_UP].pin          = BTN_UP;
    buttons[BTN_ID_DOWN].pin        = BTN_DOWN;
    buttons[BTN_ID_CONFIRM].pin     = BTN_CONFIRM;
}

ButtonManager::~ButtonManager()
{
    if (scanTimer) {
        esp_timer_stop(scanTimer);
        esp_timer_delete(scanTimer);
        scanTimer = nullptr;
    }
}

// ===== 定时器中断服务函数 =====
// 每 2ms 触发一次，内部维护 20ms (10次) 滚动窗口做多数决
// 状态变化时写入事件队列
void IRAM_ATTR ButtonManager::timerISR(void *arg)
{
    (void)arg;

    // 使用 gpio_get_level（ESP-IDF 官方 ISR 安全接口，跨芯片兼容）
    int vals[3] = {
        gpio_get_level((gpio_num_t)BTN_UP),
        gpio_get_level((gpio_num_t)BTN_DOWN),
        gpio_get_level((gpio_num_t)BTN_CONFIRM)
    };

    // ---- 滚动窗口（ISR 静态变量） ----
    static int ringBuf[3][ISR_WINDOW_SIZE];
    static int ringIdx = 0;

    for (int b = 0; b < 3; b++) {
        ringBuf[b][ringIdx] = vals[b];
    }
    ringIdx = (ringIdx + 1) % ISR_WINDOW_SIZE;

    // 每 20ms 输出一次多数决结果
    if (ringIdx == 0) {
        for (int b = 0; b < 3; b++) {
            int lowCount = 0;
            for (int i = 0; i < ISR_WINDOW_SIZE; i++) {
                if (ringBuf[b][i] == 0) lowCount++;
            }
            int newState = (lowCount >= 5) ? LOW : HIGH;

            // 检测状态变化 → 写入事件队列
            if (newState != isrLastState[b]) {
                isrLastState[b] = newState;

                uint8_t nextIdx = (eventWriteIdx + 1) % EVENT_QUEUE_SIZE;
                if (nextIdx != eventReadIdx) { // 队列未满
                    eventQueue[eventWriteIdx].btnId = (uint8_t)b;
                    eventQueue[eventWriteIdx].state = (uint8_t)newState;
                    eventWriteIdx = nextIdx;
                    hasEvents = true;
                }
            }
        }
    }
}

// ===== 初始化 =====

bool ButtonManager::init()
{
    // 1. 配置 GPIO（使用 ESP-IDF 驱动 API，比 Arduino pinMode 更可靠）
    gpio_config_t cfg[3];
    for (int i = 0; i < 3; i++) {
        cfg[i].pin_bit_mask = (1ULL << buttons[i].pin);
        cfg[i].mode = GPIO_MODE_INPUT;
        cfg[i].pull_up_en = GPIO_PULLUP_ONLY;
        cfg[i].pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg[i].intr_type = GPIO_INTR_DISABLE;
        gpio_config(&cfg[i]);

        buttons[i].lastState        = HIGH;
        buttons[i].currentState     = HIGH;
        buttons[i].stableState      = HIGH;
        buttons[i].lastDebounceTime = 0;
        pressStartTime[i]           = 0;
    }

    // 2. 重置 ISR 状态
    hasEvents = false;
    eventWriteIdx = 0;
    eventReadIdx = 0;
    for (int i = 0; i < 3; i++) isrLastState[i] = HIGH;

    // 3. 启动硬件定时器中断（周期 2ms）
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &timerISR;
    timerArgs.arg      = this;
    timerArgs.name     = "btn_scan_timer";

    esp_err_t err = esp_timer_create(&timerArgs, &scanTimer);
    if (err != ESP_OK) {
        Serial.printf("[BTN] 定时器创建失败: %d\n", err);
        return false;
    }

    err = esp_timer_start_periodic(scanTimer, SCAN_INTERVAL_US);
    if (err != ESP_OK) {
        Serial.printf("[BTN] 定时器启动失败: %d\n", err);
        return false;
    }

    Serial.printf("[BTN] 定时器启动成功，周期=%dus\n", SCAN_INTERVAL_US);

    // 诊断：打印各引脚初始电平
    for (int i = 0; i < 3; i++) {
        int raw = gpio_get_level((gpio_num_t)buttons[i].pin);
        Serial.printf("[BTN] 诊断 pin=%d init_level=%d\n", buttons[i].pin, raw);
    }
    return true;
}

// ===== 主循环轮询 =====

void ButtonManager::update()
{
    // 诊断：每 50 次循环打印一次引脚实时电平
    static uint8_t diagCount = 0;
    if (++diagCount == 0) {
        Serial.printf("[BTN] 诊断实时: up=%d down=%d confirm=%d\n",
            gpio_get_level((gpio_num_t)BTN_UP),
            gpio_get_level((gpio_num_t)BTN_DOWN),
            gpio_get_level((gpio_num_t)BTN_CONFIRM));
    }

    if (!hasEvents) return;

    // 取出事件队列中的所有待处理状态变化
    while (eventReadIdx != eventWriteIdx) {
        ISREvent ev;
        ev.btnId = eventQueue[eventReadIdx].btnId;
        ev.state = eventQueue[eventReadIdx].state;
        ButtonId btn = (ButtonId)ev.btnId;
        int state = (int)ev.state;

        eventReadIdx = (eventReadIdx + 1) % EVENT_QUEUE_SIZE;

        handleButtonEvent(btn, state, millis());
    }

    hasEvents = false;
}

void ButtonManager::readButtons()
{
    update();
}

// ===== 回调注册 =====

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

// ===== 事件处理（ISR 已做 20ms 滤波，直接触发事件）=====

void ButtonManager::handleButtonEvent(ButtonId btn, int state, unsigned long timestamp)
{
    ButtonState &bs = buttons[btn];

    // ISR 已经做了 20ms 滑动窗口多数决滤波，
    // 事件队列中的状态变化即确认结果，跳过去抖
    if (bs.stableState == state) {
        // 状态未变：可能是 ISR 连续输出了相同的事件（不应发生）
        return;
    }

    bs.stableState = state;
    bs.lastState = state;
    bs.currentState = state;

    if (state == LOW) {
        // 按键按下
        pressStartTime[btn] = timestamp;
        Serial.printf("[BTN] 按下 pin=%d\n", bs.pin);
        if (userCallback) {
            userCallback(btn, BTN_PRESSED);
        }
    } else {
        // 按键释放
        uint32_t duration = timestamp - pressStartTime[btn];
        Serial.printf("[BTN] 释放 pin=%d duration=%lu\n", bs.pin, duration);
        if (duration >= longPressThreshold) {
            // 长按
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
