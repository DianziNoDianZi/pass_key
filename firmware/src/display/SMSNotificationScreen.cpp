/**
 * @file SMSNotificationScreen.cpp
 * @brief 短信通知屏幕实现
 */

#include "SMSNotificationScreen.h"
#include "DisplayManager.h"
#include "config.h"

// ==================== 自定义颜色 ====================
#define SMS_ORANGE  0xFD20   // 取件码标签色 (RGB565)
#define SMS_GRAY    0x8410   // 未知标签色 (RGB565)

SMSNotificationScreen::SMSNotificationScreen(DisplayManager *manager,
                                             const String &s,
                                             const String &ct,
                                             const String &c,
                                             bool rec)
    : displayManager(manager)
    , sender(s)
    , codeType(ct)
    , code(c)
    , recognized(rec)
    , startTime(0)
    , remainingSeconds(15)
    , lastDisplayedSeconds(-1)
    , closeRequested(false)
    , vibrateState(VIB_IDLE)
    , vibrateStartTime(0)
{
}

SMSNotificationScreen::~SMSNotificationScreen()
{
    stopVibration();
}

void SMSNotificationScreen::onActivate()
{
    // 启动 15 秒倒计时
    startTime = millis();
    remainingSeconds = 15;
    lastDisplayedSeconds = -1;

    // 启动震动马达：短震 200ms，停 100ms，再短震 200ms
    pinMode(VIBRATOR, OUTPUT);
    digitalWrite(VIBRATOR, HIGH);
    vibrateState = VIB_PULSE_1;
    vibrateStartTime = millis();

    Serial.println(F("[SMS] 短信通知屏幕激活"));
}

void SMSNotificationScreen::onDeactivate()
{
    stopVibration();
    Serial.println(F("[SMS] 短信通知屏幕关闭"));
}

void SMSNotificationScreen::onButtonPress(uint8_t button)
{
    (void)button;
    // 按任意键标记关闭请求（延迟到 onUpdate 执行，避免 use-after-free）
    closeRequested = true;
}

void SMSNotificationScreen::onUpdate()
{
    // 检查按键触发的关闭请求
    if (closeRequested) {
        displayManager->popScreen();
        return;
    }

    // 更新震动状态机（非阻塞）
    updateVibration();

    unsigned long now = millis();
    unsigned long elapsed = now - startTime;

    // 15 秒后自动关闭
    if (elapsed >= 15000) {
        displayManager->popScreen();
        return;
    }

    // 每秒更新倒计时显示
    int sec = 15 - (elapsed / 1000);
    if (sec != lastDisplayedSeconds) {
        remainingSeconds = sec;
        lastDisplayedSeconds = sec;
        drawCountdown(displayManager->getTFT());
    }
}

void SMSNotificationScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(TFT_BLACK);

    drawTitle(tft);
    drawSender(tft);
    drawSeparator(tft);
    drawCodeTypeLabel(tft);
    drawCodeContent(tft);
    drawCountdown(tft);
}

// ==================== 震动控制 ====================

void SMSNotificationScreen::stopVibration()
{
    digitalWrite(VIBRATOR, LOW);
    vibrateState = VIB_DONE;
}

void SMSNotificationScreen::updateVibration()
{
    unsigned long now = millis();
    unsigned long elapsed = now - vibrateStartTime;

    switch (vibrateState) {
        case VIB_PULSE_1:
            if (elapsed >= 200) {
                digitalWrite(VIBRATOR, LOW);
                vibrateState = VIB_PAUSE_1;
                vibrateStartTime = now;
            }
            break;
        case VIB_PAUSE_1:
            if (elapsed >= 100) {
                digitalWrite(VIBRATOR, HIGH);
                vibrateState = VIB_PULSE_2;
                vibrateStartTime = now;
            }
            break;
        case VIB_PULSE_2:
            if (elapsed >= 200) {
                digitalWrite(VIBRATOR, LOW);
                vibrateState = VIB_DONE;
                vibrateStartTime = now;
            }
            break;
        default:
            break;
    }
}

// ==================== 码类型颜色与文本 ====================

