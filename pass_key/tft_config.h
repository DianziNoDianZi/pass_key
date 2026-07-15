/**
 * @file tft_config.h
 * @brief TFT_eSPI 硬件配置 — 在项目中直接定义，绕过 User_Setup.h
 *
 * 必须在 #include <TFT_eSPI.h> 之前包含此文件。
 * 定义 USER_SETUP_LOADED 阻止 TFT_eSPI 加载默认 User_Setup.h。
 */
#ifndef TFT_CONFIG_H
#define TFT_CONFIG_H

// 告诉 TFT_eSPI 不要加载默认 User_Setup.h
#define USER_SETUP_LOADED

// ========== 驱动选择 ==========
#define ST7789_DRIVER

// ========== 分辨率 ==========
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ========== ESP32-S3 引脚（SPI 直连） ==========
#define TFT_MISO  21   // ST7789 没有 MISO，但 ESP32 SPI 需要一个有效引脚
#define TFT_MOSI  13
#define TFT_SCLK  7
#define TFT_CS    10
#define TFT_DC    11
#define TFT_RST   12
#define TFT_BL    15

// ========== SPI 频率 ==========
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000

// ========== 字体（节省空间，只加载需要的） ==========
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// ========== 其他 ==========
#define SPI_TOUCH_FREQUENCY  2500000

#endif // TFT_CONFIG_H
