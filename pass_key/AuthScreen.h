/**
 * @file AuthScreen.h
 * @brief 登录确认屏幕 — 远程登录请求确认界面
 *
 * 显示来源网站和登录信息，提供确认/拒绝选项，
 * 处理签名并通过 MQTT 发送响应。
 */

#ifndef AUTH_SCREEN_H
#define AUTH_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

// AuthScreen 状态
enum AuthState {
    AUTH_WAITING,       // 等待用户操作
    AUTH_SIGNING,       // 签名中...
    AUTH_APPROVED,      // 已确认
    AUTH_DENIED,        // 已拒绝
    AUTH_TIMEOUT        // 已超时
};

// 选择项
enum AuthSelection {
    AUTH_SEL_CONFIRM = 0,
    AUTH_SEL_DENY    = 1
};

class AuthScreen : public Screen {
public:
    /**
     * @brief 构造认证确认屏幕
     * @param requestId  请求 ID
     * @param website    网站名称
     * @param source     来源信息（浏览器 + 地点）
     * @param challenge  挑战数据（Base64 编码）
     * @param expiresAt  过期时间戳（Unix 秒）
     */
    AuthScreen(const String &requestId, const String &website,
               const String &source, const String &challenge,
               uint32_t expiresAt);
    ~AuthScreen();

    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    String    requestId;
    String    website;
    String    source;
    String    challengeBase64;     // Base64 编码的挑战
    uint32_t  expiresAt;           // Unix 时间戳

    AuthState       state;
    AuthSelection   selection;
    uint32_t        startTime;      // 屏幕启动时间 (ms)
    uint32_t        statusStartTime; // 状态显示开始时间 (ms)
    bool            closeRequested; // 请求关闭

    // 常量
    static const int AUTH_TIMEOUT_SEC    = 60;
    static const int STATUS_APPROVED_MS  = 2000;
    static const int STATUS_DENIED_MS    = 1000;
    static const int STATUS_TIMEOUT_MS   = 1000;

    void drawWaiting(TFT_eSPI &tft);
    void drawSigning(TFT_eSPI &tft);
    void drawStatus(TFT_eSPI &tft, const char *icon, const char *message, uint16_t color);

    void handleConfirm();
    void handleDeny();
    void handleTimeout();

    /**
     * @brief Base64 解码辅助函数
     */
    bool base64Decode(const String &input, uint8_t *output, size_t &outputLen);
};

#endif // AUTH_SCREEN_H
