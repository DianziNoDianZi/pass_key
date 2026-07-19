/**
 * @file WaitingScreen.h
 * @brief 等待提示屏幕 — 模块初始化/网络连接期间显示
 *
 * 在 4G 模块启动、GPRS 附着、MQTT 连接等耗时操作期间，
 * 向用户显示英文提示信息，避免白屏/黑屏造成"死机"错觉。
 */

#ifndef WAITING_SCREEN_H
#define WAITING_SCREEN_H

#include "Screen.h"
#include "DisplayManager.h"

class WaitingScreen : public Screen
{
public:
    /**
     * @param display   DisplayManager 指针（用于请求 pop）
     * @param status    初始状态文本（如 "Module starting..."）
     */
    WaitingScreen(DisplayManager *display, const String &status = "Initializing...");
    ~WaitingScreen();

    virtual const char *getName() const override { return "WaitingScreen"; }
    virtual void onActivate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

    /**
     * @brief 更新状态文本（在主 loop 中调用，非 MQTT 任务）
     * @param status 新状态
     */
    void setStatus(const String &status);

private:
    DisplayManager *displayManager;
    String statusText;
};

#endif // WAITING_SCREEN_H
