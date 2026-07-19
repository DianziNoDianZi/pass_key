/**
 * @file AboutScreen.h
 * @brief 设备信息页面 — 固件版本、设备 ID、信号强度、公钥指纹
 */

#ifndef ABOUT_SCREEN_H
#define ABOUT_SCREEN_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Screen.h"

#define FW_VERSION "0.1.0"

class AboutScreen : public Screen
{
public:
    AboutScreen();
    ~AboutScreen();

    virtual const char *getName() const override { return "About"; }
    virtual void onActivate() override;
    virtual void onDraw(TFT_eSPI &tft) override;
};

#endif // ABOUT_SCREEN_H
