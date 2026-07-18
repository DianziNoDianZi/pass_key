/**
 * @file MQTTManager.h
 * @brief MQTT 消息管理模块
 *
 * 通过 4G 模块的 TCP/SSL 连接运行 MQTT 协议。
 * 使用 PubSubClient 库在 ESP32 上运行完整 MQTT 客户端，
 * 通过 Air780epClient（继承自 Arduino Client）进行数据传输。
 *
 * 主题规则：
 * - 订阅：passkey/{deviceId}/cmd
 * - 发布：passkey/{deviceId}/resp
 *
 * 多核设计：
 * - Core 0: 运行 loop() 处理心跳、重连、消息接收
 * - Core 1: 调用 processPendingMessages() 处理收到的消息
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <functional>
#include <PubSubClient.h>
#include "config.h"

class Air780epDriver;
class Air780epClient;

// MQTT 消息回调类型
typedef std::function<void(const char *topic, const uint8_t *payload, unsigned int length)> MQTTManagerCallback;

class MQTTManager
{
public:
    // 跨核消息队列条目（Core 0 MQTT 任务 → Core 1 主循环）
    struct PendingMsg {
        char topic[128];
        uint8_t payload[512];
        unsigned int length;
    };

    // 出站消息队列条目（Core 1 回调 → Core 0 MQTT 任务）
    struct OutgoingMsg {
        char topic[128];
        char payload[512];
        unsigned int length;
        bool retained;
    };

    MQTTManager();
    ~MQTTManager();

    /**
     * @brief 初始化 MQTT 管理器
     * @param clientId 客户端 ID
     * @param broker   Broker 地址
     * @param port     Broker 端口
     * @param useSSL   是否启用 SSL
     * @return true 成功
     */
    bool init(const char *clientId = MQTT_DEVICE_ID,
              const char *broker = MQTT_BROKER_ADDR,
              uint16_t port = MQTT_BROKER_PORT,
              bool useSSL = false);

    /**
     * @brief 连接到 MQTT Broker
     * @return true 连接成功
     */
    bool connect();

    /**
     * @brief 断开 MQTT 连接
     */
    bool disconnect();

    /**
     * @brief 检查 MQTT 连接状态
     */
    bool isConnected() const;

    /**
     * @brief 检查是否正在重连
     * @return true 连接断开且已在重连过程中
     */
    bool isReconnecting() const;

    /**
     * @brief 获取重连尝试次数
     */
    int getReconnectAttempts() const { return reconnectAttempts; }

    /**
     * @brief 发布消息
     */
    bool publish(const char *topic, const char *payload, bool retained = false);

    /**
     * @brief 订阅主题
     */
    bool subscribe(const char *topic);

    /**
     * @brief 取消订阅
     */
    bool unsubscribe(const char *topic);

    /**
     * @brief 注册消息到达回调
     */
    void setMessageCallback(MQTTManagerCallback callback);

    /**
     * @brief MQTT 客户端循环处理
     * 在 Core 0 的任务中运行。
     * 处理心跳、消息接收、自动重连。
     */
    void loop();

    /**
     * @brief 发送 PINGREQ 保活
     */
    bool ping();

    /**
     * @brief 处理 Core 0 传来的待处理 MQTT 消息
     * 在 Core 1 的主循环中调用，使用 FreeRTOS 消息队列
     * 确保跨核安全。
     */
    void processPendingMessages();

    /**
     * @brief 注册设备公钥到服务器
     * 连接成功后自动调用，发布设备公钥至 passkey/{deviceId}/resp
     */
    void registerDevice();

private:
    Air780epClient *tcpClient;
    PubSubClient   *mqttClient;
    Air780epDriver *driver;

    // 连接参数
    String clientId;
    String brokerAddr;
    uint16_t brokerPort;
    bool useSSL;

    // 连接状态
    bool connected;
    bool initialized;

    // 消息回调（用于 Core 0 的 MQTT 消息 → 用户回调转发）
    MQTTManagerCallback messageCallback;

    // 自动重连
    unsigned long lastReconnectAttempt;
    unsigned long reconnectDelay;
    int reconnectAttempts;
    int gprsFailCount;          // 连续 GPRS 配置失败次数

    // 主题
    String topicCmd;
    String topicResp;

    // 跨核消息队列
    QueueHandle_t msgQueue;

    // 出站消息队列（Core 1 → Core 0，发送响应）
    QueueHandle_t outMsgQueue;

    /**
     * @brief 处理出站消息队列（在 Core 0 上调用，通过 mqttClient 实际发送）
     */
    void processOutgoing();

    /**
     * @brief PubSubClient 内部回调（运行在 Core 0）
     */
    static void mqttCallback(char *topic, uint8_t *payload, unsigned int length);

    bool attemptReconnect();
    unsigned long getNextReconnectDelay();
};

#endif // MQTT_MANAGER_H
