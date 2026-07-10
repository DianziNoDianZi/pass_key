/**
 * @file ProgressBar.cpp
 * @brief 进度条工具类实现
 */

#include "ProgressBar.h"

void ProgressBar::draw(TFT_eSPI &tft, int x, int y, int w, int h,
                       float progress, uint16_t color)
{
    (void)color; // 未使用，内部使用渐变色

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    // 背景
    tft.fillRoundRect(x, y, w, h, 2, 0x2104); // 深灰色背景
    // 边框
    tft.drawRoundRect(x, y, w, h, 2, TFT_WHITE);

    // 填充部分（带渐变色）
    int fillW = (int)(w * progress);
    if (fillW > 0) {
        uint16_t barColor = gradientColor(progress);
        tft.fillRoundRect(x + 1, y + 1, fillW - 2, h - 2, 2, barColor);
    }

    // 百分比数字
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(progress * 100));
    tft.setCursor(x + w + 6, y + (h - 8) / 2);
    tft.print(pctStr);
}

uint16_t ProgressBar::gradientColor(float progress)
{
    uint8_t r, g, b;
    if (progress < 0.5f) {
        // 绿色 → 黄色
        float t = progress / 0.5f;
        r = (uint8_t)(t * 255.0f);
        g = 255;
        b = 0;
    } else {
        // 黄色 → 红色
        float t = (progress - 0.5f) / 0.5f;
        r = 255;
        g = (uint8_t)((1.0f - t) * 255.0f);
        b = 0;
    }
    // RGB888 → RGB565
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
