/**
 * @file ToastScreen.cpp
 * @brief 短暂弹窗屏幕实现
 */

#include "ToastScreen.h"
#include "DisplayManager.h"

ToastScreen::ToastScreen(DisplayManager *display,
                         const String &message,
                         uint32_t duration,
                         uint16_t bgColor,
                         uint16_t fgColor)
    : displayManager(display)
    , msg(message)
    , autoCloseMs(duration)
    , bg(bgColor)
    , fg(fgColor)
    , startTime(0)
    , closeRequested(false)
{
}

ToastScreen::~ToastScreen()
{
}

void ToastScreen::onActivate()
{
    startTime = millis();
}

void ToastScreen::onButtonPress(uint8_t button)
{
    (void)button;
    closeRequested = true;
}

void ToastScreen::onUpdate()
{
    if (closeRequested || (millis() - startTime >= autoCloseMs)) {
        if (displayManager) {
            displayManager->popScreen();
        }
    }
}

void ToastScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(bg);

    // 标题行
    tft.fillRect(0, 0, TFT_WIDTH, 20, PASSKEY_DARK);
    tft.setTextColor(PASSKEY_WHITE, PASSKEY_DARK);
    tft.setTextSize(1);
    tft.setCursor(8, 5);
    tft.print("PassKey");

    // 图标：使用大字体的勾号
    int iconY = 50;
    tft.setTextColor(fg, bg);
    tft.setTextSize(6);
    int iconW = tft.textWidth("V");
    tft.setCursor((TFT_WIDTH - iconW) / 2, iconY);
    tft.print("V");

    // 消息文本（居中，自适应字号）
    int msgY = 110;
    tft.setTextColor(PASSKEY_WHITE, bg);
    int fontSize = 2;
    tft.setTextSize(fontSize);
    int textW = tft.textWidth(msg.c_str());
    while (fontSize > 1 && textW > TFT_WIDTH - 20) {
        fontSize--;
        tft.setTextSize(fontSize);
        textW = tft.textWidth(msg.c_str());
    }
    int charH = fontSize * 8;
    tft.setCursor((TFT_WIDTH - textW) / 2, msgY);
    tft.print(msg);

    // 底部提示
    tft.setTextColor(0x5AEB, bg);  // 灰色
    tft.setTextSize(1);
    const char *hint = "按任意键关闭";
    int hintW = tft.textWidth(hint);
    tft.setCursor((TFT_WIDTH - hintW) / 2, 200);
    tft.print(hint);
}
