/**
 * @file DisplayManager.h
 * @brief 显示屏管理模块 — 基于 TFT_eSPI 驱动 ST7789
 *
 * 负责屏幕初始化、屏幕导航栈管理、状态栏绘制等操作。
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "Screen.h"

// ==================== 颜色定义（Apple 风格） ====================
#define PASSKEY_WHITE    0xFFFF
#define PASSKEY_BLACK    0x0000
#define APPLE_BLUE       0x03DF   // RGB(0, 122, 255)
#define APPLE_RED        0xF800   // RGB(255, 0, 0)
#define APPLE_GREEN      0x07E1   // RGB(52, 199, 89)
#define APPLE_ORANGE     0xFD20   // RGB(255, 159, 10)
#define APPLE_GRAY       0x5AEB   // RGB(142, 142, 147)
#define APPLE_GRAY2      0x39A7   // RGB(99, 99, 102)
#define APPLE_GRAY3      0x2108   // RGB(58, 58, 60)
#define APPLE_GRAY4      0x1082   // RGB(44, 44, 46)
#define APPLE_GRAY5      0x0841   // RGB(28, 28, 30)
#define APPLE_SEP        0x2124   // RGB(56, 56, 58) 分隔线
#define APPLE_BG         TFT_BLACK

// 保留旧名兼容
#define PASSKEY_BLUE     APPLE_BLUE
#define PASSKEY_DARK     APPLE_GRAY4
#define PASSKEY_GREEN    APPLE_GREEN

class DisplayManager
{
public:
    DisplayManager();
    ~DisplayManager();

    /**
     * @brief 初始化显示屏
     * @return true 成功，false 失败
     */
    bool init();

    /**
     * @brief 压入新屏幕（调用 onActivate，上一屏 onDeactivate）
     * @param screen 屏幕对象指针
     */
    void pushScreen(Screen *screen);

    /**
     * @brief 返回上一屏幕（调用 onDeactivate / onActivate）
     */
    void popScreen();

    /**
     * @brief 替换当前屏幕（不改变栈深度）
     * @param screen 新屏幕对象指针
     */
    void replaceScreen(Screen *screen);

    /**
     * @brief 每帧更新（由主 loop 调用）
     */
    void update();

    /**
     * @brief 向当前屏幕转发按键事件
     * @param button 按键 ID
     */
    void handleButtonPress(uint8_t button);

    /**
     * @brief 绘制顶部状态栏（WiFi/4G/电池/时间）
     */
    void showStatusBar();

    /**
     * @brief 清空屏幕
     */
    void clear();

    /**
     * @brief 向所有活跃屏幕发送事件通知
     * @param event 事件名称
     */
    void notifyEvent(const char *event);

    /**
     * @brief 获取当前显示的屏幕指针
     */
    Screen *getCurrentScreen();

    /**
     * @brief 请求安全弹出当前屏幕（在 update() 中安全执行）
     */
    void requestPop();

    /**
     * @brief 获取 TFT 驱动引用（供 Screen 子类直接绘制）
     */
    TFT_eSPI &getTFT();

private:
    TFT_eSPI              tft;
    std::vector<Screen *> screenStack;
    bool                  pendingPop;
    uint32_t              lastStatusBarRefresh;
};

#endif // DISPLAY_MANAGER_H
