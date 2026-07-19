/**
 * @file MQTTManager.h
 * @brief MQTT 消息管理模块 — Native MQTT AT 命令方案
 *
 * 通过 Air780ep 模块的原生 MQTT AT 命令（+QMTOPEN/+QMTCONN/+QMTSUB/+QMTPUB）
 * 实现 MQTT 协议，完全绕过 CIPSEND + PubSubClient 链路。
 *
 * 主题规则：
 * - 订阅：passkey/{deviceId}/cmd
 * - 发布：passkey/{deviceId}/resp
 *
 * 多核设计：
 * - Core 0: 运行 loop() 处理心跳、重连、消息接收
 * - Core 1: 调用 processPendingMessages() 处理收到的消息
 *
 * 相比旧方案（CIPSEND + PubSubClient）的优势：
 * - 模块内部运行完整 MQTT 协议栈，自动保活心跳
 * - 消除 +IPD 数据污染问题
 * - 无需 ESP32 拼装 MQTT 协议包
 * - TCP 连接由模块管理，更稳定
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <functional>
#include "config.h"

class Air780epDriver;
class MQTTAtDriver;

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
     * @param useSSL   是否启用 SSL（注：当前 MQTT AT 驱动暂不支持 SSL）
     * @return true 成功
     */
    bool init(const char *clientId = MQTT_DEVICE_ID,
              const char *broker = MQTT_BROKER_ADDR,
              uint16_t port = MQTT_BROKER_PORT,
              bool useSSL = false);

    /**
     * @brief 连接到 MQTT Broker
     * 内部步骤：配置 GPRS → AT+QMTOPEN → AT+QMTCONN → AT+QMTSUB
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
     * @brief 获取信号强度 RSSI (0-31, -1=未知)
     */
    int getSignalStrength() const { return cachedRssi; }

    /**
     * @brief 刷新信号强度缓存（启动时调用一次，阻塞约 200ms）
     * 不能从 MQTT 任务循环中调用，否则会干扰 MQTT 数据流。
     */
    void refreshSignalStrength();

    /**
     * @brief 发布消息（线程安全，通过出站队列异步发送）
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
     * 处理心跳、消息接收（+QMTRECV 轮询）、自动重连。
     */
    void loop();

    /**
     * @brief 发送 PINGREQ 保活
     * 注：MQTT AT 驱动由模块自动处理 PINGREQ，此方法仅为接口兼容保留
     */
    bool ping();

    /**
     * @brief 请求完全重连
     * 线程安全，可在任何核心调用，loop() 中择机执行
     */
    void requestReset();

    /**
     * @brief 检查是否有待处理的 reset 请求
     */
    bool isResetRequested() const { return resetRequested; }

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
    MQTTAtDriver   *mqttAtDriver;
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
    int gprsFailCount;              // 连续 GPRS 配置失败次数
    int cachedRssi;                 // 缓存信号强度 RSSI (0-31, -1=未知)
    unsigned long lastRssiPoll;     // 上次 RSSI 轮询时间
    unsigned long lastHeartbeatTime; // 应用层心跳上次发送时间
    volatile bool resetRequested;   // 服务器请求完全重连标记
    bool needsResubscribe;          // MQTT CONNECT 后订阅尚未确认

    // 主题
    String topicCmd;
    String topicResp;

    // 跨核消息队列
    QueueHandle_t msgQueue;

    // 出站消息队列（Core 1 → Core 0，发送响应）
    QueueHandle_t outMsgQueue;

    /**
     * @brief 处理出站消息队列（在 Core 0 上调用，通过 mqttAtDriver 实际发送）
     */
    void processOutgoing();

    /**
     * @brief +QMTRECV URC 回调（由 Air780epDriver 在检测到 +QMTRECV: 时调用）
     */
    void onRecvURC(const String &line);

    /**
     * @brief 静态转发函数，兼容 C 函数指针回调类型
     */
    static void urcCallbackStatic(const String &line);

    bool attemptReconnect();
    unsigned long getNextReconnectDelay();
};

#endif // MQTT_MANAGER_H
