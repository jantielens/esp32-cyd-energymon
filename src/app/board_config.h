#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ============================================================================
// Board Configuration - Two-Phase Include Pattern
// ============================================================================
// This file provides default configuration for all boards using a two-phase
// include pattern:
//
// Phase 1: Load board-specific overrides first (if they exist)
// Phase 2: Define defaults using #ifndef guards (only if not already defined)
//
// To customize for a specific board, create: src/boards/[board-name]/board_overrides.h
// The build system will automatically detect and include board-specific overrides.
//
// Example board-specific override:
//   src/boards/esp32c3-waveshare-169-st7789v2/board_overrides.h

// ============================================================================
// Phase 1: Load Board-Specific Overrides
// ============================================================================
// build.sh defines BOARD_HAS_OVERRIDE when a board override directory exists
// and adds that directory to the include path. Board-specific settings are
// loaded first so they can override the defaults below.

#ifdef BOARD_HAS_OVERRIDE
#include "board_overrides.h"
#endif

// ============================================================================
// Project Branding
// ============================================================================
// Human-friendly project name used in the web UI and device name (can be set by build system).
#ifndef PROJECT_DISPLAY_NAME
#define PROJECT_DISPLAY_NAME "ESP32 Device"
#endif

// ============================================================================
// Phase 2: Default Hardware Capabilities
// ============================================================================
// These defaults are only applied if not already defined by board overrides.

// Enable built-in status LED support.
#ifndef HAS_BUILTIN_LED
#define HAS_BUILTIN_LED false
#endif

// Enable MQTT and Home Assistant integration.
#ifndef HAS_MQTT
#define HAS_MQTT true
#endif

// GPIO for the built-in LED (only used when HAS_BUILTIN_LED is true).
#ifndef LED_PIN
#define LED_PIN 2  // Common GPIO for ESP32 boards
#endif

// LED polarity: true if HIGH turns the LED on.
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH true  // true = HIGH turns LED on, false = LOW turns LED on
#endif

// ============================================================================
// Default WiFi Configuration
// ============================================================================

// Maximum WiFi connection attempts at boot before falling back.
#ifndef WIFI_MAX_ATTEMPTS
#define WIFI_MAX_ATTEMPTS 3
#endif

// ============================================================================
// Additional Default Configuration Settings
// ============================================================================
// Add new hardware features here using #ifndef guards to allow board-specific
// overrides.
//
// Usage Pattern in Application Code:
//   1. Define capabilities in board_overrides.h: #define HAS_BUTTON true
//   2. Use conditional compilation in app.ino:
//
//      #if HAS_BUTTON
//        pinMode(BUTTON_PIN, INPUT_PULLUP);
//        // Button-specific code only compiled when HAS_BUTTON is true
//      #endif
//
// Examples:
//
// Button:
// #ifndef HAS_BUTTON
// #define HAS_BUTTON false
// #endif
//
// #ifndef BUTTON_PIN
// #define BUTTON_PIN 0
// #endif
//
// Battery Monitor:
// #ifndef HAS_BATTERY_MONITOR
// #define HAS_BATTERY_MONITOR false
// #endif
//
// #ifndef BATTERY_ADC_PIN
// #define BATTERY_ADC_PIN 34
// #endif
//
// Display:
// #ifndef HAS_DISPLAY
// #define HAS_DISPLAY false
// #endif

// ============================================================================
// Web Portal Health Widget
// ============================================================================
// How often the web UI polls /api/health.
#ifndef HEALTH_POLL_INTERVAL_MS
#define HEALTH_POLL_INTERVAL_MS 5000
#endif

// How much client-side history (sparklines) to keep.
#ifndef HEALTH_HISTORY_SECONDS
#define HEALTH_HISTORY_SECONDS 300
#endif

// ============================================================================
// Optional: Device-side Health History (/api/health/history)
// ============================================================================
// When enabled, firmware keeps a fixed-size ring buffer for sparklines so the
// portal can render history even when no client was connected.
// Default: enabled.
#ifndef HEALTH_HISTORY_ENABLED
#define HEALTH_HISTORY_ENABLED 1
#endif

// Sampling cadence for the device-side history (ms). Default aligns with UI poll.
#ifndef HEALTH_HISTORY_PERIOD_MS
#define HEALTH_HISTORY_PERIOD_MS 5000
#endif

