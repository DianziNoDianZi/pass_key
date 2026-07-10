/**
 * @file Air780epDriver.cpp
 * @brief Air780ep 4G 模块驱动实现
 *
 * 使用 AT 命令通过 UART 控制 Air780ep 模块。
 * 兼容移远 EC200 系列 AT 指令集。
 */

#include "Air780epDriver.h"
#include "config.h"
#include <string.h>

Air780epDriver::Air780epDriver()
    : uart(nullptr)
    , moduleReady(false)
    , currentConnectId(0)
    , tcpConnected(false)
    , sslMode(false)
    , rxHead(0)
    , rxTail(0)
{
}

Air780epDriver::~Air780epDriver()
{
    if (tcpConnected) {
        disconnect();
    }
}

// ==================== 初始化 ====================

bool Air780epDriver::init()
{
    // 1. 初始化 UART2，连接 Air780ep
    uart = &Serial2;
    uart->begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX);
    uart->setTimeout(100);

    // 2. 通过 PWRKEY 启动模块
    pinMode(AT_PWRKEY, OUTPUT);
    digitalWrite(AT_PWRKEY, LOW);
    delay(1000);        // 拉低 1 秒
    digitalWrite(AT_PWRKEY, HIGH);
    delay(1000);        // 等待模块启动

    // 3. 清空 UART 缓冲区
    flushUART();

    // 4. 等待模块就绪：最多重试 10 次，每次间隔 1 秒
    moduleReady = false;
    for (int i = 0; i < 10; i++) {
        if (sendCommand("AT", "OK", 2000)) {
            moduleReady = true;
            break;
        }
        Serial.printf("[Air780ep] 等待模块就绪... (%d/10)\n", i + 1);
        delay(1000);
    }

    if (moduleReady) {
        // 关闭回显，减少干扰
        sendCommand("ATE0", "OK", 2000);
        // 设置 URC 通知模式（异步通知主动上报）
        sendCommand("AT+QIURC=1", "OK", 2000);
    }

    return moduleReady;
}

// ==================== AT 通信基础 ====================

void Air780epDriver::flushUART()
{
    if (!uart) return;
    while (uart->available()) {
        uart->read();
    }
    ringFlush();
}

bool Air780epDriver::sendCommand(const char *cmd, const char *expectedResp, uint32_t timeoutMs, String *collectOut)
{
    if (!uart) return false;

    // 先处理可能存在的 URC 通知
    while (uart->available()) {
        String line = uart->readStringUntil('\n');
        line.trim();
        if (line.startsWith("+QIURC:")) {
            handleURC(line);
        }
    }

    // 发送命令（自动追加 \r\n）
    uart->print(cmd);
    uart->print("\r\n");

    // 等待并解析响应
    return waitForResponse(expectedResp, timeoutMs, collectOut);
}

String Air780epDriver::readResponse(uint32_t timeoutMs)
{
    String resp;
    unsigned long start = millis();

    // 先读取 UART 中已有的数据
    while (millis() - start < timeoutMs) {
        if (uart && uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();

            // 处理 URC 通知
            if (line.startsWith("+QIURC:")) {
                handleURC(line);
                continue;
            }

            if (line.length() > 0) {
                if (resp.length() > 0) resp += "\n";
                resp += line;
            }
        } else {
            if (resp.length() > 0) {
                // 已有数据，给模块一点时间发送更多
                delay(20);
                if (!uart->available() && (millis() - start > 500)) break;
            } else {
                delay(10);
            }
        }
    }

    return resp;
}

bool Air780epDriver::waitForResponse(const char *expected, uint32_t timeoutMs, String *collectOut)
{
    if (!uart || !expected) return false;

    unsigned long start = millis();
    String collected;

    while (millis() - start < timeoutMs) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();

            // 处理 URC 通知
            if (line.startsWith("+QIURC:")) {
                handleURC(line);
                continue;
            }

            if (line.length() > 0) {
                if (collected.length() > 0) collected += "\n";
                collected += line;
            }

            // 检查是否包含期望响应
            if (strcmp(expected, "") != 0 && collected.indexOf(expected) >= 0) {
                if (collectOut) *collectOut = collected;
                return true;
            }
        }

        if (collected.length() > 0) {
            // 有数据但还没匹配到期望响应，给小延时等更多数据
            delay(50);
        } else {
            delay(10);
        }
    }

    if (collectOut) *collectOut = collected;
    return false;
}

// ==================== 环形缓冲区 ====================

bool Air780epDriver::ringGetc(uint8_t &c)
{
    if (rxHead == rxTail) return false;
    c = rxBuf[rxTail];
    rxTail = (rxTail + 1) % AIR780EP_RX_BUF_SIZE;
    return true;
}

