/**
 * @file SecurityScreen.cpp
 * @brief 安全设置页面实现
 */

#include "SecurityScreen.h"
#include "DisplayManager.h"
#include "CryptoEngine.h"
#include "SecureStorage.h"
#include "MQTTManager.h"
#include "ButtonManager.h"
#include "config.h"
#include <ArduinoJson.h>

extern CryptoEngine  cryptoEngine;
extern SecureStorage secureStorage;
extern MQTTManager   mqttManager;

SecurityScreen::SecurityScreen(DisplayManager *displayMgr)
    : displayMgr(displayMgr), selectedIndex(0) {}
SecurityScreen::~SecurityScreen() {}

void SecurityScreen::onActivate()
{
    selectedIndex = 0;
    String pk = cryptoEngine.getPublicKeyBase64();
    if (pk.length() > 20) {
        pubKeyPreview = pk.substring(0, 20) + "...";
    } else {
        pubKeyPreview = pk.length() > 0 ? pk : "N/A";
    }
}

void SecurityScreen::showToast(const String &msg)
{
    Serial.printf("[Security] %s\n", msg.c_str());
}

void SecurityScreen::onButtonPress(uint8_t button)
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
                case ITEM_PUBKEY: {
                    // 通过 MQTT 发布公钥
                    String pk = cryptoEngine.getPublicKeyBase64();
                    if (pk.length() > 0) {
                        String topic = "passkey/" + String(MQTT_DEVICE_ID) + "/resp";
                        JsonDocument doc;
                        doc["type"] = "pubkey_query";
                        doc["publicKey"] = pk;
                        String payload;
                        serializeJson(doc, payload);
                        mqttManager.publish(topic.c_str(), payload.c_str());
                        showToast("Public key published via MQTT");
                    }
                    break;
                }
                case ITEM_AUTH_LOG: {
                    // 请求服务器发送最近的认证记录
                    String topic = "passkey/" + String(MQTT_DEVICE_ID) + "/resp";
                    JsonDocument doc;
                    doc["type"] = "auth_log_request";
                    String payload;
                    serializeJson(doc, payload);
                    mqttManager.publish(topic.c_str(), payload.c_str());
                    showToast("Auth log requested");
                    break;
                }
                case ITEM_REGENERATE: {
                    // 重新生成密钥对
                    cryptoEngine.init();  // 重新初始化会生成新密钥
                    String newPk = cryptoEngine.getPublicKeyBase64();
                    String topic = "passkey/" + String(MQTT_DEVICE_ID) + "/resp";
                    JsonDocument doc;
                    doc["type"] = "pubkey_regenerated";
                    doc["publicKey"] = newPk;
                    String payload;
                    serializeJson(doc, payload);
                    mqttManager.publish(topic.c_str(), payload.c_str());
                    showToast("Key pair regenerated");
                    // 更新预览
                    pubKeyPreview = newPk.substring(0, 20) + "...";
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

void SecurityScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(2);

    const char *title = "Security";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, 10);
    tft.print(title);
    tft.drawFastHLine(20, 36, TFT_WIDTH - 40, APPLE_SEP);

    const char *labels[] = {"Show Public Key", "Request Auth Log", "Regenerate Keys"};
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

    // 公钥预览
    tft.setTextSize(1);
    tft.setTextColor(APPLE_GRAY2, APPLE_BG);
    tft.setCursor(leftX, y + 8);
    tft.print("PubKey: " + pubKeyPreview);

    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "CONFIRM: execute   BACK: long press";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 20);
    tft.print(hint);
}
