#include "st7789v2_driver.h"
#include "../log_manager.h"

ST7789V2_Driver::ST7789V2_Driver() : spi(&SPI), currentBrightness(100) {
    // Initialize SPI pointer to default hardware SPI
}

void ST7789V2_Driver::writeCommand(uint8_t cmd) {
    digitalWrite(LCD_DC_PIN, LOW);
    spi->transfer(cmd);
    digitalWrite(LCD_DC_PIN, HIGH);  // Return to data mode
}

void ST7789V2_Driver::writeData(uint8_t data) {
    digitalWrite(LCD_DC_PIN, HIGH);
    spi->transfer(data);
}

void ST7789V2_Driver::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // 1.69" module uses 20px Y offset (panel always in portrait mode)
    const uint16_t y_offset = 20;
    const uint16_t x_offset = 0;

    writeCommand(ST7789_CASET);
    writeData((x0 + x_offset) >> 8);
    writeData((x0 + x_offset) & 0xFF);
    writeData((x1 + x_offset) >> 8);
    writeData((x1 + x_offset) & 0xFF);

    writeCommand(ST7789_RASET);
    writeData((y0 + y_offset) >> 8);
    writeData((y0 + y_offset) & 0xFF);
    writeData((y1 + y_offset) >> 8);
    writeData((y1 + y_offset) & 0xFF);

    writeCommand(ST7789_RAMWR);
}

void ST7789V2_Driver::rgb565ToBgr565(uint16_t* pixels, uint32_t count) {
    // LVGL blends colors in RGB space, but ST7789V2 display expects BGR
    // Swapping here fixes color artifacts on anti-aliased text edges
    for (uint32_t i = 0; i < count; i++) {
        uint16_t color = pixels[i];
        // Extract RGB components from RGB565
        uint16_t r = (color >> 11) & 0x1F;  // 5 bits red
        uint16_t g = (color >> 5) & 0x3F;   // 6 bits green
        uint16_t b = color & 0x1F;          // 5 bits blue
        // Recombine as BGR565
        pixels[i] = (b << 11) | (g << 5) | r;
    }
}

void ST7789V2_Driver::init() {
    Logger.logLine("ST7789V2: Initializing native driver");
    
    // Configure GPIO pins
    pinMode(LCD_CS_PIN, OUTPUT);
    pinMode(LCD_DC_PIN, OUTPUT);
    pinMode(LCD_RST_PIN, OUTPUT);
    pinMode(LCD_BL_PIN, OUTPUT);

    digitalWrite(LCD_CS_PIN, HIGH);
    digitalWrite(LCD_DC_PIN, HIGH);

    // Start backlight off until init completes
    analogWrite(LCD_BL_PIN, 0);

    // SPI (Mode 3, MSB first) per Waveshare sample
    spi->begin(LCD_SCK_PIN, -1, LCD_MOSI_PIN, LCD_CS_PIN);
    spi->setDataMode(SPI_MODE3);
    spi->setBitOrder(MSBFIRST);
    spi->setFrequency(60000000); // 60MHz - within ST7789 spec
    
    Logger.logLine("ST7789V2: SPI initialized at 60MHz");

    // Hardware reset (matches sample timing)
    digitalWrite(LCD_CS_PIN, LOW);
    delay(20);
    digitalWrite(LCD_RST_PIN, LOW);
    delay(20);
    digitalWrite(LCD_RST_PIN, HIGH);
    delay(120);

    // ST7789V2 init sequence (from Waveshare sample)
    // BGR mode (manufacturer default) - we swap RGB→BGR in pushColors
    // This allows LVGL to blend colors correctly in RGB space, fixing anti-aliasing artifacts
    writeCommand(0x36);
    writeData(0x00);  // BGR mode (bit 3 = 0), portrait orientation

    writeCommand(0x3A);
    writeData(0x05);

    writeCommand(0xB2);
    writeData(0x0B);
    writeData(0x0B);
    writeData(0x00);
    writeData(0x33);
    writeData(0x35);

    writeCommand(0xB7);
    writeData(0x11);

    writeCommand(0xBB);
    writeData(0x35);

    writeCommand(0xC0);
    writeData(0x2C);

    writeCommand(0xC2);
    writeData(0x01);

    writeCommand(0xC3);
    writeData(0x0D);

    writeCommand(0xC4);
    writeData(0x20);

    writeCommand(0xC6);
    writeData(0x13);

    writeCommand(0xD0);
    writeData(0xA4);
    writeData(0xA1);

    writeCommand(0xD6);
    writeData(0xA1);

    writeCommand(0xE0);
    writeData(0xF0);
    writeData(0x06);
    writeData(0x0B);
    writeData(0x0A);
    writeData(0x09);
    writeData(0x26);
    writeData(0x29);
    writeData(0x33);
    writeData(0x41);
    writeData(0x18);
    writeData(0x16);
    writeData(0x15);
    writeData(0x29);
    writeData(0x2D);

    writeCommand(0xE1);
    writeData(0xF0);
    writeData(0x04);
    writeData(0x08);
    writeData(0x08);
    writeData(0x07);
    writeData(0x03);
    writeData(0x28);
    writeData(0x32);
    writeData(0x40);
    writeData(0x3B);
    writeData(0x19);
    writeData(0x18);
    writeData(0x2A);
    writeData(0x2E);

    writeCommand(0xE4);
    writeData(0x25);
    writeData(0x00);
    writeData(0x00);

    writeCommand(0x21);  // Invert on

    writeCommand(0x11);  // Sleep out
    delay(120);

    writeCommand(0x29);  // Display on
    delay(20);
    
    // End init sequence transaction
    digitalWrite(LCD_CS_PIN, HIGH);

    Logger.logLine("ST7789V2: Display initialized");
    
    // Backlight on at saved brightness
    setBacklightBrightness(currentBrightness);
    
    Logger.logLinef("ST7789V2: Backlight set to %d%%", currentBrightness);
}

