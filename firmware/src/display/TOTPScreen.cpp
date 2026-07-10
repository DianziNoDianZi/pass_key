/**
 * @file TOTPScreen.cpp
 * @brief TOTP 验证码显示屏幕实现
 */

#include "TOTPScreen.h"
#include "DisplayManager.h"
#include "../totp/TOTPManager.h"
#include "../common/ButtonManager.h"
#include "config.h"

// ==================== 外部全局实例 ====================
extern TOTPManager totpManager;

// ==================== 构造 / 析构 ====================

TOTPScreen::TOTPScreen(DisplayManager *displayMgr)
    : currentState(STATE_LIST)
    , selectedIndex(0)
    , lastActivityTime(0)
    , codeDirty(true)
    , lastCodeTime(0xFFFFFFFF)
    , displayMgr(displayMgr)
{
}

TOTPScreen::~TOTPScreen()
{
}

// ==================== 生命周期 ====================

void TOTPScreen::onActivate()
{
    lastActivityTime = millis();
    selectedIndex = 0;
    currentState = STATE_LIST;
    codeDirty = true;
}

void TOTPScreen::onDeactivate()
{
}

void TOTPScreen::onButtonPress(uint8_t button)
{
    lastActivityTime = millis();

    if (currentState == STATE_LIST) {
        // 列表视图：UP/DOWN 切换，CONFIRM 进入详情
        int count = totpManager.getAccountCount();
        if (count == 0) return;

        if (button == BTN_ID_UP) {
            selectedIndex--;
            if (selectedIndex < 0) {
                selectedIndex = count - 1;
            }
        } else if (button == BTN_ID_DOWN) {
            selectedIndex++;
            if (selectedIndex >= count) {
                selectedIndex = 0;
            }
        } else if (button == BTN_ID_CONFIRM) {
            // 进入验证码详情视图
            currentState = STATE_CODE;
            codeDirty = true;
            lastCodeTime = 0xFFFFFFFF; // 强制刷新
        }
    } else if (currentState == STATE_CODE) {
        // 验证码详情视图：UP/DOWN/BACK 返回列表
        // CONFIRM 也返回列表
        currentState = STATE_LIST;
        codeDirty = true;
    }
}

void TOTPScreen::onUpdate()
{
    uint32_t now = millis();

    if (currentState == STATE_LIST) {
        // 列表视图超时自动返回主菜单（使用安全弹出）
        if (now - lastActivityTime > LIST_TIMEOUT_MS) {
            if (displayMgr) {
                displayMgr->requestPop();
            }
            return;
        }
    } else if (currentState == STATE_CODE) {
        // 验证码详情视图超时自动返回列表
        if (now - lastActivityTime > CODE_TIMEOUT_MS) {
            currentState = STATE_LIST;
            codeDirty = true;
            lastActivityTime = now;
            // 重绘列表
            if (displayMgr) {
                displayMgr->clear();
                displayMgr->getTFT().fillScreen(TFT_BLACK);
                drawListView(displayMgr->getTFT());
                displayMgr->showStatusBar();
            }
            return;
        }

        // 每秒刷新：检查时间计数器是否变化
        uint32_t tc = totpManager.getTimeCounter();
        if (tc != lastCodeTime) {
            lastCodeTime = tc;
            codeDirty = true;
            currentCode = totpManager.generateCodeAtIndex(selectedIndex);
        }

        // 每秒重绘（秒数和进度条更新）
        if (codeDirty || (now % 1000) < 20) {
            if (displayMgr) {
                displayMgr->clear();
                // onDraw 会被外部 update 流程调用，但这里直接绘制
                // 实际上 DisplayManager::update 只调用 onUpdate，不调用 onDraw
                // 所以我们需要在这里触发重绘
                displayMgr->getTFT().fillScreen(TFT_BLACK);
                drawCodeView(displayMgr->getTFT());
                displayMgr->showStatusBar();
            }
            codeDirty = false;
        }
    }
}

void TOTPScreen::onDraw(TFT_eSPI &tft)
{
    if (currentState == STATE_LIST) {
        drawListView(tft);
    } else {
        // 首次进入 CODE 视图时生成验证码
        if (codeDirty) {
            currentCode = totpManager.generateCodeAtIndex(selectedIndex);
            lastCodeTime = totpManager.getTimeCounter();
            codeDirty = false;
        }
        drawCodeView(tft);
    }
}

// ==================== 列表视图 ====================

