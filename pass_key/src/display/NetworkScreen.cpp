/**
 * @file NetworkScreen.cpp
 * @brief 网络诊断页面实现
 */

#include "NetworkScreen.h"
#include "DisplayManager.h"
#include "MQTTManager.h"
#include "Air780epDriver.h"
#include "ButtonManager.h"
#include "config.h"

extern Air780epDriver air780epDriver;
extern MQTTManager     mqttManager;

NetworkScreen::NetworkScreen(DisplayManager *displayMgr)
    : displayMgr(displayMgr)
    , selectedIndex(0)
    , cachedRssi(-1)
    , pingResult(false)
    , pingDone(false) {}
NetworkScreen::~NetworkScreen() {}

void NetworkScreen::onActivate()
{
    selectedIndex = 0;
    cachedRssi = mqttManager.getSignalStrength();
    pingResult = mqttManager.isConnected();
    pingDone = true;
}

void NetworkScreen::onButtonPress(uint8_t button)
{
    switch (button) {
        case BTN_ID_DOWN:
            if (selectedIndex < ITEM_COUNT - 1) selectedIndex++;
            break;
        case BTN_ID_UP:
            if (selectedIndex > 0) selectedIndex--;
            break;
        case BTN_ID_CONFIRM: {
            switch (selectedIndex) {
                case ITEM_SIGNAL:
                    mqttManager.refreshSignalStrength();
                    cachedRssi = mqttManager.getSignalStrength();
                    break;
                case ITEM_PING:
                    pingResult = mqttManager.isConnected();
                    pingDone = true;
                    break;
                case ITEM_REBOOT_MODEM:
                    Serial.println("[Network] 正在重启 4G 模块...");
                    if (air780epDriver.resetModule()) {
                        Serial.println("[Network] 4G 模块重启成功");
                    } else {
                        Serial.println("[Network] 4G 模块重启失败");
                    }
                    break;
            }
            break;
        }
        default: break;
    }
}

void NetworkScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(2);

    const char *title = "Network";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, 10);
    tft.print(title);
    tft.drawFastHLine(20, 36, TFT_WIDTH - 40, APPLE_SEP);

    int y = 55;
    int leftX = 20;

    // RSSI
    bool sel = (selectedIndex == ITEM_SIGNAL);
    if (sel) tft.fillRoundRect(leftX - 4, y - 2, TFT_WIDTH - 2 * leftX + 8, 30, 6, APPLE_GRAY4);
    tft.setTextColor(sel ? APPLE_BLUE : PASSKEY_WHITE, sel ? APPLE_GRAY4 : APPLE_BG);
    tft.setCursor(leftX, y + 4);
    tft.print(sel ? "▸ " : "  ");
    tft.print("Signal Test");
    String rssiStr = (cachedRssi >= 0) ? String(cachedRssi) + " RSSI" : "Press CONFIRM";
    tft.setCursor(TFT_WIDTH - leftX - tft.textWidth(rssiStr), y + 4);
    tft.print(rssiStr);
    y += 36;

    // MQTT Ping
    sel = (selectedIndex == ITEM_PING);
    if (sel) tft.fillRoundRect(leftX - 4, y - 2, TFT_WIDTH - 2 * leftX + 8, 30, 6, APPLE_GRAY4);
    tft.setTextColor(sel ? APPLE_BLUE : PASSKEY_WHITE, sel ? APPLE_GRAY4 : APPLE_BG);
    tft.setCursor(leftX, y + 4);
    tft.print(sel ? "▸ " : "  ");
    tft.print("MQTT Ping");
    String pingStr = pingDone ? (pingResult ? "Connected" : "Disconnected") : "...";
    tft.setCursor(TFT_WIDTH - leftX - tft.textWidth(pingStr), y + 4);
    tft.setTextColor(pingResult ? APPLE_GREEN : APPLE_RED, sel ? APPLE_GRAY4 : APPLE_BG);
    tft.print(pingStr);
    y += 36;

    // 模块重启
    sel = (selectedIndex == ITEM_REBOOT_MODEM);
    if (sel) tft.fillRoundRect(leftX - 4, y - 2, TFT_WIDTH - 2 * leftX + 8, 30, 6, APPLE_GRAY4);
    tft.setTextColor(sel ? APPLE_BLUE : PASSKEY_WHITE, sel ? APPLE_GRAY4 : APPLE_BG);
    tft.setCursor(leftX, y + 4);
    tft.print(sel ? "▸ " : "  ");
    tft.print("Reboot Modem");
    y += 36;

    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "CONFIRM: execute   BACK: long press";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 20);
    tft.print(hint);
}
