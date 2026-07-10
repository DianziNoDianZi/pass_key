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
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <functional>
#include <PubSubClient.h>

class Air780epDriver;
class Air780epClient;

// MQTT 消息回调类型
typedef std::function<void(const char *topic, const uint8_t *payload, unsigned int length)> MQTTManagerCallback;

class MQTTManager
{
public:
    MQTTManager();
    ~MQTTManager();

    /**
     * @brief 初始化 MQTT 管理器
     * @param clientId 客户端 ID（默认从 config.h 读取）
     * @param broker   Broker 地址（默认从 config.h 读取）
     * @param port     Broker 端口（默认从 config.h 读取）
     * @param useSSL   是否启用 SSL 连接（默认 false）
     * @return true 成功
     */
    bool init(const char *clientId = MQTT_DEVICE_ID,
              const char *broker = MQTT_BROKER_ADDR,
              uint16_t port = MQTT_BROKER_PORT,
              bool useSSL = false);

    /**
     * @brief 连接到 MQTT Broker
     * - 先通过 Air780epDriver 建立 TCP/SSL 连接
     * - 然后发送 MQTT CONNECT 报文
     * @return true 连接成功
     */
    bool connect();

    /**
     * @brief 断开 MQTT 连接
     * - MQTT DISCONNECT + TCP 断开
     */
    bool disconnect();

    /**
     * @brief 检查 MQTT 连接状态
     * @return true 已连接
     */
    bool isConnected() const;

    /**
     * @brief 发布消息到指定主题
     * @param topic    主题
     * @param payload  消息内容
     * @param retained 是否保留消息（默认 false）
     * @return true 发布成功
     */
    bool publish(const char *topic, const char *payload, bool retained = false);

    /**
     * @brief 订阅指定主题
     * @param topic 主题
     * @return true 订阅成功
     */
    bool subscribe(const char *topic);

    /**
     * @brief 取消订阅
     * @param topic 主题
     * @return true 取消成功
     */
    bool unsubscribe(const char *topic);

    /**
     * @brief 注册消息到达回调
     * @param callback 回调函数
     */
    void setMessageCallback(MQTTManagerCallback callback);

    /**
     * @brief MQTT 客户端循环处理
     * - 维持 MQTT 心跳
     * - 检查接收队列并分发消息
     * - 自动重连（指数退避：5s → 30s → 1m → 5m）
     * - 需要在主 loop() 中定期调用
     */
    void loop();

    /**
     * @brief 发送 PINGREQ 保活
     * @return true 收到 PINGRESP
     */
    bool ping();

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

    // 消息回调
    MQTTManagerCallback messageCallback;

    // 自动重连
    unsigned long lastReconnectAttempt;
    unsigned long reconnectDelay;     // 当前重连等待间隔
    int reconnectAttempts;            // 当前重连尝试次数

    // 主题（使用设备 ID 构建）
    String topicCmd;    // passkey/{deviceId}/cmd  (订阅)
    String topicResp;   // passkey/{deviceId}/resp (发布)

    /**
     * @brief PubSubClient 内部回调转发
     * @param topic   主题
     * @param payload 消息内容
     * @param length  消息长度
     */
    static void mqttCallback(char *topic, uint8_t *payload, unsigned int length);

    /**
     * @brief 执行实际的重连逻辑
     * @return true 重连成功
     */
    bool attemptReconnect();

    /**
     * @brief 计算下一次重连等待时间（指数退避）
     * @return 等待时间（毫秒）
     */
    unsigned long getNextReconnectDelay();
};

#endif // MQTT_MANAGER_H
