/**
 * @file MQTTAtDriver.h
 * @brief Native MQTT AT 命令驱动 — 通过 Air780ep 合宙 MQTT AT 命令集实现
 *
 * 使用 AT+MCONFIG / AT+MIPSTART / AT+MCONNECT / AT+MSUB / AT+MPUBEX 等
 * 合宙原生 MQTT AT 命令，完全绕过 CIPSEND + PubSubClient 链路。
 *
 * Air780ep 模块内部运行完整 MQTT 协议栈（含自动保活 PINGREQ），
 * ESP32 只需发送 AT 命令和接收 +MSUB: URC 即可。
 *
 * 对比旧方案（CIPSEND + PubSubClient）：
 *   - 旧: ESP32 拼 MQTT 包 → CIPSEND → 模块发 TCP → 等 SEND OK → +IPD 数据污染
 *   - 新: AT+MPUBEX="topic",... → 模块内部发 MQTT PUBLISH → +MSUB: URC 收消息
 *
 * 重要：合宙 MQTT AT 命令集与移远系 QMTOPEN/QMTCONN/QMTSUB/QMTPUB 不兼容。
 * 参考文档：https://yinerda.yuque.com/yt1fh6/4gdtu/rq1k2ege1avsqga8
 */

#ifndef MQTT_AT_DRIVER_H
#define MQTT_AT_DRIVER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Air780epDriver;

class MQTTAtDriver
{
public:
    // +QMTRECV 入站消息
    struct RecvMessage {
        char topic[128];
        uint8_t payload[512];
        uint16_t payloadLen;
    };

    MQTTAtDriver(Air780epDriver *driver);
    ~MQTTAtDriver();

    /**
     * @brief 打开 MQTT TCP 连接并执行 MQTT CONNECT
     * @param connIdx   连接索引 (0 或 1)
     * @param host      Broker 地址
     * @param port      Broker 端口
     * @param clientId  客户端 ID
     * @param username  MQTT 用户名
     * @param password  MQTT 密码
     * @param keepalive 保活秒数 (0=模块默认 60s)
     * @param cleanSession false=持久会话
     * @return true 成功
     */
    bool openAndConnect(int connIdx, const char *host, uint16_t port,
                        const char *clientId, const char *username,
                        const char *password, uint16_t keepalive = 60,
                        bool cleanSession = false);

    /**
     * @brief 断开 MQTT 连接并关闭 TCP
     */
    bool disconnect(int connIdx);

    /**
     * @brief 检查 MQTT 连接状态
     */
    bool isConnected(int connIdx);

    /**
     * @brief 订阅主题
     * @param connIdx  连接索引
     * @param msgId    消息 ID (用于匹配响应)
     * @param topic    主题
     * @param qos      QoS 等级
     * @return true 订阅成功
     */
    bool subscribe(int connIdx, uint16_t msgId, const char *topic, uint8_t qos = 1);

    /**
     * @brief 取消订阅
     */
    bool unsubscribe(int connIdx, uint16_t msgId, const char *topic);

    /**
     * @brief 发布消息
     * @param connIdx  连接索引
     * @param msgId    消息 ID
     * @param topic    主题
     * @param payload  负载数据
     * @param len      负载长度
     * @param qos      QoS
     * @param retained 保留消息
     * @return true 发送成功
     */
    bool publish(int connIdx, uint16_t msgId, const char *topic,
                  const uint8_t *payload, uint16_t len,
                  uint8_t qos = 1, bool retained = false);

    /**
     * @brief 读取入站消息（Core 0 的 loop() 中调用）
     * @param msg 输出参数 — 收到的消息
     * @return true 有消息可读
     */
    bool readIncoming(RecvMessage &msg);

    /**
     * @brief +QMTRECV URC 处理（由 Air780epDriver::available() 调用）
     * @param line 去除后缀 \r\n 的 URC 行
     */
    void handleRecvURC(const String &line);

    /**
     * @brief 设置接收消息队列（与 MQTTManager 共享）
     */
    void setRecvQueue(QueueHandle_t queue) { recvQueue = queue; }

private:
    Air780epDriver *driver;
    QueueHandle_t recvQueue;   // 入站消息队列（MQTTManager 的 msgQueue）

    int currentConnIdx;
    bool mqttConnected;

    // 等待 MIPSTART 的 CONNECT OK 响应
    bool waitConnectOk(uint32_t timeoutMs);
    // 等待 MCONNECT 的 CONNACK OK 响应
    bool waitConnackOk(uint32_t timeoutMs);
};

#endif // MQTT_AT_DRIVER_H
