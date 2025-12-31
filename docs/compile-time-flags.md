# Compile-Time Flags Report

This document is a template. Sections marked with `COMPILE_FLAG_REPORT` markers are auto-updated by `tools/compile_flags_report.py`.

## How to update

- Update this doc:
  - `python3 tools/compile_flags_report.py md --out docs/compile-time-flags.md`
- Print active flags during a build (example):
  - `python3 tools/compile_flags_report.py build --board cyd-v2`

## Flags (generated)

<!-- BEGIN COMPILE_FLAG_REPORT:FLAGS -->
Total flags: 78

### Features (HAS_*)

- **HAS_BACKLIGHT** default: `false` — Enable backlight control (typically via PWM).
- **HAS_BUILTIN_LED** default: `false` — Enable built-in status LED support.
- **HAS_DISPLAY** default: `false` — Enable display + LVGL UI support.
- **HAS_IMAGE_API** default: `false` — Enable Image API endpoints (JPEG upload/download/display).
- **HAS_MQTT** default: `true` — Enable MQTT and Home Assistant integration.
- **HAS_TOUCH** default: `false` — Enable touch input support.

### Selectors (*_DRIVER)

- **DISPLAY_DRIVER** default: `DISPLAY_DRIVER_TFT_ESPI` (values: DISPLAY_DRIVER_ARDUINO_GFX, DISPLAY_DRIVER_ESP_PANEL, DISPLAY_DRIVER_ST7789V2, DISPLAY_DRIVER_TFT_ESPI) — Select the display HAL backend (one of the DISPLAY_DRIVER_* constants).
- **TOUCH_DRIVER** default: `TOUCH_DRIVER_XPT2046` (values: TOUCH_DRIVER_AXS15231B, TOUCH_DRIVER_CST816S_ESP_PANEL, TOUCH_DRIVER_XPT2046) — Select the touch HAL backend (one of the TOUCH_DRIVER_* constants).

### Hardware (Geometry)

- **DISPLAY_HEIGHT** default: `(no default)` — Panel height in pixels.
- **DISPLAY_ROTATION** default: `(no default)` — UI rotation (LVGL).
- **DISPLAY_WIDTH** default: `(no default)` — Panel width in pixels.

### Hardware (Pins)

- **LCD_BL_PIN** default: `(no default)` — LCD backlight pin.
- **LCD_CS_PIN** default: `(no default)` — LCD SPI CS pin.
- **LCD_DC_PIN** default: `(no default)` — LCD SPI DC pin.
- **LCD_MOSI_PIN** default: `(no default)` — LCD SPI MOSI pin.
- **LCD_QSPI_CS** default: `(no default)` — QSPI chip select pin.
- **LCD_QSPI_D0** default: `(no default)` — QSPI data line 0 pin.
- **LCD_QSPI_D1** default: `(no default)` — QSPI data line 1 pin.
- **LCD_QSPI_D2** default: `(no default)` — QSPI data line 2 pin.
- **LCD_QSPI_D3** default: `(no default)` — QSPI data line 3 pin.
- **LCD_QSPI_PCLK** default: `(no default)` — QSPI pixel clock pin.
- **LCD_QSPI_RST** default: `(no default)` — QSPI reset pin (-1 = none).
- **LCD_QSPI_TE** default: `(no default)` — Panel TE pin.
- **LCD_RST_PIN** default: `(no default)` — LCD reset pin.
- **LCD_SCK_PIN** default: `(no default)` — LCD SPI SCK pin.
- **LED_PIN** default: `2` — GPIO for the built-in LED (only used when HAS_BUILTIN_LED is true).
- **TFT_BL** default: `(no default)` — TFT_eSPI: backlight pin.
- **TFT_CS** default: `(no default)` — TFT_eSPI: CS pin.
- **TFT_DC** default: `(no default)` — TFT_eSPI: DC pin.
- **TFT_MISO** default: `(no default)` — TFT_eSPI: MISO pin.
- **TFT_MOSI** default: `(no default)` — TFT_eSPI: MOSI pin.
- **TFT_RST** default: `(no default)` — TFT_eSPI: RST pin (-1 = none).
- **TFT_SCK** default: `(no default)` — QSPI clock pin.
- **TFT_SCLK** default: `(no default)` — TFT_eSPI: SCLK pin.
- **TFT_SDA0** default: `(no default)` — QSPI data line 0 pin.
- **TFT_SDA1** default: `(no default)` — QSPI data line 1 pin.
- **TFT_SDA2** default: `(no default)` — QSPI data line 2 pin.
- **TFT_SDA3** default: `(no default)` — QSPI data line 3 pin.
- **TOUCH_CS** default: `(no default)` — TFT_eSPI touch: CS pin.
- **TOUCH_I2C_SCL** default: `(no default)` — I2C SCL pin.
- **TOUCH_I2C_SDA** default: `(no default)` — I2C SDA pin.
- **TOUCH_INT** default: `(no default)` — Touch interrupt pin.
- **TOUCH_IRQ** default: `(no default)` — TFT_eSPI touch: IRQ pin (optional).
- **TOUCH_MISO** default: `(no default)` — TFT_eSPI touch: MISO pin.
- **TOUCH_MOSI** default: `(no default)` — TFT_eSPI touch: MOSI pin.
- **TOUCH_RST** default: `(no default)` — Touch reset pin.
- **TOUCH_SCLK** default: `(no default)` — TFT_eSPI touch: SCLK pin.
- **XPT2046_CLK** default: `(no default)` — XPT2046 CLK pin.
- **XPT2046_CS** default: `(no default)` — XPT2046 CS pin.
- **XPT2046_IRQ** default: `(no default)` — XPT2046 IRQ pin.
- **XPT2046_MISO** default: `(no default)` — XPT2046 MISO pin.
- **XPT2046_MOSI** default: `(no default)` — XPT2046 MOSI pin.

