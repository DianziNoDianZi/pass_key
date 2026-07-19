/**
 * @file SecurityScreen.h
 * @brief 安全设置页面 — 查看公钥、认证记录、重新生成密钥
 */

#ifndef SECURITY_SCREEN_H
#define SECURITY_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

class DisplayManager;

class SecurityScreen : public Screen
{
public:
    SecurityScreen(DisplayManager *displayMgr);
    ~SecurityScreen();

    virtual const char *getName() const override { return "Security"; }
    virtual void onActivate() override;
    virtual void onButtonPress(uint8_t button) override;
    virtual void onDraw(TFT_eSPI &tft) override;

private:
    DisplayManager *displayMgr;
    int selectedIndex;

    enum SecItem {
        ITEM_PUBKEY,
        ITEM_AUTH_LOG,
        ITEM_REGENERATE,
        ITEM_COUNT
    };

    String pubKeyPreview;  // 公钥指纹摘要
    void showToast(const String &msg);
};

#endif // SECURITY_SCREEN_H
