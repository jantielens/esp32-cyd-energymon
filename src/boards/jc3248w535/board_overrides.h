#ifndef BOARD_OVERRIDES_JC3248W535_H
#define BOARD_OVERRIDES_JC3248W535_H

// ============================================================================
// Guition JC3248W535 Board Configuration Overrides
// ============================================================================
// Manufacturer sample reference: /sample
// - Panel: AXS15231B, QSPI, 320x480
// - Touch: AXS15231B touch, I2C
//
// Notes:
// - The sample uses LVGL rotation (90°) and keeps the panel in portrait.
// - The sample defines a DC pin, but QSPI IO uses dc_gpio_num=-1.

// ============================================================================
// Display Configuration
// ============================================================================
// Enable display support on this board.
#define HAS_DISPLAY true

// ============================================================================
// Driver Selection (HAL)
// ============================================================================
// Display backend: Arduino_GFX (AXS15231B over QSPI)
// Touch backend:   AXS15231B (I2C)
// Select Arduino_GFX as the display HAL backend.
#define DISPLAY_DRIVER DISPLAY_DRIVER_ARDUINO_GFX
// Select AXS15231B as the touch HAL backend.
#define TOUCH_DRIVER   TOUCH_DRIVER_AXS15231B

// Physical panel resolution (portrait)
// Panel width in pixels.
#define DISPLAY_WIDTH  320
// Panel height in pixels.
#define DISPLAY_HEIGHT 480

// Software rotation via LVGL (1 = 90° landscape)
// UI rotation (LVGL).
#define DISPLAY_ROTATION 1

// LVGL buffer size - larger for 320x480 display
// Increase to reduce the number of flush chunks LVGL emits per frame.
// This buffer is allocated from PSRAM in DisplayManager.
// LVGL draw buffer size in pixels.
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 80)

// QSPI pins (from sample/esp_bsp.h)
// QSPI host peripheral.
#define LCD_QSPI_HOST SPI2_HOST
// QSPI chip select pin.
#define LCD_QSPI_CS   45
// QSPI pixel clock pin.
#define LCD_QSPI_PCLK 47
// QSPI data line 0 pin.
#define LCD_QSPI_D0   21
// QSPI data line 1 pin.
#define LCD_QSPI_D1   48
// QSPI data line 2 pin.
#define LCD_QSPI_D2   40
// QSPI data line 3 pin.
#define LCD_QSPI_D3   39
// QSPI reset pin (-1 = none).
#define LCD_QSPI_RST  -1

// Optional tear effect pin (from sample)
// Panel TE pin.
#define LCD_QSPI_TE   38

// Backlight
// Enable backlight control on this board.
#define HAS_BACKLIGHT true
// Backlight pin.
#define LCD_BL_PIN 1
// LEDC channel used for backlight PWM.
#define TFT_BACKLIGHT_PWM_CHANNEL 1

// ============================================================================
// Touch Configuration
// ============================================================================
// Enable touch support on this board.
#define HAS_TOUCH true

// Touch is I2C (from sample/esp_bsp.h)
// I2C controller index.
#define TOUCH_I2C_PORT 0    // I2C_NUM_0
// I2C SCL pin.
#define TOUCH_I2C_SCL  8
// I2C SDA pin.
#define TOUCH_I2C_SDA  4
// I2C frequency (Hz).
#define TOUCH_I2C_FREQ_HZ 400000

#define TOUCH_RST -1
#define TOUCH_INT -1

// Touch calibration (from sample: dispcfg.h)
// Touch calibration: X minimum.
#define TOUCH_CAL_X_MIN 12
// Touch calibration: X maximum.
#define TOUCH_CAL_X_MAX 310
// Touch calibration: Y minimum.
#define TOUCH_CAL_Y_MIN 14
// Touch calibration: Y maximum.
#define TOUCH_CAL_Y_MAX 461

// ============================================================================
// Image API
// ============================================================================
// Enable Image API on this board.
#define HAS_IMAGE_API true

// PSRAM board: allow larger full-image uploads than the global default.
// Worst-case (high-entropy) 480x320 JPEGs at quality 95 can exceed 100KB.
// Max JPEG bytes accepted for full image uploads.
#define IMAGE_API_MAX_SIZE_BYTES (300 * 1024)

#endif // BOARD_OVERRIDES_JC3248W535_H
