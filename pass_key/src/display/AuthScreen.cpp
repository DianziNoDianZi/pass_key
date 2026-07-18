/**
 * @file AuthScreen.cpp
 * @brief 登录确认屏幕实现
 */

#include "AuthScreen.h"
#include "DisplayManager.h"
#include "CryptoEngine.h"
#include "MQTTManager.h"
#include "ButtonManager.h"
#include "config.h"

#include <ArduinoJson.h>
#include <mbedtls/base64.h>

// 外部全局实例
extern DisplayManager displayManager;
extern CryptoEngine   cryptoEngine;
extern MQTTManager    mqttManager;

// MQTT 响应主题（通过 config.h 中的设备 ID 构建）
static String getRespTopic()
{
    return "passkey/" + String(MQTT_DEVICE_ID) + "/resp";
}

AuthScreen::AuthScreen(const String &reqId, const String &web,
                       const String &src, const String &chal,
                       uint32_t expires)
    : requestId(reqId)
    , website(web)
    , source(src)
    , challengeBase64(chal)
    , expiresAt(expires)
    , state(AUTH_WAITING)
    , selection(AUTH_SEL_CONFIRM)
    , startTime(0)
    , statusStartTime(0)
    , closeRequested(false)
{
}

AuthScreen::~AuthScreen()
{
}

void AuthScreen::onActivate()
{
    state = AUTH_WAITING;
    selection = AUTH_SEL_CONFIRM;
    startTime = millis();
    statusStartTime = 0;
}

void AuthScreen::onDeactivate()
{
}

void AuthScreen::onButtonPress(uint8_t button)
{
    if (state == AUTH_SIGNING) {
        // 签名中不接受操作
        return;
    }

    if (state == AUTH_WAITING) {
        if (button == BTN_ID_UP || button == BTN_ID_DOWN) {
            // 切换选择
            selection = (selection == AUTH_SEL_CONFIRM) ? AUTH_SEL_DENY : AUTH_SEL_CONFIRM;
        } else if (button == BTN_ID_CONFIRM) {
            if (selection == AUTH_SEL_CONFIRM) {
                handleConfirm();
            } else {
                handleDeny();
            }
        }
        return;
    }

    // 在状态显示（已确认/已拒绝/已超时）阶段，按键关闭屏幕
    if (state == AUTH_APPROVED || state == AUTH_DENIED || state == AUTH_TIMEOUT) {
        closeRequested = true;
    }
}

void AuthScreen::onUpdate()
{
    // 关闭请求
    if (closeRequested) {
        displayManager.requestPop();
        return;
    }

    unsigned long now = millis();

    if (state == AUTH_WAITING) {
        // 检查超时
        unsigned long elapsed = (now - startTime) / 1000;
        if (elapsed >= AUTH_TIMEOUT_SEC) {
            handleTimeout();
            return;
        }

        // 检查 expires_at（如果已同步时间）
        if (expiresAt > 0) {
            time_t nowUnix = time(nullptr);
            if (nowUnix > 100000 && nowUnix >= (time_t)expiresAt) {
                handleTimeout();
                return;
            }
        }
    }

    // 状态显示结束后自动关闭
    if (state == AUTH_APPROVED && (now - statusStartTime) >= STATUS_APPROVED_MS) {
        displayManager.requestPop();
    }
    if (state == AUTH_DENIED && (now - statusStartTime) >= STATUS_DENIED_MS) {
        displayManager.requestPop();
    }
    if (state == AUTH_TIMEOUT && (now - statusStartTime) >= STATUS_TIMEOUT_MS) {
        displayManager.requestPop();
    }
}

void AuthScreen::onDraw(TFT_eSPI &tft)
{
    switch (state) {
        case AUTH_WAITING:
            drawWaiting(tft);
            break;
        case AUTH_SIGNING:
            drawSigning(tft);
            break;
        case AUTH_APPROVED:
            drawStatus(tft, "OK", "  Approved", TFT_GREEN);
            break;
        case AUTH_DENIED:
            drawStatus(tft, "X", "  Denied", TFT_RED);
            break;
        case AUTH_TIMEOUT:
            drawStatus(tft, "Z", "  Timeout", TFT_ORANGE);
            break;
    }
}

// ==================== 绘制函数（iOS 弹窗卡片风格） ====================

