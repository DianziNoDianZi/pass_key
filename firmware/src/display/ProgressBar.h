/**
 * @file ProgressBar.h
 * @brief 进度条工具类 — 支持渐变颜色和百分比显示
 */

#ifndef PROGRESS_BAR_H
#define PROGRESS_BAR_H

#include <Arduino.h>
#include <TFT_eSPI.h>

class ProgressBar {
public:
    /**
     * @brief 绘制进度条
     * @param tft       TFT 驱动引用
     * @param x         左上角 X 坐标
     * @param y         左上角 Y 坐标
     * @param w         宽度（像素）
     * @param h         高度（像素）
     * @param progress  进度值 (0.0 ~ 1.0)
     * @param color     主颜色（未使用，内部使用渐变色）
     */
    static void draw(TFT_eSPI &tft, int x, int y, int w, int h,
                     float progress, uint16_t color);

private:
    /**
     * @brief 根据进度值计算渐变色（绿 → 黄 → 红）
     * @param progress 进度值 (0.0 ~ 1.0)
     * @return RGB565 颜色值
     */
    static uint16_t gradientColor(float progress);
};

#endif // PROGRESS_BAR_H
