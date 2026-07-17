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
    mqttClient->setCallback(mqttCallback);

    // 注册全局实例用于静态回调转发
    gMqttManagerInstance = this;

    // 创建跨核消息队列（Core 0 → Core 1）
    msgQueue = xQueueCreate(8, sizeof(PendingMsg));

    // 创建出站消息队列（Core 1 → Core 0，mqttManager.publish() 的异步通道）
    outMsgQueue = xQueueCreate(4, sizeof(OutgoingMsg));

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

    // 带用户名密码认证
    if (strlen(MQTT_USERNAME) > 0 && strcmp(MQTT_USERNAME, "user") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
    } else if (strlen(MQTT_PASSWORD) > 0 && strcmp(MQTT_PASSWORD, "password") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        // 如果用户名/密码还是默认值，使用无认证连接
        mqttOk = mqttClient->connect(clientId.c_str());
    }

    if (mqttOk) {
        connected = true;
        reconnectAttempts = 0;
        reconnectDelay = RECONNECT_DELAY_MIN;
        gprsFailCount = 0;
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

void MQTTManager::loop()
{
    if (!initialized || !mqttClient) return;

    if (connected) {
        // 维持 MQTT 心跳和处理消息
        if (!mqttClient->loop()) {
            // PubSubClient::loop() 返回 false 表示连接断开
            connected = false;
            Serial.println("[MQTT] 连接丢失");
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

    xQueueSend(gMqttManagerInstance->msgQueue, &msg, 0);
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

    // MQTT CONNECT
    bool mqttOk = false;
    if (strlen(MQTT_USERNAME) > 0 && strcmp(MQTT_USERNAME, "user") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
    } else if (strlen(MQTT_PASSWORD) > 0 && strcmp(MQTT_PASSWORD, "password") != 0) {
        mqttOk = mqttClient->connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        mqttOk = mqttClient->connect(clientId.c_str());
    }

    if (mqttOk) {
        gprsFailCount = 0;
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
    publish(topicResp.c_str(), json.c_str());

    Serial.printf("[MQTT] 设备公钥已发送注册: %s...\n",
                  pubKeyB64.substring(0, 40).c_str());
}
