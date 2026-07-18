/**
 * @file MQTTManager.cpp
 * @brief MQTT 消息管理模块实现
 *
 * 在 ESP32 上运行 PubSubClient，通过 Air780epClient
 * 提供 TCP/SSL 数据通道，实现完整的 MQTT 客户端功能。
 */

#include "MQTTManager.h"
#include "Air780epClient.h"
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

// PubSubClient 全局实例指针，用于静态回调转发
static MQTTManager *gMqttManagerInstance = nullptr;

MQTTManager::MQTTManager()
    : tcpClient(nullptr)
    , mqttClient(nullptr)
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
{
}

MQTTManager::~MQTTManager()
{
    disconnect();
    delete mqttClient;
    delete tcpClient;
    if (gMqttManagerInstance == this) {
        gMqttManagerInstance = nullptr;
    }
}

bool MQTTManager::init(const char *clientId, const char *broker, uint16_t port, bool ssl)
{
    // 保存配置参数
    this->clientId   = String(clientId);
    this->brokerAddr = String(broker);
    this->brokerPort = port;
    this->useSSL     = ssl;

    // 构建主题字符串
    // 订阅：passkey/{deviceId}/cmd
    // 发布：passkey/{deviceId}/resp
    topicCmd  = "passkey/" + this->clientId + "/cmd";
    topicResp = "passkey/" + this->clientId + "/resp";

    // 查找 Air780epDriver 实例
    // Air780epDriver 是全局变量，通过 extern 引用
    extern Air780epDriver air780epDriver;
    driver = &air780epDriver;

    // 创建 TCP 客户端封装
    tcpClient = new Air780epClient(driver);
    tcpClient->setSSL(useSSL);

    // 创建 PubSubClient
    mqttClient = new PubSubClient(*tcpClient);
    mqttClient->setServer(brokerAddr.c_str(), brokerPort);
    mqttClient->setKeepAlive(MQTT_KEEPALIVE);
    mqttClient->setBufferSize(1024);
    mqttClient->setCallback(mqttCallback);

    // 注册全局实例用于静态回调转发
    gMqttManagerInstance = this;

    // 创建跨核消息队列（Core 0 → Core 1）
    msgQueue = xQueueCreate(16, sizeof(PendingMsg));

    // 创建出站消息队列（Core 1 → Core 0，mqttManager.publish() 的异步通道）
    outMsgQueue = xQueueCreate(8, sizeof(OutgoingMsg));

    initialized = true;

    Serial.printf("[MQTT] 初始化完成: broker=%s:%u, clientId=%s, SSL=%s\n",
                  brokerAddr.c_str(), brokerPort, this->clientId.c_str(),
                  useSSL ? "是" : "否");
    Serial.printf("[MQTT] 订阅主题: %s\n", topicCmd.c_str());
    Serial.printf("[MQTT] 发布主题: %s\n", topicResp.c_str());

    return true;
}

