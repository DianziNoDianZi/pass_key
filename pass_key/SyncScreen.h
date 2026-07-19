/**
 * @file SyncScreen.h
 * @brief 手动同步页面 — TOTP 全量同步、配置同步、RTC 时间同步
 */

#ifndef SYNC_SCREEN_H
#define SYNC_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class SyncScreen : public Screen
{
public:
    SyncScreen(DisplayManager *displayMgr);
    ~SyncScreen();

    virtual const char *getName() const override { return "Sync"; }
    virtual void onActivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayMgr;
    int selectedIndex;

    enum SyncItem {
        ITEM_TOTP,
        ITEM_CONFIG,
        ITEM_TIME,
        ITEM_COUNT
    };
};

#endif // SYNC_SCREEN_H
