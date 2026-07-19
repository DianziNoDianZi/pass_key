/**
 * @file MQTTManager.cpp
 * @brief MQTT 消息管理模块实现 — Native MQTT AT 命令方案
 *
 * 使用 Air780ep 的 MQTT AT 命令集（+QMTOPEN/+QMTCONN/+QMTSUB/+QMTPUB）
 * 实现 MQTT 连接管理，完全绕过 CIPSEND + PubSubClient 链路。
 *
 * 相比旧方案优势：
 *   - 模块内部运行完整 MQTT 协议栈（含自动 PINGREQ 保活）
 *   - 无 +IPD 数据污染，+QMTRECV URC 直接推送消息
 *   - 无需 ESP32 拼装 MQTT 协议包
 *   - TCP 连接由模块管理，断开时自动上报 URCCLOSED 事件
 */

#include "MQTTManager.h"
#include "MQTTAtDriver.h"
#include "Air780epDriver.h"
#include "CryptoEngine.h"
#include "config.h"

// 全局 CryptoEngine 实例（在 pass_key.ino 中定义）
extern CryptoEngine cryptoEngine;

// 重连退避常量（毫秒）
#define RECONNECT_DELAY_MIN      5000    // 5 秒
#define RECONNECT_DELAY_MID     30000    // 30 秒
#define RECONNECT_DELAY_HIGH    60000    // 1 分钟
#define RECONNECT_DELAY_MAX    300000    // 5 分钟

// MQTT AT 驱动 Keepalive（秒），模块默认 60s
// 模块内部自动发送 PINGREQ，无需应用层心跳
#define MQTT_AT_KEEPALIVE       60

// 全局实例指针，用于静态 URC 回调转发
static MQTTManager *gMqttManagerInstance = nullptr;

MQTTManager::MQTTManager()
    : mqttAtDriver(nullptr)
    , driver(nullptr)
    , brokerPort(MQTT_BROKER_PORT)
    , useSSL(false)
    , connected(false)
    , initialized(false)
    , messageCallback(nullptr)
    , lastReconnectAttempt(0)
    , reconnectDelay(RECONNECT_DELAY_MIN)
    , reconnectAttempts(0)
    , gprsFailCount(0)
    , cachedRssi(-1)
    , lastRssiPoll(0)
    , lastHeartbeatTime(0)
    , resetRequested(false)
    , needsResubscribe(false)
{
}

MQTTManager::~MQTTManager()
{
    // 先清空全局实例指针，避免析构过程中回调悬空
    if (gMqttManagerInstance == this) {
        gMqttManagerInstance = nullptr;
    }
    disconnect();
    delete mqttAtDriver;
}

bool MQTTManager::init(const char *clientId, const char *broker, uint16_t port, bool ssl)
{
    // 保存配置参数
    this->clientId   = String(clientId);
    this->brokerAddr = String(broker);
    this->brokerPort = port;
    this->useSSL     = ssl;

    // 构建主题字符串
    topicCmd  = "passkey/" + this->clientId + "/cmd";
    topicResp = "passkey/" + this->clientId + "/resp";

    // 查找 Air780epDriver 全局实例
    extern Air780epDriver air780epDriver;
    driver = &air780epDriver;

    // 创建 MQTT AT 驱动
    mqttAtDriver = new MQTTAtDriver(driver);

    // 注册全局实例用于静态 URC 回调转发
    gMqttManagerInstance = this;

    // 注册 +MSUB URC 回调到 Air780epDriver
    // 当模块收到 MQTT 消息时，通过 URC 通道 +MSUB:"topic",len byte,<payload> 推送，
    // Air780epDriver 在 available()/waitForResponse() 中检测到该前缀后
    // 调用此回调，由 mqttAtDriver->handleRecvURC() 解析并将消息放入 msgQueue
    driver->setURCCallback(urcCallbackStatic);

    // 创建跨核消息队列（Core 0 UAV → Core 1 处理）
    msgQueue = xQueueCreate(16, sizeof(PendingMsg));

    // 将 MQTTAtDriver 的接收队列指向 msgQueue，使其解析的 +MSUB 消息直接入队
    mqttAtDriver->setRecvQueue(msgQueue);

    // 创建出站消息队列（Core 1 → Core 0，publish() 的异步通道）
    outMsgQueue = xQueueCreate(8, sizeof(OutgoingMsg));

    initialized = true;

    Serial.printf("[MQTT-AT] 初始化完成: broker=%s:%u, clientId=%s, SSL=%s\n",
                  brokerAddr.c_str(), brokerPort, this->clientId.c_str(),
                  useSSL ? "是" : "否");
    Serial.printf("[MQTT-AT] 订阅主题: %s\n", topicCmd.c_str());
    Serial.printf("[MQTT-AT] 发布主题: %s\n", topicResp.c_str());

    return true;
}