void TOTPScreen::drawListView(TFT_eSPI &tft)
{
    tft.fillScreen(TFT_BLACK);

    int count = totpManager.getAccountCount();

    // 标题栏
    int titleY = STATUS_HEIGHT;
    tft.fillRect(0, titleY, TFT_WIDTH, LIST_TITLE_HEIGHT, PASSKEY_BLUE);
    tft.setTextColor(PASSKEY_WHITE, PASSKEY_BLUE);
    tft.setTextSize(2);
    const char *title = "TOTP 代码";
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, titleY + 4);
    tft.print(title);

    // 列表项
    int startY = STATUS_HEIGHT + LIST_TITLE_HEIGHT;
    tft.setTextSize(2);

    for (int i = 0; i < VISIBLE_LINES && i < count; i++) {
        int y = startY + i * LIST_LINE_HEIGHT;
        String name = totpManager.getAccountName(i);

        // 选中指示器
        if (i == selectedIndex) {
            tft.fillCircle(10, y + LIST_LINE_HEIGHT / 2, 3, PASSKEY_BLUE);
            tft.setTextColor(PASSKEY_BLUE, TFT_BLACK);
        } else {
            tft.drawCircle(10, y + LIST_LINE_HEIGHT / 2, 3, PASSKEY_WHITE);
            tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
        }

        tft.setCursor(20, y + 2);
        tft.print(name.c_str());
    }

    // 页码指示
    if (count > VISIBLE_LINES) {
        int totalPages  = (count + VISIBLE_LINES - 1) / VISIBLE_LINES;
        int currentPage = 1; // 简化：当前仅显示第一页
        tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
        tft.setTextSize(1);
        char pageStr[8];
        snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, totalPages);
        tft.setCursor(TFT_WIDTH - 30, TFT_HEIGHT - 14);
        tft.print(pageStr);
    }
}

// ==================== 验证码详情视图 ====================

void TOTPScreen::drawCodeView(TFT_eSPI &tft)
{
    tft.fillScreen(TFT_BLACK);

    if (selectedIndex < 0 || selectedIndex >= totpManager.getAccountCount()) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(20, TFT_HEIGHT / 2 - 10);
        tft.print("无账户");
        return;
    }

    String accountName = totpManager.getAccountName(selectedIndex);
    uint32_t remaining = totpManager.getRemainingSeconds();

    // 1. 账户名称（大字体，顶部居中）
    int nameY = STATUS_HEIGHT + 10;
    tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor((TFT_WIDTH - tft.textWidth(accountName.c_str())) / 2, nameY);
    tft.print(accountName.c_str());

    // 2. 6 位验证码（超大字体，屏幕中央）
    int codeY = TFT_HEIGHT / 2 - 30;
    tft.setTextColor(PASSKEY_BLUE, TFT_BLACK);
    tft.setTextSize(5);
    int codeW = tft.textWidth(currentCode.c_str());
    tft.setCursor((TFT_WIDTH - codeW) / 2, codeY);
    tft.print(currentCode.c_str());

    // 3. 剩余秒数（小字体，码下方）
    int secY = codeY + 50;
    tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    char secStr[16];
    snprintf(secStr, sizeof(secStr), "%us", remaining);
    tft.setCursor((TFT_WIDTH - tft.textWidth(secStr)) / 2, secY);
    tft.print(secStr);

    // 4. 倒计时进度条
    int barY = TFT_HEIGHT - 40;
    int barW = 200;
    int barH = 16;
    int barX = (TFT_WIDTH - barW) / 2;
    drawProgressBar(tft, barX, barY, barW, barH, remaining);
}

// ==================== 进度条 ====================

void TOTPScreen::drawProgressBar(TFT_eSPI &tft, int x, int y, int w, int h,
                                  uint32_t remaining)
{
    // 背景
    tft.fillRoundRect(x, y, w, h, 3, 0x2104);
    // 边框
    tft.drawRoundRect(x, y, w, h, 3, TFT_WHITE);

    // 填充宽度
    float ratio = (float)remaining / TOTP_PERIOD;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int fillW = (int)((w - 2) * ratio);

    if (fillW > 0) {
        uint16_t color = getProgressColor(remaining);
        tft.fillRoundRect(x + 1, y + 1, fillW, h - 2, 2, color);
    }

    // 百分比数字
    tft.setTextColor(TFT_WHITE, 0x2104);
    tft.setTextSize(1);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(ratio * 100));
    tft.setCursor(x + w + 6, y + (h - 8) / 2);
    tft.print(pctStr);
}

uint16_t TOTPScreen::getProgressColor(uint32_t remaining)
{
    // 剩余 >15 秒绿色，<10 秒黄色，<5 秒红色
    if (remaining > 15) {
        return PASSKEY_GREEN;   // 绿色
    } else if (remaining > 5) {
        // 5-10 秒：黄色；10-15 秒：绿→黄过渡
        if (remaining > 10) {
            // 10-15 秒：从绿渐变到黄
            float t = (float)(remaining - 10) / 5.0f; // 10→1.0, 15→0.0
            uint8_t r = (uint8_t)((1.0f - t) * 255.0f);
            uint8_t g = 255;
            return ((r >> 3) << 11) | ((g >> 2) << 5);
        } else {
            return 0xFFE0;  // 黄色 (RGB565)
        }
    } else {
        return TFT_RED;     // 红色
    }
}
