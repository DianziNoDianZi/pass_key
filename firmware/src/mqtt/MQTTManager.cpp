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
#include "config.h"

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
        Serial.println("[MQTT] TCP/SSL 连接失败");
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
        Serial.println(" 成功");

        // 订阅命令主题
        if (subscribe(topicCmd.c_str())) {
            Serial.printf("[MQTT] 已订阅: %s\n", topicCmd.c_str());
        }
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
    return true;
}

bool MQTTManager::isConnected() const
{
    return connected;
}

bool MQTTManager::publish(const char *topic, const char *payload, bool retained)
{
    if (!connected || !mqttClient) return false;

    // 使用 QoS 1 发布
    return mqttClient->publish(topic, (const uint8_t*)payload, strlen(payload), retained);
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
    }

    // 自动重连逻辑
    if (!connected && driver && driver->isModuleReady()) {
        unsigned long now = millis();

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

bool MQTTManager::ping()
{
    if (!mqttClient || !connected) return false;
    return mqttClient->loop();  // PubSubClient loop 中会自动发送 PINGREQ
}

// ==================== 私有方法 ====================

void MQTTManager::mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
    if (!gMqttManagerInstance) return;

    // 转发到用户注册的回调
    if (gMqttManagerInstance->messageCallback) {
        gMqttManagerInstance->messageCallback(topic, payload, length);
    }
}

bool MQTTManager::attemptReconnect()
{
    if (!driver || !mqttClient || !tcpClient) return false;

    // 重新建立 TCP/SSL 连接（内部包含 GPRS 配置）
    int ret = tcpClient->connect(brokerAddr.c_str(), brokerPort);
    if (ret != 1) {
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
        // 重新订阅命令主题
        mqttClient->subscribe(topicCmd.c_str(), 1);
        return true;
    }

    tcpClient->stop();
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
