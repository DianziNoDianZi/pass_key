/**
 * @file User_Setup.h
 * @brief TFT_eSPI 自定义配置 — ST7789 240x240
 *
 * 本文件配置 TFT_eSPI 库驱动 ST7789 控制器，
 * 分辨率 240x240，使用 SPI 接口。
 * 引脚定义需与 config.h 中的引脚保持一致。
 */

#define USER_SETUP_INFO "ST7789 240x240 SPI"

// ==================== 驱动选择 ====================
#define ST7789_DRIVER

// ==================== 屏幕分辨率 ====================
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ==================== SPI 接口类型 ====================
#define TFT_SPI_PORT 1          // 使用 SPI1 (HSPI)

// ==================== 引脚定义 ====================
// 以下定义与 config.h 中的引脚分配保持一致
#define TFT_CS    10
#define TFT_DC    11
#define TFT_RST   12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_BL    15           // 背光控制引脚

// ==================== SPI 频率 ====================
#define SPI_FREQUENCY   40000000    // 40MHz SPI 时钟
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// ==================== 字体与渲染 ====================
#define LOAD_GLCD    字体
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF    // 加载 FreeFonts
#define SMOOTH_FONT

// ==================== 颜色深度 ====================
#define SPI_TRANSACTION  // 使用 SPI 事务

// ==================== 其他配置 ====================
#define TFT_SDA_READ     // ST7789 支持 SDA 读取
#define TFT_INVERSION_ON // ST7789 需要反转显示