bool MQTTManager::connect()
{
    if (!initialized || !driver || !mqttAtDriver) {
        Serial.println(F("[MQTT-AT] 未初始化，无法连接"));
        return false;
    }

    // 检查模块是否就绪
    if (!driver->isModuleReady()) {
        Serial.println(F("[MQTT-AT] 4G 模块未就绪，重试初始化"));
        if (!driver->init()) {
            return false;
        }
    }

    // ---- 1. 配置 GPRS ----
    Serial.printf("[MQTT-AT] 配置 GPRS (APN=%s)...\n", AIR780EP_APN);
    if (!driver->configureGPRS(AIR780EP_APN)) {
        gprsFailCount++;
        Serial.printf("[MQTT-AT] GPRS 配置失败 (gprsFail=%d)\n", gprsFailCount);
        return false;
    }
    gprsFailCount = 0;

    // GPRS 就绪后等待 500ms，让模块网络栈稳定
    delay(500);

    // 断开模块中残留的旧 MQTT 连接（如果有），并清空 UART 缓冲区
    mqttAtDriver->disconnect(0);
    driver->flushUART();
    delay(100);

    // ---- 2. 打开 MQTT 连接 ----
    // 使用持久会话（cleanSession=false），断线后 Broker 保留订阅和未送达消息
    // Air780ep 模块内部自动发送 MQTT PINGREQ 保活，无需应用层心跳
    Serial.printf("[MQTT-AT] 连接 %s:%u...\n", brokerAddr.c_str(), brokerPort);

    bool ok = mqttAtDriver->openAndConnect(
        0,                              // connIdx=0
        brokerAddr.c_str(),
        brokerPort,
        clientId.c_str(),
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_AT_KEEPALIVE,              // 模块内部自动保活
        false                           // cleanSession=false（持久会话）
    );

    if (!ok) {
        Serial.println(F("[MQTT-AT] 连接失败"));
        return false;
    }

    connected = true;
    reconnectAttempts = 0;
    reconnectDelay = RECONNECT_DELAY_MIN;
    lastHeartbeatTime = millis();

    // ---- 3. 订阅命令主题 ----
    if (subscribe(topicCmd.c_str())) {
        Serial.printf("[MQTT-AT] 已订阅: %s\n", topicCmd.c_str());
    }

    // ---- 4. 注册设备公钥 ----
    registerDevice();

    return true;
}

bool MQTTManager::disconnect()
{
    if (mqttAtDriver) {
        mqttAtDriver->disconnect(0);
    }
    connected = false;

    // 清空出站队列，避免重连后发送过时的消息
    if (outMsgQueue) {
        OutgoingMsg msg;
        while (xQueueReceive(outMsgQueue, &msg, 0) == pdTRUE) {}
    }
    return true;
}

bool MQTTManager::isConnected() const
{
    return connected;
}

bool MQTTManager::isReconnecting() const
{
    return !connected && reconnectAttempts > 0;
}

bool MQTTManager::publish(const char *topic, const char *payload, bool retained)
{
    if (!initialized) return false;

    // 推入出站队列，由 Core 0 的 loop() 实际发送
    // 这样避免 Core 1 的 processPendingMessages() 和 Core 0 的 mqttAtDriver
    // 同时操作 MQTT 驱动导致线程安全问题。
    OutgoingMsg msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    msg.length = strlen(msg.payload);
    msg.retained = retained;

    if (outMsgQueue && xQueueSend(outMsgQueue, &msg, 0) != pdTRUE) {
        Serial.println(F("[MQTT-AT] 出站消息队列满，消息丢弃"));
        return false;
    }
    return true;
}

