/**
 * @file SettingsScreen.cpp
 * @brief 设备设置页面实现
 */

#include "SettingsScreen.h"
#include "DisplayManager.h"
#include "PowerManager.h"
#include "TimeManager.h"
#include "config.h"
#include <esp_sleep.h>

// ==================== 外部全局实例 ====================
extern PowerManager powerManager;
extern TimeManager  timeManager;

// ==================== 构造 / 析构 ====================

SettingsScreen::SettingsScreen(DisplayManager *displayMgr)
    : displayMgr(displayMgr)
    , selectedIndex(0)
    , origBrightness(255)
    , origStandby(30)
    , origSleep(300)
    , origTzOffset(TZ_OFFSET_SEC)
    , editBrightness(255)
    , editStandbySec(30)
    , editSleepSec(300)
    , editTzOffset(TZ_OFFSET_SEC)
{
}

SettingsScreen::~SettingsScreen()
{
}

// ==================== 生命周期 ====================

void SettingsScreen::onActivate()
{
    // 从当前配置加载备份值
    origBrightness = 255;  // 当前亮度，写死默认值
    origStandby    = powerManager.getStandbyTimeout() / 1000;
    origSleep      = powerManager.getDeepSleepTimeout() / 1000;
    origTzOffset   = TZ_OFFSET_SEC;

    editBrightness = origBrightness;
    editStandbySec = origStandby;
    editSleepSec   = origSleep;
    editTzOffset   = origTzOffset;
    selectedIndex  = 0;
}

void SettingsScreen::onDeactivate()
{
    // 离开时还原为由 onActivate 时加载的值
    // 若需要保留修改，请在退出时显式调用 apply
}

// ==================== 按键处理 ====================

void SettingsScreen::onButtonPress(uint8_t button)
{
    switch (button) {
        case BTN_ID_UP:
            // 调整当前选中项的值
            switch (selectedIndex) {
                case ITEM_BRIGHTNESS:
                    if (editBrightness < 255) editBrightness = (editBrightness + 17 > 255) ? 255 : editBrightness + 17;
                    powerManager.setBacklightBrightness(editBrightness);
                    break;
                case ITEM_STANDBY: {
                    const uint32_t opts[] = {10, 30, 60, 120, 300, 600};
                    int idx = 0;
                    while (idx < 6 && opts[idx] <= editStandbySec) idx++;
                    if (idx < 6) editStandbySec = opts[idx];
                    powerManager.setStandbyTimeout(editStandbySec);
                    break;
                }
                case ITEM_SLEEP: {
                    const uint32_t opts[] = {60, 120, 300, 600, 1800};
                    int idx = 0;
                    while (idx < 5 && opts[idx] <= editSleepSec) idx++;
                    if (idx < 5) editSleepSec = opts[idx];
                    powerManager.setDeepSleepTimeout(editSleepSec);
                    break;
                }
                case ITEM_TZ:
                    if (editTzOffset < 14 * 3600) editTzOffset += 3600;
                    break;
                default: break;
            }
            break;

        case BTN_ID_DOWN:
            switch (selectedIndex) {
                case ITEM_BRIGHTNESS:
                    if (editBrightness > 0) editBrightness = (editBrightness < 17) ? 0 : editBrightness - 17;
                    powerManager.setBacklightBrightness(editBrightness);
                    break;
                case ITEM_STANDBY: {
                    const uint32_t opts[] = {10, 30, 60, 120, 300, 600};
                    int idx = 5;
                    while (idx >= 0 && opts[idx] >= editStandbySec) idx--;
                    if (idx >= 0) editStandbySec = opts[idx];
                    powerManager.setStandbyTimeout(editStandbySec);
                    break;
                }
                case ITEM_SLEEP: {
                    const uint32_t opts[] = {60, 120, 300, 600, 1800};
                    int idx = 4;
                    while (idx >= 0 && opts[idx] >= editSleepSec) idx--;
                    if (idx >= 0) editSleepSec = opts[idx];
                    powerManager.setDeepSleepTimeout(editSleepSec);
                    break;
                }
                case ITEM_TZ:
                    if (editTzOffset > -12 * 3600) editTzOffset -= 3600;
                    break;
                default: break;
            }
            break;

        case BTN_ID_CONFIRM:
            // 切换到下一个设置项，或返回主菜单（在最后一项时）
            if (selectedIndex < ITEM_COUNT - 1) {
                selectedIndex++;
            } else {
                // 时区变更需要持久化，但暂不保存到 flash
                // 仅保留在实例中，重启后恢复 config.h 的默认值
                // 直接 pop 返回
                if (displayMgr) displayMgr->popScreen();
            }
            break;

        default:
            break;
    }
}

// ==================== 绘制 ====================

void SettingsScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(2);

    // 标题
    const char *title = "Settings";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, 10);
    tft.print(title);
    tft.drawFastHLine(20, 36, TFT_WIDTH - 40, APPLE_SEP);

    int y = TOP_Y;

    drawSettingItem(tft, ITEM_BRIGHTNESS, y, "Brightness",
                    String(editBrightness) + "/255",
                    selectedIndex == ITEM_BRIGHTNESS);
    y += LINE_H;

    drawSettingItem(tft, ITEM_STANDBY, y, "Standby after",
                    standbyLabel(), selectedIndex == ITEM_STANDBY);
    y += LINE_H;

    drawSettingItem(tft, ITEM_SLEEP, y, "Sleep after",
                    sleepLabel(), selectedIndex == ITEM_SLEEP);
    y += LINE_H;

    drawSettingItem(tft, ITEM_TZ, y, "Timezone",
                    tzLabel(), selectedIndex == ITEM_TZ);

    // 底部提示
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "UP/DOWN: adjust  CONFIRM: next  BACK: long press";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 20);
    tft.print(hint);
}

void SettingsScreen::drawSettingItem(TFT_eSPI &tft, int index, int y,
                                     const char *label, const String &value,
                                     bool isSelected)
{
    if (isSelected) {
        tft.fillRoundRect(LEFT_X - 4, y - 2, TFT_WIDTH - 2 * LEFT_X + 8,
                          LINE_H, 6, APPLE_GRAY4);
    }

    tft.setTextColor(isSelected ? APPLE_BLUE : PASSKEY_WHITE, isSelected ? APPLE_GRAY4 : APPLE_BG);
    tft.setTextSize(2);
    tft.setCursor(LEFT_X, y + 4);
    tft.print(isSelected ? "▸" : " ");
    tft.print(" ");
    tft.print(label);

    // 值（右对齐）
    tft.setTextColor(PASSKEY_WHITE, isSelected ? APPLE_GRAY4 : APPLE_BG);
    int valX = TFT_WIDTH - LEFT_X - tft.textWidth(value);
    tft.setCursor(valX, y + 4);
    tft.print(value);
}

// ==================== 值格式化 ====================

String SettingsScreen::standbyLabel() const
{
    if (editStandbySec < 60) return String(editStandbySec) + "s";
    return String(editStandbySec / 60) + "min";
}

String SettingsScreen::sleepLabel() const
{
    if (editSleepSec < 60) return String(editSleepSec) + "s";
    return String(editSleepSec / 60) + "min";
}

String SettingsScreen::tzLabel() const
{
    int hours = editTzOffset / 3600;
    if (hours >= 0) return "UTC+" + String(hours);
    return "UTC" + String(hours);  // UTC-5 等
}
