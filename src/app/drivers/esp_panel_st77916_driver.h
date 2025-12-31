#ifndef ESP_PANEL_ST77916_DRIVER_H
#define ESP_PANEL_ST77916_DRIVER_H

#include "../display_driver.h"

// ESP32_Display_Panel provides backward-compatible ESP_Panel_Library.h
#include <ESP_Panel_Library.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ESPPanel_ST77916_Driver : public DisplayDriver {
public:
    ESPPanel_ST77916_Driver();
    ~ESPPanel_ST77916_Driver() override;

    void init() override;

    void setRotation(uint8_t rotation) override;
    int width() override { return (int)DISPLAY_WIDTH; }
    int height() override { return (int)DISPLAY_HEIGHT; }
    void setBacklight(bool on) override;

    void setBacklightBrightness(uint8_t brightness) override;
    uint8_t getBacklightBrightness() override;
    bool hasBacklightControl() override;

    void applyDisplayFixes() override;

    void startWrite() override;
    void endWrite() override;
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;

private:
    ESP_PanelBacklight* backlight;
    ESP_PanelLcd* lcd;

    SemaphoreHandle_t busMutex;

    uint8_t currentBrightness;
    bool backlightIsOn;

    int16_t currentX;
    int16_t currentY;
    uint16_t currentW;
    uint16_t currentH;

    uint16_t* swapBuf;
    uint32_t swapBufCapacityPixels;
};

#endif // ESP_PANEL_ST77916_DRIVER_H
