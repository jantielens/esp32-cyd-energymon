/*
 * Arduino_GFX Display Driver
 * 
 * Wrapper for moononournation's Arduino_GFX library.
 * Supports QSPI displays like AXS15231B (JC3248W535).
 * 
 * This driver wraps Arduino_GFX + Arduino_Canvas to match our DisplayDriver HAL.
 * It translates LVGL flush callbacks to canvas operations, similar to the sample's approach.
 */

#ifndef ARDUINO_GFX_DRIVER_H
#define ARDUINO_GFX_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <Arduino_GFX_Library.h>

class Arduino_GFX_Driver : public DisplayDriver {
private:
    Arduino_DataBus* bus;
    Arduino_GFX* gfx;
    Arduino_Canvas* canvas;
    uint8_t currentBrightness;  // Current brightness level (0-100%)
    bool backlightPwmAttached;
    uint16_t displayWidth;
    uint16_t displayHeight;
    uint8_t displayRotation;
    
    // Current drawing area (set by setAddrWindow, used by pushColors)
    int16_t currentX, currentY;
    uint16_t currentW, currentH;
    
public:
    Arduino_GFX_Driver();
    ~Arduino_GFX_Driver() override;
    
    void init() override;
    void setRotation(uint8_t rotation) override;
    int width() override;
    int height() override;
    void setBacklight(bool on) override;
    void setBacklightBrightness(uint8_t brightness) override;  // 0-100%
    uint8_t getBacklightBrightness() override;
    bool hasBacklightControl() override;
    void applyDisplayFixes() override;
    
    void startWrite() override;
    void endWrite() override;
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;

    RenderMode renderMode() const override;
    void present() override;  // Flush canvas buffer to physical display
    
    // Override LVGL configuration to use software rotation
    void configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) override;
};

#endif // ARDUINO_GFX_DRIVER_H
