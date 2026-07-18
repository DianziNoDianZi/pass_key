/**
 * @file TOTPScreen.cpp
 * @brief TOTP 验证码显示屏幕实现
 */

#include "TOTPScreen.h"
#include "DisplayManager.h"
#include "TOTPManager.h"
#include "ButtonManager.h"
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
    , accountsDirty(false)
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

void TOTPScreen::notifyAccountsChanged()
{
    accountsDirty = true;
}

void TOTPScreen::onEvent(const char *event)
{
    if (strcmp(event, "totp_accounts_changed") == 0) {
        accountsDirty = true;
    }
}

void TOTPScreen::onUpdate()
{
    uint32_t now = millis();

    // 账户列表已变更：修正 selectedIndex 并重绘
    if (accountsDirty) {
        accountsDirty = false;
        int count = totpManager.getAccountCount();
        if (selectedIndex >= count) {
            selectedIndex = count > 0 ? count - 1 : 0;
        }
        codeDirty = true;
        lastActivityTime = now;
        if (displayMgr) {
            displayMgr->clear();
            displayMgr->getTFT().fillScreen(TFT_BLACK);
            if (currentState == STATE_LIST) {
                drawListView(displayMgr->getTFT());
            } else {
                drawCodeView(displayMgr->getTFT());
            }
            displayMgr->showStatusBar();
        }
        return;
    }

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
                // 安全弹出：先清除内容，再重绘列表
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

// ==================== 列表视图（iOS 风格） ====================

void TOTPScreen::drawListView(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);

    int count = totpManager.getAccountCount();

    // iOS 分段标题
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    tft.setCursor(16, 22);
    tft.print("TOTP CODES");

    // 列表项（iOS 表格风格）
    int rowH = 28;
    int startY = 32;
    tft.setTextSize(2);

    for (int i = 0; i < VISIBLE_LINES && i < count; i++) {
        int y = startY + i * rowH;
        String name = totpManager.getAccountName(i);

        // 选中行蓝色高亮
        if (i == selectedIndex) {
            tft.fillRect(0, y, TFT_WIDTH, rowH, APPLE_BLUE);
            tft.setTextColor(PASSKEY_WHITE, APPLE_BLUE);
        } else {
            tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
        }

        tft.setCursor(16, y + (rowH - 16) / 2);
        tft.print(name.c_str());

        // 右侧 chevron ">"
        if (i != selectedIndex) {
            tft.setTextColor(APPLE_GRAY3, APPLE_BG);
            tft.setTextSize(1);
            tft.setCursor(TFT_WIDTH - 20, y + (rowH - 8) / 2);
            tft.print(">");
        }

        // 分隔线（选中行不需要）
        if (i != selectedIndex && i < count - 1) {
            tft.drawLine(16, y + rowH - 1, TFT_WIDTH - 16, y + rowH - 1, APPLE_SEP);
        }
    }

    // 页码指示
    if (count > VISIBLE_LINES) {
        int totalPages  = (count + VISIBLE_LINES - 1) / VISIBLE_LINES;
        tft.setTextColor(APPLE_GRAY, APPLE_BG);
        tft.setTextSize(1);
        char pageStr[8];
        snprintf(pageStr, sizeof(pageStr), "%d/%d", 1, totalPages);
        tft.setCursor(TFT_WIDTH - 36, TFT_HEIGHT - 12);
        tft.print(pageStr);
    }
}

// ==================== 验证码详情视图（iOS 风格） ====================

void TOTPScreen::drawCodeView(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);

    if (selectedIndex < 0 || selectedIndex >= totpManager.getAccountCount()) {
        tft.setTextColor(APPLE_RED, APPLE_BG);
        tft.setTextSize(2);
        tft.setCursor(20, TFT_HEIGHT / 2 - 10);
        tft.print("No Accounts");
        return;
    }

    String accountName = totpManager.getAccountName(selectedIndex);
    uint32_t remaining = totpManager.getRemainingSeconds();

    // 1. 账户名称（灰色小字，类似 iOS 锁屏上的应用名称）
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    tft.setCursor((TFT_WIDTH - tft.textWidth(accountName.c_str())) / 2, 30);
    tft.print(accountName);

    // 2. 6 位验证码（超大粗体，屏幕中央，白色）
    int codeY = TFT_HEIGHT / 2 - 35;
    tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
    tft.setTextSize(6);
    int codeW = tft.textWidth(currentCode.c_str());
    if (codeW > TFT_WIDTH - 20) {
        tft.setTextSize(5);
        codeW = tft.textWidth(currentCode.c_str());
    }
    tft.setCursor((TFT_WIDTH - codeW) / 2, codeY);
    tft.print(currentCode.c_str());

    // 3. 剩余秒数（彩色大号）
    int secY = codeY + 56;
    tft.setTextSize(3);
    char secStr[8];
    snprintf(secStr, sizeof(secStr), "%us", remaining);
    uint16_t secColor = (remaining > 10) ? APPLE_GREEN : (remaining > 5) ? APPLE_ORANGE : APPLE_RED;
    tft.setTextColor(secColor, APPLE_BG);
    tft.setCursor((TFT_WIDTH - tft.textWidth(secStr)) / 2, secY);
    tft.print(secStr);

    // 4. 细进度条（iOS 风格, 圆角细条）
    int barY = TFT_HEIGHT - 50;
    int barW = TFT_WIDTH - 40;
    int barH = 4;
    int barX = (TFT_WIDTH - barW) / 2;
    drawProgressBar(tft, barX, barY, barW, barH, remaining);
}

// ==================== 进度条（iOS 细条风格） ====================

void TOTPScreen::drawProgressBar(TFT_eSPI &tft, int x, int y, int w, int h,
                                  uint32_t remaining)
{
    // 浅灰背景
    tft.fillRoundRect(x, y, w, h, 2, APPLE_GRAY3);

    // 填充
    float ratio = (float)remaining / TOTP_PERIOD;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int fillW = (int)(w * ratio);

    if (fillW > 0) {
        uint16_t color = getProgressColor(remaining);
        tft.fillRoundRect(x, y, fillW, h, 2, color);
    }
}

uint16_t TOTPScreen::getProgressColor(uint32_t remaining)
{
    if (remaining > 15) return APPLE_GREEN;
    if (remaining > 10) return APPLE_GREEN;
    if (remaining > 5)  return APPLE_ORANGE;
    return APPLE_RED;
}
