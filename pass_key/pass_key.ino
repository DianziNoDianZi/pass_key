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
#include "WaitingScreen.h"
#include "ProgressBar.h"

#include "TOTPScreen.h"
#include "ToastScreen.h"
#include "EarthquakeScreen.h"
#include "ButtonManager.h"
#include "PowerManager.h"
#include "TimeManager.h"
#include "Air780epDriver.h"
#include "MQTTManager.h"
#include "TOTPManager.h"
#include "CryptoEngine.h"
#include "SecureStorage.h"
#include "FIDO2Manager.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
FIDO2Manager    fido2Manager;

// 主菜单屏幕（全局以便回调访问）
MenuScreen *mainMenu = nullptr;

// ==================== Core 0 MQTT 任务 ====================
// 将 MQTT 心跳、重连、消息接收放到 Core 0 运行，
// 避免阻塞 Core 1 的主循环（按键、显示）。
void mqttCore0Task(void *pvParameters)
{
    MQTTManager *mgr = (MQTTManager *)pvParameters;
    TickType_t lastWake = xTaskGetTickCount();
    UBaseType_t minStack = UINT32_MAX;
    int initPrint = 0;

    while (1) {
        mgr->loop();  // 心跳 + 重连（内部有 delay 让出 CPU）

        // 诊断 Core 0 栈水位（前 5 次无条件输出，之后只打印新低）
        UBaseType_t freeNow = uxTaskGetStackHighWaterMark(NULL);
        if (freeNow < minStack || initPrint < 5) {
            minStack = freeNow;
            Serial.printf("[STACK] mqttCore0 剩余栈: %u 字节 (历史最低 %u)\n",
                          freeNow, minStack);
            initPrint++;
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(50));  // ~20 Hz
    }
}