bool MQTTManager::connect()
{
    if (!initialized || !driver || !mqttClient || !tcpClient) {
        Serial.println("[MQTT] 未初始化，无法连接");
        return false;
    }

    // 检查模块是否就绪
    if (!driver->isModuleReady()) {
        Serial.println("[MQTT] 4G 模块未就绪，重试初始化");
        if (!driver->init()) {
            return false;
        }
    }

    // 通过 Air780epClient 建立 TCP/SSL 连接
    // 注意：connectInternal 会在 connect() 中自动配置 GPRS
    Serial.printf("[MQTT] 正在连接 %s:%u (SSL=%s)...\n",
                  brokerAddr.c_str(), brokerPort, useSSL ? "是" : "否");

    // Air780epClient::connect() 内部会调用 configureGPRS + connectTCP/connectSSL
    int ret = tcpClient->connect(brokerAddr.c_str(), brokerPort);
    if (ret != 1) {
        gprsFailCount++;
        Serial.printf("[MQTT] TCP 连接失败 (ret=%d, gprsFail=%d)\n", ret, gprsFailCount);
        return false;
    }

    // 发送 MQTT CONNECT
    Serial.print("[MQTT] MQTT CONNECT...");
    bool mqttOk = false;

    // 带用户名密码认证（持久会话，断线后 Broker 不会丢弃队列中的消息）
    if (strlen(MQTT_USERNAME) > 0 && strcmp(MQTT_USERNAME, "user") != 0) {
        // cleanSession=false: broker 保持 session, 离线期间 QoS1 消息不会丢失
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, 0, 0, 0, 0, false);
    } else if (strlen(MQTT_PASSWORD) > 0 && strcmp(MQTT_PASSWORD, "password") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, 0, 0, 0, 0, false);
    } else {
        // 如果用户名/密码还是默认值，使用无认证连接（持久会话）
        mqttOk = mqttClient->connect(clientId.c_str(), 0, 0, 0, 0, 0, 0, false);
    }

    if (mqttOk) {
        connected = true;
        reconnectAttempts = 0;
        reconnectDelay = RECONNECT_DELAY_MIN;
        gprsFailCount = 0;
        lastHeartbeatTime = millis();   // 重置心跳定时器，避免立即发送
        Serial.println(" 成功");

        // 订阅命令主题
        if (subscribe(topicCmd.c_str())) {
            Serial.printf("[MQTT] 已订阅: %s\n", topicCmd.c_str());
        }

        // 注册设备公钥
        registerDevice();
    } else {
        connected = false;
        Serial.printf(" 失败 (rc=%d)\n", mqttClient->state());
        // TCP 连接已建立但 MQTT 连接失败，断开 TCP
        tcpClient->stop();
    }

    return connected;
}

bool MQTTManager::disconnect()
{
    if (mqttClient && connected) {
        mqttClient->disconnect();
        connected = false;
    }
    if (tcpClient) {
        tcpClient->stop();
    }
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
    // 这样避免 Core 1 的 processPendingMessages() 和 Core 0 的 mqttClient->loop()
    // 同时操作 PubSubClient 内部状态导致线程安全问题。
    OutgoingMsg msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    msg.length = strlen(msg.payload);
    msg.retained = retained;

    if (outMsgQueue && xQueueSend(outMsgQueue, &msg, 0) != pdTRUE) {
        // 队列满，丢弃（不影响主流程）
        Serial.println("[MQTT] 出站消息队列满，消息丢弃");
        return false;
    }
    return true;
}

bool MQTTManager::subscribe(const char *topic)
{
    if (!mqttClient) return false;

    // 使用 QoS 1 订阅
    // 即使当前未连接也记录订阅状态，重连后会重新订阅
    if (connected) {
        return mqttClient->subscribe(topic, 1);
    }
    // 未连接时先记录，重连时重新订阅
    return true;
}

bool MQTTManager::unsubscribe(const char *topic)
{
    if (!mqttClient || !connected) return false;
    return mqttClient->unsubscribe(topic);
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
    Serial.println("[MQTT] 收到 reset 请求，将在下次 loop 中执行完全重连");
}