### Limits & Tuning

- **IMAGE_API_DECODE_HEADROOM_BYTES** default: `(50 * 1024)` — Extra free RAM required for decoding (bytes).
- **IMAGE_API_DEFAULT_TIMEOUT_MS** default: `10000` — Default image display timeout in milliseconds.
- **IMAGE_API_MAX_SIZE_BYTES** default: `(100 * 1024)` — Max bytes accepted for full image uploads (JPEG).
- **IMAGE_API_MAX_TIMEOUT_MS** default: `(86400UL * 1000UL)` — Maximum image display timeout in milliseconds.
- **IMAGE_STRIP_BATCH_MAX_ROWS** default: `16` — Max rows batched per LCD transaction when decoding JPEG strips.
- **LVGL_BUFFER_PREFER_INTERNAL** default: `false` — Prefer internal RAM over PSRAM for LVGL draw buffer allocation.
- **LVGL_BUFFER_SIZE** default: `(DISPLAY_WIDTH * 10)` — LVGL draw buffer size in pixels (larger = faster, more RAM).
- **LVGL_TICK_PERIOD_MS** default: `5` — LVGL tick period in milliseconds.
- **TFT_SPI_FREQUENCY** default: `(no default)` — TFT SPI clock frequency.
- **TFT_SPI_FREQ_HZ** default: `(no default)` — QSPI clock frequency (Hz).
- **TOUCH_I2C_FREQ_HZ** default: `(no default)` — I2C frequency (Hz).
- **WIFI_MAX_ATTEMPTS** default: `3` — Maximum WiFi connection attempts at boot before falling back.

### Other