void AuthScreen::drawWaiting(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);

    int centerX = TFT_WIDTH / 2;

    // 卡片区域（带圆角的浅灰矩形）
    int cardW   = 220;
    int cardH   = 180;
    int cardX   = (TFT_WIDTH - cardW) / 2;
    int cardY   = 24;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, APPLE_GRAY5);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 12, APPLE_GRAY3);

    // 卡片标题
    tft.setTextColor(PASSKEY_WHITE, APPLE_GRAY5);
    tft.setTextSize(2);
    const char *title = "Login Request";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, cardY + 16);
    tft.print(title);

    // 网站名称（粗体，大号）
    int webY = cardY + 48;
    tft.setTextColor(PASSKEY_WHITE, APPLE_GRAY5);
    tft.setTextSize(2);
    int nameWidth = tft.textWidth(website.c_str());
    if (nameWidth > cardW - 20) {
        tft.setTextSize(1);
        nameWidth = tft.textWidth(website.c_str());
    }
    tft.setCursor((TFT_WIDTH - nameWidth) / 2, webY);
    tft.print(website);

    // 来源信息（灰色小字）
    int srcY = webY + 24;
    tft.setTextSize(1);
    tft.setTextColor(APPLE_GRAY, APPLE_GRAY5);
    int srcWidth = tft.textWidth(source.c_str());
    tft.setCursor((TFT_WIDTH - srcWidth) / 2, srcY);
    tft.print(source);

    // 分隔线
    int sepY = cardY + 132;
    tft.drawLine(cardX + 2, sepY, cardX + cardW - 2, sepY, APPLE_SEP);

    // 按钮：Deny / Allow
    int btnY = sepY + 4;
    int btnW = 100;
    int btnH = 36;
    int btnGap = 8;
    int denyX = centerX - btnGap / 2 - btnW;
    int allowX = centerX + btnGap / 2;

    // Deny 按钮
    if (selection == AUTH_SEL_DENY) {
        tft.fillRoundRect(denyX, btnY, btnW, btnH, 8, APPLE_RED);
        tft.setTextColor(PASSKEY_WHITE, APPLE_RED);
    } else {
        tft.fillRoundRect(denyX, btnY, btnW, btnH, 8, APPLE_GRAY3);
        tft.setTextColor(APPLE_GRAY, APPLE_GRAY3);
    }
    tft.setTextSize(2);
    tft.setCursor(denyX + (btnW - tft.textWidth("Deny")) / 2, btnY + (btnH - 16) / 2);
    tft.print("Deny");

    // Allow 按钮
    if (selection == AUTH_SEL_CONFIRM) {
        tft.fillRoundRect(allowX, btnY, btnW, btnH, 8, APPLE_BLUE);
        tft.setTextColor(PASSKEY_WHITE, APPLE_BLUE);
    } else {
        tft.fillRoundRect(allowX, btnY, btnW, btnH, 8, APPLE_GRAY3);
        tft.setTextColor(APPLE_GRAY, APPLE_GRAY3);
    }
    tft.setTextSize(2);
    tft.setCursor(allowX + (btnW - tft.textWidth("Allow")) / 2, btnY + (btnH - 16) / 2);
    tft.print("Allow");

    // 倒计时
    unsigned long elapsed = (millis() - startTime) / 1000;
    int remaining = AUTH_TIMEOUT_SEC - (int)elapsed;
    if (remaining < 0) remaining = 0;

    tft.setTextSize(1);
    tft.setTextColor(APPLE_GRAY, APPLE_GRAY3);
    char buf[16];
    snprintf(buf, sizeof(buf), "%ds  |  OK: Confirm", remaining);
    tft.setCursor((TFT_WIDTH - tft.textWidth(buf)) / 2, TFT_HEIGHT - 10);
    tft.print(buf);
}

void AuthScreen::drawSigning(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);

    int centerX = TFT_WIDTH / 2;

    // 卡片
    int cardW   = 200;
    int cardH   = 120;
    int cardX   = (TFT_WIDTH - cardW) / 2;
    int cardY   = (TFT_HEIGHT - cardH) / 2;
    tft.fillRoundRect(cardX, cardY, cardW, cardH, 12, APPLE_GRAY5);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 12, APPLE_GRAY3);

    // 进度动画圆点
    tft.fillCircle(centerX, cardY + 36, 6, APPLE_ORANGE);

    // 提示文字
    tft.setTextColor(PASSKEY_WHITE, APPLE_GRAY5);
    tft.setTextSize(2);
    const char *signMsg = "Signing...";
    tft.setCursor((TFT_WIDTH - tft.textWidth(signMsg)) / 2, cardY + 60);
    tft.print(signMsg);

    tft.setTextSize(1);
    tft.setTextColor(APPLE_GRAY, APPLE_GRAY5);
    const char *waitMsg = "Please wait";
    tft.setCursor((TFT_WIDTH - tft.textWidth(waitMsg)) / 2, cardY + 82);
    tft.print(waitMsg);
}