#if HEALTH_HISTORY_ENABLED
// Derived number of samples.
#ifndef HEALTH_HISTORY_SAMPLES
#define HEALTH_HISTORY_SAMPLES ((HEALTH_HISTORY_SECONDS * 1000) / HEALTH_HISTORY_PERIOD_MS)
#endif

// Guardrails (must compile in both C and C++ translation units).
#if (HEALTH_HISTORY_PERIOD_MS < 1000)
#error HEALTH_HISTORY_PERIOD_MS too small
#endif

#if (((HEALTH_HISTORY_SECONDS * 1000UL) % (HEALTH_HISTORY_PERIOD_MS)) != 0)
#error HEALTH_HISTORY_SECONDS must be divisible by HEALTH_HISTORY_PERIOD_MS
#endif

#if (HEALTH_HISTORY_SAMPLES < 10)
#error HEALTH_HISTORY_SAMPLES too small
#endif

#if (HEALTH_HISTORY_SAMPLES > 600)
#error HEALTH_HISTORY_SAMPLES too large
#endif
#endif

// ============================================================================
// Display Configuration
// ============================================================================
// Enable display + LVGL UI support.
#ifndef HAS_DISPLAY
#define HAS_DISPLAY false
#endif

// Display driver selection
// Available drivers:
//   DISPLAY_DRIVER_TFT_ESPI (1) - Bodmer's TFT_eSPI (supports ILI9341, ST7789, etc.)
//   DISPLAY_DRIVER_ST7789V2 (2) - Native ST7789V2 driver (1.69" IPS LCD 240x280)
//   DISPLAY_DRIVER_LOVYANGFX (3) - LovyanGFX (future support)
//   DISPLAY_DRIVER_ARDUINO_GFX (4) - Arduino_GFX (QSPI displays like AXS15231B)
#define DISPLAY_DRIVER_TFT_ESPI 1
#define DISPLAY_DRIVER_ST7789V2 2
#define DISPLAY_DRIVER_LOVYANGFX 3
#define DISPLAY_DRIVER_ARDUINO_GFX 4
#define DISPLAY_DRIVER_ESP_PANEL 5

// Select the display HAL backend (one of the DISPLAY_DRIVER_* constants).
#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI  // Default to TFT_eSPI
#endif

// ============================================================================
// LVGL Configuration
// ============================================================================
// LVGL draw buffer size in pixels (larger = faster, more RAM).
#ifndef LVGL_BUFFER_SIZE
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)  // 10 lines buffer
#endif

// LVGL tick period in milliseconds.
#ifndef LVGL_TICK_PERIOD_MS
#define LVGL_TICK_PERIOD_MS 5
#endif

// ============================================================================
// Backlight Configuration
// ============================================================================
// Enable backlight control (typically via PWM).
#ifndef HAS_BACKLIGHT
#define HAS_BACKLIGHT false
#endif

// LEDC channel used for backlight PWM.
#ifndef TFT_BACKLIGHT_PWM_CHANNEL
#define TFT_BACKLIGHT_PWM_CHANNEL 0  // LEDC channel for PWM control
#endif

// ============================================================================
// Touch Configuration
// ============================================================================
// Enable touch input support.
#ifndef HAS_TOUCH
#define HAS_TOUCH false
#endif

// Touch driver selection
// Available drivers:
//   TOUCH_DRIVER_XPT2046 (1) - XPT2046 resistive touch (via TFT_eSPI)
//   TOUCH_DRIVER_FT6236 (2) - FT6236 capacitive touch (future support)
//   TOUCH_DRIVER_AXS15231B (3) - AXS15231B capacitive touch (I2C, JC3248W535)
#define TOUCH_DRIVER_XPT2046 1
#define TOUCH_DRIVER_FT6236 2
#define TOUCH_DRIVER_AXS15231B 3
#define TOUCH_DRIVER_CST816S_ESP_PANEL 4

// Prefer allocating LVGL draw buffer in internal RAM before PSRAM.
// Default: false (keeps historical PSRAM-first behavior; boards can override).
// Prefer internal RAM over PSRAM for LVGL draw buffer allocation.
#ifndef LVGL_BUFFER_PREFER_INTERNAL
#define LVGL_BUFFER_PREFER_INTERNAL false
#endif

