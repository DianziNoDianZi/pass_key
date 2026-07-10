/**
 * @file Screen.h
 * @brief 屏幕基类 — 所有页面的抽象接口
 */

#ifndef SCREEN_H
#define SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>

class Screen {
public:
    virtual ~Screen() {}

    /**
     * @brief 进入屏幕时调用
     */
    virtual void onActivate() {}

    /**
     * @brief 离开屏幕时调用
     */
    virtual void onDeactivate() {}

    /**
     * @brief 按键事件
     * @param button 按键 ID (ButtonId)
     */
    virtual void onButtonPress(uint8_t button) {}

    /**
     * @brief 每帧更新（由 DisplayManager::update 定时调用）
     */
    virtual void onUpdate() {}

    /**
     * @brief 接收事件通知（如账户变更、配置更新等）
     * @param event 事件名称字符串
     */
    virtual void onEvent(const char *event) {}

    /**
     * @brief 绘制屏幕内容
     * @param tft TFT 显示驱动引用
     */
    virtual void onDraw(TFT_eSPI &tft) = 0;
};

#endif // SCREEN_H