bool MQTTManager::subscribe(const char *topic)
{
    if (!mqttAtDriver) return false;

    // 使用 QoS 1 订阅，msgId 固定为 1
    // MQTT AT 驱动同步等待 +QMTSUB: 响应，超时 10 秒
    if (connected) {
        return mqttAtDriver->subscribe(0, 1, topic, 1);
    }
    // 未连接时记录状态，重连时重新订阅
    needsResubscribe = true;
    return true;
}

bool MQTTManager::unsubscribe(const char *topic)
{
    if (!mqttAtDriver || !connected) return false;
    return mqttAtDriver->unsubscribe(0, 1, topic);
}

void MQTTManager::setMessageCallback(MQTTManagerCallback callback)
{
    messageCallback = callback;
}

void MQTTManager::refreshSignalStrength()
{
    int rssi = 0;
    if (driver && driver->getSignalStrength(rssi)) {
        cachedRssi = rssi;
        Serial.printf("[SIG] 信号强度 RSSI=%d\n", rssi);
    } else {
        cachedRssi = -1;
    }
}

void MQTTManager::requestReset()
{
    resetRequested = true;
    Serial.println(F("[MQTT-AT] 收到 reset 请求，将在下次 loop 中执行完全重连"));
}

void MQTTManager::loop()
{
    if (!initialized || !driver) return;

    // 始终读取 UART 数据以处理 URC（如 +QMTRECV、+QIURC、CLOSED 等）
    // 否则断开连接等待重连期间 URC 会堆积在 UART 缓冲区中。
    // available() 内部会检测 +QMTRECV 并通过回调转发到 msgQueue。
    driver->available();

    // 处理出站消息队列（在 Core 0 上，与 mqttAtDriver 在同一核心）
    processOutgoing();

    // ========== 已连接状态 ==========
    if (connected) {
        unsigned long nowMs = millis();

        // 每 30 秒检查一次 MQTT 连接健康状态：
        // 使用 AT+QMTSTAT 查询模块内部 MQTT 连接状态。
        // 注意：不每轮 loop() 都调用的原因：
        //   isConnected() 发 AT+QMTSTAT 并等待响应（至少 100ms），
        //   每 50ms 调用一次会严重拖慢 MQTT 任务循环。
        if (nowMs - lastHeartbeatTime >= 30000) {
            lastHeartbeatTime = nowMs;
            if (!mqttAtDriver->isConnected(0)) {
                connected = false;
                Serial.println(F("[MQTT-AT] MQTT 连接丢失 (QMTSTAT 检测)"));
                mqttAtDriver->disconnect(0);
            }
        }

        // 旧方案：
        //   - CIPSTO=5s 导致 TCP 空闲 5 秒后模块自动断开连接
        //   - 需要每 2 秒发 PUBLISH 心跳防止超时
        // 新方案（+QMTOPEN/+QMTCONN 原生 MQTT AT 命令）：
        //   - 模块内部运行完整 MQTT 协议栈，自动发送 PINGREQ
        //   - keepalive=60 秒时，模块在约 45 秒无活动时发 PINGREQ
        //   - 无需应用层心跳，降低流量和 CPU 开销
        // 断开检测依赖：
        //   1) 30 秒周期的 QMTSTAT 查询
        //   2) publish/subscribe 发送失败时主动标记断开
        //   3) 模块上报的 URC（CLOSED、+QIURC 等）

        // 如果 subscribe 在 attemptReconnect() 中失败（AT+QMTSUB 暂不可用），
        // 在后续 loop() 中自动重试。
        if (needsResubscribe && mqttAtDriver) {
            if (mqttAtDriver->subscribe(0, 1, topicCmd.c_str(), 1)) {
                needsResubscribe = false;
                Serial.printf("[MQTT-AT] 已订阅 %s (loop 重试)\n", topicCmd.c_str());
            }
        }
    }

    unsigned long now = millis();

    // ========== 模块未就绪 ==========
    if (driver && !driver->isModuleReady()) {
        if (now - lastReconnectAttempt >= 5000) {
            lastReconnectAttempt = now;
            Serial.println(F("[MQTT-AT] 4G 模块未就绪，尝试重新初始化..."));
            if (driver->init()) {
                Serial.println(F("[MQTT-AT] 4G 模块重新初始化成功"));
                gprsFailCount = 0;
            } else {
                Serial.println(F("[MQTT-AT] 4G 模块重新初始化失败，5 秒后重试"));
            }
        }
        return;
    }

    // ========== 检查 reset 请求 ==========
    if (resetRequested) {
        resetRequested = false;
        Serial.println(F("[MQTT-AT] 执行完全重连..."));
        if (connected) {
            mqttAtDriver->disconnect(0);
            connected = false;
        }
        if (driver && driver->isConnected()) {
            driver->disconnect();
        }
        if (driver) {
            driver->resetGprsConfig();
        }
        OutgoingMsg discard;
        while (outMsgQueue && xQueueReceive(outMsgQueue, &discard, 0) == pdTRUE) {}
        lastReconnectAttempt = 0;
        reconnectAttempts = 0;
        reconnectDelay = RECONNECT_DELAY_MIN;
        Serial.println(F("[MQTT-AT] 完全重连触发完成，等待下次 loop 执行重连"));
    }

    // ========== 自动重连逻辑 ==========
    if (!connected && driver) {
        if (now - lastReconnectAttempt >= reconnectDelay) {
            lastReconnectAttempt = now;

            Serial.printf("[MQTT-AT] 尝试重连 (第 %d 次)...\n", reconnectAttempts + 1);

            if (attemptReconnect()) {
                connected = true;
                reconnectAttempts = 0;
                reconnectDelay = RECONNECT_DELAY_MIN;
                Serial.println(F("[MQTT-AT] 重连成功"));
            } else {
                reconnectAttempts++;
                reconnectDelay = getNextReconnectDelay();
                Serial.printf("[MQTT-AT] 重连失败，%u ms 后重试\n", reconnectDelay);
            }
        }
    }
}