uint16_t SMSNotificationScreen::getLabelColor() const
{
    if (!recognized) return SMS_GRAY;

    if (codeType == "验证码") return PASSKEY_BLUE;
    if (codeType == "取件码") return SMS_ORANGE;
    if (codeType == "确认码") return PASSKEY_GREEN;

    return SMS_GRAY;
}

const char *SMSNotificationScreen::getLabelText() const
{
    if (!recognized) return "未知";

    if (codeType == "验证码") return "验证码";
    if (codeType == "取件码") return "取件码";
    if (codeType == "确认码") return "确认码";

    return "短信";
}

// ==================== 绘制方法 ====================

void SMSNotificationScreen::drawTitle(TFT_eSPI &tft)
{
    const char *title = "短信通知";

    tft.fillRect(0, TITLE_Y, TFT_WIDTH, TITLE_H, PASSKEY_BLUE);
    tft.setTextColor(PASSKEY_WHITE, PASSKEY_BLUE);
    tft.setTextSize(2);

    int textW = tft.textWidth(title);
    tft.setCursor((TFT_WIDTH - textW) / 2, TITLE_Y + 4);
    tft.print(title);
}

void SMSNotificationScreen::drawSender(TFT_eSPI &tft)
{
    tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
    tft.setTextSize(3);

    int textW = tft.textWidth(sender.c_str());
    if (textW > TFT_WIDTH - 20) {
        tft.setTextSize(2);
        textW = tft.textWidth(sender.c_str());
    }
    tft.setCursor((TFT_WIDTH - textW) / 2, SENDER_Y + 8);
    tft.print(sender);
}

void SMSNotificationScreen::drawSeparator(TFT_eSPI &tft)
{
    tft.drawLine(20, SEPARATOR_Y, TFT_WIDTH - 20, SEPARATOR_Y, 0x5AEB);
}

void SMSNotificationScreen::drawCodeTypeLabel(TFT_eSPI &tft)
{
    uint16_t color = getLabelColor();
    const char *text = getLabelText();

    int textW = tft.textWidth(text);
    int padding = 12;
    int labelW = textW + padding * 2;
    int labelX = (TFT_WIDTH - labelW) / 2;

    tft.fillRoundRect(labelX, LABEL_Y, labelW, LABEL_H, 6, color);
    tft.setTextColor(PASSKEY_WHITE, color);
    tft.setTextSize(2);
    tft.setCursor(labelX + padding, LABEL_Y + 4);
    tft.print(text);
}

void SMSNotificationScreen::drawCodeContent(TFT_eSPI &tft)
{
    int centerY = CODE_Y + CODE_H / 2;

    if (recognized && code.length() > 0) {
        tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);

        // 根据码长度自适应字号（从大到小尝试）
        int fontSize = 6;
        tft.setTextSize(fontSize);
        int textW = tft.textWidth(code.c_str());
        while (fontSize > 2 && textW > TFT_WIDTH - 20) {
            fontSize--;
            tft.setTextSize(fontSize);
            textW = tft.textWidth(code.c_str());
        }

        int charH = fontSize * 8;
        tft.setCursor((TFT_WIDTH - textW) / 2, centerY - charH / 2);
        tft.print(code);
    } else {
        // 未识别短信：不显示具体内容
        tft.setTextColor(SMS_GRAY, TFT_BLACK);
        tft.setTextSize(2);
        const char *hint = "仅显示发送者";
        int textW = tft.textWidth(hint);
        tft.setCursor((TFT_WIDTH - textW) / 2, centerY - 8);
        tft.print(hint);
    }
}

void SMSNotificationScreen::drawCountdown(TFT_eSPI &tft)
{
    // 清除底部区域后重绘
    tft.fillRect(0, BOTTOM_Y, TFT_WIDTH, 22, TFT_BLACK);

    tft.setTextColor(SMS_GRAY, TFT_BLACK);
    tft.setTextSize(1);

    char buf[24];
    snprintf(buf, sizeof(buf), "%ds 后自动关闭", remainingSeconds);

    int textW = tft.textWidth(buf);
    tft.setCursor((TFT_WIDTH - textW) / 2, BOTTOM_Y + 5);
    tft.print(buf);
}