void ST7789V2_Driver::setRotation(uint8_t rotation) {
    // Rotation is handled by LVGL software rotation (set in display_manager.cpp)
    // ST7789V2 panel stays in portrait mode (240x280)
    // No action needed here - just log for clarity
    Logger.logLinef("ST7789V2: Rotation %d (handled by LVGL software rotation)", rotation);
}

void ST7789V2_Driver::setBacklight(bool on) {
    setBacklightBrightness(on ? 100 : 0);
}

void ST7789V2_Driver::setBacklightBrightness(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    currentBrightness = brightness;
    
    // Map 0-100% to 0-255 PWM duty cycle
    uint32_t dutyCycle = (brightness * 255) / 100;
    
    // Simple Arduino-style PWM (matches Waveshare sample behavior)
    analogWrite(LCD_BL_PIN, dutyCycle);
}

uint8_t ST7789V2_Driver::getBacklightBrightness() {
    return currentBrightness;
}

bool ST7789V2_Driver::hasBacklightControl() {
    return true;  // ST7789V2 driver always has PWM backlight control
}

void ST7789V2_Driver::applyDisplayFixes() {
    // ST7789V2 init sequence already handles all required configuration
    // No additional fixes needed (inversion, gamma already set in init)
    Logger.logLine("ST7789V2: Display fixes applied during init");
}

void ST7789V2_Driver::startWrite() {
    // Begin SPI transaction
    digitalWrite(LCD_CS_PIN, LOW);
}

void ST7789V2_Driver::endWrite() {
    // End SPI transaction
    digitalWrite(LCD_CS_PIN, HIGH);
}

void ST7789V2_Driver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    setWindow(x, y, x + w - 1, y + h - 1);
}

void ST7789V2_Driver::pushColors(uint16_t* data, uint32_t len, bool swap_bytes) {
    // NOTE: Removed RGB→BGR swap - this panel appears to be in RGB mode
    // (despite 0x36=0x00 configuration which should be BGR mode)
    // If colors appear correct without swap, the panel ignores the BGR bit
    // or has different register mapping than standard ST7789V2
    
    // Push pixel data
    digitalWrite(LCD_DC_PIN, HIGH);
    // CS already managed by startWrite/endWrite

    if (!data || len == 0) {
        return;
    }

    // LVGL typically provides RGB565 in CPU-endian order.
    // The panel expects MSB-first bytes on the wire.
    // To enable fast bulk SPI transfers, optionally swap bytes in-place,
    // perform a single write, then swap back.
    if (swap_bytes) {
        uint8_t* b = (uint8_t*)data;
        for (uint32_t i = 0; i < len; i++) {
            uint8_t tmp = b[i * 2 + 0];
            b[i * 2 + 0] = b[i * 2 + 1];
            b[i * 2 + 1] = tmp;
        }
    }

    // Bulk transfer is dramatically faster than per-byte spi->transfer() loops.
    // SPIClass::writeBytes is provided by the ESP32 Arduino core.
    spi->writeBytes((const uint8_t*)data, len * 2);

    if (swap_bytes) {
        uint8_t* b = (uint8_t*)data;
        for (uint32_t i = 0; i < len; i++) {
            uint8_t tmp = b[i * 2 + 0];
            b[i * 2 + 0] = b[i * 2 + 1];
            b[i * 2 + 1] = tmp;
        }
    }
}

// Configure LVGL display driver for ST7789V2-specific behavior
void ST7789V2_Driver::configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) {
    // ST7789V2 panel stays in portrait mode (240x280)
    // LVGL handles rotation via software rendering (sw_rotate)
    // This is more efficient than reconfiguring panel registers per frame
    
    switch (rotation) {
        case 1:  // 90° (landscape)
            drv->sw_rotate = 1;
            drv->rotated = LV_DISP_ROT_90;
            break;
        case 2:  // 180° (portrait inverted)
            drv->sw_rotate = 1;
            drv->rotated = LV_DISP_ROT_180;
            break;
        case 3:  // 270° (landscape inverted)
            drv->sw_rotate = 1;
            drv->rotated = LV_DISP_ROT_270;
            break;
        default:  // 0° (portrait)
            // No rotation needed
            break;
    }
}