void MQTTManager::processPendingMessages()
{
    if (!messageCallback || !msgQueue) return;

    PendingMsg msg;
    // 非阻塞取出所有待处理消息
    while (xQueueReceive(msgQueue, &msg, 0) == pdTRUE) {
        messageCallback(msg.topic, msg.payload, msg.length);
    }
}

void MQTTManager::processOutgoing()
{
    if (!outMsgQueue || !mqttAtDriver) return;

    OutgoingMsg msg;
    // 非阻塞取出所有待发送消息
    while (xQueueReceive(outMsgQueue, &msg, 0) == pdTRUE) {
        if (connected) {
            // 通过 MQTT AT 驱动发布（msgId=0，QoS=1）
            if (!mqttAtDriver->publish(
                    0, 0,
                    msg.topic,
                    (const uint8_t *)msg.payload,
                    msg.length,
                    1,
                    msg.retained
                )) {
                Serial.printf("[MQTT-AT] 发送失败: %s\n", msg.topic);
                // publish 失败很可能是连接已断开，主动标记断开
                // 比等待 30 秒的 QMTSTAT 检查响应更快
                connected = false;
            }
        }
        // 未连接时直接丢弃（重连后通过服务器重新同步）
    }
}

bool MQTTManager::ping()
{
    // MQTT AT 驱动由模块内部自动处理 PINGREQ
    // 此方法保留仅为接口兼容，返回模块连接状态
    if (!mqttAtDriver || !connected) return false;
    return mqttAtDriver->isConnected(0);
}

// ==================== URC 回调处理 ====================

void MQTTManager::urcCallbackStatic(const String &line)
{
    if (gMqttManagerInstance) {
        gMqttManagerInstance->onRecvURC(line);
    }
}

void MQTTManager::onRecvURC(const String &line)
{
    // 委托给 MQTTAtDriver 解析 +MSUB: URC，结果通过 recvQueue（=msgQueue）入队
    if (mqttAtDriver) {
        mqttAtDriver->handleRecvURC(line);
    }
}

