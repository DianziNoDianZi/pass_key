/**
 * @file User_Setup.h
 * @brief TFT_eSPI 配置 — ST7789 240x240, ESP32-S3 SPI
 *
 * 此文件位于项目目录，会覆盖库目录的 User_Setup.h。
 * TFT_eSPI 库通过 User_Setup_Select.h 加载本文件。
 */
#define USER_SETUP_LOADED
#define USER_SETUP_INFO "PassKey ST7789 240x240"

// ==================== 驱动选择 ====================
#define ST7789_DRIVER

// ==================== 分辨率 ====================
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ==================== SPI 引脚 (ESP32-S3) ====================
#define TFT_MISO  21
#define TFT_MOSI  13
#define TFT_SCLK  7
#define TFT_CS    10
#define TFT_DC    11
#define TFT_RST   12
#define TFT_BL    15

// ==================== SPI 频率 ====================
#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// ==================== 字体 ====================
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
