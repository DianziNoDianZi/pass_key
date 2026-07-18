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
    , cipMode(false)
    , rxHead(0)
    , rxTail(0)
    , closedDetectState(0)
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

            // CIP 模式：+IPD 数据直接送入环形缓冲区（避免被 collected 吞掉）
            if (cipMode && line.startsWith("+IPD,")) {
                int colon = line.indexOf(':');
                if (colon > 5) {
                    int ipdLen = line.substring(5, colon).toInt();
                    int dataStart = colon + 1;
                    int lineDataLen = line.length() - dataStart;
                    for (int i = dataStart; i < (int)line.length() && (i - dataStart) < ipdLen; i++) {
                        ringPutc((uint8_t)line.charAt(i));
                    }
                    int remaining = ipdLen - lineDataLen;
                    while (remaining > 0 && uart->available()) {
                        uint8_t c = (uint8_t)uart->read();
                        ringPutc(c);
                        remaining--;
                    }
                }
                continue;  // +IPD 行不加入 collected
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

    if (cipMode) {
        // CIP 模式：使用 CIPSEND
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned int)len);
        uart->print(cmd);
        uart->print("\r\n");

        // 等待 ">" 提示符（最多 5 秒，超过说明模块卡死）
        unsigned long start = millis();
        bool gotPrompt = false;
        while (millis() - start < 5000) {
            if (uart->available()) {
                char c = uart->read();
                if (c == '>') {
                    gotPrompt = true;
                    break;
                }
                // 快速检测错误或断开，不等超时
                // 模块可能在等待期间发送 CLOSED/ERROR/CME ERROR
                if (c == 'E' || c == 'C' || c == '+') {
                    // 读一整行看是不是 ERROR/CLOSED/CME ERROR
                    String line = String(c) + uart->readStringUntil('\n');
                    line.trim();
                    if (line.indexOf("ERROR") >= 0 || line.indexOf("CME ERROR") >= 0) {
                        Serial.printf("[CIPSEND] 模块返回错误: %s\n", line.c_str());
                        if (line.indexOf("CME ERROR: 3") >= 0) {
                            tcpConnected = false;
                            Serial.println("[CIPSEND] CME ERROR: 3 → TCP 不可用");
                        }
                        return false;
                    }
                    if (line.indexOf("CLOSED") >= 0) {
                        tcpConnected = false;
                        Serial.println("[Air780ep] CIPSEND 时检测到 TCP 已关闭");
                        return false;
                    }
                }
            }
            delay(5);  // 更短的延迟，更快响应
        }
        if (!gotPrompt) {
            // 模块不响应 > 提示符，说明 TCP 连接已断或模块卡死
            // 立即标记 TCP 断开，让上层尽快触发重连
            tcpConnected = false;
            Serial.println("[CIPSEND] 超时未收到 > 提示符，标记 TCP 断开");
            return false;
        }

        // 发送数据
        uart->write(data, len);
        // SEND OK 最多等 5 秒（TCP 发送不应超过 1 秒）
        bool sent = waitForResponse("SEND OK", 5000);
        if (!sent) {
            // 检查缓冲区是否有 CME ERROR（异步来的，waitForResponse 可能已经读到了）
            // 还要检查 UART 中是否有残留错误
            if (uart) {
                while (uart->available()) {
                    String errLine = uart->readStringUntil('\n');
                    errLine.trim();
                    if (errLine.indexOf("CME ERROR: 3") >= 0 ||
                        errLine.indexOf("+CME ERROR: 3") >= 0) {
                        Serial.println("[CIPSEND] CME ERROR: 3 → TCP 连接不可用，标记断开");
                        tcpConnected = false;
                        break;
                    }
                }
            }
            // SEND OK 超时通常意味着 TCP 已断，模块无法确认数据到达对端
            tcpConnected = false;
            Serial.printf("[CIPSEND] 发送 %u 字节: SEND OK 超时，标记 TCP 断开\n", (unsigned int)len);
        } else {
            // 成功时只简短打印，减少 Serial 占用
            if (len > 64) {
                Serial.printf("[CIPSEND] 发送 %u 字节: OK\n", (unsigned int)len);
            }
        }

        return sent;
    }

    // QI 模式：使用 QISEND
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
        tcpConnected = false;
        Serial.println("[CIPSEND/QI] 超时未收到 > 提示符，标记 TCP 断开");
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
    bool qiSent = waitForResponse("SEND OK", 5000);
    if (!qiSent) {
        tcpConnected = false;
        Serial.println("[CIPSEND/QI] SEND OK 超时，标记 TCP 断开");
    }
    return qiSent;
}

