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
    , gprsConfigured(false)
    , urcCallback(nullptr)
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

            // +MSUB URC：转发给注册的回调（MQTTAtDriver）
            if (line.startsWith("+MSUB:")) {
                if (urcCallback) {
                    urcCallback(line);
                }
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

            // +MSUB URC：转发给注册的回调（MQTTAtDriver）
            if (line.startsWith("+MSUB:")) {
                if (urcCallback) {
                    urcCallback(line);
                }
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

// waitForResponseCIP 已移除 — CIP 模式不再使用

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

int Air780epDriver::available()
{
    // 检查 UART 中是否有新的数据
    if (uart) {
        while (uart->available()) {
            String line = uart->readStringUntil('\n');
            line.trim();

            if (line.startsWith("+QIURC:") || line.startsWith("+QURC:")) {
                handleURC(line);
            } else if (line.startsWith("+MSUB:")) {
                // +MSUB URC：转发给注册的回调（MQTTAtDriver）
                if (urcCallback) {
                    urcCallback(line);
                }
            } else if (line.startsWith("+CGEV:")) {
                // 网络事件通知（NW DETACH 等）
                // NW DETACH 是模块的短期网络脱落，模块通常自动恢复。
                // 不应设置 moduleReady = false（那会触发 30 秒的暴力重置流程），
                // 仅断开 TCP，让重连逻辑自行处理即可。
                Serial.printf("[Air780ep] 网络事件: %s\n", line.c_str());
                if (line.indexOf("NW DETACH") >= 0 || line.indexOf("ME DETACH") >= 0) {
                    tcpConnected = false;
                    // 不设置 moduleReady = false，模块本身仍是工作的
                }
            } else if (line.length() > 0) {
                // [诊断] 不匹配已知 URC 的行 — 打印出来观察模块实际输出
                Serial.printf("[Air780ep] 未识别: %s\n", line.c_str());
            }
        }
    }
    return (int)ringAvailable();
}

// ==================== 网络连接 ====================

bool Air780epDriver::configureGPRS(const char *apn)
{
    if (!moduleReady || !uart) return false;

    // 如果已经配置过 GPRS，跳过配置
    if (gprsConfigured) {
        return true;
    }

    flushUART();
    char cmd[128];
    String resp;

    // ===== 1. 配置 APN =====
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (!sendCommand(cmd, "OK", 5000)) {
        Serial.println("[Air780ep] APN 配置失败");
        return false;
    }

    // ===== 2. GPRS 附着 =====
    if (sendCommand("AT+CGATT?", "+CGATT: 1", 5000)) {
        Serial.println("[Air780ep] GPRS 已附着");
    } else {
        Serial.print("[Air780ep] GPRS 附着中...");
        if (!sendCommand("AT+CGATT=1", "OK", 10000)) {
            Serial.println(" 失败");
            return false;
        }
        delay(1000);
        if (!sendCommand("AT+CGATT?", "+CGATT: 1", 5000)) {
            Serial.println("[Air780ep] GPRS 附着确认失败");
            return false;
        }
        Serial.println(" 成功");
    }

    // ===== 3. 激活 PDP（只用 CGACT，不用 QIACT） =====
    flushUART();
    Serial.print("[Air780ep] PDP 激活中...");
    if (!sendCommand("AT+CGACT=1,1", "OK", 10000)) {
        // 可能已经激活了
        sendCommand("AT+CGACT?", "+CGACT: 1,1", 3000, &resp);
        if (resp.indexOf("+CGACT: 1,1") < 0) {
            Serial.println(" 失败");
            return false;
        }
    }
    delay(500);

    // ===== 4. 获取 IP =====
    resp = "";
    if (sendCommand("AT+CGPADDR=1", "+CGPADDR: 1,", 3000, &resp)) {
        int idx = resp.indexOf('"');
        if (idx >= 0) {
            String ip = resp.substring(idx + 1);
            int end = ip.indexOf('"');
            if (end >= 0) ip = ip.substring(0, end);
            Serial.printf("[Air780ep] PDP 激活成功, IP: %s\n", ip.c_str());
        }
    }

    Serial.printf("[Air780ep] GPRS 配置完成, APN=%s\n", apn);

    // 标记 GPRS 已配置
    gprsConfigured = true;
    return true;
}

bool Air780epDriver::resetModule()
{
    Serial.println("[Air780ep] 硬件复位模块...");
    moduleReady = false;
    gprsConfigured = false;
    tcpConnected = false;

    pinMode(AT_PWRKEY, OUTPUT);
    digitalWrite(AT_PWRKEY, LOW);
    delay(1200);
    digitalWrite(AT_PWRKEY, HIGH);

    // 等待模块重新就绪
    for (int i = 0; i < 15; i++) {
        if (sendCommand("AT", "OK", 3000)) {
            // 重新初始化
            sendCommand("ATE0", "OK", 2000);
            sendCommand("AT+QIURC=1", "OK", 2000);
            moduleReady = true;
            Serial.println("[Air780ep] 硬件复位成功，模块就绪");
            flushUART();
            return true;
        }
        Serial.printf("[Air780ep] 等待复位就绪... (%d/15)\n", i + 1);
    }

    Serial.println("[Air780ep] 硬件复位失败");
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
    // 超时时间 20 秒
    unsigned long start = millis();

    while (millis() - start < 20000) {
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
    if (!uart) return false;

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
