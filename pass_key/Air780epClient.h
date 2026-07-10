/**
 * @file Air780epClient.h
 * @brief Arduino Client 接口封装，将 Air780epDriver 的 TCP/SSL 通道
 *        适配为标准的 Client 接口，供 PubSubClient 使用。
 */

#ifndef AIR780EP_CLIENT_H
#define AIR780EP_CLIENT_H

#include <Arduino.h>
#include <Client.h>
#include "Air780epDriver.h"

class Air780epClient : public Client
{
public:
    /**
     * @param driver Air780epDriver 实例指针
     */
    Air780epClient(Air780epDriver *driver);
    virtual ~Air780epClient();

    // ==================== Client 接口实现 ====================

    /**
     * @brief 建立连接（通过 IP 地址）
     * 注：Air780ep 驱动通过域名连接，此方法 fallback 到同名字符串调用
     */
    int connect(IPAddress ip, uint16_t port) override;

    /**
     * @brief 建立连接（通过主机名）
     * 根据 useSSL 标志选择 TCP 或 SSL 连接
     * @return 1 成功, 0 失败
     */
    int connect(const char *host, uint16_t port) override;

    /**
     * @brief 写入一个字节
     */
    size_t write(uint8_t data) override;

    /**
     * @brief 写入数据块
     */
    size_t write(const uint8_t *buf, size_t size) override;

    /**
     * @brief 获取可读取的字节数
     */
    int available() override;

    /**
     * @brief 读取一个字节
     * @return 字节值，-1 表示无数据
     */
    int read() override;

    /**
     * @brief 读取数据到缓冲区
     * @return 实际读取的字节数
     */
    int read(uint8_t *buf, size_t size) override;

    /**
     * @brief 预读一个字节（不消耗）
     * @return 字节值，-1 表示无数据
     */
    int peek() override;

    /**
     * @brief 刷新输出缓冲区（空操作，数据实时通过 UART 发送）
     */
    void flush() override;

    /**
     * @brief 断开连接
     */
    void stop() override;

    /**
     * @brief 检查连接状态
     * @return 0 断开, 1 连接中
     */
    uint8_t connected() override;

    /**
     * @brief bool 运算符重载
     */
    operator bool() override;

    // ==================== 配置 ====================

    /**
     * @brief 设置是否使用 SSL
     * @param ssl true=SSL, false=TCP
     */
    void setSSL(bool ssl) { useSSL = ssl; }

    /**
     * @brief 获取是否使用 SSL
     */
    bool getSSL() const { return useSSL; }

protected:
    Air780epDriver *driver;
    bool useSSL;
    bool _connected;
    int peekByte;       // peek() 缓存的单字节，-1 表示无缓存
};

#endif // AIR780EP_CLIENT_H
