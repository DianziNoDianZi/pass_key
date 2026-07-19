/**
 * @file MQTTAtDriver.cpp
 * @brief Native MQTT AT 命令驱动实现 — 合宙 Air780ep MIPSTART/MCONNECT/MSUB/MPUB 指令集
 *
 * 使用 Air780ep 模块的合宙 MQTT AT 命令集：
 *   AT+MCONFIG     — 设置 MQTT 参数（clientId, username, password）
 *   AT+MIPSTART    — 打开 TCP 连接（响应：CONNECT OK）
 *   AT+MCONNECT    — MQTT CONNECT（响应：CONNACK OK）
 *   AT+MSUB        — 订阅（响应：SUBACK）
 *   AT+MPUBEX      — 二进制模式发布（> 提示符 → payload → Ctrl-Z → PUBACK）
 *   AT+MDISCONNECT — 断开 MQTT
 *   AT+MIPCLOSE    — 关闭 TCP
 *   +MSUB:         — 入站消息 URC
 *
 * 注意：不与移远系 QMTOPEN/QMTCONN/QMTSUB/QMTPUB 兼容。
 */

#include "MQTTAtDriver.h"
#include "Air780epDriver.h"
#include <string.h>

MQTTAtDriver::MQTTAtDriver(Air780epDriver *drv)
    : driver(drv)
    , recvQueue(nullptr)
    , currentConnIdx(-1)
    , mqttConnected(false)
{
}

MQTTAtDriver::~MQTTAtDriver()
{
    if (mqttConnected) {
        disconnect(currentConnIdx);
    }
}

// ==================== 连接管理 ====================

bool MQTTAtDriver::openAndConnect(int connIdx, const char *host, uint16_t port,
                                  const char *clientId, const char *username,
                                  const char *password, uint16_t keepalive,
                                  bool cleanSession)
{
    if (!driver || !host || !clientId) return false;

    currentConnIdx = connIdx;
    mqttConnected = false;

    if (!driver->isModuleReady()) {
        Serial.println(F("[MQTT-AT] 模块未就绪"));
        return false;
    }

    // ---- 1. AT+MCONFIG — 设置 MQTT 参数 ----
    char cfgCmd[384];
    const char *user = (username && strlen(username) > 0 &&
                        strcmp(username, "user") != 0) ? username : "";
    const char *pass = (password && strlen(password) > 0 &&
                        strcmp(password, "password") != 0) ? password : "";

    snprintf(cfgCmd, sizeof(cfgCmd),
             "AT+MCONFIG=\"%s\",\"%s\",\"%s\"",
             clientId, user, pass);
    Serial.printf("[MQTT-AT] 配置 MQTT 参数...\n");
    if (!driver->sendCommand(cfgCmd, "OK", 5000)) {
        Serial.println(F("[MQTT-AT] MCONFIG 失败"));
        return false;
    }
    Serial.println(F("[MQTT-AT] MQTT 参数配置成功"));

    // ---- 2. AT+MIPSTART — 打开 TCP 连接 ----
    // 注意：sendRaw 不等待响应，由 waitMipStart 读取 CONNECT OK
    char ipStartCmd[272];
    snprintf(ipStartCmd, sizeof(ipStartCmd),
             "AT+MIPSTART=\"%s\",\"%u\"", host, port);
    Serial.printf("[MQTT-AT] 打开 TCP %s:%u...\n", host, port);

    driver->sendRaw(ipStartCmd);

    // 等待 CONNECT OK（TCP 连接成功）
    if (!waitConnectOk(30000)) {
        Serial.println(F("[MQTT-AT] MIPSTART 超时/失败 (未收到 CONNECT OK)"));
        return false;
    }
    Serial.println(F("[MQTT-AT] TCP 连接成功 (CONNECT OK)"));

    // ---- 3. AT+MCONNECT — MQTT CONNECT ----
    // cleanSession=1(clean)/0(resume), keepalive 秒
    char mConnCmd[64];
    snprintf(mConnCmd, sizeof(mConnCmd),
             "AT+MCONNECT=%d,%u", cleanSession ? 1 : 0, keepalive);
    Serial.printf("[MQTT-AT] MQTT CONNECT (keepalive=%ds)...\n", keepalive);

    driver->sendRaw(mConnCmd);

    if (!waitConnackOk(15000)) {
        Serial.println(F("[MQTT-AT] MCONNECT 失败 (未收到 CONNACK OK)"));
        return false;
    }

    mqttConnected = true;
    Serial.println(F("[MQTT-AT] MQTT 连接成功 (CONNACK OK)"));
    return true;
}