// ==================== Core 1 主循环任务 ====================
// 默认 Arduino loop 任务栈只有 8KB，在屏幕绘制 + JSON 解析下容易溢出。
// 改用独立任务分配 16KB 栈，确保不会栈溢出崩溃。
void mainLoopTask(void *pvParameters)
{
    TickType_t lastWake = xTaskGetTickCount();
    UBaseType_t minStack = UINT32_MAX;
    int initPrint = 0;

    while (1) {
        buttonManager.update();       // 轮询按键状态
        displayManager.update();      // 轮询显示屏更新
        mqttManager.processPendingMessages();  // 处理 MQTT 消息
        timeManager.update();         // NTP 重试
        powerManager.update();        // 电源管理
        fido2Manager.update();        // FIDO2 BLE

        // 每秒输出栈水位 + 帧率诊断
        UBaseType_t freeNow = uxTaskGetStackHighWaterMark(NULL);
        if (freeNow < minStack || initPrint < 5) {
            minStack = freeNow;
            Serial.printf("[STACK] mainLoop 剩余栈: %u 字节 (历史最低 %u)\n",
                          freeNow, minStack);
            initPrint++;
        }

        // 固定帧率 50 FPS
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20));
    }
}

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

            // 推送等待界面到屏幕栈，在 module init 及后续操作期间显示
            displayManager.pushScreen(new WaitingScreen(&displayManager, "Module starting..."));

            // 直接绘制 TFT 以获得即时视觉反馈（pushScreen 的 onDraw 在 setup 结束前不生效）
            TFT_eSPI &tft = displayManager.getTFT();
            tft.fillScreen(APPLE_BG);
            tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
            tft.setTextSize(3);
            const char *title = "PassKey";
            tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, TFT_HEIGHT / 2 - 50);
            tft.print(title);
            tft.drawFastHLine(30, TFT_HEIGHT / 2 - 20, TFT_WIDTH - 60, APPLE_GRAY3);
            tft.setTextColor(APPLE_GRAY, APPLE_BG);
            tft.setTextSize(2);
            const char *status = "Module starting...";
            tft.setCursor((TFT_WIDTH - tft.textWidth(status)) / 2, TFT_HEIGHT / 2 + 10);
            tft.print(status);
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

        // [修复] 唤醒路径必须初始化安全存储和加密引擎
        // TOTPManager 和 MQTT 设备注册均依赖它们
        if (!secureStorage.init()) {
            Serial.println(F("[ERROR] 安全存储唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] 安全存储唤醒初始化成功"));
        }
        if (!cryptoEngine.init()) {
            Serial.println(F("[ERROR] 加密引擎唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] 加密引擎唤醒初始化成功"));
        }

        // 初始化 TOTP 管理器
        if (!totpManager.init()) {
            Serial.println(F("[ERROR] TOTP 管理器唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] TOTP 管理器唤醒初始化成功"));
        }

        // 初始化 FIDO2 BLE 安全密钥
        if (!fido2Manager.init(&cryptoEngine)) {
            Serial.println(F("[WARN] FIDO2 管理器唤醒初始化失败"));
        } else {
            Serial.println(F("[OK] FIDO2 管理器唤醒初始化成功"));
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

            // 推送等待界面到屏幕栈，在 module init 及后续操作期间显示
            displayManager.pushScreen(new WaitingScreen(&displayManager, "Module starting..."));

            // 直接绘制 TFT 以获得即时视觉反馈
            TFT_eSPI &tft = displayManager.getTFT();
            tft.fillScreen(APPLE_BG);
            tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
            tft.setTextSize(3);
            const char *title = "PassKey";
            tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, TFT_HEIGHT / 2 - 50);
            tft.print(title);
            tft.drawFastHLine(30, TFT_HEIGHT / 2 - 20, TFT_WIDTH - 60, APPLE_GRAY3);
            tft.setTextColor(APPLE_GRAY, APPLE_BG);
            tft.setTextSize(2);
            const char *status = "Module starting...";
            tft.setCursor((TFT_WIDTH - tft.textWidth(status)) / 2, TFT_HEIGHT / 2 + 10);
            tft.print(status);
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

        // 初始化安全存储
        if (!secureStorage.init()) {
            Serial.println(F("[ERROR] 安全存储初始化失败"));
        } else {
            Serial.println(F("[OK] 安全存储初始化成功"));
        }

        // 初始化加密引擎
        if (!cryptoEngine.init()) {
            Serial.println(F("[ERROR] 加密引擎初始化失败"));
        } else {
            Serial.println(F("[OK] 加密引擎初始化成功"));
        }

        // 初始化 FIDO2 BLE 安全密钥
        if (!fido2Manager.init(&cryptoEngine)) {
            Serial.println(F("[WARN] FIDO2 管理器初始化失败"));
        } else {
            Serial.println(F("[OK] FIDO2 管理器初始化成功"));
        }

        // 初始化 TOTP 管理器
        if (!totpManager.init()) {
            Serial.println(F("[ERROR] TOTP 管理器初始化失败"));
        } else {
            Serial.println(F("[OK] TOTP 管理器初始化成功"));
        }

        // 启动 FIDO2 BLE 广播（在加密引擎就绪后）
        Serial.println(F("[OK] FIDO2 BLE 安全密钥已就绪"));
    }

    // ----- 显示初始化（所有启动方式共用） -----
    // 创建主菜单并替换等待界面（如果 WaitingScreen 存在则删除它）
    mainMenu = new MenuScreen("PassKey");
    mainMenu->addItem("TOTP Codes");
    mainMenu->addItem("Accounts");
    mainMenu->addItem("Settings");
    mainMenu->addItem("About");
    mainMenu->addItem("Sync");
    mainMenu->addItem("Security");
    mainMenu->addItem("Network");
    mainMenu->addItem("Logs");
    displayManager.replaceScreen(mainMenu);

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
            if (selected && strcmp(selected, "TOTP Codes") == 0) {
                // 检查当前栈顶是否已是 TOTPScreen，避免重复创建
                Screen *top = displayManager.getCurrentScreen();
                if (top && strcmp(top->getName(), "TOTP Codes") != 0) {
                    TOTPScreen *totpScreen = new TOTPScreen(&displayManager);
                    displayManager.pushScreen(totpScreen);
                }
            }
        }

        Serial.printf("[BTN] ShortPress pin=%d -> id=%d\n", pin, id);
    });

    // 长按回调：返回上一级 + 重置空闲计时器 + FIDO2 用户确认
    buttonManager.addLongPressCallback([](uint8_t pin) {
        if (pin == BTN_CONFIRM) {
            Serial.println(F("[BTN] LongPress CONFIRM -> 返回上一级"));
            // 弹出当前屏幕回到上一级（主菜单不会被弹出）
            displayManager.popScreen();
            fido2Manager.confirmUserPresence(true);
        } else if (pin == BTN_DOWN) {
            fido2Manager.confirmUserPresence(true);
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
            const char *reqId     = doc["requestId"].as<const char *>();
            const char *website   = doc["website"].as<const char *>();
            const char *source    = doc["source"].as<const char *>();
            const char *challenge = doc["challenge"].as<const char *>();
            uint32_t expiresAt    = doc["expiresAt"] | 0;

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
        }
        // ===== TOTP 远程管理 =====
        else if (strcmp(type, "totp_sync") == 0) {
            Serial.printf("[MQTT] 收到 totp_sync, payload 长度=%u\n", length);
            JsonArray accounts = doc["accounts"].as<JsonArray>();
            Serial.printf("[MQTT] totp_sync accounts 是否为 null: %d\n", accounts.isNull());
            if (!accounts.isNull()) {
                // 全量同步：替换所有账户
                String jsonStr;
                serializeJson(accounts, jsonStr);
                Serial.printf("[MQTT] totp_sync JSON 序列化完成, 长度=%d\n", jsonStr.length());
                int count = totpManager.syncFromServer(jsonStr.c_str());
                Serial.printf("[MQTT] TOTP 全量同步完成: %d 个账户\n", count);

                // 通知屏幕刷新
                displayManager.notifyEvent("totp_accounts_changed");

                // 弹出 Toast 提示同步结果
                {
                    char toastMsg[48];
                    snprintf(toastMsg, sizeof(toastMsg), "TOTP synced: %d accounts", count);
                    Serial.printf("[MQTT] 准备显示 Toast: %s\n", toastMsg);
                    ToastScreen *toast = new ToastScreen(&displayManager, String(toastMsg));
                    if (toast) {
                        displayManager.pushScreen(toast);
                        Serial.println("[MQTT] Toast 已推送");
                    }
                }

                // 发送确认
                String resp;
                JsonDocument respDoc;
                respDoc["type"] = "totp_sync_ack";
                respDoc["status"] = "ok";
                respDoc["accountCount"] = count;
                respDoc["timestamp"] = millis();
                serializeJson(respDoc, resp);
                bool pubOk = mqttManager.publish(("passkey/" + String(MQTT_DEVICE_ID) + "/resp").c_str(), resp.c_str());
                Serial.printf("[MQTT] totp_sync_ack 发送: %s\n", pubOk ? "成功" : "失败");
            } else {
                Serial.printf("[MQTT] totp_sync 处理失败: accounts 字段为空\n");
            }
        } else if (strcmp(type, "totp_add") == 0) {
            // 添加单个账户
            JsonObject account = doc["account"].as<JsonObject>();
            if (!account.isNull()) {
                const char *issuer = account["issuer"].as<const char *>();
                const char *accountName = account["accountName"].as<const char *>();
                const char *secret = account["secret"].as<const char *>();
                const char *name = (accountName && strlen(accountName) > 0) ? accountName : issuer;
                if (issuer && secret) {
                    bool ok = totpManager.addAccount(name, secret);
                    Serial.printf("[MQTT] TOTP 添加账户 '%s': %s\n", name, ok ? "成功" : "失败（可能已存在）");
                    if (ok) displayManager.notifyEvent("totp_accounts_changed");
                }
            }
        } else if (strcmp(type, "totp_delete") == 0) {
            // 删除账户（通过 accountName）
            const char *accountName = doc["accountName"].as<const char *>();
            if (accountName) {
                bool ok = totpManager.removeAccount(accountName);
                Serial.printf("[MQTT] TOTP 删除账户 '%s': %s\n", accountName, ok ? "成功" : "未找到");
                if (ok) displayManager.notifyEvent("totp_accounts_changed");
            }
        }
        // ===== 远程配置更新 =====
        else if (strcmp(type, "config_update") == 0) {
            JsonObject cfg = doc["config"].as<JsonObject>();
            if (!cfg.isNull()) {
                if (cfg["standbyTimeout"].is<int>()) {
                    powerManager.setStandbyTimeout(cfg["standbyTimeout"].as<int>());
                }
                if (cfg["deepSleepTimeout"].is<int>()) {
                    powerManager.setDeepSleepTimeout(cfg["deepSleepTimeout"].as<int>());
                }
                if (cfg["vibrationEnabled"].is<bool>()) {
                    // Vibration control - handled at notification level
                }
                if (cfg["screenBrightness"].is<int>()) {
                    powerManager.setBacklightBrightness(cfg["screenBrightness"].as<int>());
                }
                if (cfg["fido2Enabled"].is<bool>()) {
                    fido2Manager.setEnabled(cfg["fido2Enabled"].as<bool>());
                }
                if (cfg["fido2BleName"].is<const char *>()) {
                    FIDO2Config fc = fido2Manager.getConfig();
                    strncpy(fc.deviceName, cfg["fido2BleName"].as<const char *>(), sizeof(fc.deviceName) - 1);
                    fido2Manager.setConfig(fc);
                }
                Serial.println("[MQTT] 配置已更新");

                // 发送确认
                String resp;
                JsonDocument respDoc;
                respDoc["type"] = "config_update_ack";
                respDoc["status"] = "ok";
                respDoc["timestamp"] = millis();
                serializeJson(respDoc, resp);
                mqttManager.publish(("passkey/" + String(MQTT_DEVICE_ID) + "/resp").c_str(), resp.c_str());
            }
        }
        // ===== 地震预警 =====
        else if (strcmp(type, "earthquake_alert") == 0) {
            const char *epicenterVal = doc["epicenter"].as<const char *>();
            float magnitudeVal       = doc["magnitude"] | 0.0f;
            const char *intensityVal = doc["intensity"].as<const char *>();
            uint32_t countdownVal    = doc["countdown"] | 0;
            uint32_t depthVal        = doc["depth"] | 0;

            if (!epicenterVal || !intensityVal) {
                Serial.println("[MQTT] earthquake_alert 字段不完整");
                return;
            }

            Serial.printf("[MQTT] 地震预警: 震中=%s M=%.1f 烈度=%s 倒计时=%us\n",
                          epicenterVal, magnitudeVal, intensityVal, countdownVal);

            // 强制打断当前屏幕，全屏报警
            EarthquakeScreen *eqScreen = new EarthquakeScreen(
                &displayManager,
                String(epicenterVal),
                magnitudeVal,
                String(intensityVal),
                countdownVal,
                depthVal
            );
            displayManager.pushScreen(eqScreen);
        }
        // ===== 设备重置 =====
        else if (strcmp(type, "reset") == 0) {
            Serial.println("[MQTT] 收到 reset 指令，请求完全重连");
            // 发送确认
            String resp;
            JsonDocument respDoc;
            respDoc["type"] = "reset_ack";
            respDoc["status"] = "ok";
            respDoc["timestamp"] = millis();
            serializeJson(respDoc, resp);
            mqttManager.publish(("passkey/" + String(MQTT_DEVICE_ID) + "/resp").c_str(), resp.c_str());
            // 触发完全重连（会尽快断开并重新连接）
            mqttManager.requestReset();
        }
    });

    Serial.println(F("=== PassKey 初始化完成 ==="));

    // 启动 FIDO2 BLE 广播
    fido2Manager.start();

    // ---- 在 MQTT 任务启动前查询一次信号强度 ----
    // getSignalStrength 阻塞约 200ms，不能在 MQTT 任务循环中调用
    mqttManager.refreshSignalStrength();

    // ---- 启动 Core 0 MQTT 任务 ----
    // 将 MQTT 心跳/重连剥离到独立核心，主循环不再阻塞
    xTaskCreatePinnedToCore(
        mqttCore0Task,      // 任务函数
        "mqtt_core0",       // 任务名
        24576,              // 栈大小（字节）— 8K→16K 仍然崩溃，确诊是栈不足导致
        &mqttManager,       // 参数（MQTTManager 实例）
        1,                  // 优先级
        NULL,               // 任务句柄（不需要）
        0                   // Core 0
    );
    Serial.println(F("[OK] Core 0 MQTT 任务已启动"));

    // ---- 启动 Core 1 主循环任务 ----
    // 默认 loop 任务栈只有 8KB，TFT 绘制 + JSON 解析 + Serial.printf 很容易超
    xTaskCreatePinnedToCore(
        mainLoopTask,       // 任务函数
        "main_loop",        // 任务名
        24576,              // 栈大小 24KB（16KB 仍溢出, 保守加大）
        NULL,               // 参数
        1,                  // 优先级
        NULL,               // 任务句柄（不需要）
        1                   // Core 1
    );
    Serial.println(F("[OK] Core 1 主循环任务已启动"));
}

void loop()
{
    // 主循环工作已交给 mainLoopTask（Core 1, 16KB 栈）
    // 默认 loop 任务的 8KB 栈不够用
    vTaskDelay(pdMS_TO_TICKS(50));
}
