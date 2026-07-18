/**
 * @file DisplayManager.cpp
 * @brief 显示屏管理模块实现
 */

#include "DisplayManager.h"
#include "TimeManager.h"
#include "MQTTManager.h"

// ==================== 外部全局实例 ====================
extern TimeManager   timeManager;
extern MQTTManager   mqttManager;

DisplayManager::DisplayManager()
    : pendingPop(false)
    , lastStatusBarRefresh(0)
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

    // 栈底为根屏幕（主菜单），不让删除
    if (screenStack.size() == 1) {
        return;
    }

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

    // 每 2 秒刷新状态栏（更新时间、连接状态）
    uint32_t now = millis();
    if (now - lastStatusBarRefresh > 2000) {
        lastStatusBarRefresh = now;
        showStatusBar();
    }
}

void DisplayManager::handleButtonPress(uint8_t button)
{
    if (screenStack.empty()) return;

    Screen *current = screenStack.back();
    current->onButtonPress(button);

    // onButtonPress 可能通过 closeRequest 导致屏幕弹出（如 AuthScreen）
    // 只有当前屏幕还在时，才刷新显示
    if (!screenStack.empty() && screenStack.back() == current) {
        clear();
        current->onDraw(tft);
    }
}

void DisplayManager::showStatusBar()
{
    // iOS 风格状态栏 — 纯黑背景
    tft.fillRect(0, 0, TFT_WIDTH, 16, APPLE_BG);

    // 左侧网络状态指示
    tft.setTextSize(1);
    if (mqttManager.isConnected()) {
        // 已连接：绿色圆点 + "ON"
        tft.fillCircle(8, 8, 3, APPLE_GREEN);
        tft.setTextColor(APPLE_GREEN, APPLE_BG);
        tft.setCursor(14, 4);
        tft.print("ON");
    } else if (mqttManager.isReconnecting()) {
        // 重连中：黄色 "REC" + 黄色圆点
        tft.fillCircle(8, 8, 3, TFT_YELLOW);
        tft.setTextColor(TFT_YELLOW, APPLE_BG);
        tft.setCursor(14, 4);
        tft.print("REC");
        // 显示重连次数（小字）
        int n = mqttManager.getReconnectAttempts();
        if (n > 0) {
            tft.setCursor(38, 4);
            tft.setTextColor(APPLE_GRAY, APPLE_BG);
            tft.printf("#%d", n);
        }
    } else {
        // 断连：红色 "OFF"
        tft.fillCircle(8, 8, 3, APPLE_RED);
        tft.setTextColor(APPLE_GRAY, APPLE_BG);
        tft.setCursor(14, 4);
        tft.print("OFF");
    }

    // 右侧时间（iOS 风格，粗体居中）
    time_t now = timeManager.getUnixTime();
    if (now > 100000) {
        struct tm *ti = localtime(&now);
        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", ti->tm_hour, ti->tm_min);
        tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
        tft.setCursor(TFT_WIDTH - 46, 4);
        tft.print(timeStr);
    } else {
        tft.setTextColor(APPLE_GRAY, APPLE_BG);
        tft.setCursor(TFT_WIDTH - 46, 4);
        tft.print("--:--");
    }
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