bool MQTTAtDriver::disconnect(int connIdx)
{
    if (!driver) return false;

    mqttConnected = false;

    // 先发 MDISCONNECT（断开 MQTT 会话）
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+MDISCONNECT=%d", connIdx);
    driver->sendCommand(cmd, "OK", 3000);

    // 再发 MIPCLOSE（关闭 TCP 连接），即使 MDISCONNECT 失败也执行
    snprintf(cmd, sizeof(cmd), "AT+MIPCLOSE=%d", connIdx);
    driver->sendCommand(cmd, "OK", 3000);

    Serial.printf("[MQTT-AT] 已断开连接 (connIdx=%d)\n", connIdx);
    return true;
}

bool MQTTAtDriver::isConnected(int connIdx)
{
    // 合宙 MQTT AT 没有标准的状态查询命令。
    // mqttConnected 在成功收到 CONNACK OK 后被设为 true，
    // 在以下情况被设为 false：
    //   1) disconnect() 调用
    //   2) publish/subscribe 失败时由外部标记
    //   3) 模块上报 CLOSED URC（在 available() 中处理）
    // 这种方法避免了发送不可靠的 AT 状态查询命令。
    (void)connIdx;
    return mqttConnected;
}

// ==================== 发布/订阅 ====================

bool MQTTAtDriver::subscribe(int connIdx, uint16_t msgId,
                              const char *topic, uint8_t qos)
{
    if (!driver || !topic || !mqttConnected) return false;

    // AT+MSUB="topic",qos
    // 响应: OK\nSUBACK
    // 注意：直接使用 sendCommand 等待 SUBACK，sendCommand 会先读 OK 再读 SUBACK
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+MSUB=\"%s\",%d", topic, qos);

    Serial.printf("[MQTT-AT] 订阅: %s (QoS=%d)...\n", topic, qos);

    String resp;
    if (!driver->sendCommand(cmd, "SUBACK", 10000, &resp)) {
        Serial.printf("[MQTT-AT] SUBSCRIBE 失败: %s\n", topic);
        return false;
    }

    Serial.printf("[MQTT-AT] 订阅成功: %s\n", topic);
    return true;
}

bool MQTTAtDriver::unsubscribe(int connIdx, uint16_t msgId, const char *topic)
{
    if (!driver || !topic || !mqttConnected) return false;

    // AT+MUNS="topic"
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "AT+MUNS=\"%s\"", topic);

    return driver->sendCommand(cmd, "OK", 10000);
}

bool MQTTAtDriver::publish(int connIdx, uint16_t msgId, const char *topic,
                            const uint8_t *payload, uint16_t len,
                            uint8_t qos, bool retained)
{
    if (!driver || !topic || !payload || !mqttConnected) return false;

    // 使用 AT+MPUBEX 二进制模式：避免 JSON 中的引号需要转义
    // AT+MPUBEX="topic",qos,retain,len
    // 模块返回 > 提示符，然后发送 payload + Ctrl-Z
    char cmd[320];
    snprintf(cmd, sizeof(cmd),
             "AT+MPUBEX=\"%s\",%d,%d,%u",
             topic, qos, retained ? 1 : 0, len);

    driver->sendRaw(cmd);

    // ---- 等待 > 提示符 ----
    unsigned long start = millis();
    bool gotPrompt = false;
    while (millis() - start < 10000) {
        if (driver->uartAvailable()) {
            char c = (char)driver->uartRead();
            if (c == '>') {
                gotPrompt = true;
                break;
            }
            // 检查错误响应
            if (c == 'E' || c == 'C' || c == '+') {
                String rest = String(c) + driver->uartReadLine();
                rest.trim();
                if (rest.indexOf("ERROR") >= 0 || rest.indexOf("+CME ERROR") >= 0) {
                    Serial.printf("[MQTT-AT] MPUBEX 错误: %s\n", rest.c_str());
                    return false;
                }
            }
        }
        delay(5);
    }

    if (!gotPrompt) {
        Serial.println(F("[MQTT-AT] MPUBEX > 提示超时"));
        return false;
    }

    // ---- 发送负载 + Ctrl-Z ----
    driver->uartWrite(payload, len);
    driver->uartWriteByte(0x1A);

    // ---- 等待发布确认 ----
    // QoS 0: OK
    // QoS 1: PUBACK
    // QoS 2: PUBCOMP
    const char *expected = (qos == 0) ? "OK" :
                           (qos == 1) ? "PUBACK" : "PUBCOMP";

    if (!driver->waitForResponse(expected, 10000)) {
        Serial.printf("[MQTT-AT] PUBLISH 超时 (期望: %s)\n", expected);
        return false;
    }

    return true;
}

// ==================== 入站消息 ====================

