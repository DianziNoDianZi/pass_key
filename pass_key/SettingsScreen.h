/**
 * @file SettingsScreen.h
 * @brief 设备设置页面 — 背光亮度、休眠超时、时区配置
 */

#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class SettingsScreen : public Screen
{
public:
    SettingsScreen(DisplayManager *displayMgr);
    ~SettingsScreen();

    virtual const char *getName() const override { return "Settings"; }
    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayMgr;

    // 设置项索引
    enum SettingItem {
        ITEM_BRIGHTNESS,
        ITEM_STANDBY,
        ITEM_SLEEP,
        ITEM_TZ,
        ITEM_COUNT
    };

    int selectedIndex;  // 当前选中的设置项

    // 备份的原始值（用于取消时恢复）
    uint8_t  origBrightness;
    uint32_t origStandby;
    uint32_t origSleep;
    int32_t  origTzOffset;

    // 当前编辑中的值（临时存储）
    uint8_t  editBrightness;
    uint32_t editStandbySec;
    uint32_t editSleepSec;
    int32_t  editTzOffset;

    // 布局辅助
    static const int LINE_H    = 28;
    static const int LABEL_W   = 130;
    static const int TOP_Y     = 50;
    static const int LEFT_X    = 20;

    void drawSettingItem(TFT_eSPI &tft, int index, int y, const char *label,
                         const String &value, bool isSelected);

    String standbyLabel() const;
    String sleepLabel() const;
    String tzLabel() const;
};

#endif // SETTINGS_SCREEN_H