// ESP_Panel (QSPI) display driver: prefer internal RAM for the byte-swap buffer.
// Default: true. Some panel buses are more reliable with internal/DMA-capable buffers.
#ifndef ESP_PANEL_SWAPBUF_PREFER_INTERNAL
#define ESP_PANEL_SWAPBUF_PREFER_INTERNAL true
#endif

// ============================================================================
// Diagnostics / Telemetry
// ============================================================================
// Low-memory tripwire: when the internal heap minimum free (bytes) drops below this
// threshold, dump per-task stack watermarks once.
// Default: disabled (0). Enable per-board if you want early warning logs.
#ifndef MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES
#define MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES 0
#endif

// How often to check tripwires from the main loop.
#ifndef MEMORY_TRIPWIRE_CHECK_INTERVAL_MS
#define MEMORY_TRIPWIRE_CHECK_INTERVAL_MS 5000
#endif

// ============================================================================
// Web Portal
// ============================================================================
// Max JSON body size accepted by /api/config.
#ifndef WEB_PORTAL_CONFIG_MAX_JSON_BYTES
#define WEB_PORTAL_CONFIG_MAX_JSON_BYTES 4096
#endif

// Timeout for an incomplete /api/config upload (ms) before freeing the buffer.
#ifndef WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS
#define WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS 5000
#endif

// Select the touch HAL backend (one of the TOUCH_DRIVER_* constants).
#ifndef TOUCH_DRIVER
#define TOUCH_DRIVER TOUCH_DRIVER_XPT2046  // Default to XPT2046
#endif

// ============================================================================
// Image API Configuration
// ============================================================================
// Enable web-based image upload and display functionality
// Requires: HAS_DISPLAY = true
//
// Template note (bloat control):
// - When HAS_IMAGE_API is enabled, the firmware also compiles an optional LVGL-based image screen
//   (screen id: "lvgl_image") which can display downloaded/uploaded JPEGs via LVGL (lv_img).
// - LVGL image widget + zoom support are enabled via src/app/lv_conf.h when HAS_IMAGE_API is true.
// - If you want Image API without the LVGL image widget/zoom code, you can override LVGL config
//   by setting LV_USE_IMG=0 and/or LV_USE_IMG_TRANSFORM=0 in src/app/lv_conf.h (or via build flags).
// Adds REST endpoints:
//   POST   /api/display/image          - Upload full JPEG (deferred decode)
//   DELETE /api/display/image          - Dismiss current image
//   POST   /api/display/image/strips   - Upload JPEG strip (synchronous)
//   POST   /api/display/image_url      - Download JPEG via HTTP/HTTPS (deferred)
// Enable Image API endpoints (JPEG upload/download/display).
#ifndef HAS_IMAGE_API
#define HAS_IMAGE_API false
#endif

// Image API configuration (only relevant when HAS_IMAGE_API is true)
// Max bytes accepted for full image uploads (JPEG).
#ifndef IMAGE_API_MAX_SIZE_BYTES
#define IMAGE_API_MAX_SIZE_BYTES (100 * 1024)  // 100KB max for full image upload
#endif

// Extra free RAM required for decoding (bytes).
#ifndef IMAGE_API_DECODE_HEADROOM_BYTES
#define IMAGE_API_DECODE_HEADROOM_BYTES (50 * 1024)  // 50KB headroom for decoding
#endif

// Default image display timeout in milliseconds.
#ifndef IMAGE_API_DEFAULT_TIMEOUT_MS
#define IMAGE_API_DEFAULT_TIMEOUT_MS 10000  // 10 seconds default display timeout
#endif

// Maximum image display timeout in milliseconds.
#ifndef IMAGE_API_MAX_TIMEOUT_MS
#define IMAGE_API_MAX_TIMEOUT_MS (86400UL * 1000UL)  // 24 hours max timeout
#endif

// Image API performance tuning
// Controls how many rows the strip decoder batches into one LCD transaction.
// Higher = fewer LCD transactions (faster) but more temporary RAM.
// Set to 1 to disable batching.
// Max rows batched per LCD transaction when decoding JPEG strips.
#ifndef IMAGE_STRIP_BATCH_MAX_ROWS
#define IMAGE_STRIP_BATCH_MAX_ROWS 16
#endif

#endif // BOARD_CONFIG_H

