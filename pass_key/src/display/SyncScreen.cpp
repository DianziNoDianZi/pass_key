/**
 * @file SyncScreen.cpp
 * @brief 手动同步页面实现
 */

#include "SyncScreen.h"
#include "DisplayManager.h"
#include "MQTTManager.h"
#include "TOTPManager.h"
#include "TimeManager.h"
#include "config.h"
#include <ArduinoJson.h>

extern MQTTManager mqttManager;
extern TOTPManager totpManager;
extern TimeManager timeManager;

SyncScreen::SyncScreen(DisplayManager *displayMgr)
    : displayMgr(displayMgr), selectedIndex(0) {}
SyncScreen::~SyncScreen() {}

void SyncScreen::onActivate()
{
    selectedIndex = 0;
}

void SyncScreen::onButtonPress(uint8_t button)
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
                case ITEM_TOTP: {
                    // 请求服务器重新同步 TOTP
                    String resp;
                    JsonDocument doc;
                    doc["type"] = "totp_sync_request";
                    serializeJson(doc, resp);
                    mqttManager.publish(("passkey/" + String(MQTT_DEVICE_ID) + "/resp").c_str(), resp.c_str());
                    Serial.println("[Sync] 已请求 TOTP 重新同步");
                    break;
                }
                case ITEM_CONFIG: {
                    String resp;
                    JsonDocument doc;
                    doc["type"] = "config_request";
                    serializeJson(doc, resp);
                    mqttManager.publish(("passkey/" + String(MQTT_DEVICE_ID) + "/resp").c_str(), resp.c_str());
                    Serial.println("[Sync] 已请求配置重新同步");
                    break;
                }
                case ITEM_TIME: {
                    if (timeManager.syncRTC()) {
                        Serial.println("[Sync] 时间同步成功");
                    } else {
                        Serial.println("[Sync] 时间同步失败");
                    }
                    break;
                }
                default: break;
            }
            // 同步后 pop 回主菜单
            if (displayMgr) displayMgr->popScreen();
            break;
        }
        default: break;
    }
}

void SyncScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(2);

    const char *title = "Sync";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, 10);
    tft.print(title);
    tft.drawFastHLine(20, 36, TFT_WIDTH - 40, APPLE_SEP);

    const char *labels[] = {"TOTP Accounts", "Device Config", "RTC Time"};
    int y = 55;
    int leftX = 20;

    for (int i = 0; i < ITEM_COUNT; i++) {
        bool sel = (i == selectedIndex);
        if (sel) {
            tft.fillRoundRect(leftX - 4, y - 2, TFT_WIDTH - 2 * leftX + 8, 30, 6, APPLE_GRAY4);
        }
        tft.setTextColor(sel ? APPLE_BLUE : PASSKEY_WHITE, sel ? APPLE_GRAY4 : APPLE_BG);
        tft.setCursor(leftX, y + 4);
        tft.print(sel ? "▸ " : "  ");
        tft.print(labels[i]);
        y += 36;
    }

    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "Select an item and press CONFIRM to sync";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 20);
    tft.print(hint);
}
