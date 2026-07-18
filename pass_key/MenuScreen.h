/**
 * @file MenuScreen.h
 * @brief 菜单列表屏幕 — 支持滚动、循环选择和页码指示
 */

#ifndef MENU_SCREEN_H
#define MENU_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "Screen.h"

class MenuScreen : public Screen {
public:
    MenuScreen(const char *title);
    ~MenuScreen();

    void addItem(const char *text);
    void clearItems();
    void setItems(const std::vector<const char *> &items);

    int getSelectedIndex() const;
    const char *getSelectedItem() const;

    virtual void onActivate() override;
    virtual void onDeactivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onUpdate() override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    static const int VISIBLE_LINES  = 7;
    static const int TITLE_HEIGHT   = 24;
    static const int LINE_HEIGHT    = 20;
    static const int STATUS_HEIGHT  = 16;

    const char *title;
    std::vector<const char *> items;
    int selectedIndex;
    int scrollOffset;

    void drawTitle(TFT_eSPI &tft);
    void drawItems(TFT_eSPI &tft);
    void drawPageIndicator(TFT_eSPI &tft);
};

#endif // MENU_SCREEN_H
