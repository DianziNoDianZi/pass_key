/**
 * @file AboutScreen.cpp
 * @brief 设备信息页面实现
 */

#include "AboutScreen.h"
#include "config.h"
#include "MQTTManager.h"
#include "Air780epDriver.h"
#include "CryptoEngine.h"

extern Air780epDriver air780epDriver;
extern MQTTManager     mqttManager;
extern CryptoEngine    cryptoEngine;

AboutScreen::AboutScreen() {}
AboutScreen::~AboutScreen() {}

void AboutScreen::onActivate()
{
    // 尝试刷新信号强度
    mqttManager.refreshSignalStrength();
}

void AboutScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(2);

    // 标题
    const char *title = "About";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, 10);
    tft.print(title);
    tft.drawFastHLine(20, 36, TFT_WIDTH - 40, APPLE_SEP);

    int y = 55;
    int leftX = 20;
    int valX  = 100;

    tft.setTextSize(2);

    // 固件版本
    tft.setCursor(leftX, y); tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.print("Firmware");
    tft.setCursor(valX, y); tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.print(FW_VERSION);
    y += 30;

    // 设备 ID
    tft.setCursor(leftX, y); tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.print("Device ID");
    tft.setCursor(valX, y); tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.print(MQTT_DEVICE_ID);
    y += 30;

    // 信号强度
    int rssi = mqttManager.getSignalStrength();
    tft.setCursor(leftX, y); tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.print("Signal");
    tft.setCursor(valX, y); tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    if (rssi >= 0) {
        // RSSI 0-31 映射到信号格数
        int bars = (rssi >= 28) ? 4 : (rssi >= 22) ? 3 : (rssi >= 15) ? 2 : (rssi >= 8) ? 1 : 0;
        tft.printf("%d (%d/4 bars)", rssi, bars);
    } else {
        tft.print("Unknown");
    }
    y += 30;

    // MQTT 状态
    tft.setCursor(leftX, y); tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.print("MQTT");
    tft.setCursor(valX, y);
    if (mqttManager.isConnected()) {
        tft.setTextColor(APPLE_GREEN, APPLE_BG);
        tft.print("Connected");
    } else {
        tft.setTextColor(APPLE_RED, APPLE_BG);
        tft.print("Disconnected");
    }
    y += 30;

    // 公钥指纹
    String pubKeyBase64 = cryptoEngine.getPublicKeyBase64();
    if (pubKeyBase64.length() > 0) {
        // 取前 16 字符 + "..." 作为指纹
        String fp = pubKeyBase64.substring(0, 16) + "...";
        tft.setCursor(leftX, y); tft.setTextColor(APPLE_GRAY, APPLE_BG);
        tft.print("PubKey FP");
        tft.setTextSize(1);
        tft.setCursor(valX, y + 2); tft.setTextColor(APPLE_GRAY2, APPLE_BG);
        tft.print(fp);
    }

    // 底部提示
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "Long press CONFIRM to go back";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 20);
    tft.print(hint);
}