// ==================== 私有方法 ====================

bool MQTTManager::attemptReconnect()
{
    if (!driver || !mqttAtDriver) return false;

    // 检查模块是否就绪，否则重新初始化
    if (!driver->isModuleReady()) {
        Serial.println(F("[MQTT-AT] 4G 模块未就绪，重新初始化..."));
        if (!driver->init()) {
            Serial.println(F("[MQTT-AT] 4G 模块重新初始化失败"));
            return false;
        }
        gprsFailCount = 0;
    }

    // 连续 GPRS 失败 3 次，复位 4G 模块
    if (gprsFailCount >= 3) {
        Serial.printf("[MQTT-AT] GPRS 连续失败 %d 次，硬件复位 4G 模块...\n", gprsFailCount);
        if (driver->resetModule()) {
            gprsFailCount = 0;
        } else {
            Serial.println(F("[MQTT-AT] 4G 模块复位失败，等待下次重试"));
            return false;
        }
    }

    // 配置 GPRS（如果之前失败了，这里会重试）
    if (!driver->configureGPRS(AIR780EP_APN)) {
        gprsFailCount++;
        Serial.printf("[MQTT-AT] GPRS 配置失败 (gprsFail=%d)\n", gprsFailCount);
        return false;
    }

    // GPRS 就绪后等待 500ms，让模块网络栈稳定
    delay(500);

    // 断开模块中残留的旧 MQTT 连接（如果有），并清空 UART 缓冲区
    mqttAtDriver->disconnect(0);
    driver->flushUART();
    delay(100);

    // 重新建立 MQTT 连接（AT+QMTOPEN → AT+QMTCONN）
    bool ok = mqttAtDriver->openAndConnect(
        0,
        brokerAddr.c_str(),
        brokerPort,
        clientId.c_str(),
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_AT_KEEPALIVE,
        false
    );

    if (ok) {
        gprsFailCount = 0;
        lastHeartbeatTime = millis();

        // 重新订阅命令主题
        needsResubscribe = true;
        if (mqttAtDriver->subscribe(0, 1, topicCmd.c_str(), 1)) {
            needsResubscribe = false;
            Serial.printf("[MQTT-AT] 已订阅 %s\n", topicCmd.c_str());
        } else {
            Serial.printf("[MQTT-AT] subscribe 暂失败，loop 中重试...\n");
        }

        // 注册设备公钥
        registerDevice();
        return true;
    }

    gprsFailCount++;
    return false;
}

unsigned long MQTTManager::getNextReconnectDelay()
{
    // 指数退避：5s → 30s → 1m → 5m
    if (reconnectAttempts < 3) {
        return RECONNECT_DELAY_MIN;       // 前 3 次：5 秒
    } else if (reconnectAttempts < 6) {
        return RECONNECT_DELAY_MID;       // 第 4-6 次：30 秒
    } else if (reconnectAttempts < 10) {
        return RECONNECT_DELAY_HIGH;      // 第 7-10 次：1 分钟
    } else {
        return RECONNECT_DELAY_MAX;       // 之后：5 分钟
    }
}

void MQTTManager::registerDevice()
{
    // 检查 CryptoEngine 是否就绪且有公钥
    if (!cryptoEngine.hasECDSAKey()) {
        Serial.println(F("[MQTT-AT] 设备注册跳过：ECDSA 密钥未就绪"));
        return;
    }

    String pubKeyB64 = cryptoEngine.getPublicKeyBase64();
    if (pubKeyB64.length() == 0) {
        Serial.println(F("[MQTT-AT] 设备注册跳过：公钥导出失败"));
        return;
    }

    // 构建注册消息 JSON
    String json;
    json += "{\"type\":\"device_register\",\"publicKey\":\"";
    json += pubKeyB64;
    json += "\"}";

    // 通过出站队列发布（线程安全）
    bool queued = publish(topicResp.c_str(), json.c_str());

    Serial.printf("[MQTT-AT] 设备公钥已%s: 长度=%u字节, 内容前40=%s\n",
                  queued ? "入队" : "入队失败",
                  (unsigned int)json.length(),
                  pubKeyB64.substring(0, 40).c_str());
}
