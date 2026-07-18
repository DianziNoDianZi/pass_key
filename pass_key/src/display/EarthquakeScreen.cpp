/**
 * @file EarthquakeScreen.cpp
 * @brief 地震预警屏幕实现
 */

#include "EarthquakeScreen.h"
#include "DisplayManager.h"

#include <cmath>

// ==================== 构造 / 析构 ====================

EarthquakeScreen::EarthquakeScreen(DisplayManager *displayMgr,
                                   const String &epi,
                                   float mag,
                                   const String &inten,
                                   uint32_t countdown,
                                   uint32_t depth)
    : displayManager(displayMgr)
    , epicenter(epi)
    , magnitude(mag)
    , intensity(inten)
    , countdownSec(countdown)
    , depthKm(depth)
    , waveArrived(false)
    , closeRequested(false)
    , flashState(true)
    , lastFlashToggle(0)
    , startTime(0)
{
}

EarthquakeScreen::~EarthquakeScreen()
{
}

// ==================== 生命周期 ====================

void EarthquakeScreen::onActivate()
{
    startTime = millis();
    lastFlashToggle = millis();
    flashState = true;
    waveArrived = false;
    closeRequested = false;

    Serial.printf("[EQ] 地震预警屏幕激活: 震中=%s 震级=%.1f 烈度=%s 倒计时=%us\n",
                  epicenter.c_str(), magnitude, intensity.c_str(), countdownSec);
}

void EarthquakeScreen::onDeactivate()
{
    Serial.println(F("[EQ] 地震预警屏幕关闭"));
}

void EarthquakeScreen::onButtonPress(uint8_t button)
{
    (void)button;
    if (!closeRequested) {
        Serial.println(F("[EQ] 用户确认已避险，关闭报警"));
        closeRequested = true;
    }
}

void EarthquakeScreen::onUpdate()
{
    if (closeRequested) {
        if (displayManager) {
            displayManager->requestPop();
        }
        return;
    }

    uint32_t now = millis();

    // 计算当前倒计时
    uint32_t elapsedSec = (now - startTime) / 1000;
    int remaining;
    if (elapsedSec >= countdownSec) {
        remaining = 0;
        if (!waveArrived) {
            waveArrived = true;
            Serial.println(F("[EQ] 地震波已到达！"));
        }
    } else {
        remaining = countdownSec - elapsedSec;
    }

    // 边框脉冲：每 1s 交替（信息始终可见，边框闪烁吸引注意）
    if (now - lastFlashToggle >= 1000) {
        flashState = !flashState;
        lastFlashToggle = now;

        if (displayManager) {
            // 不 clear 全屏，只重绘边框（避免闪烁时信息消失）
            drawAlertScreen(displayManager->getTFT(), flashState);
            // 状态栏被红底覆盖，需要重新绘制
            displayManager->showStatusBar();
        }
    }
}

void EarthquakeScreen::onDraw(TFT_eSPI &tft)
{
    flashState = true;
    drawAlertScreen(tft, true);
}

// ==================== 辅助方法 ====================

String EarthquakeScreen::getMagnitudeStr() const
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", magnitude);
    return String(buf);
}

String EarthquakeScreen::getDepthStr() const
{
    if (depthKm == 0) return "";
    char buf[16];
    snprintf(buf, sizeof(buf), "%u km", depthKm);
    return String(buf);
}

// ==================== 绘制主方法 ====================

