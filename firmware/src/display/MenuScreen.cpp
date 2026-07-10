/**
 * @file MenuScreen.cpp
 * @brief 菜单列表屏幕实现
 */

#include "MenuScreen.h"
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
    tft.fillScreen(TFT_BLACK);
    drawTitle(tft);
    drawItems(tft);
    drawPageIndicator(tft);
}

void MenuScreen::drawTitle(TFT_eSPI &tft)
{
    int y = STATUS_HEIGHT;
    tft.fillRect(0, y, TFT_WIDTH, TITLE_HEIGHT, PASSKEY_BLUE);
    tft.setTextColor(PASSKEY_WHITE, PASSKEY_BLUE);
    tft.setTextSize(2);
    tft.setCursor((TFT_WIDTH - tft.textWidth(title)) / 2, y + 4);
    tft.print(title);
}

void MenuScreen::drawItems(TFT_eSPI &tft)
{
    int startY  = STATUS_HEIGHT + TITLE_HEIGHT;
    tft.setTextSize(2);

    for (int i = 0; i < VISIBLE_LINES; i++) {
        int idx = i + scrollOffset;
        if (idx >= (int)items.size()) break;

        int y = startY + i * LINE_HEIGHT;

        // 绘制圆点指示器：选中=实心，未选中=空心
        if (idx == selectedIndex) {
            tft.fillCircle(10, y + LINE_HEIGHT / 2, 3, PASSKEY_BLUE);
            tft.setTextColor(PASSKEY_BLUE, TFT_BLACK);
        } else {
            tft.drawCircle(10, y + LINE_HEIGHT / 2, 3, PASSKEY_WHITE);
            tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
        }

        tft.setCursor(20, y + 2);
        tft.print(items[idx]);
    }
}

void MenuScreen::drawPageIndicator(TFT_eSPI &tft)
{
    if (items.empty()) return;

    int totalPages  = (items.size() + VISIBLE_LINES - 1) / VISIBLE_LINES;
    int currentPage = scrollOffset / VISIBLE_LINES + 1;

    tft.setTextColor(PASSKEY_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    char pageStr[8];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPage, totalPages);
    tft.setCursor(TFT_WIDTH - 30, TFT_HEIGHT - 14);
    tft.print(pageStr);
}