int Air780epDriver::receiveData(uint8_t *buffer, size_t maxLen)
{
    // 在 CIP 模式下，+IPD 数据已在 available() 中写入环形缓冲区
    // QI 模式下，处理 URC 通知
    if (!cipMode && uart) {
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
    // 检查 UART 中是否有新的数据
    if (uart) {
        while (uart->available()) {
            if (cipMode) {
                // CIP 模式：直接读取原始字节到环形缓冲区
                // Air780ep 不包装 +IPD 前缀，数据直接以原始二进制到达
                uint8_t c = (uint8_t)uart->read();

                // 状态机检测 "CLOSED\r\n" URC（TCP 断开通知）
                bool skipByte = false;
                switch (closedDetectState) {
                    case 0: if (c == 'C') closedDetectState = 1; break;
                    case 1: closedDetectState = (c == 'L') ? 2 : 0; break;
                    case 2: closedDetectState = (c == 'O') ? 3 : 0; break;
                    case 3: closedDetectState = (c == 'S') ? 4 : 0; break;
                    case 4: closedDetectState = (c == 'E') ? 5 : 0; break;
                    case 5: closedDetectState = (c == 'D') ? 6 : 0; break;
                    case 6: closedDetectState = (c == '\r') ? 7 : 0; break;
                    case 7:
                        if (c == '\n') {
                            tcpConnected = false;
                            Serial.println("[Air780ep] TCP 连接已关闭");
                        }
                        closedDetectState = 0;
                        skipByte = true;  // 跳过 \n，不写入环形缓冲区
                        break;
                }

                if (!skipByte) {
                    ringPutc(c);
                }
            } else {
                String line = uart->readStringUntil('\n');
                line.trim();
                if (line.startsWith("+QIURC:") || line.startsWith("+QURC:")) {
                    handleURC(line);
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
                }
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

    cipMode = false;
    flushUART();
    char cmd[128];
    String resp;

    // ===== 诊断输出 =====
    sendCommand("AT+CPIN?", "+CPIN:", 3000, &resp);
    if (resp.indexOf("READY") >= 0)
        Serial.println("[Air780ep] SIM 卡: READY");
    else
        Serial.printf("[Air780ep] SIM 卡状态: %s\n", resp.c_str());

    resp = "";
    sendCommand("AT+CSQ", "+CSQ:", 3000, &resp);
    int comma = resp.indexOf(',');
    if (comma > 0) {
        int rssi = resp.substring(resp.indexOf(':') + 1, comma).toInt();
        Serial.printf("[Air780ep] 信号: CSQ=%d", rssi);
        if (rssi == 99) Serial.println(" (无信号)");
        else if (rssi <= 9) Serial.println(" (弱)");
        else if (rssi <= 20) Serial.println(" (中等)");
        else Serial.println(" (强)");
    }

    // ===== 1. 配置 APN =====
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (!sendCommand(cmd, "OK", 10000)) {
        Serial.println("[Air780ep] APN 配置失败");
        return false;
    }

    // ===== 2. GPRS 附着 =====
    if (sendCommand("AT+CGATT?", "+CGATT: 1", 5000)) {
        Serial.println("[Air780ep] GPRS 已附着");
    } else {
        Serial.print("[Air780ep] GPRS 附着中...");
        if (!sendCommand("AT+CGATT=1", "OK", 20000)) {
            Serial.println(" 失败");
            return false;
        }
        delay(3000);
        if (!sendCommand("AT+CGATT?", "+CGATT: 1", 8000)) {
            Serial.println("[Air780ep] GPRS 附着确认失败");
            return false;
        }
        Serial.println(" 成功");
    }

    // ===== 3. 激活 PDP（只用 CGACT，不用 QIACT） =====
    flushUART();
    Serial.print("[Air780ep] PDP 激活中...");
    if (!sendCommand("AT+CGACT=1,1", "OK", 20000)) {
        // 可能已经激活了
        sendCommand("AT+CGACT?", "+CGACT: 1,1", 5000, &resp);
        if (resp.indexOf("+CGACT: 1,1") < 0) {
            Serial.println(" 失败");
            return false;
        }
    }
    delay(2000);

    // ===== 4. 获取 IP =====
    resp = "";
    if (sendCommand("AT+CGPADDR=1", "+CGPADDR: 1,", 5000, &resp)) {
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
    cipMode = false;
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

bool Air780epDriver::connectTCP(const char *host, uint16_t port)
{
    if (!moduleReady || !uart) return false;

    // 如果已有连接先断开
    if (tcpConnected) {
        disconnect();
    }

    sslMode = false;
    cipMode = true;
    char cmd[256];

    // 1. 彻底清理：CIPSHUT 关闭所有残留连接
    flushUART();
    uart->print("AT+CIPSHUT\r\n");

    // 构建 CIPSTART 命令
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);

    // 2. 建立 CIP TCP 连接（最多重试 3 次）
    tcpConnected = false;

    for (int retry = 0; retry < 3 && !tcpConnected && moduleReady; retry++) {
        if (retry > 0) {
            Serial.printf("[TCP] CIPSTART 重试 (%d/3)...\n", retry + 1);
            // 重试前先清理残留连接
            flushUART();
            uart->print("AT+CIPSHUT\r\n");
            delay(500);
            while (uart->available()) uart->read();
            flushUART();
        }

        // 发送 CIPSTART 命令
        uart->print(cmd);
        uart->print("\r\n");

        unsigned long start = millis();
        bool cipstartFailed = false;

        while (millis() - start < 20000 && !cipstartFailed && !tcpConnected) {
            while (uart->available()) {
                String line = uart->readStringUntil('\n');
                line.trim();

                // 无操作：ALREADY CONNECT → 关闭再试
                if (line.indexOf("ALREADY CONNECT") >= 0) {
                    uart->print("AT+CIPCLOSE\r\n");
                    delay(300);
                    while (uart->available()) uart->read();
                    uart->print(cmd);
                    uart->print("\r\n");
                    break;
                }
                if (line.equalsIgnoreCase("CLOSED")) continue;

                // 成功
                if (line.startsWith("CONNECT OK") || line.equalsIgnoreCase("CONNECT")) {
                    tcpConnected = true;
                    Serial.println("[TCP] CIPSTART 连接成功");
                    // 等待 TCP 握手完成（模块需完成 TCP ACK，过早发 CIPSEND 会 CME ERROR: 3）
                    delay(500);
                    flushUART();
                    // 尝试设置 TCP 空闲超时（部分模块支持，Air780ep 报 ERROR 但不影响连接）
                    uart->print("AT+CIPSTO=120\r\n");
                    {
                        String resp = readResponse(1500);
                        if (resp.indexOf("OK") >= 0) {
                            Serial.println("[TCP] CIPSTO=120 配置成功");
                        }
                        // ERROR 是正常（模块不支持），继续往下走
                    }
                    // 注：CIPKEEPALIVE 在该模块上总返回 ERROR，不再重试（浪费 ~2 秒）
                    break;
                }
                if (line.startsWith("STATE:") && line.indexOf("CONNECT OK") >= 0) {
                    tcpConnected = true;
                    delay(500);
                    flushUART();
                    // 同样尝试设置 TCP 超时
                    uart->print("AT+CIPSTO=120\r\n");
                    {
                        String resp = readResponse(1500);
                        if (resp.indexOf("OK") >= 0) {
                            Serial.println("[TCP] CIPSTO=120 配置成功");
                        }
                    }
                    break;
                }

                // 失败 — 立即标记退出外层循环
                if (line.startsWith("CONNECT FAIL") ||
                    line.startsWith("+CME ERROR:") ||
                    line.equalsIgnoreCase("ERROR")) {
                    Serial.printf("[TCP] CIPSTART 失败: %s\n", line.c_str());
                    cipstartFailed = true;
                    break;
                }

                // 模块重启
                if (line.indexOf("^boot.") >= 0) {
                    Serial.println("[TCP] 模块重启");
                    moduleReady = false;
                    cipstartFailed = true;
                    break;
                }
            }
            if (!cipstartFailed && !tcpConnected) delay(50);
        }
    }

    if (tcpConnected) {
        return true;
    }

    // 所有重试都失败
    if (!moduleReady) {
        Serial.println("[TCP] 模块不在就绪状态");
    } else {
        Serial.println("[TCP] CIPSTART 多次重试均失败");
    }
    cipMode = false;
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
    if (cipMode) {
        result = sendCommand("AT+CIPSHUT", "SHUT OK", 10000) ||
                 sendCommand("AT+CIPSHUT", "OK", 5000);
        cipMode = false;
    } else if (sslMode) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+QSSLCLOSE=%d", currentConnectId);
        result = sendCommand(cmd, "OK", 10000);
    } else {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%d", currentConnectId);
        result = sendCommand(cmd, "OK", 10000);
    }

    tcpConnected = false;
    // 注意：不断开 GPRS 上下文，下次重连时不需要重新附着
    // gprsConfigured = false;  // 保留不变，省去 GPRS 重新附着的 15 秒
    return result;
}

bool Air780epDriver::isConnected()
{
    if (!moduleReady || !uart) return false;

    if (cipMode) {
        // CIP 模式：不发 AT+QISTATE（该命令在 CIP 模式下无效）
        // CLOSED 通知在 available() 中处理自动更新 tcpConnected
        return tcpConnected;
    }

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
