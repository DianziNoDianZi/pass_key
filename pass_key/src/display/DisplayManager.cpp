/**
 * @file DisplayManager.cpp
 * @brief 显示屏管理模块实现
 */

#include "DisplayManager.h"

DisplayManager::DisplayManager()
    : pendingPop(false)
{
}

DisplayManager::~DisplayManager()
{
}

bool DisplayManager::init()
{
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 0);

    // 基本显示测试：填充红/绿/蓝，显示 "PassKey" 文本
    tft.fillScreen(TFT_RED);
    delay(500);
    tft.fillScreen(TFT_GREEN);
    delay(500);
    tft.fillScreen(TFT_BLUE);
    delay(500);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor((TFT_WIDTH - tft.textWidth("PassKey")) / 2,
                  (TFT_HEIGHT - 16) / 2);
    tft.print("PassKey");
    delay(2000);

    tft.fillScreen(TFT_BLACK);

    return true;
}

void DisplayManager::pushScreen(Screen *screen)
{
    if (!screenStack.empty()) {
        screenStack.back()->onDeactivate();
    }
    screenStack.push_back(screen);
    screen->onActivate();
    clear();
    screen->onDraw(tft);
    showStatusBar();
}

void DisplayManager::popScreen()
{
    if (screenStack.empty()) return;

    screenStack.back()->onDeactivate();
    delete screenStack.back();
    screenStack.pop_back();

    if (!screenStack.empty()) {
        screenStack.back()->onActivate();
        clear();
        screenStack.back()->onDraw(tft);
    } else {
        clear();
    }
    showStatusBar();
}

void DisplayManager::replaceScreen(Screen *screen)
{
    if (!screenStack.empty()) {
        screenStack.back()->onDeactivate();
        delete screenStack.back();
        screenStack.pop_back();
    }
    screenStack.push_back(screen);
    screen->onActivate();
    clear();
    screen->onDraw(tft);
    showStatusBar();
}

void DisplayManager::update()
{
    if (screenStack.empty()) return;

    pendingPop = false;
    screenStack.back()->onUpdate();

    if (pendingPop && !screenStack.empty()) {
        popScreen();
    }
}

void DisplayManager::handleButtonPress(uint8_t button)
{
    if (screenStack.empty()) return;

    Screen *current = screenStack.back();
    current->onButtonPress(button);
    clear();
    current->onDraw(tft);
    showStatusBar();
}

void DisplayManager::showStatusBar()
{
    // 顶部状态栏，高度 16px，深色背景
    tft.fillRect(0, 0, TFT_WIDTH, 16, PASSKEY_DARK);
    tft.setTextColor(PASSKEY_WHITE, PASSKEY_DARK);
    tft.setTextSize(1);

    // WiFi 连接状态图标（占位符）
    tft.setCursor(2, 4);
    tft.print("W");

    // 4G 连接状态图标（占位符）
    tft.setCursor(14, 4);
    tft.print("4");

    // 电池图标（占位符，暂无实际电量数据）
    tft.setCursor(26, 4);
    tft.print((char)3); // 使用 TFT_eSPI 内置字符代替电池图标

    // 当前时间（占位符）
    tft.setCursor(TFT_WIDTH - 46, 4);
    tft.print("12:00");
}

void DisplayManager::clear()
{
    tft.fillScreen(TFT_BLACK);
}

TFT_eSPI &DisplayManager::getTFT()
{
    return tft;
}

Screen *DisplayManager::getCurrentScreen()
{
    if (screenStack.empty()) return nullptr;
    return screenStack.back();
}

void DisplayManager::requestPop()
{
    pendingPop = true;
}

void DisplayManager::notifyEvent(const char *event)
{
    for (auto *screen : screenStack) {
        if (screen) {
            screen->onEvent(event);
        }
    }
}
