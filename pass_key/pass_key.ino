/**
 * @file pass_key.ino
 * @brief PassKey 主入口文件
 *
 * PassKey 是一款基于 ESP32-S3 的硬件密码管理设备，
 * 支持 TOTP 动态口令生成、MQTT 远程同步等功能。
 * 本文件为程序入口，负责初始化各模块并进入主循环。
 */

#include "config.h"
#include "DisplayManager.h"
#include "Screen.h"
#include "MenuScreen.h"
#include "AuthScreen.h"
#include "ProgressBar.h"
#include "SMSNotificationScreen.h"
#include "TOTPScreen.h"
#include "ButtonManager.h"
#include "PowerManager.h"
#include "TimeManager.h"
#include "Air780epDriver.h"
#include "MQTTManager.h"
#include "TOTPManager.h"
#include "CryptoEngine.h"
#include "SecureStorage.h"

#include <ArduinoJson.h>

// 全局模块实例
DisplayManager displayManager;
ButtonManager   buttonManager;
PowerManager    powerManager;
TimeManager     timeManager;
Air780epDriver  air780epDriver;
MQTTManager     mqttManager;
TOTPManager     totpManager;
CryptoEngine    cryptoEngine;
SecureStorage   secureStorage;

// 主菜单屏幕（全局以便回调访问）
MenuScreen *mainMenu = nullptr;