void EarthquakeScreen::drawAlertScreen(TFT_eSPI &tft, bool isBright)
{
    // 红色背景始终不变
    // 所有信息始终可见，仅边框脉冲闪烁吸引注意
    uint32_t now = millis();
    uint32_t elapsedSec = (now - startTime) / 1000;
    int remaining;
    if (elapsedSec >= countdownSec) {
        remaining = 0;
    } else {
        remaining = countdownSec - elapsedSec;
    }

    // 脉冲边框颜色：亮时为白，暗时为深红
    uint16_t borderColor = isBright ? TFT_WHITE : 0x7800; // 暗红

    if (waveArrived || remaining == 0) {
        // ===== 地震波已到达 =====
        tft.fillScreen(TFT_RED);

        // 脉冲边框
        tft.drawRect(0, 0, TFT_WIDTH, TFT_HEIGHT, borderColor);
        tft.drawRect(1, 1, TFT_WIDTH - 2, TFT_HEIGHT - 2, borderColor);

        // !! 警告图标（始终显示）
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextSize(4);
        const char *warnIcon = "!!";
        tft.setCursor((TFT_WIDTH - tft.textWidth(warnIcon)) / 2, 30);
        tft.print(warnIcon);

        // 警示文字（始终显示）
        tft.setTextSize(2);
        const char *msg = "Seismic Wave";
        tft.setCursor((TFT_WIDTH - tft.textWidth(msg)) / 2, 80);
        tft.print(msg);

        tft.setTextSize(2);
        const char *msg2 = "Has Arrived!";
        tft.setCursor((TFT_WIDTH - tft.textWidth(msg2)) / 2, 108);
        tft.print(msg2);

        tft.setTextSize(1);
        const char *protect = "Drop, Cover, Hold On";
        tft.setCursor((TFT_WIDTH - tft.textWidth(protect)) / 2, 150);
        tft.print(protect);

        // 底部提示
        tft.setTextColor(TFT_WHITE, TFT_RED);
        const char *dismissHint = "Press any key to dismiss";
        tft.setCursor((TFT_WIDTH - tft.textWidth(dismissHint)) / 2, TFT_HEIGHT - 16);
        tft.print(dismissHint);

    } else {
        // ===== 倒计时阶段（地震波尚未到达）=====
        tft.fillScreen(TFT_RED);

        // 脉冲边框
        tft.drawRect(0, 0, TFT_WIDTH, TFT_HEIGHT, borderColor);
        tft.drawRect(1, 1, TFT_WIDTH - 2, TFT_HEIGHT - 2, borderColor);

        // --- 以下所有信息始终可见 ---
        tft.setTextColor(TFT_WHITE, TFT_RED);

        // 第 1 行：标题
        tft.setTextSize(2);
        const char *title = "EARTHQUAKE";
        tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, TITLE_Y);
        tft.print(title);
        tft.setTextSize(1);
        const char *subtitle = "Early Warning";
        tft.setCursor((TFT_WIDTH - tft.textWidth(subtitle)) / 2, TITLE_Y + 18);
        tft.print(subtitle);

        // 第 2 行：震中位置
        tft.setTextSize(3);
        tft.setCursor((TFT_WIDTH - tft.textWidth(epicenter.c_str())) / 2, ICON_Y);
        tft.print(epicenter);

        // 第 3 行：震级 + 深度
        tft.setTextSize(2);
        String magStr = "M " + getMagnitudeStr();
        String depthStr = getDepthStr();
        if (depthStr.length() > 0) {
            magStr += "  |  Depth: " + depthStr;
        }
        tft.setCursor((TFT_WIDTH - tft.textWidth(magStr.c_str())) / 2, MAG_Y);
        tft.print(magStr);

        // 第 4 行：烈度
        tft.setTextSize(1);
        String intenseStr = "Intensity: " + intensity;
        tft.setCursor((TFT_WIDTH - tft.textWidth(intenseStr.c_str())) / 2, INFO_Y);
        tft.print(intenseStr);

        // 第 5 行：倒计时
        char countdownBuf[16];
        if (remaining > 0) {
            snprintf(countdownBuf, sizeof(countdownBuf), "%us", remaining);
        } else {
            snprintf(countdownBuf, sizeof(countdownBuf), "NOW");
        }

        uint16_t countdownColor = (remaining > 20) ? TFT_WHITE
                                 : (remaining > 10) ? TFT_YELLOW
                                 : 0xFDA0;
        tft.setTextColor(countdownColor, TFT_RED);
        tft.setTextSize(6);

        int cdW = tft.textWidth(countdownBuf);
        if (cdW > TFT_WIDTH - 10) {
            tft.setTextSize(4);
            cdW = tft.textWidth(countdownBuf);
        }
        tft.setCursor((TFT_WIDTH - cdW) / 2, COUNTDOWN_Y);
        tft.print(countdownBuf);

        // 倒计时标签
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.setTextSize(1);
        const char *cdLabel = "Seconds to Arrival";
        tft.setCursor((TFT_WIDTH - tft.textWidth(cdLabel)) / 2, COUNTDOWN_Y + 48);
        tft.print(cdLabel);

        // 底部提示
        const char *dismissHint = "Press any key to dismiss";
        tft.setCursor((TFT_WIDTH - tft.textWidth(dismissHint)) / 2, TFT_HEIGHT - 12);
        tft.print(dismissHint);
    }
}
