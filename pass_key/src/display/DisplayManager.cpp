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
    Screen *cur = screenStack.back();
    if (cur) {
        cur->onUpdate();
    }

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
    if (!current) return;           // 防御：nullptr

    current->onButtonPress(button);

    // onButtonPress 后当前屏幕可能已被弹出（closeRequest → requestPop 等路径）
    // 必须重新检查栈顶是否还是同一个屏幕
    if (!screenStack.empty()) {
        Screen *top = screenStack.back();
        if (top && top == current) {
            clear();
            current->onDraw(tft);
        }
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

    // 信号强度指示条（仅连接时有效）
    if (mqttManager.isConnected()) {
        int rssi = mqttManager.getSignalStrength();
        if (rssi >= 0) {
            // RSSI 映射为 0-5 格（AT+CSQ: 0=最弱, 31=最强）
            int bars = 0;
            if      (rssi >= 25) bars = 5;
            else if (rssi >= 20) bars = 4;
            else if (rssi >= 14) bars = 3;
            else if (rssi >= 9)  bars = 2;
            else if (rssi >= 4)  bars = 1;

            // 右侧信号条：5 根竖条，从左到右递增高度
            const int startX = TFT_WIDTH - 70;   // 紧挨时间左侧
            const int barW  = 3;                 // 每根宽度
            const int barGap = 1;                // 间距
            const int barBase = 13;              // 底部 y 坐标
            // 每根高度（从低到高）
            const uint8_t heights[5] = {2, 4, 6, 8, 10};

            for (int i = 0; i < 5; i++) {
                int x = startX + i * (barW + barGap);
                uint16_t color = (i < bars) ? APPLE_GREEN : APPLE_GRAY2;
                tft.fillRect(x, barBase - heights[i], barW, heights[i], color);
            }
        }
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
