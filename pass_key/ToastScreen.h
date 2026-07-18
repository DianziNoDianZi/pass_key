/**
 * @file ToastScreen.h
 * @brief 短暂弹窗屏幕 — 用于显示操作反馈
 *
 * 常用于 TOTP 同步完成、配置推送成功等场景的视觉提示。
 * 显示消息 2 秒后自动关闭，按任意键提前关闭。
 */

#ifndef TOAST_SCREEN_H
#define TOAST_SCREEN_H

#include "Screen.h"
#include "DisplayManager.h"

class ToastScreen : public Screen {
public:
    /**
     * @param display  DisplayManager 指针（用于 popScreen）
     * @param message  要显示的消息文本
     * @param duration 自动关闭时间（毫秒，默认 2000）
     * @param bgColor  背景色（默认 APPLE_BG）
     * @param fgColor  文字色（默认 APPLE_GREEN）
     */
    ToastScreen(DisplayManager *display,
                const String &message,
                uint32_t duration = 2000,
                uint16_t bgColor = APPLE_BG,
                uint16_t fgColor = APPLE_GREEN);
    virtual ~ToastScreen();

    virtual const char *getName() const override { return "Toast"; }
    virtual void onActivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayManager;
    String msg;
    uint32_t autoCloseMs;
    uint16_t bg, fg;
    uint32_t startTime;
    bool closeRequested;
};

#endif // TOAST_SCREEN_H
