/**
 * @file Air780epDriver.h
 * @brief Air780ep 4G 模块驱动 — UART AT 指令通信
 *
 * 通过 UART 与 Air780ep 模块通信，发送 AT 指令、
 * 解析响应、建立/断开 TCP/SSL 连接、收发数据。
 * 兼容移远 EC200 系列 AT 指令集。
 */

#ifndef AIR780EP_DRIVER_H
#define AIR780EP_DRIVER_H

#include <Arduino.h>

// 接收环形缓冲区大小
#define AIR780EP_RX_BUF_SIZE  4096

class Air780epDriver
{
public:
    Air780epDriver();
    ~Air780epDriver();

    /**
     * @brief 初始化 UART 与模块
     * - 初始化 UART2 (TX=GPIO17, RX=GPIO18, baud=115200)
     * - 通过 PWRKEY (GPIO16) 拉低 1 秒启动模块
     * - 等待模块就绪（发送 AT 等待返回 OK，最多重试 10 次，间隔 1 秒）
     * @return true 成功
     */
    bool init();

    /**
     * @brief 发送 AT 命令并等待期望响应
     * @param cmd           AT 命令内容（不含 \r\n，方法会自动追加）
     * @param expectedResp  期望的响应关键字（如 "OK"、"ERROR"）
     * @param timeoutMs     超时时间 (ms)
     * @param collectOut    可选，收集到的完整响应文本
     * @return true 收到期望响应
     */
    bool sendCommand(const char *cmd, const char *expectedResp = "OK",
                     uint32_t timeoutMs = 10000, String *collectOut = nullptr);

    /**
     * @brief 读取模块返回的所有原始数据
     * @param timeoutMs 超时时间 (ms)
     * @return 响应字符串
     */
    String readResponse(uint32_t timeoutMs = 5000);

    // ==================== 网络连接 ====================

    /**
     * @brief 配置 GPRS APN
     * @param apn APN 名称（中国移动卡一般用 "CMNET"）
     * @return true 配置成功
     */
    bool configureGPRS(const char *apn);

    /**
     * @brief 建立 TCP 连接
     * @param host 服务器地址
     * @param port 端口号
     * @return true 连接成功
     */
    bool connectTCP(const char *host, uint16_t port);

    /**
     * @brief 断开 TCP 连接
     * @return true 断开成功
     */
    bool disconnect();

    /**
     * @brief 检查 TCP 连接状态
     * @return true 已连接
     */
    bool isConnected();

    // ==================== SSL/TLS 支持 ====================

    /**
     * @brief 建立 SSL 加密 TCP 连接
     * @param host 服务器地址
     * @param port 端口号
     * @return true 连接成功
     */
    bool connectSSL(const char *host, uint16_t port);

    // ==================== 数据收发 ====================

    /**
     * @brief 通过当前连接发送数据
     * @param data 数据缓冲区
     * @param len  数据长度
     * @return true 发送成功
     */
    bool sendData(const uint8_t *data, size_t len);

    /**
     * @brief 读取接收缓冲区中已缓存的数据
     * @param buffer 接收缓冲区
     * @param maxLen 最大读取字节数
     * @return 实际读取的字节数，无数据返回 0
     */
    int receiveData(uint8_t *buffer, size_t maxLen);

    /**
     * @brief 获取接收缓冲区中可用的字节数
     * @return 可用字节数
     */
    int available();

    // ==================== 电源管理 ====================

    /**
     * @brief 让模块进入低功耗模式
     * @return true 成功
     */
    bool sleep();

    /**
     * @brief 唤醒模块
     * @return true 成功
     */
    bool wakeup();

    /**
     * @brief 硬件复位模块（拉低 PWRKEY 重启）
     * 当模块长时间无响应或 GPRS 反复失败时调用。
     * @return true 复位后模块就绪
     */
    bool resetModule();

    /**
     * @brief 获取信号强度
     * @param rssi 输出 RSSI 值（0-31, 99=未知）
     * @return true 获取成功
     */
    bool getSignalStrength(int &rssi);

    // ==================== 状态查询 ====================

    /**
     * @brief 获取模块就绪状态
     * @return true 模块已就绪
     */
    bool isModuleReady() const { return moduleReady; }

    /**
     * @brief 获取当前连接 ID
     * @return 连接 ID
     */
    int getConnectId() const { return currentConnectId; }

    /**
     * @brief 是否使用 SSL 模式
     * @return true 当前为 SSL 连接
     */
    bool isSSLMode() const { return sslMode; }

private:
    HardwareSerial *uart;
    bool  moduleReady;
    int   currentConnectId;   // 当前 TCP/SSL 连接 ID（通常为 0）
    bool  tcpConnected;       // TCP 连接状态
    bool  sslMode;            // 是否 SSL 模式
    bool  gprsConfigured;      // GPRS 已配置（避免重复配置）
    bool  cipMode;            // 是否使用 CIPSTART/CIPSEND 模式（回退方案）

    // 接收环形缓冲区
    uint8_t rxBuf[AIR780EP_RX_BUF_SIZE];
    volatile size_t rxHead;
    volatile size_t rxTail;

    /**
     * @brief 清空 UART 接收缓冲区
     */
    void flushUART();

    /**
     * @brief 从环形缓冲区读取一个字节
     * @param c 输出字节
     * @return true 读取成功
     */
    bool ringGetc(uint8_t &c);

    /**
     * @brief 向环形缓冲区写入一个字节
     * @param c 字节
     */
    void ringPutc(uint8_t c);

    /**
     * @brief 获取环形缓冲区可用字节数
     */
    size_t ringAvailable() const;

    /**
     * @brief 清空环形缓冲区
     */
    void ringFlush();

    /**
     * @brief 处理 +QIURC 异步通知
     * @param line 通知行内容
     */
    void handleURC(const String &line);

    /**
     * @brief 等待 UART 读取到指定字符串
     * @param expected 期望字符串
     * @param timeoutMs 超时时间
     * @param collectOut 收集到的全部响应
     * @return true 匹配成功
     */
    bool waitForResponse(const char *expected, uint32_t timeoutMs, String *collectOut = nullptr);

    // CIP 模式 CLOSED 检测状态机
    uint8_t closedDetectState;  // 0=待机, 1-7=匹配中
};

#endif // AIR780EP_DRIVER_H