- **DISPLAY_COLOR_ORDER_BGR** default: `(no default)` — Panel uses BGR byte order.
- **DISPLAY_DRIVER_ILI9341_2** default: `(no default)` — Use the ILI9341_2 controller setup in TFT_eSPI.
- **DISPLAY_INVERSION_ON** default: `(no default)` — Enable display inversion (panel-specific).
- **DISPLAY_NEEDS_GAMMA_FIX** default: `(no default)` — Apply gamma correction fix for this panel variant.
- **LCD_QSPI_HOST** default: `(no default)` — QSPI host peripheral.
- **LED_ACTIVE_HIGH** default: `true` — LED polarity: true if HIGH turns the LED on.
- **PROJECT_DISPLAY_NAME** default: `"ESP32 Device"` — Human-friendly project name used in the web UI and device name (can be set by build system).
- **TFT_BACKLIGHT_ON** default: `(no default)` — Backlight "on" level.
- **TFT_BACKLIGHT_PWM_CHANNEL** default: `0` — LEDC channel used for backlight PWM.
- **TOUCH_CAL_X_MAX** default: `(no default)` — Touch calibration: X maximum.
- **TOUCH_CAL_X_MIN** default: `(no default)` — Touch calibration: X minimum.
- **TOUCH_CAL_Y_MAX** default: `(no default)` — Touch calibration: Y maximum.
- **TOUCH_CAL_Y_MIN** default: `(no default)` — Touch calibration: Y minimum.
- **TOUCH_I2C_PORT** default: `(no default)` — I2C controller index.
<!-- END COMPILE_FLAG_REPORT:FLAGS -->

## Board Matrix: Features (generated)

Legend: ✅ = enabled/true, blank = disabled/false, ? = unknown/undefined

<!-- BEGIN COMPILE_FLAG_REPORT:MATRIX_FEATURES -->
| board-name | HAS_BACKLIGHT | HAS_BUILTIN_LED | HAS_DISPLAY | HAS_IMAGE_API | HAS_MQTT | HAS_TOUCH |
| --- | --- | --- | --- | --- | --- | --- |
| esp32-nodisplay |  |  |  |  | ✅ |  |
| cyd-v2 | ✅ |  | ✅ | ✅ | ✅ | ✅ |
| esp32c3-waveshare-169-st7789v2 | ✅ | ✅ | ✅ | ✅ | ✅ |  |
| jc3248w535 | ✅ |  | ✅ | ✅ | ✅ | ✅ |
| jc3636w518 | ✅ |  | ✅ | ✅ | ✅ | ✅ |
<!-- END COMPILE_FLAG_REPORT:MATRIX_FEATURES -->

## Board Matrix: Selectors (generated)

<!-- BEGIN COMPILE_FLAG_REPORT:MATRIX_SELECTORS -->
| board-name | DISPLAY_DRIVER | TOUCH_DRIVER |
| --- | --- | --- |
| esp32-nodisplay | — | — |
| cyd-v2 | DISPLAY_DRIVER_TFT_ESPI | TOUCH_DRIVER_XPT2046 |
| esp32c3-waveshare-169-st7789v2 | DISPLAY_DRIVER_ST7789V2 | — |
| jc3248w535 | DISPLAY_DRIVER_ARDUINO_GFX | TOUCH_DRIVER_AXS15231B |
| jc3636w518 | DISPLAY_DRIVER_ESP_PANEL | TOUCH_DRIVER_CST816S_ESP_PANEL |
<!-- END COMPILE_FLAG_REPORT:MATRIX_SELECTORS -->

## Usage Map (preprocessor only, generated)

<!-- BEGIN COMPILE_FLAG_REPORT:USAGE -->
- **HAS_BACKLIGHT**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/display_manager.cpp
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/tft_espi_driver.cpp
- **HAS_BUILTIN_LED**
  - src/app/app.ino
  - src/app/board_config.h
- **HAS_DISPLAY**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/config_manager.h
  - src/app/display_drivers.cpp
  - src/app/display_manager.cpp
  - src/app/image_api.cpp
  - src/app/lvgl_jpeg_decoder.cpp
  - src/app/lvgl_jpeg_decoder.h
  - src/app/screen_saver_manager.cpp
  - src/app/screen_saver_manager.h
  - src/app/screens.cpp
  - src/app/screens/lvgl_image_screen.cpp
  - src/app/screens/lvgl_image_screen.h
  - src/app/touch_manager.cpp
  - src/app/web_portal.cpp
