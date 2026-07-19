/**
 * @file config.h
 * @brief PassKey 设备引脚定义与系统配置
 *
 * 本文件定义了 ESP32-S3 的引脚分配、MQTT 代理配置、
 * NTP 服务器配置等系统级常量。
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== 显示屏 — 部分引脚由项目目录 User_Setup.h 定义 ====================
// TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_WIDTH, TFT_HEIGHT
// 由 User_Setup.h（项目根目录）统一管理
// TFT_BL 在此定义，供 PowerManager.cpp 等非 TFT_eSPI 文件引用
// 完整引脚定义在库目录 User_Setup.h 中（库的 .cpp 文件编译时需要）
#define TFT_BL      GPIO_NUM_21

// ==================== 按键 ====================
#define BTN_UP      GPIO_NUM_6
#define BTN_DOWN    GPIO_NUM_7
#define BTN_CONFIRM GPIO_NUM_13
#define BTN_DEBOUNCE_MS  50    // 按键去抖时间 (ms)

// ==================== Air780ep (UART) ====================
#define UART_TX     GPIO_NUM_17
#define UART_RX     GPIO_NUM_18
#define AT_PWRKEY   GPIO_NUM_12
#define UART_BAUD   115200

// ==================== 蜂鸣器 & 震动马达 ====================
#define BUZZER      GPIO_NUM_9
#define VIBRATOR    GPIO_NUM_14

// ==================== MQTT 配置 ====================
#define MQTT_BROKER_ADDR     "154.40.45.21"
#define MQTT_BROKER_PORT     1883
#define MQTT_BROKER_PORT_SSL 8883     // MQTTS 默认端口
#define MQTT_DEVICE_ID       "pass_key_test"
#define MQTT_USERNAME        "pass_key_test"
#define MQTT_PASSWORD        "c65a35bb9846270cc4f5fec2230a416ace6d3f05460f3405319b8a9a0219a0ce"
#define MQTT_KEEPALIVE       65535   // 最大值，Aedes 计算超时为 65535×1.5≈27 小时，实际永远不会触发。
                                        // 设备每 2 秒发应用层心跳 PUBLISH 保活，PINGREQ 不再需要。

// ==================== Air780ep 4G 模块配置 ====================
#define AIR780EP_APN         "CMIOT"   // 中国移动物联网 APN

// ==================== 串口配置 ====================
// ESP32-S3 用 USB CDC（原生 USB 口）时需要勾选 "USB CDC On Boot"
// 如果用的是 UART 转 USB 芯片（CH340/CP210x）则不需要
#define SERIAL_BAUD     115200
// ==================== NTP 配置 ====================
#define NTP_SERVER1        "pool.ntp.org"

// ==================== 时区配置 ====================
// AT+CCLK 返回的是本地时间，减去此偏移量得到 UTC 时间
// 中国 = +8 小时：8 * 3600 = 28800
// 日本 = +9 小时，新加坡 = +8 小时，欧洲 = +1 ~ +2，美国 = -5 ~ -8
#define TZ_OFFSET_SEC     (8 * 3600)   // UTC+8（中国）

// ==================== TOTP 配置 ====================
#define TOTP_PERIOD       30           // TOTP 刷新周期（秒）

#endif // CONFIG_H
