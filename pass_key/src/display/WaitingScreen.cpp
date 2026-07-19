/**
 * @file WaitingScreen.cpp
 * @brief 等待提示屏幕实现
 */

#include "WaitingScreen.h"

WaitingScreen::WaitingScreen(DisplayManager *display, const String &status)
    : displayManager(display)
    , statusText(status)
{
}

WaitingScreen::~WaitingScreen()
{
}

void WaitingScreen::onActivate()
{
    // 进入时不需要额外操作，onDraw 会在下一帧被调用
}

void WaitingScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);

    // 标题
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(3);
    const char *title = "PassKey";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, TFT_HEIGHT / 2 - 50);
    tft.print(title);

    // 分隔线
    tft.drawFastHLine(30, TFT_HEIGHT / 2 - 20, TFT_WIDTH - 60, APPLE_GRAY3);

    // 状态文本
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(2);
    tft.setCursor((TFT_WIDTH - tft.textWidth(statusText)) / 2, TFT_HEIGHT / 2 + 10);
    tft.print(statusText);

    // 等待动画（占位符，后续可扩展）
    tft.setTextColor(APPLE_GRAY2, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "Please wait...";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT / 2 + 45);
    tft.print(hint);
}

void WaitingScreen::setStatus(const String &status)
{
    statusText = status;
}