void MQTTManager::loop()
{
    if (!initialized || !mqttClient) return;

    // 始终读取 UART 数据以处理 URC（如 CLOSED、NW DETACH 等），
    // 否则断开连接等待重连期间 URC 会堆积在 UART 缓冲区中。
    if (driver) {
        driver->available();
    }

    if (connected) {
        // 维持 MQTT 心跳和处理消息
        if (!mqttClient->loop()) {
            // PubSubClient::loop() 返回 false 表示连接断开
            connected = false;
            // 重置 PubSubClient 内部 _state，否则下次 connect() 可能误判为"已连接"
            // 直接跳过 MQTT CONNECT 包的发送。
            mqttClient->disconnect();
            Serial.println("[MQTT] 连接丢失");
        }

        // 应用层心跳：每 2 秒发一条真实的 MQTT PUBLISH 消息
        // 原因：Air780ep 默认 CIPSTO=5s（不支持修改），TCP 空闲 5 秒后模块自动断开连接。
        // 2 秒间隔确保在超时前有足够余量，且第一次心跳在连接后 2 秒
        // （CIPSTART 后 1.5s + 2s = 3.5s，远早于 5s 超时）。
        // 心跳比 PINGREQ（2 字节）更可靠——CIPSEND=61+ 字节不会被网络设备忽略。
        unsigned long nowMs = millis();
        if (connected && nowMs - lastHeartbeatTime >= 2000) {
            lastHeartbeatTime = nowMs;
            String topic = topicResp;  // 使用 resp 主题
            String payload = "{\"type\":\"heartbeat\",\"t\":" + String(nowMs) + "}";
            if (mqttClient && mqttClient->publish(topic.c_str(), (const uint8_t *)payload.c_str(), payload.length(), false)) {
                Serial.println("[MQTT] 心跳已发送");
            } else {
                Serial.println("[MQTT] 心跳发送失败");
            }
        }

        // 发送出站队列中的消息（在 Core 0 上，与 mqttClient 在同一核心）
        processOutgoing();
    }

    unsigned long now = millis();

    // 模块未就绪时，尝试重新初始化（每 5 秒试一次）
    if (driver && !driver->isModuleReady()) {
        if (now - lastReconnectAttempt >= 5000) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] 4G 模块未就绪，尝试重新初始化...");
            if (driver->init()) {
                Serial.println("[MQTT] 4G 模块重新初始化成功");
                gprsFailCount = 0;
                // 初始化成功，立即尝试重连
            } else {
                Serial.println("[MQTT] 4G 模块重新初始化失败，5 秒后重试");
            }
        }
        return;  // 模块未就绪时不执行后续重连逻辑
    }

    // ===== 检查 reset 请求，执行完全重连 =====
    if (resetRequested) {
        resetRequested = false;
        Serial.println("[MQTT] 执行完全重连...");
        // 强制断开所有连接
        if (connected) {
            mqttClient->disconnect();
            connected = false;
        }
        if (driver && driver->isConnected()) {
            driver->disconnect();
        }
        // 重置 GPRS，下次重连会重新附着 PDP
        if (driver) {
            driver->resetGprsConfig();
        }
        OutgoingMsg discard;
        while (outMsgQueue && xQueueReceive(outMsgQueue, &discard, 0) == pdTRUE) {}
        // 让下次 reconnect 立即执行
        lastReconnectAttempt = 0;
        reconnectAttempts = 0;
        reconnectDelay = RECONNECT_DELAY_MIN;
        Serial.println("[MQTT] 完全重连触发完成，等待下次 loop 执行重连");
    }

    // 自动重连逻辑
    if (!connected && driver) {
        // 检查是否需要尝试重连
        if (now - lastReconnectAttempt >= reconnectDelay) {
            lastReconnectAttempt = now;

            Serial.printf("[MQTT] 尝试重连 (第 %d 次)...\n", reconnectAttempts + 1);

            if (attemptReconnect()) {
                connected = true;
                reconnectAttempts = 0;
                reconnectDelay = RECONNECT_DELAY_MIN;
                Serial.println("[MQTT] 重连成功");
            } else {
                reconnectAttempts++;
                reconnectDelay = getNextReconnectDelay();
                Serial.printf("[MQTT] 重连失败，%u ms 后重试\n", reconnectDelay);
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
    if (!outMsgQueue || !mqttClient) return;

    OutgoingMsg msg;
    // 非阻塞取出所有待发送消息
    while (xQueueReceive(outMsgQueue, &msg, 0) == pdTRUE) {
        if (connected) {
            if (!mqttClient->publish(msg.topic, (const uint8_t *)msg.payload, msg.length, msg.retained)) {
                Serial.printf("[MQTT] 发送失败: %s\n", msg.topic);
            }
        }
        // 未连接时直接丢弃（重连后通过服务器重新同步）
    }
}

bool MQTTManager::ping()
{
    if (!mqttClient || !connected) return false;
    return mqttClient->loop();  // PubSubClient loop 中会自动发送 PINGREQ
}

// ==================== 私有方法 ====================

void MQTTManager::mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    if (!gMqttManagerInstance) return;

    // 将消息放入跨核队列（Core 0 MQTT → Core 1 处理）
    // 非阻塞发送，队列满时丢弃最旧消息
    PendingMsg msg;
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    msg.topic[sizeof(msg.topic) - 1] = '\0';
    unsigned int copyLen = (length < sizeof(msg.payload)) ? length : (sizeof(msg.payload) - 1);
    memcpy(msg.payload, payload, copyLen);
    msg.length = copyLen;

    if (xQueueSend(gMqttManagerInstance->msgQueue, &msg, 0) != pdTRUE) {
        Serial.println("[MQTT] 入站消息队列满，消息丢弃！");
    }
}

bool MQTTManager::attemptReconnect()
{
    if (!driver || !mqttClient || !tcpClient) return false;

    // 检查模块是否就绪，否则重新初始化
    if (!driver->isModuleReady()) {
        Serial.println("[MQTT] 4G 模块未就绪，重新初始化...");
        if (!driver->init()) {
            Serial.println("[MQTT] 4G 模块重新初始化失败");
            return false;
        }
        gprsFailCount = 0;  // 重新初始化成功，清零失败计数
    }

    // 连续 GPRS 失败 3 次，复位 4G 模块
    if (gprsFailCount >= 3) {
        Serial.printf("[MQTT] GPRS 连续失败 %d 次，硬件复位 4G 模块...\n", gprsFailCount);
        if (driver->resetModule()) {
            gprsFailCount = 0;
        } else {
            Serial.println("[MQTT] 4G 模块复位失败，等待下次重试");
            return false;
        }
    }

    // 重新建立 TCP/SSL 连接（内部包含 GPRS 配置）
    int ret = tcpClient->connect(brokerAddr.c_str(), brokerPort);
    if (ret != 1) {
        gprsFailCount++;
        Serial.printf("[MQTT] 连接失败 (ret=%d, gprsFail=%d)\n", ret, gprsFailCount);
        return false;
    }

    // MQTT CONNECT（持久会话，离线消息不丢失）
    bool mqttOk = false;
    if (strlen(MQTT_USERNAME) > 0 && strcmp(MQTT_USERNAME, "user") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, 0, 0, 0, 0, false);
    } else if (strlen(MQTT_PASSWORD) > 0 && strcmp(MQTT_PASSWORD, "password") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, 0, 0, 0, 0, false);
    } else {
        mqttOk = mqttClient->connect(clientId.c_str(), 0, 0, 0, 0, 0, 0, false);
    }

    if (mqttOk) {
        gprsFailCount = 0;
        lastHeartbeatTime = millis();   // 重置心跳定时器

        // 刷新 UART 缓冲区：清除 CONNACK 交换期间可能到达的 CLOSED/URC
        // Air780ep 在收到 TCP 数据后可能立即发送 URC 通知（如 CLOSED），
        // 如果不清除，下次 loop() 中 available() 会读到而误判连接断开。
        driver->flushUART();

        // 重新订阅命令主题
        mqttClient->subscribe(topicCmd.c_str(), 1);
        Serial.printf("[MQTT] 已订阅 %s\n", topicCmd.c_str());

        // 连接成功后自动注册设备公钥
        registerDevice();

        return true;
    }

    // 输出 MQTT 连接失败原因
    Serial.printf("[MQTT] MQTT CONNECT 失败 (rc=%d): ",
                  mqttClient->state());
    switch (mqttClient->state()) {
        case -4: Serial.println("连接超时"); break;
        case -3: Serial.println("网络断开"); break;
        case -2: Serial.println("连接中..."); break;
        case -1: Serial.println("未连接"); break;
        case 1:  Serial.println("协议版本被拒"); break;
        case 2:  Serial.println("标识符被拒"); break;
        case 3:  Serial.println("服务器不可达"); break;
        case 4:  Serial.println("认证失败"); break;
        case 5:  Serial.println("未授权"); break;
        default: Serial.println("未知错误"); break;
    }

    tcpClient->stop();
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
        Serial.println("[MQTT] 设备注册跳过：ECDSA 密钥未就绪");
        return;
    }

    String pubKeyB64 = cryptoEngine.getPublicKeyBase64();
    if (pubKeyB64.length() == 0) {
        Serial.println("[MQTT] 设备注册跳过：公钥导出失败");
        return;
    }

    // 构建注册消息 JSON
    String json;
    json += "{\"type\":\"device_register\",\"publicKey\":\"";
    json += pubKeyB64;
    json += "\"}";

    // 通过出站队列发布（线程安全）
    bool queued = publish(topicResp.c_str(), json.c_str());

    Serial.printf("[MQTT] 设备公钥已%s: 长度=%u字节, 内容前40=%s\n",
                  queued ? "入队" : "入队失败",
                  (unsigned int)json.length(),
                  pubKeyB64.substring(0, 40).c_str());
}
