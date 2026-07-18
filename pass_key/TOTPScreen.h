/**
 * @file TOTPScreen.h
 * @brief TOTP 验证码显示屏幕
 *
 * 支持账户列表浏览和验证码详情展示，
 * 包含倒计时进度条和自动返回功能。
 */

#ifndef TOTP_SCREEN_H
#define TOTP_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class TOTPScreen : public Screen
{
public:
    /**
     * @param displayMgr DisplayManager 指针，用于自动 pop 回主菜单
     */
    TOTPScreen(DisplayManager *displayMgr);
    ~TOTPScreen();

    virtual const char *getName() const override { return "TOTP Codes"; }
    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

    /**
     * @brief 事件通知："totp_accounts_changed" 时触发重绘
     */
    virtual void onEvent(const char *event) override;

    /**
     * @brief 通知账户列表已变更（由 MQTT 同步触发）
     * 下次 update 时自动重绘并修正 selectedIndex
     */
    void notifyAccountsChanged();

private:
    // ==================== 视图状态 ====================
    enum ViewState {
        STATE_LIST,      // 账户列表视图
        STATE_CODE       // 验证码详情视图
    };

    ViewState  currentState;
    int        selectedIndex;    // 列表中的选中项索引
    uint32_t   lastActivityTime; // 上次按键时间（millis）
    bool       codeDirty;        // 验证码需要刷新
    uint32_t   lastCodeTime;     // 上次生成验证码的时间计数器
    String     currentCode;      // 当前显示的验证码

    DisplayManager *displayMgr;

    bool       accountsDirty;    // 账户列表已变更，需要重绘

    // ==================== 布局常量 ====================
    static const int STATUS_HEIGHT = 16;
    static const int LIST_TITLE_HEIGHT = 24;
    static const int LIST_LINE_HEIGHT = 24;
    static const int VISIBLE_LINES = 8;

    // ==================== 超时配置 ====================
    static const uint32_t LIST_TIMEOUT_MS  = 30000;  // 列表视图 30 秒超时
    static const uint32_t CODE_TIMEOUT_MS  = 60000;  // 验证码视图 60 秒超时

    // ==================== 内部方法 ====================

    /**
     * @brief 绘制账户列表视图
     */
    void drawListView(TFT_eSPI &tft);

    /**
     * @brief 绘制验证码详情视图
     */
    void drawCodeView(TFT_eSPI &tft);

    /**
     * @brief 绘制倒计时进度条（自定义颜色）
     * @param tft       TFT 驱动
     * @param x         左上角 X
     * @param y         左上角 Y
     * @param w         宽度
     * @param h         高度
     * @param remaining 剩余秒数
     */
    void drawProgressBar(TFT_eSPI &tft, int x, int y, int w, int h,
                         uint32_t remaining);

    /**
     * @brief 根据剩余秒数获取进度条颜色
     * @param remaining 剩余秒数
     * @return RGB565 颜色值
     */
    uint16_t getProgressColor(uint32_t remaining);
};

#endif // TOTP_SCREEN_H
