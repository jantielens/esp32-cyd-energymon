/*
 * ST7789V2 Display Driver (Native SPI)
 * 
 * Optimized driver for 1.69" IPS LCD (240x280 ST7789V2).
 * Direct SPI control for maximum performance (60MHz).
 * 
 * Features:
 * - BGR565 color swap for proper anti-aliasing
 * - 20px Y-offset handling for 1.69" panel
 * - PWM backlight control (0-100%)
 * - Landscape mode via LVGL software rotation
 */

#ifndef ST7789V2_DRIVER_H
#define ST7789V2_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <SPI.h>

// ST7789V2 commands
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C

class ST7789V2_Driver : public DisplayDriver {
private:
    SPIClass* spi;
    uint8_t currentBrightness;
    
    // Low-level SPI communication
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    
    // RGB565 to BGR565 color swap for anti-aliasing fix
    void rgb565ToBgr565(uint16_t* pixels, uint32_t count);
    
public:
    ST7789V2_Driver();
    ~ST7789V2_Driver() override = default;
    
    void init() override;
    void setRotation(uint8_t rotation) override;
    int width() override { return (int)DISPLAY_WIDTH; }
    int height() override { return (int)DISPLAY_HEIGHT; }
    void setBacklight(bool on) override;
    void setBacklightBrightness(uint8_t brightness) override;
    uint8_t getBacklightBrightness() override;
    bool hasBacklightControl() override;
    void applyDisplayFixes() override;
    
    // LVGL configuration - ST7789V2 requires software rotation
    void configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) override;
    
    void startWrite() override;
    void endWrite() override;
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;
};

#endif // ST7789V2_DRIVER_H
