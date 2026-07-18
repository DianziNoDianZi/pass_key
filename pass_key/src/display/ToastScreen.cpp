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
    tft.fillScreen(APPLE_BG);

    // iOS 通知横幅 — 顶部卡片（圆角，浅灰背景）
    int bannerX = 10;
    int bannerY = 22;
    int bannerW = TFT_WIDTH - 20;
    int bannerH = 80;
    tft.fillRoundRect(bannerX, bannerY, bannerW, bannerH, 12, APPLE_GRAY5);
    tft.drawRoundRect(bannerX, bannerY, bannerW, bannerH, 12, APPLE_GRAY3);

    // 状态图标（左侧小圆点）
    tft.fillCircle(bannerX + 20, bannerY + bannerH / 2, 4, fg);

    // 消息文本（图标右侧，居中垂直）
    tft.setTextColor(PASSKEY_WHITE, APPLE_GRAY5);
    tft.setTextSize(2);
    int textW = tft.textWidth(msg.c_str());
    if (textW > bannerW - 50) {
        tft.setTextSize(1);
        textW = tft.textWidth(msg.c_str());
    }
    tft.setCursor(bannerX + 34, bannerY + (bannerH - 16) / 2);
    tft.print(msg);

    // 底部提示
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "Press any key";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 14);
    tft.print(hint);
}