void Air780epDriver::ringPutc(uint8_t c)
{
    size_t next = (rxHead + 1) % AIR780EP_RX_BUF_SIZE;
    if (next != rxTail) {
        rxBuf[rxHead] = c;
        rxHead = next;
    }
    // 如果缓冲区满，丢弃最早的数据
    // （头部覆盖尾部，保持最新数据）
}

size_t Air780epDriver::ringAvailable() const
{
    if (rxHead >= rxTail) {
        return rxHead - rxTail;
    }
    return AIR780EP_RX_BUF_SIZE - rxTail + rxHead;
}

void Air780epDriver::ringFlush()
{
    rxHead = 0;
    rxTail = 0;
}

// ==================== URC 处理 ====================

void Air780epDriver::handleURC(const String &line)
{
    // +QIURC: "recv",<connect_id>,<data_length>
    if (line.indexOf("\"recv\"") >= 0) {
        // 解析参数
        int firstComma = line.indexOf(',', line.indexOf("\"recv\"") + 6);
        int secondComma = line.indexOf(',', firstComma + 1);

        if (firstComma > 0 && secondComma > 0) {
            int connId = line.substring(firstComma + 1, secondComma).toInt();
            int dataLen = line.substring(secondComma + 1).toInt();

            (void)connId;  // 暂不区分连接 ID，当前只支持单连接

            // 读取数据到环形缓冲区
            unsigned long start = millis();
            int bytesRead = 0;
            while (bytesRead < dataLen && (millis() - start < 5000)) {
                if (uart && uart->available()) {
                    uint8_t c = uart->read();
                    ringPutc(c);
                    bytesRead++;
                } else if (bytesRead == 0) {
                    delay(10);
                }
            }
        }
    }
    // 其他 URC 类型可在此扩展
}

// ==================== 数据收发 ====================

bool Air780epDriver::sendData(const uint8_t *data, size_t len)
{
    if (!uart || !tcpConnected) return false;

    char cmd[64];
    if (sslMode) {
        snprintf(cmd, sizeof(cmd), "AT+QSSLSEND=%d,%u", currentConnectId, (unsigned int)len);
    } else {
        snprintf(cmd, sizeof(cmd), "AT+QISEND=%d,%u", currentConnectId, (unsigned int)len);
    }

    // 发送数据长度命令
    uart->print(cmd);
    uart->print("\r\n");

    // 等待 ">" 提示符
    unsigned long start = millis();
    bool gotPrompt = false;
    while (millis() - start < 5000) {
        if (uart->available()) {
            char c = uart->read();
            if (c == '>') {
                gotPrompt = true;
                break;
            }
        }
        delay(5);
    }

    if (!gotPrompt) {
        return false;
    }

    // 发送原始数据
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = (len - sent > 1460) ? 1460 : (len - sent);
        uart->write(data + sent, chunk);
        sent += chunk;
    }

    // 等待 SEND OK
    return waitForResponse("SEND OK", 30000);
}

int Air780epDriver::receiveData(uint8_t *buffer, size_t maxLen)
{
    // 先检查 UART 中是否有新的 URC 数据
    if (uart) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();
            if (line.startsWith("+QIURC:")) {
                handleURC(line);
            }
        }
    }

    // 从环形缓冲区读取数据
    size_t available = ringAvailable();
    size_t toRead = (available < maxLen) ? available : maxLen;

    for (size_t i = 0; i < toRead; i++) {
        ringGetc(buffer[i]);
    }

    return (int)toRead;
}

int Air780epDriver::available()
{
    // 检查 UART 中是否有新的 URC
    if (uart) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();
            if (line.startsWith("+QIURC:")) {
                handleURC(line);
            }
        }
    }
    return (int)ringAvailable();
}

// ==================== 网络连接 ====================

bool Air780epDriver::configureGPRS(const char *apn)
{
    if (!moduleReady || !uart) return false;

    char cmd[128];

    // 配置 PDP context
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (!sendCommand(cmd, "OK", 10000)) {
        Serial.println("[Air780ep] APN 配置失败");
        return false;
    }

    // 激活 PDP context
    if (!sendCommand("AT+CGACT=1,1", "OK", 30000)) {
        Serial.println("[Air780ep] PDP 激活失败");
        return false;
    }

    // 检查网络注册状态
    if (!sendCommand("AT+CREG?", "+CREG: 0,1", 5000)
        && !sendCommand("AT+CREG?", "+CREG: 0,5", 5000)) {
        // 注册状态可能是 1（已注册）或 5（已注册漫游）
        // 如果不是，也继续尝试
        Serial.println("[Air780ep] 网络注册状态检查未通过");
    }

    // 检查 GPRS 附着状态
    if (!sendCommand("AT+CGATT?", "+CGATT: 1", 10000)) {
        Serial.println("[Air780ep] GPRS 附着失败");
        return false;
    }

    Serial.printf("[Air780ep] GPRS 配置成功, APN=%s\n", apn);
    return true;
}

