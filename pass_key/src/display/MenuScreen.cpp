/**
 * @file MenuScreen.cpp
 * @brief 菜单列表屏幕实现
 */

#include "MenuScreen.h"
#include "ButtonManager.h"
#include "DisplayManager.h"
#include "config.h"

MenuScreen::MenuScreen(const char *title)
    : title(title)
    , selectedIndex(0)
    , scrollOffset(0)
{
}

MenuScreen::~MenuScreen()
{
}

void MenuScreen::addItem(const char *text)
{
    items.push_back(text);
}

void MenuScreen::clearItems()
{
    items.clear();
    selectedIndex = 0;
    scrollOffset  = 0;
}

void MenuScreen::setItems(const std::vector<const char *> &newItems)
{
    items          = newItems;
    selectedIndex  = 0;
    scrollOffset   = 0;
}

int MenuScreen::getSelectedIndex() const
{
    return selectedIndex;
}

const char *MenuScreen::getSelectedItem() const
{
    if (items.empty() || selectedIndex < 0 ||
        selectedIndex >= (int)items.size()) {
        return nullptr;
    }
    return items[selectedIndex];
}

void MenuScreen::onActivate() {}
void MenuScreen::onDeactivate() {}

void MenuScreen::onButtonPress(uint8_t button)
{
    if (items.empty()) return;

    if (button == BTN_ID_UP) {
        selectedIndex--;
        if (selectedIndex < 0) {
            selectedIndex = items.size() - 1; // 循环滚动到末尾
        }
    } else if (button == BTN_ID_DOWN) {
        selectedIndex++;
        if (selectedIndex >= (int)items.size()) {
            selectedIndex = 0; // 循环滚动到开头
        }
    }
    // BTN_ID_CONFIRM: 确认动作由外部处理

    // 调整滚动偏移，确保选中项可见
    if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
    }
    if (selectedIndex >= scrollOffset + VISIBLE_LINES) {
        scrollOffset = selectedIndex - VISIBLE_LINES + 1;
    }
}

void MenuScreen::onUpdate()
{
}

void MenuScreen::onDraw(TFT_eSPI &tft)
{
    tft.fillScreen(APPLE_BG);
    drawTitle(tft);
    drawItems(tft);
    drawPageIndicator(tft);
}

void MenuScreen::drawTitle(TFT_eSPI &tft)
{
    // iOS 分段标题：灰色小字，左对齐
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    tft.setCursor(16, 22);
    tft.print(title);
}

void MenuScreen::drawItems(TFT_eSPI &tft)
{
    int rowH = 28;
    int startY = 32;

    for (int i = 0; i < VISIBLE_LINES; i++) {
        int idx = i + scrollOffset;
        if (idx >= (int)items.size()) break;

        int y = startY + i * rowH;

        // 选中行：蓝色背景高亮
        if (idx == selectedIndex) {
            tft.fillRect(0, y, TFT_WIDTH, rowH, APPLE_BLUE);
            tft.setTextColor(PASSKEY_WHITE, APPLE_BLUE);
        } else {
            tft.setTextColor(PASSKEY_WHITE, APPLE_BG);
        }

        // 项目名称
        tft.setTextSize(2);
        tft.setCursor(16, y + (rowH - 16) / 2);
        tft.print(items[idx]);

        // 右侧 chevron ">"
        if (idx != selectedIndex) {
            tft.setTextColor(APPLE_GRAY3, APPLE_BG);
            tft.setTextSize(1);
            tft.setCursor(TFT_WIDTH - 20, y + (rowH - 8) / 2);
            tft.print(">");
        }

        // 分隔线（选中行不需要）
        if (idx != selectedIndex && idx < (int)items.size() - 1) {
            tft.drawLine(16, y + rowH - 1, TFT_WIDTH - 16, y + rowH - 1, APPLE_SEP);
        }
    }
}

void MenuScreen::drawPageIndicator(TFT_eSPI &tft)
{
    if (items.empty()) return;

    int totalPages  = (items.size() + VISIBLE_LINES - 1) / VISIBLE_LINES;
    if (totalPages <= 1) return;

    int currentPage = scrollOffset / VISIBLE_LINES + 1;

    char pageStr[8];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, totalPages);
    tft.setTextColor(APPLE_GRAY, APPLE_BG);
    tft.setTextSize(1);
    tft.setCursor(TFT_WIDTH - 36, TFT_HEIGHT - 12);
    tft.print(pageStr);
}
