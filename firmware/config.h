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

// ==================== 显示屏 (ST7789, SPI) ====================
#define TFT_CS      GPIO_NUM_10
#define TFT_DC      GPIO_NUM_11
#define TFT_RST     GPIO_NUM_12
#define TFT_MOSI    GPIO_NUM_13
#define TFT_SCLK    GPIO_NUM_7
#define TFT_BL      GPIO_NUM_15
#define TFT_WIDTH   240
#define TFT_HEIGHT  240

// ==================== 按键 ====================
#define BTN_UP      GPIO_NUM_6
#define BTN_DOWN    GPIO_NUM_7
#define BTN_CONFIRM GPIO_NUM_20
#define BTN_DEBOUNCE_MS  50   // 按键去抖时间 (ms)

// ==================== Air780ep (UART) ====================
#define UART_TX     GPIO_NUM_17
#define UART_RX     GPIO_NUM_18
#define AT_PWRKEY   GPIO_NUM_12
#define UART_BAUD   115200

// ==================== 蜂鸣器 & 震动马达 ====================
#define BUZZER      GPIO_NUM_7
#define VIBRATOR    GPIO_NUM_8

// ==================== MQTT 配置 ====================
#define MQTT_BROKER_ADDR     "mqtt.example.com"
#define MQTT_BROKER_PORT     1883
#define MQTT_BROKER_PORT_SSL 8883     // MQTTS 默认端口
#define MQTT_DEVICE_ID       "pass_key_test"
#define MQTT_USERNAME        "pass_key_test"
#define MQTT_PASSWORD        "c6b92b9b2ddf41ad4caa97610837ee9edb819e4c3d50e3d1ef8ad78b429c72de"
#define MQTT_KEEPALIVE       60

// ==================== Air780ep 4G 模块配置 ====================
#define AIR780EP_APN         "CMIOT"   // 中国移动物联网 APN

// ==================== NTP 配置 ====================
#define NTP_SERVER1        "pool.ntp.org"
#define NTP_SERVER2        "time.nist.gov"
#define NTP_SERVER3        "cn.ntp.org.cn"
#define NTP_TIMEOUT_MS     5000
#define TZ_OFFSET_SEC      28800   // UTC+8 (北京时间)

// ==================== 其他配置 ====================
#define SERIAL_BAUD        115200
#define WIFI_SSID          ""
#define WIFI_PASSWORD      ""
#define TOTP_PERIOD        30      // TOTP 默认周期 (秒)
#define TOTP_DIGITS        6       // TOTP 默认位数

#endif // CONFIG_H
