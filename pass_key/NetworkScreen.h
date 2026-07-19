/**
 * @file NetworkScreen.h
 * @brief 网络诊断页面 — 信号测试、MQTT Ping、模块重启
 */

#ifndef NETWORK_SCREEN_H
#define NETWORK_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class NetworkScreen : public Screen
{
public:
    NetworkScreen(DisplayManager *displayMgr);
    ~NetworkScreen();

    virtual const char *getName() const override { return "Network"; }
    virtual void onActivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayMgr;
    int selectedIndex;

    enum NetItem {
        ITEM_SIGNAL,
        ITEM_PING,
        ITEM_REBOOT_MODEM,
        ITEM_COUNT
    };

    int cachedRssi;
    bool pingResult;
    bool pingDone;
};

#endif // NETWORK_SCREEN_H
