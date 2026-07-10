/**
 * @file SMSNotificationScreen.h
 * @brief 短信通知屏幕 — 收到 sms_forward 消息时显示
 *
 * 显示内容：发件人、码类型标签（带颜色背景）、码内容、
 * 底部倒计时。15秒自动关闭，按任意键立即关闭。
 * 同时触发音震动马达提示。
 */

#ifndef SMS_NOTIFICATION_SCREEN_H
#define SMS_NOTIFICATION_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class SMSNotificationScreen : public Screen {
public:
    /**
     * @param manager   显示管理器指针（用于 popScreen）
     * @param sender    发件人名称
     * @param codeType  码类型（验证码/取件码/确认码/未知）
     * @param code      码内容
     * @param recognized 是否识别成功
     */
    SMSNotificationScreen(DisplayManager *manager,
                          const String &sender,
                          const String &codeType,
                          const String &code,
                          bool recognized);
    ~SMSNotificationScreen();

    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayManager;
    String sender;
    String codeType;
    String code;
    bool recognized;

    // 自动关闭计时
    unsigned long startTime;
    int remainingSeconds;
    int lastDisplayedSeconds;
    bool closeRequested;

    // 震动状态机
    enum VibrateState {
        VIB_IDLE,
        VIB_PULSE_1,   // 第一次震动 200ms
        VIB_PAUSE_1,   // 间隔 100ms
        VIB_PULSE_2,   // 第二次震动 200ms
        VIB_DONE
    };
    VibrateState vibrateState;
    unsigned long vibrateStartTime;

    // 布局常量
    static const int STATUS_BAR_H = 16;
    static const int TITLE_Y      = STATUS_BAR_H;
    static const int TITLE_H      = 28;
    static const int SENDER_Y     = TITLE_Y + TITLE_H + 4;
    static const int SENDER_H     = 42;
    static const int SEPARATOR_Y  = SENDER_Y + SENDER_H + 2;
    static const int LABEL_Y      = SEPARATOR_Y + 5;
    static const int LABEL_H      = 28;
    static const int CODE_Y       = LABEL_Y + LABEL_H + 10;
    static const int CODE_H       = 75;
    static const int BOTTOM_Y     = TFT_HEIGHT - 22;

    void stopVibration();
    void updateVibration();
    uint16_t getLabelColor() const;
    const char *getLabelText() const;
    void drawTitle(TFT_eSPI &tft);
    void drawSender(TFT_eSPI &tft);
    void drawSeparator(TFT_eSPI &tft);
    void drawCodeTypeLabel(TFT_eSPI &tft);
    void drawCodeContent(TFT_eSPI &tft);
    void drawCountdown(TFT_eSPI &tft);
};

#endif // SMS_NOTIFICATION_SCREEN_H
