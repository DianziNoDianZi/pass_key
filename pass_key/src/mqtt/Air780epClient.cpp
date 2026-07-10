/**
 * @file Air780epClient.cpp
 * @brief Air780epClient 实现 — Arduino Client 适配层
 */

#include "Air780epClient.h"
#include "config.h"

Air780epClient::Air780epClient(Air780epDriver *driver)
    : driver(driver)
    , useSSL(false)
    , _connected(false)
    , peekByte(-1)
{
}

Air780epClient::~Air780epClient()
{
    stop();
}

int Air780epClient::connect(IPAddress ip, uint16_t port)
{
    // 将 IP 地址转为字符串，通过 host:port 方式连接
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return connect(buf, port);
}

int Air780epClient::connect(const char *host, uint16_t port)
{
    // 先配置 GPRS
    if (!driver->isModuleReady()) {
        return 0;
    }

    // 使用默认 APN 配置 GPRS
    if (!driver->configureGPRS(AIR780EP_APN)) {
        return 0;
    }

    // 建立连接
    bool ok;
    if (useSSL) {
        ok = driver->connectSSL(host, port);
    } else {
        ok = driver->connectTCP(host, port);
    }

    _connected = ok;
    peekByte = -1;
    return ok ? 1 : 0;
}

size_t Air780epClient::write(uint8_t data)
{
    return write(&data, 1);
}

size_t Air780epClient::write(const uint8_t *buf, size_t size)
{
    if (!_connected) return 0;
    if (driver->sendData(buf, size)) {
        return size;
    }
    return 0;
}

int Air780epClient::available()
{
    if (!_connected) return 0;
    int avail = driver->available();
    if (peekByte >= 0) avail++;
    return avail;
}

int Air780epClient::read()
{
    if (peekByte >= 0) {
        int b = peekByte;
        peekByte = -1;
        return b;
    }
    uint8_t c;
    if (driver->receiveData(&c, 1) == 1) {
        return c;
    }
    return -1;
}

int Air780epClient::read(uint8_t *buf, size_t size)
{
    if (!buf || size == 0) return 0;
    if (peekByte >= 0) {
        buf[0] = (uint8_t)peekByte;
        peekByte = -1;
        if (size == 1) return 1;
        int n = driver->receiveData(buf + 1, size - 1);
        return (n >= 0) ? (n + 1) : 1;
    }
    return driver->receiveData(buf, size);
}

int Air780epClient::peek()
{
    if (peekByte >= 0) return peekByte;
    uint8_t c;
    if (driver->receiveData(&c, 1) == 1) {
        peekByte = c;
        return c;
    }
    return -1;
}

void Air780epClient::flush()
{
    // 数据已通过 UART 实时发送，无需额外冲洗
}

void Air780epClient::stop()
{
    if (_connected) {
        driver->disconnect();
        _connected = false;
    }
    peekByte = -1;
}

uint8_t Air780epClient::connected()
{
    if (!_connected) return 0;
    // 通过驱动层检查真实连接状态
    _connected = driver->isConnected();
    return _connected ? 1 : 0;
}

Air780epClient::operator bool()
{
    return _connected;
}