void AuthScreen::drawStatus(TFT_eSPI &tft, const char *icon,
                             const char *message, uint16_t color)
{
    tft.fillScreen(APPLE_BG);

    int centerX = TFT_WIDTH / 2;

    // 大号状态图标
    tft.setTextColor(color, APPLE_BG);
    tft.setTextSize(6);
    tft.setCursor(centerX - 24, 50);
    tft.print(icon);

    // 状态消息
    tft.setTextColor(color, APPLE_BG);
    tft.setTextSize(2);
    int msgWidth = tft.textWidth(message);
    tft.setCursor((TFT_WIDTH - msgWidth) / 2, 115);
    tft.print(message);

    // 底部提示
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    const char *hint = "Press any key";
    tft.setCursor((TFT_WIDTH - tft.textWidth(hint)) / 2, TFT_HEIGHT - 14);
    tft.print(hint);
}

// ==================== 操作处理 ====================

void AuthScreen::handleConfirm()
{
    state = AUTH_SIGNING;

    // Base64 解码挑战数据
    uint8_t challenge[256];
    size_t challengeLen = sizeof(challenge);
    if (!base64Decode(challengeBase64, challenge, challengeLen)) {
        Serial.println("[Auth] 挑战数据 Base64 解码失败");
        state = AUTH_DENIED;
        statusStartTime = millis();
        return;
    }

    // 签名
    uint8_t signature[256];
    size_t sigLen = sizeof(signature);
    if (!cryptoEngine.signChallenge(challenge, challengeLen, signature, sigLen)) {
        Serial.println("[Auth] 签名失败");
        state = AUTH_DENIED;
        statusStartTime = millis();
        return;
    }

    // Base64 编码签名结果
    size_t b64Len = 0;
    mbedtls_base64_encode(NULL, 0, &b64Len, signature, sigLen);
    size_t b64BufSize = b64Len + 1;
    uint8_t *sigB64 = (uint8_t *)malloc(b64BufSize);
    if (!sigB64) {
        state = AUTH_DENIED;
        statusStartTime = millis();
        return;
    }
    mbedtls_base64_encode(sigB64, b64BufSize, &b64Len, signature, sigLen);
    sigB64[b64Len] = '\0';

    // 获取公钥 Base64
    String pubKeyB64 = cryptoEngine.getPublicKeyBase64();

    // 构建 JSON 响应
    JsonDocument doc;
    doc["type"] = "auth_response";
    doc["request_id"] = requestId;
    doc["status"] = "approved";
    doc["signature"] = String((char *)sigB64);
    doc["public_key"] = pubKeyB64;

    String payload;
    serializeJson(doc, payload);

    // 通过 MQTT 发送
    String topic = getRespTopic();
    if (!mqttManager.publish(topic.c_str(), payload.c_str())) {
        Serial.println("[Auth] MQTT 发布确认消息失败");
    } else {
        Serial.println("[Auth] 已发送确认响应");
    }

    free(sigB64);

    state = AUTH_APPROVED;
    statusStartTime = millis();
}

void AuthScreen::handleDeny()
{
    JsonDocument doc;
    doc["type"] = "auth_response";
    doc["request_id"] = requestId;
    doc["status"] = "denied";
    doc["signature"] = "";
    doc["public_key"] = "";

    String payload;
    serializeJson(doc, payload);

    String topic = getRespTopic();
    if (!mqttManager.publish(topic.c_str(), payload.c_str())) {
        Serial.println("[Auth] MQTT 发布拒绝消息失败");
    } else {
        Serial.println("[Auth] 已发送拒绝响应");
    }

    state = AUTH_DENIED;
    statusStartTime = millis();
}

void AuthScreen::handleTimeout()
{
    JsonDocument doc;
    doc["type"] = "auth_response";
    doc["request_id"] = requestId;
    doc["status"] = "timeout";
    doc["signature"] = "";
    doc["public_key"] = "";

    String payload;
    serializeJson(doc, payload);

    String topic = getRespTopic();
    if (!mqttManager.publish(topic.c_str(), payload.c_str())) {
        Serial.println("[Auth] MQTT 发布超时消息失败");
    } else {
        Serial.println("[Auth] 已发送超时响应");
    }

    state = AUTH_TIMEOUT;
    statusStartTime = millis();
}

// ==================== 辅助方法 ====================

bool AuthScreen::base64Decode(const String &input, uint8_t *output, size_t &outputLen)
{
    size_t needed = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &needed,
                                     (const uint8_t *)input.c_str(), input.length());
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && ret != 0) {
        return false;
    }

    if (needed > outputLen) {
        outputLen = needed;
        return false;
    }

    ret = mbedtls_base64_decode(output, outputLen, &needed,
                                 (const uint8_t *)input.c_str(), input.length());
    if (ret != 0) {
        return false;
    }

    outputLen = needed;
    return true;
}