void setup()
{
    // 初始化串口调试输出
    Serial.begin(SERIAL_BAUD);
    Serial.println();
    Serial.println(F("=== PassKey 启动 ==="));

    // ---- 第一步：电源管理初始化（检测唤醒原因） ----
    bool isWakeupFromDeepSleep = powerManager.init();

    if (isWakeupFromDeepSleep) {
        // ===== 深度睡眠唤醒路径 =====
        Serial.println(F("[PM] 深度睡眠唤醒，执行唤醒初始化..."));

        // 初始化显示屏
        if (!displayManager.init()) {
            Serial.println(F("[ERROR] 显示屏初始化失败"));
        } else {
            Serial.println(F("[OK] 显示屏初始化成功"));
        }

        // 初始化按键
        if (!buttonManager.init()) {
            Serial.println(F("[ERROR] 按键初始化失败"));
        } else {
            Serial.println(F("[OK] 按键初始化成功"));
        }

        // 重新初始化 4G 模块（TimeManager 依赖此模块获取时间）
        if (!air780epDriver.init()) {
            Serial.println(F("[WARN] Air780ep 唤醒初始化失败，稍后重试"));
        } else {
            Serial.println(F("[OK] Air780ep 唤醒初始化成功"));
        }

        // 初始化时间管理（通过 Air780ep AT+CCLK 获取 RTC 时间）
        if (!timeManager.init()) {
            Serial.println(F("[ERROR] 时间管理初始化失败"));
        } else {
            Serial.println(F("[OK] 时间管理初始化成功"));
        }

        // 初始化 TOTP 管理器
        if (!totpManager.init()) {
            Serial.println(F("[ERROR] TOTP 管理器唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] TOTP 管理器唤醒初始化成功"));
        }

        // 重新初始化 MQTT 并重新连接
        if (!mqttManager.init()) {
            Serial.println(F("[ERROR] MQTT 管理器唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] MQTT 管理器唤醒初始化成功"));
        }
        if (mqttManager.connect()) {
            Serial.println(F("[OK] MQTT 唤醒重连成功"));
        } else {
            Serial.println(F("[WARN] MQTT 唤醒重连失败，稍后重试"));
        }
    } else {
        // ===== 首次冷启动路径 =====
        Serial.println(F("[PM] 首次冷启动，执行完整初始化..."));

        // 初始化显示屏
        if (!displayManager.init()) {
            Serial.println(F("[ERROR] 显示屏初始化失败"));
        } else {
            Serial.println(F("[OK] 显示屏初始化成功"));
        }

        // 初始化按键
        if (!buttonManager.init()) {
            Serial.println(F("[ERROR] 按键初始化失败"));
        } else {
            Serial.println(F("[OK] 按键初始化成功"));
        }

        // 初始化 Air780ep 4G 模块（TimeManager 依赖此模块获取时间）
        if (!air780epDriver.init()) {
            Serial.println(F("[WARN] Air780ep 初始化失败，稍后重试"));
        } else {
            Serial.println(F("[OK] Air780ep 初始化成功"));
        }

        // 初始化时间管理（通过 Air780ep AT+CCLK 获取 RTC 时间）
        if (!timeManager.init()) {
            Serial.println(F("[ERROR] 时间管理初始化失败"));
        } else {
            Serial.println(F("[OK] 时间管理初始化成功"));
        }

        // 初始化 MQTT 管理器
        if (!mqttManager.init()) {
            Serial.println(F("[ERROR] MQTT 管理器初始化失败"));
        } else {
            Serial.println(F("[OK] MQTT 管理器初始化成功"));
        }

        // 初始化加密引擎
        if (!cryptoEngine.init()) {
            Serial.println(F("[ERROR] 加密引擎初始化失败"));
        } else {
            Serial.println(F("[OK] 加密引擎初始化成功"));
        }

        // 初始化安全存储
        if (!secureStorage.init()) {
            Serial.println(F("[ERROR] 安全存储初始化失败"));
        } else {
            Serial.println(F("[OK] 安全存储初始化成功"));
        }

        // 初始化 TOTP 管理器
        if (!totpManager.init()) {
            Serial.println(F("[ERROR] TOTP 管理器初始化失败"));
        } else {
            Serial.println(F("[OK] TOTP 管理器初始化成功"));
        }
    }

    // ----- 显示初始化（所有启动方式共用） -----
    // 创建主菜单并压入屏幕栈
    mainMenu = new MenuScreen("PassKey");
    mainMenu->addItem("TOTP 代码");
    mainMenu->addItem("账户管理");
    mainMenu->addItem("系统设置");
    mainMenu->addItem("关于设备");
    mainMenu->addItem("数据同步");
    mainMenu->addItem("安全选项");
    mainMenu->addItem("网络配置");
    mainMenu->addItem("日志查看");
    displayManager.pushScreen(mainMenu);

    // ----- 注册按键回调（所有启动方式共用） -----
    // 短按回调：路由到当前屏幕
    buttonManager.addShortPressCallback([](uint8_t pin) {
        ButtonId id;
        if (pin == BTN_UP) {
            id = BTN_ID_UP;
        } else if (pin == BTN_DOWN) {
            id = BTN_ID_DOWN;
        } else {
            id = BTN_ID_CONFIRM;
        }
        displayManager.handleButtonPress(id);
        powerManager.resetIdleTimer();  // 重置空闲计时器

        // 从主菜单确认进入 TOTP 代码视图
        if (id == BTN_ID_CONFIRM && mainMenu &&
            displayManager.getCurrentScreen() == mainMenu) {
            const char *selected = mainMenu->getSelectedItem();
            if (selected && strcmp(selected, "TOTP 代码") == 0) {
                TOTPScreen *totpScreen = new TOTPScreen(&displayManager);
                displayManager.pushScreen(totpScreen);
            }
        }

        Serial.printf("[BTN] ShortPress pin=%d -> id=%d\n", pin, id);
    });

    // 长按回调：返回主菜单 + 重置空闲计时器
    buttonManager.addLongPressCallback([](uint8_t pin) {
        if (pin == BTN_CONFIRM) {
            Serial.println(F("[BTN] LongPress CONFIRM -> 返回主菜单"));
        }
        powerManager.resetIdleTimer();  // 重置空闲计时器
    });

    // ----- 注册 MQTT 消息回调 -----
    mqttManager.setMessageCallback([](const char *topic, const uint8_t *payload, unsigned int length) {
        // 只处理 passkey/{deviceId}/cmd 主题的消息
        String topicStr = topic;
        String expectedTopic = "passkey/" + String(MQTT_DEVICE_ID) + "/cmd";
        if (topicStr != expectedTopic) {
            return;
        }

        // 解析 JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.printf("[MQTT] JSON 解析失败: %s\n", error.c_str());
            return;
        }

        // 检查消息类型
        const char *type = doc["type"].as<const char *>();
        if (!type) return;

        if (strcmp(type, "auth_request") == 0) {
            // 提取字段
            const char *reqId     = doc["request_id"].as<const char *>();
            const char *website   = doc["website"].as<const char *>();
            const char *source    = doc["source"].as<const char *>();
            const char *challenge = doc["challenge"].as<const char *>();
            uint32_t expiresAt    = doc["expires_at"] | 0;

            if (!reqId || !website || !source || !challenge) {
                Serial.println("[MQTT] auth_request 字段不完整");
                return;
            }

            Serial.printf("[MQTT] 收到登录请求: website=%s, source=%s\n", website, source);

            // 创建 AuthScreen 并压入屏幕栈
            AuthScreen *authScreen = new AuthScreen(
                String(reqId), String(website), String(source),
                String(challenge), expiresAt
            );
            displayManager.pushScreen(authScreen);
        } else if (strcmp(type, "sms_forward") == 0) {
            const char *senderVal   = doc["sender"].as<const char *>();
            const char *codeTypeVal = doc["code_type"].as<const char *>();
            const char *codeVal     = doc["code"].as<const char *>();
            bool recognizedVal      = doc["recognized"] | false;

            if (!senderVal)   senderVal   = "";
            if (!codeTypeVal) codeTypeVal = "";
            if (!codeVal)     codeVal     = "";

            SMSNotificationScreen *smsScreen = new SMSNotificationScreen(
                &displayManager,
                String(senderVal),
                String(codeTypeVal),
                String(codeVal),
                recognizedVal
            );
            displayManager.pushScreen(smsScreen);
            Serial.printf("[MQTT] sms_forward: sender=%s type=%s code=%s recognized=%d\n",
                          senderVal, codeTypeVal, codeVal, recognizedVal);
        }
    });

    Serial.println(F("=== PassKey 初始化完成 ==="));
}

void loop()
{
    // 轮询按键状态
    buttonManager.update();

    // 轮询显示屏更新
    displayManager.update();

    // 轮询 MQTT 消息
    mqttManager.loop();

    // NTP 重试逻辑（首次同步失败时后台重试）
    timeManager.update();

    // 电源管理轮询（检查空闲超时，自动切换电源状态）
    powerManager.update();

    // 简单延时，避免过度占用 CPU
    delay(10);
}
