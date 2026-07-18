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
    uint32_t now = millis();

    if (closeRequested) {
        if (displayManager) {
            displayManager->popScreen();
        }
        return;
    }

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

    // 闪烁切换：每 500ms 交替
    if (now - lastFlashToggle >= 500) {
        flashState = !flashState;
        lastFlashToggle = now;

        if (displayManager) {
            displayManager->clear();
            drawAlertScreen(displayManager->getTFT(), flashState);
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
    uint32_t now = millis();
    uint32_t elapsedSec = (now - startTime) / 1000;
    int remaining;
    if (elapsedSec >= countdownSec) {
        remaining = 0;
    } else {
        remaining = countdownSec - elapsedSec;
    }

    if (waveArrived || remaining == 0) {
        // ===== 地震波已到达：红底 + 警示文字 =====
        tft.fillScreen(TFT_RED);

        if (isBright) {
            tft.setTextColor(TFT_WHITE, TFT_RED);

            tft.setTextSize(4);
            const char *warnIcon = "!!";
            tft.setCursor((TFT_WIDTH - tft.textWidth(warnIcon)) / 2, 30);
            tft.print(warnIcon);

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
        }

        tft.setTextColor(isBright ? TFT_WHITE : 0x7800, TFT_RED);
        tft.setTextSize(1);
        const char *dismissHint = "Press any key to dismiss";
        tft.setCursor((TFT_WIDTH - tft.textWidth(dismissHint)) / 2, TFT_HEIGHT - 16);
        tft.print(dismissHint);

    } else {
        // ===== 倒计时阶段（地震波尚未到达）=====
        tft.fillScreen(TFT_RED);

        if (isBright) {
            tft.setTextColor(TFT_WHITE, TFT_RED);
            tft.setTextSize(2);
            const char *title = "EARTHQUAKE";
            tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, TITLE_Y);
            tft.print(title);
            tft.setTextSize(1);
            const char *subtitle = "Early Warning";
            tft.setCursor((TFT_WIDTH - tft.textWidth(subtitle)) / 2, TITLE_Y + 18);
            tft.print(subtitle);

            tft.setTextSize(3);
            tft.setCursor((TFT_WIDTH - tft.textWidth(epicenter.c_str())) / 2, ICON_Y);
            tft.print(epicenter);

            tft.setTextSize(2);
            String magStr = "M " + getMagnitudeStr();
            String depthStr = getDepthStr();
            if (depthStr.length() > 0) {
                magStr += "  |  Depth: " + depthStr;
            }
            tft.setCursor((TFT_WIDTH - tft.textWidth(magStr.c_str())) / 2, MAG_Y);
            tft.print(magStr);

            tft.setTextSize(1);
            String intenseStr = "Intensity: " + intensity;
            tft.setCursor((TFT_WIDTH - tft.textWidth(intenseStr.c_str())) / 2, INFO_Y);
            tft.print(intenseStr);

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

            tft.setTextColor(TFT_WHITE, TFT_RED);
            tft.setTextSize(1);
            const char *cdLabel = "Seconds to Arrival";
            tft.setCursor((TFT_WIDTH - tft.textWidth(cdLabel)) / 2, COUNTDOWN_Y + 48);
            tft.print(cdLabel);
        }

        tft.setTextColor(isBright ? TFT_WHITE : 0x7800, TFT_RED);
        tft.setTextSize(1);
        const char *dismissHint = "Press any key to dismiss";
        tft.setCursor((TFT_WIDTH - tft.textWidth(dismissHint)) / 2, TFT_HEIGHT - 12);
        tft.print(dismissHint);
    }
}