- **HAS_IMAGE_API**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/display_manager.cpp
  - src/app/display_manager.h
  - src/app/image_api.cpp
  - src/app/image_api.h
  - src/app/jpeg_preflight.cpp
  - src/app/jpeg_preflight.h
  - src/app/lv_conf.h
  - src/app/lvgl_jpeg_decoder.cpp
  - src/app/lvgl_jpeg_decoder.h
  - src/app/screens.cpp
  - src/app/screens/direct_image_screen.cpp
  - src/app/screens/direct_image_screen.h
  - src/app/screens/lvgl_image_screen.cpp
  - src/app/screens/lvgl_image_screen.h
  - src/app/strip_decoder.cpp
  - src/app/strip_decoder.h
  - src/app/web_portal.cpp
  - src/app/web_portal.h
- **HAS_MQTT**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/ha_discovery.cpp
  - src/app/ha_discovery.h
  - src/app/mqtt_manager.cpp
  - src/app/mqtt_manager.h
- **HAS_TOUCH**
  - src/app/app.ino
  - src/app/board_config.h
  - src/app/config_manager.cpp
  - src/app/screen_saver_manager.cpp
  - src/app/touch_drivers.cpp
  - src/app/touch_manager.cpp
  - src/app/touch_manager.h
- **DISPLAY_DRIVER**
  - src/app/board_config.h
  - src/app/display_drivers.cpp
  - src/app/display_manager.cpp
- **TOUCH_DRIVER**
  - src/app/board_config.h
  - src/app/touch_drivers.cpp
  - src/app/touch_manager.cpp
- **DISPLAY_INVERSION_ON**
  - src/app/drivers/tft_espi_driver.cpp
- **DISPLAY_NEEDS_GAMMA_FIX**
  - src/app/drivers/tft_espi_driver.cpp
- **DISPLAY_ROTATION**
  - src/app/touch_manager.cpp
- **IMAGE_API_DECODE_HEADROOM_BYTES**
  - src/app/board_config.h
- **IMAGE_API_DEFAULT_TIMEOUT_MS**
  - src/app/board_config.h
- **IMAGE_API_MAX_SIZE_BYTES**
  - src/app/board_config.h
- **IMAGE_API_MAX_TIMEOUT_MS**
  - src/app/board_config.h
- **IMAGE_STRIP_BATCH_MAX_ROWS**
  - src/app/board_config.h
- **LCD_BL_PIN**
  - src/app/drivers/arduino_gfx_driver.cpp
- **LCD_QSPI_CS**
  - src/app/drivers/arduino_gfx_driver.cpp
- **LED_ACTIVE_HIGH**
  - src/app/board_config.h
- **LED_PIN**
  - src/app/board_config.h
- **LVGL_BUFFER_PREFER_INTERNAL**
  - src/app/board_config.h
- **LVGL_BUFFER_SIZE**
  - src/app/board_config.h
- **LVGL_TICK_PERIOD_MS**
  - src/app/board_config.h
- **PROJECT_DISPLAY_NAME**
  - src/app/board_config.h
- **TFT_BACKLIGHT_ON**
  - src/app/drivers/arduino_gfx_driver.cpp
  - src/app/drivers/tft_espi_driver.cpp
- **TFT_BACKLIGHT_PWM_CHANNEL**
  - src/app/board_config.h
- **TFT_BL**
  - src/app/drivers/tft_espi_driver.cpp
- **TFT_SPI_FREQ_HZ**
  - src/app/drivers/esp_panel_st77916_driver.cpp
- **TOUCH_CAL_X_MAX**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_X_MIN**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_Y_MAX**
  - src/app/touch_manager.cpp
- **TOUCH_CAL_Y_MIN**
  - src/app/touch_manager.cpp
- **TOUCH_I2C_SCL**
  - src/app/drivers/axs15231b_touch_driver.cpp
- **TOUCH_INT**
  - src/app/drivers/axs15231b_touch_driver.cpp
- **TOUCH_MISO**
  - src/app/drivers/xpt2046_driver.cpp
- **TOUCH_MOSI**
  - src/app/drivers/xpt2046_driver.cpp
- **TOUCH_SCLK**
  - src/app/drivers/xpt2046_driver.cpp
- **WIFI_MAX_ATTEMPTS**
  - src/app/board_config.h
<!-- END COMPILE_FLAG_REPORT:USAGE -->
