/**
 * @file EarthquakeScreen.h
 * @brief 地震预警屏幕 — 强制全屏报警（P2 地震预警功能）
 *
 * 收到服务器下发的 earthquake_alert MQTT 消息时，
 * 打断当前任何操作，全屏红底白字闪烁报警。
 *
 * 功能：
 * - 屏幕红底白字闪烁（500ms 交替）
 * - 显示震中、震级、本地烈度、到达倒计时
 * - 倒计时归零后显示 "地震波已到达，请保持防护"
 * - 按任意键确认已避险，关闭报警
 */

#ifndef EARTHQUAKE_SCREEN_H
#define EARTHQUAKE_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"
#include "DisplayManager.h"

class EarthquakeScreen : public Screen {
public:
    /**
     * @param displayMgr  显示管理器指针（用于 popScreen）
     * @param epicenter   震中位置名称
     * @param magnitude   震级
     * @param intensity   本地预估烈度（如 "V度"）
     * @param countdown   地震波到达倒计时（秒）
     * @param depth       震源深度（公里，0 表示未知）
     */
    EarthquakeScreen(DisplayManager *displayMgr,
                     const String &epicenter,
                     float magnitude,
                     const String &intensity,
                     uint32_t countdown,
                     uint32_t depth = 0);
    virtual ~EarthquakeScreen();

    virtual const char *getName() const override { return "EarthquakeAlert"; }
    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayManager;

    // 地震数据
    String epicenter;
    float  magnitude;
    String intensity;
    uint32_t countdownSec;
    uint32_t depthKm;

    // 状态
    bool   waveArrived;        // 地震波已到达（倒计时归零）
    bool   closeRequested;     // 请求关闭屏幕

    // 闪烁状态机
    bool   flashState;         // true=亮, false=暗
    uint32_t lastFlashToggle;  // 上次闪烁切换时间

    // 启动时间（用于倒计时计算）
    uint32_t startTime;

    // 布局常量
    static const int TITLE_Y   = 10;
    static const int ICON_Y    = 40;
    static const int MAG_Y     = 100;
    static const int INFO_Y    = 140;
    static const int COUNTDOWN_Y = 180;

    String getMagnitudeStr() const;
    String getDepthStr() const;
    void drawAlertScreen(TFT_eSPI &tft, bool isBright);
};

#endif // EARTHQUAKE_SCREEN_H