bool MQTTAtDriver::readIncoming(RecvMessage &msg)
{
    if (!recvQueue) return false;
    return xQueueReceive(recvQueue, &msg, 0) == pdTRUE;
}

void MQTTAtDriver::handleRecvURC(const String &line)
{
    // +MSUB: "<topic>",<len> byte,<payload>
    // 非缓存模式：整个消息在同一行
    // 示例：+MSUB: "passkey/test/cmd",45 byte,{"type":"totp_sync"...}

    if (!recvQueue) return;
    if (!line.startsWith("+MSUB:")) return;

    // 解析 topic（双引号内）
    int topicStart = line.indexOf('"');
    if (topicStart < 0) return;
    int topicEnd = line.indexOf('"', topicStart + 1);
    if (topicEnd < 0) return;

    // 找 payload_len（在 topic 后的逗号之后，" byte," 之前）
    int commaAfterTopic = line.indexOf(',', topicEnd);
    if (commaAfterTopic < 0) return;

    int bytePos = line.indexOf(" byte,", commaAfterTopic);
    if (bytePos < 0) return;

    // payload_len
    String lenStr = line.substring(commaAfterTopic + 1, bytePos);
    int payloadLen = lenStr.toInt();

    // payload 起始位置（跳过 " byte,"）
    int payloadStart = bytePos + 6;  // skip " byte,"

    // 构造 RecvMessage
    RecvMessage msg;
    memset(&msg, 0, sizeof(msg));

    // 提取 topic
    String topicStr = line.substring(topicStart + 1, topicEnd);
    strncpy(msg.topic, topicStr.c_str(), sizeof(msg.topic) - 1);

    // 提取 payload（同一行中的部分）
    int lineLen = line.length();
    int payloadOnLine = lineLen - payloadStart;
    int toCopy = (payloadLen < payloadOnLine) ? payloadLen : payloadOnLine;

    int copyBytes = (toCopy < (int)sizeof(msg.payload)) ? toCopy : (int)sizeof(msg.payload) - 1;
    for (int i = 0; i < copyBytes; i++) {
        msg.payload[i] = (uint8_t)line.charAt(payloadStart + i);
    }
    msg.payloadLen = copyBytes;

    // 如果 payload 被截断（超过一行，例如含 \n 的二进制数据），剩余部分从 UART 读取
    int remaining = payloadLen - copyBytes;
    if (remaining > 0) {
        for (int i = 0; i < remaining && msg.payloadLen < sizeof(msg.payload) - 1; i++) {
            if (driver && driver->uartAvailable()) {
                msg.payload[msg.payloadLen++] = (uint8_t)driver->uartRead();
            } else {
                delay(5);
            }
        }
    }

    // 入队
    if (xQueueSend(recvQueue, &msg, 0) != pdTRUE) {
        Serial.println(F("[MQTT-AT] 接收队列满，丢弃消息"));
    }
}

// ==================== 私有方法 ====================

bool MQTTAtDriver::waitConnectOk(uint32_t timeoutMs)
{
    // 等待 "CONNECT OK" — MIPSTART 成功的标志
    // 模块先返回 OK（命令已接收），然后异步连接，成功后返回 CONNECT OK
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (driver->uartAvailable()) {
            String line = driver->uartReadLine();
            line.trim();
            if (line.indexOf("CONNECT OK") >= 0) {
                return true;
            }
            // 检查错误（CONNECT FAIL 等）
            if (line.indexOf("CONNECT FAIL") >= 0 ||
                line.indexOf("ERROR") >= 0 ||
                line.indexOf("+CME ERROR") >= 0) {
                Serial.printf("[MQTT-AT] MIPSTART 错误: %s\n", line.c_str());
                return false;
            }
        }
        delay(50);
    }
    Serial.println(F("[MQTT-AT] MIPSTART 超时"));
    return false;
}

bool MQTTAtDriver::waitConnackOk(uint32_t timeoutMs)
{
    // 等待 "CONNACK OK" — MCONNECT 成功的标志
    // 模块先返回 OK（命令已接收），然后异步完成 CONNECT，成功后返回 CONNACK OK
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (driver->uartAvailable()) {
            String line = driver->uartReadLine();
            line.trim();
            if (line.indexOf("CONNACK OK") >= 0) {
                return true;
            }
            // 检查错误（CONNACK FAIL 等）
            if (line.indexOf("CONNACK FAIL") >= 0 ||
                line.indexOf("ERROR") >= 0 ||
                line.indexOf("+CME ERROR") >= 0) {
                Serial.printf("[MQTT-AT] MCONNECT 错误: %s\n", line.c_str());
                return false;
            }
        }
        delay(50);
    }
    Serial.println(F("[MQTT-AT] MCONNECT 超时"));
    return false;
}