bool Air780epDriver::connectTCP(const char *host, uint16_t port)
{
    if (!moduleReady || !uart) return false;

    // 如果已有连接先断开
    if (tcpConnected) {
        disconnect();
    }

    sslMode = false;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QIOPEN=1,%d,\"TCP\",\"%s\",%u",
             currentConnectId, host, port);

    // 发送连接命令
    uart->print(cmd);
    uart->print("\r\n");

    // 等待 +QIOPEN: <id>,<result>  其中 result=0 表示成功
    // 超时时间 120 秒（模块可能需要时间建立连接）
    unsigned long start = millis();
    String response;

    while (millis() - start < 120000) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();

            if (line.startsWith("+QIURC:")) {
                handleURC(line);
                continue;
            }

            if (line.length() > 0) {
                if (response.length() > 0) response += "\n";
                response += line;
            }

            // 检查连接结果: +QIOPEN: <id>,<err>
            if (line.startsWith("+QIOPEN:")) {
                int comma = line.indexOf(',');
                if (comma > 0) {
                    int err = line.substring(comma + 1).toInt();
                    tcpConnected = (err == 0);
                    return tcpConnected;
                }
            }
        }
        delay(50);
    }

    tcpConnected = false;
    return false;
}

bool Air780epDriver::connectSSL(const char *host, uint16_t port)
{
    if (!moduleReady || !uart) return false;

    // 如果已有连接先断开
    if (tcpConnected) {
        disconnect();
    }

    sslMode = true;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QSSLOPEN=%d,%d,\"TCP\",\"%s\",%u",
             currentConnectId, currentConnectId, host, port);

    // 发送连接命令
    uart->print(cmd);
    uart->print("\r\n");

    // 等待 +QSSLOPEN: <id>,<result>  其中 result=0 表示成功
    unsigned long start = millis();

    while (millis() - start < 120000) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();

            if (line.startsWith("+QIURC:")) {
                handleURC(line);
                continue;
            }

            if (line.startsWith("+QSSLOPEN:")) {
                // +QSSLOPEN: <connectID>,<err>
                int comma = line.indexOf(',');
                if (comma > 0) {
                    int err = line.substring(comma + 1).toInt();
                    tcpConnected = (err == 0);
                    return tcpConnected;
                }
            }

            if (line.indexOf("OK") >= 0) {
                // 可能有 OK 紧随其后
                continue;
            }
        }
        delay(50);
    }

    tcpConnected = false;
    return false;
}

bool Air780epDriver::disconnect()
{
    if (!uart || !tcpConnected) return false;

    bool result = false;
    if (sslMode) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+QSSLCLOSE=%d", currentConnectId);
        result = sendCommand(cmd, "OK", 10000);
    } else {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%d", currentConnectId);
        result = sendCommand(cmd, "OK", 10000);
    }

    tcpConnected = false;
    return result;
}

bool Air780epDriver::isConnected()
{
    if (!moduleReady || !uart) return false;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+QISTATE=%d,1", currentConnectId);

    // 发送 AT+QISTATE 命令
    uart->print(cmd);
    uart->print("\r\n");

    // +QISTATE: <connectID>,<service_type>,<IP_address>,<remote_port>,<local_port>,<send_ack>,<access_mode>,<server>
    // 如果连接存在则返回状态信息
    String resp;
    bool ok = waitForResponse("+QISTATE:", 5000, &resp);

    if (ok) {
        // 更新 tcpConnected 状态
        tcpConnected = true;
    } else {
        tcpConnected = false;
    }

    return tcpConnected;
}

// ==================== 电源管理 ====================

bool Air780epDriver::sleep()
{
    if (!moduleReady || !uart) return false;
    return sendCommand("AT+QSCLK=1", "OK", 5000);
}

bool Air780epDriver::wakeup()
{
    if (!uart) return false;
    // 发送任意数据唤醒模块
    uart->print("AT\r\n");
    delay(500);
    moduleReady = sendCommand("AT", "OK", 3000);
    return moduleReady;
}

bool Air780epDriver::getSignalStrength(int &rssi)
{
    if (!moduleReady || !uart) return false;

    // AT+CSQ -> +CSQ: <rssi>,<ber>
    uart->print("AT+CSQ\r\n");

    String resp;
    if (waitForResponse("+CSQ:", 5000, &resp)) {
        // 解析 +CSQ: <rssi>,<ber>
        int colon = resp.indexOf('+');
        if (colon >= 0) {
            int spaceColon = resp.indexOf(':');
            if (spaceColon > 0) {
                String valStr = resp.substring(spaceColon + 1);
                valStr.trim();
                int comma = valStr.indexOf(',');
                if (comma > 0) {
                    rssi = valStr.substring(0, comma).toInt();
                    return true;
                }
            }
        }
    }

    rssi = 99;  // 未知
    return false;
}
