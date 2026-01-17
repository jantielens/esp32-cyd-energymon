# Display & Touch Architecture

This document describes the display and touch subsystem architecture, design patterns, and extension points for adding new displays, touch controllers, and screens.

## Table of Contents

- [Overview](#overview)
- [Architecture Layers](#architecture-layers)
- [Display Driver HAL](#display-driver-hal)
- [Touch Driver HAL](#touch-driver-hal)
- [Screen Management](#screen-management)
- [Rendering System](#rendering-system)
- [Adding New Display Drivers](#adding-new-display-drivers)
- [Adding New Touch Drivers](#adding-new-touch-drivers)
- [Adding New Screens](#adding-new-screens)
- [Multi-Board Support](#multi-board-support)
- [Performance Considerations](#performance-considerations)

## Overview

The display and touch subsystem is built on four main pillars:

1. **Display HAL** - Isolates display hardware library specifics
2. **Touch HAL** - Isolates touch controller library specifics
3. **Screen Pattern** - Base class for creating reusable UI screens
4. **Manager Classes** - Centralized management of hardware, LVGL, and lifecycle

**Key Technologies:**
- **LVGL 8.4** - Embedded graphics library
- **TFT_eSPI** - Default display driver (supports ILI9341, ST7789, ST7735, etc.)
- **XPT2046_Touchscreen** - Resistive touch support
- **FreeRTOS** - Task-based continuous rendering

## Architecture Layers

```
┌──────────────────────────────────────────────────────────────┐
│  Application Code (app.ino)                                 │
│  - Minimal interaction with display/touch                    │
│  - display_manager_init(), touch_manager_init()            │
│  - display_manager_show_info()                             │
└──────────────────────────────────────────────────────────────┘
                        ↓
        ┌───────────────┴───────────────┐
        ↓                               ↓
┌──────────────────┐          ┌──────────────────┐
│ DisplayManager   │          │ TouchManager     │
│ - Hardware       │          │ - Hardware       │
│ - LVGL display   │          │ - LVGL input     │
│ - Screens        │          │ - Callbacks      │
│ - Navigation     │          └──────────────────┘
│ - Rendering task │
└──────────────────┘
        ↓                               ↓
┌──────────────────┐          ┌──────────────────┐
│ DisplayDriver    │          │ TouchDriver      │
│ (HAL Interface)  │          │ (HAL Interface)  │
└──────────────────┘          └──────────────────┘
        ↓                               ↓
┌──────────────────┐          ┌──────────────────┐
│ TFT_eSPI_Driver  │          │ XPT2046_Driver   │
│ LovyanGFX_Driver │          │ FT6236_Driver    │
└──────────────────┘          └──────────────────┘
        ↓                               ↓
┌──────────────────┐          ┌──────────────────┐
│ TFT_eSPI library │          │ XPT2046 library  │
│ LovyanGFX library│          │ FT6236 library   │
└──────────────────┘          └──────────────────┘
```
│  (Base Class)    │          │  HAL Interface   │
│                  │          │                  │
│  - SplashScreen  │          │ - TFT_eSPI_Driver│
│  - InfoScreen    │          │ - LovyanGFX (fut)│
│  - TestScreen    │          │ - Custom drivers │
└──────────────────┘          └──────────────────┘
        ↓                               ↓
┌─────────────────────────────────────────────────────┐
│  LVGL 8.4                                           │
│  - Widget rendering                                 │
│  - Themes, fonts, animations                       │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│  Hardware (SPI, Display Panel)                      │
└─────────────────────────────────────────────────────┘
```

## Display Driver HAL

### Purpose

The DisplayDriver interface decouples LVGL from specific display libraries, allowing support for TFT_eSPI, LovyanGFX, or custom drivers without changing DisplayManager code.

### Interface Definition

```cpp
// src/app/display_driver.h
class DisplayDriver {
public:
    virtual ~DisplayDriver() = default;

    // How the driver completes a frame:
    // - Direct: LVGL flush pushes pixels straight to the panel
    // - Buffered: LVGL flush writes into a buffer/canvas; DisplayManager calls present()
    enum class RenderMode : uint8_t { Direct = 0, Buffered = 1 };
    
    // Hardware initialization
    virtual void init() = 0;
    
    // Display configuration
    virtual void setRotation(uint8_t rotation) = 0;

    // Active coordinate space dimensions for setAddrWindow()/pushColors().
    // Drivers should report the post-rotation width/height of their address space.
    virtual int width() = 0;
    virtual int height() = 0;
    virtual void setBacklight(bool on) = 0;
    virtual void applyDisplayFixes() = 0;
    
    // Brightness control (optional - only when HAS_BACKLIGHT enabled)
    virtual void setBacklightBrightness(uint8_t brightness_percent) {}  // 0-100%
    virtual uint8_t getBacklightBrightness() { return 100; }
    virtual bool hasBacklightControl() { return false; }
    
    // LVGL flush interface (hot path - called frequently)
    virtual void startWrite() = 0;
    virtual void endWrite() = 0;
    virtual void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) = 0;
    virtual void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) = 0;

    // Default: Direct
    virtual RenderMode renderMode() const { return RenderMode::Direct; }

    // Buffered drivers override this to push the accumulated framebuffer/canvas to the panel.
    virtual void present() {}
    
    // LVGL configuration hook (override for driver-specific behavior)
    // Called during LVGL initialization to allow driver-specific settings
    // such as software rotation, full refresh mode, etc.
    // Default implementation: no special configuration (hardware handles rotation)
    virtual void configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) {
        // Override if driver needs software rotation or other LVGL tweaks
    }
};
```

### Arduino Build System Note (Driver Compilation Units)

Arduino only compiles `.cpp` files in the sketch root directory. Driver implementations under `src/app/drivers/` are compiled by including the selected driver `.cpp` from dedicated translation units:

- `src/app/display_drivers.cpp`
- `src/app/touch_drivers.cpp`

### LVGL Configuration Hook

The `configureLVGL()` method allows drivers to customize LVGL behavior without modifying DisplayManager code:

**Use Cases:**
- **Software rotation** - When panel hardware doesn't support rotation via registers (e.g., ST7789V2)
- **Full refresh mode** - For e-paper displays that need full-screen updates
- **Direct mode** - For high-FPS applications bypassing buffering
- **Custom DPI** - For panels with non-standard pixel density

**Example: ST7789V2 Software Rotation**
```cpp
void ST7789V2_Driver::configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) {
    // ST7789V2 panel stays in portrait mode (240x280)
    // LVGL handles rotation via software rendering (more efficient than register updates)
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
        default:  // 0° (portrait) - no rotation
            break;
    }
}
```

**Example: TFT_eSPI Hardware Rotation (Default)**
```cpp
// TFT_eSPI_Driver doesn't override configureLVGL()
// Uses default implementation (no special configuration)
// Hardware rotation via setRotation() is sufficient
```

**DisplayManager Integration:**
```cpp
void DisplayManager::initLVGL() {
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = DisplayManager::flushCallback;
    
    // Call driver's LVGL configuration hook
    driver->configureLVGL(&disp_drv, DISPLAY_ROTATION);
    
    lv_disp_drv_register(&disp_drv);
}
```

**Benefits:**
- Driver encapsulates its own LVGL requirements
- No `#if DISPLAY_DRIVER == ...` conditionals in DisplayManager
- Easy to add new drivers with quirky behavior
- Self-documenting (driver implementation shows what's needed)

### Performance Impact

**Measured overhead:** +640 bytes flash (0.046%), negligible runtime cost

The hot path (`pushColors`) is called 24 times per full screen refresh:
- Virtual call overhead: ~0.01 µs
- SPI transfer time: 640-1280 µs (at 40-80 MHz)
- **Total overhead: 0.01%** (completely negligible)

### Backlight Brightness Control

The HAL supports optional PWM-based brightness control via the `HAS_BACKLIGHT` feature flag:

**Enable in board_config.h or board_overrides.h:**
```cpp
#define HAS_BACKLIGHT true
#define TFT_BL 27                    // Backlight GPIO pin
#define TFT_BACKLIGHT_ON HIGH        // Active-high or active-low
```

**Implementation Details:**
- Uses ESP32 LEDC peripheral (PWM at 5kHz, 8-bit resolution)
- Brightness range: 0-100% (user-friendly percentage)
- Automatically handles active-high/low polarity via `TFT_BACKLIGHT_ON`
- Supports both Arduino Core 2.x and 3.x LEDC APIs
- Stored in NVS configuration (persists across reboots)
- Exposed via REST API (`PUT /api/display/brightness`, `GET/POST /api/config`)
- Web UI slider for live adjustment

**Driver Methods:**
```cpp
virtual void setBacklightBrightness(uint8_t brightness_percent);  // 0-100%
virtual uint8_t getBacklightBrightness();                         // Current value
virtual bool hasBacklightControl();                               // Feature detection
```

**Manager API:**
```cpp
void display_manager_set_backlight_brightness(uint8_t brightness_percent);
```

### Screen Saver (Burn-In Prevention v1)

When `HAS_DISPLAY` is enabled, the firmware includes a screen saver manager that reduces burn-in risk by turning the backlight off after a period of inactivity.

**Behavior:**
- After `screen_saver_timeout_seconds` of inactivity, the backlight fades to 0.
- Wake fades back to the configured `backlight_brightness`.
- On touch devices, wake can optionally be triggered by touch (`screen_saver_wake_on_touch`).
- While dimming/asleep/fading in, touch input is suppressed so “wake gestures” can’t click through into LVGL UI navigation.
- When a warning threshold is exceeded while the device is asleep, a dedicated warning screen is shown with the backlight on; clearing the warning returns to normal sleep.

**Configuration / APIs:**
- Config fields are exposed via `GET/POST /api/config` (only when `HAS_DISPLAY`).
- Runtime control endpoints:
    - `GET /api/display/sleep` (status)
    - `POST /api/display/sleep` (sleep now)
    - `POST /api/display/wake` (wake now)
    - `POST /api/display/activity` (reset timer; optional `?wake=1`)

**Example Implementation (TFT_eSPI_Driver):**
```cpp
void TFT_eSPI_Driver::setBacklightBrightness(uint8_t brightness_percent) {
    #if HAS_BACKLIGHT
    uint8_t duty = map(brightness_percent, 0, 100, 0, 255);
    if (!TFT_BACKLIGHT_ON) duty = 255 - duty;  // Invert for active-low
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(TFT_BL, duty);
    #else
    ledcWrite(BACKLIGHT_CHANNEL, duty);
    #endif
    #endif
}
```

### Selecting a Driver

In `board_config.h` or `board_overrides.h`:

```cpp
// Available drivers
#define DISPLAY_DRIVER_TFT_ESPI 1
#define DISPLAY_DRIVER_ST7789V2 2
#define DISPLAY_DRIVER_LOVYANGFX 3
#define DISPLAY_DRIVER_ARDUINO_GFX 4

// Select driver (defaults to TFT_eSPI)
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI
```

## Screen Management

### Screen Base Class

All screens inherit from the `Screen` interface:

```cpp
// src/app/screens/screen.h
class Screen {
public:
    virtual void create() = 0;   // Create LVGL objects
    virtual void destroy() = 0;  // Free LVGL objects
    virtual void show() = 0;     // Load screen (lv_scr_load)
    virtual void hide() = 0;     // Hide screen
    virtual void update() = 0;   // Update dynamic content (called every 5ms)
    virtual ~Screen() = default;
};
```

### Lifecycle

1. **Create** - Called once during `DisplayManager::init()`
   - Allocate LVGL objects
   - Set initial content
   - Position widgets
   
2. **Show** - Called when navigating to screen
   - `lv_scr_load(screen)` to make visible
   
3. **Update** - Called continuously by rendering task (every 5ms)
   - Refresh dynamic data (uptime, WiFi status, etc.)
   - Only update if screen is active
   
4. **Hide** - Called when navigating away
   - LVGL handles screen unloading automatically
   
5. **Destroy** - Called in DisplayManager destructor
   - Free all LVGL objects

### Included Screens

**SplashScreen** (`splash_screen.h/cpp`)
- Boot screen with animated spinner
- Status text updates during initialization
- Optimized layout for 240x240 round displays

**InfoScreen** (`info_screen.h/cpp`)
- Device information and real-time stats
- Round display compatible (all text centered)
- Shows: device name, uptime, memory, WiFi, IP, version, chip info
- Device name as hero element with separator lines

**TestScreen** (`test_screen.h/cpp`)
- Display calibration and color testing
- RGB and CMY color bars
- Centered grayscale gradient (black to white)
- Resolution info display

**WarningScreen** (`warning_screen.h/cpp`)
- Black screen with a warning icon that pulses using the alarm pulse settings
- Icon position updates periodically to reduce burn-in
- Shown while the screen saver is active and a warning threshold is exceeded

**DirectImageScreen** (`direct_image_screen.h/cpp`)
- Blank black LVGL screen for direct LCD hardware writes
- Used by Image API for JPEG image display
- Automatic timeout returns to previous screen
- No LVGL widgets (allows strip decoder to write directly to display)
- Configured via `display_manager_show_direct_image(timeout_ms)`

## Rendering System

### FreeRTOS Task-Based Architecture

DisplayManager creates a dedicated task for LVGL rendering:

```cpp
void DisplayManager::lvglTask(void* pvParameter) {
    while (true) {
        mgr->lock();                        // Acquire mutex
        uint32_t delayMs = lv_timer_handler();  // LVGL rendering (returns suggested delay)
        if (mgr->currentScreen) {
            mgr->currentScreen->update();   // Screen data refresh
        }

        // Buffered display drivers (e.g., Arduino_GFX canvas) require a post-render present().
        if (mgr->flushPending && mgr->driver->renderMode() == DisplayDriver::RenderMode::Buffered) {
            mgr->driver->present();
        }
        mgr->flushPending = false;
        mgr->unlock();                      // Release mutex

        // Clamp delay to keep UI responsive while avoiding busy looping on static screens.
        if (delayMs < 1) delayMs = 1;
        if (delayMs > 10) delayMs = 10;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}
```

**Benefits:**
- Continuous LVGL updates (animations, timers work automatically)
- No manual `update()` calls needed in `loop()`
- Works on both single-core and dual-core ESP32
- Thread-safe via mutex protection

**Core Assignment:**
- **Dual-core:** Task pinned to Core 0, Arduino `loop()` on Core 1
- **Single-core:** Task time-sliced with Arduino `loop()` on Core 0

### Thread Safety

All display operations from outside the rendering task must be protected:

```cpp
displayManager->lock();
// LVGL operations here
displayManager->unlock();
```

**Deferred Screen Switching:**

DisplayManager uses a deferred pattern for screen navigation (`showSplash()`, `showInfo()`, `showTest()`, etc.):

1. Navigation methods set `pendingScreen` flag (no mutex, returns instantly)
2. LVGL rendering task checks flag and performs switch on next frame
3. Avoids blocking rendering task during screen transitions (prevents FPS drops)
4. Screens switch within 1 frame (~30ms), imperceptible to users

Direct LVGL operations still require manual locking.

## Touch Driver HAL

### Interface Definition

All touch drivers implement the `TouchDriver` interface ([`src/app/touch_driver.h`](../src/app/touch_driver.h)):

```cpp
class TouchDriver {
public:
    virtual void init() = 0;
    virtual bool isTouched() = 0;
    virtual bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) = 0;
    virtual void setCalibration(uint16_t x_min, uint16_t x_max, 
                                 uint16_t y_min, uint16_t y_max) = 0;
    virtual void setRotation(uint8_t rotation) = 0;
    virtual ~TouchDriver() = default;
};
```

### Implementations

**XPT2046_Driver** ([`src/app/drivers/xpt2046_driver.h/cpp`](../src/app/drivers/xpt2046_driver.cpp))
- **Library**: `XPT2046_Touchscreen` by Paul Stoffregen (standalone)
- **Hardware**: Resistive touch controller (4-wire/5-wire)
- **Communication**: Separate SPI bus (VSPI on ESP32)
- **Features**:
  - IRQ pin support for power efficiency
  - Pressure sensing (z-axis)
  - Built-in noise filtering (pressure threshold)
  - Automatic SPI bus initialization
  - Calibration via raw coordinate mapping

**Key Features**:
- **Independent SPI bus** - Can run on separate SPI from display
- **Pressure filtering** - Rejects electrical noise (z < 200 threshold)
- **Persistent SPIClass** - Avoids dangling reference by allocating with `new`
- **Clean up** - Destructor properly deletes SPIClass instance

### Touch Manager

TouchManager ([`src/app/touch_manager.h/cpp`](../src/app/touch_manager.cpp)) handles:
- Driver initialization and configuration
- LVGL input device registration
- Coordinate translation for LVGL events
- Calibration application from board config

### Touch Event Flow

```
1. User touches screen
   ↓
2. XPT2046 hardware detects via IRQ pin
   ↓
3. LVGL timer calls TouchManager::readCallback() (every ~5ms)
   ↓
4. TouchDriver::getTouch() reads SPI data
   ↓
5. Raw coordinates (0-4095) mapped to screen pixels
   ↓
6. Pressure validated (z >= 200 threshold)
   ↓
7. LVGL receives LV_INDEV_STATE_PRESSED + coordinates
   ↓
8. LVGL dispatches LV_EVENT_CLICKED to screen object
   ↓
9. Screen's touchEventCallback() handles navigation
```

### Touch Integration Pattern

Screens handle touch via LVGL event callbacks:

```cpp
class InfoScreen : public Screen {
private:
    static void touchEventCallback(lv_event_t* e) {
        InfoScreen* instance = (InfoScreen*)lv_event_get_user_data(e);
        instance->displayMgr->showTest();  // Navigate on tap
    }
    
public:
    void create() override {
        screen = lv_obj_create(NULL);
        
        // Make entire screen clickable
        lv_obj_add_event_cb(screen, touchEventCallback, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        
        // Make all child objects click-through
        lv_obj_t* label = lv_label_create(screen);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);  // Pass-through
    }
};
```

**Critical Details**:
- Use `lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE)` on **all child objects**
- Without this, clicks on labels/bars won't reach parent screen
- Use `LV_EVENT_CLICKED` for tap events (LVGL handles press/release)
- Pass `this` as user_data to access instance in static callback

## Adding New Display Drivers

### Step 1: Create Driver Class

Create `src/app/drivers/my_driver.h` and `.cpp`:

```cpp
#include "../display_driver.h"
#include <MyDisplayLib.h>

class MyDisplayDriver : public DisplayDriver {
private:
    MyDisplayLib display;
    
public:
    void init() override {
        display.begin();
    }
    
    void setRotation(uint8_t rotation) override {
        display.setRotation(rotation);
    }
    
    void setBacklight(bool on) override {
        // Control backlight on/off
    }
    
    void setBacklightBrightness(uint8_t brightness_percent) override {
        // PWM brightness control (0-100%)
        #if HAS_BACKLIGHT
        uint8_t duty = map(brightness_percent, 0, 100, 0, 255);
        // Apply to PWM channel
        #endif
    }
    
    bool hasBacklightControl() override {
        #if HAS_BACKLIGHT
        return true;
        #else
        return false;
        #endif
    }
    
    void applyDisplayFixes() override {
        // Board-specific fixes (inversion, gamma, etc.)
    }
    
    void configureLVGL(lv_disp_drv_t* drv, uint8_t rotation) override {
        // Override to customize LVGL behavior (e.g., software rotation)
        // Example: ST7789V2 uses software rotation for landscape mode
        if (rotation == 1) {
            drv->sw_rotate = 1;
            drv->rotated = LV_DISP_ROT_90;
        }
        // Default: hardware handles rotation via setRotation()
    }
    
    void startWrite() override { display.startWrite(); }
    void endWrite() override { display.endWrite(); }
    void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override {
        display.setWindow(x, y, w, h);
    }
    void pushColors(uint16_t* data, uint32_t len, bool swap_bytes) override {
        display.writePixels(data, len);
    }
};
```

### Step 2: Register Driver

In `board_config.h`:

```cpp
#define DISPLAY_DRIVER_MY_DRIVER 3

#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER DISPLAY_DRIVER_MY_DRIVER
#endif
```

### Step 3: Integrate in DisplayManager

In `display_manager.cpp`:

```cpp
#if DISPLAY_DRIVER == DISPLAY_DRIVER_MY_DRIVER
#include "drivers/my_driver.h"
#endif

// In constructor:
#if DISPLAY_DRIVER == DISPLAY_DRIVER_MY_DRIVER
driver = new MyDisplayDriver();
#endif
```

### Step 4: Compile Driver

**IMPORTANT**: Arduino build system only auto-compiles `.cpp` files in the sketch root directory, not subdirectories.

This repo solves that by compiling driver implementations via dedicated “translation unit” files in the sketch root:
- `src/app/display_drivers.cpp` for display backends
- `src/app/touch_drivers.cpp` for touch backends

To add a new display driver implementation (`src/app/drivers/my_driver.cpp`), include it conditionally in `src/app/display_drivers.cpp`:

```cpp
// src/app/display_drivers.cpp
#if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
    #include "drivers/tft_espi_driver.cpp"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_MY_DRIVER
    #include "drivers/my_driver.cpp"
#else
    #error "Unknown DISPLAY_DRIVER"
#endif
```

**Why this pattern?**
- Keeps `.cpp`-includes out of manager code
- Ensures only the selected driver is compiled
- Avoids duplicate-symbol issues (each driver `.cpp` is included exactly once)

## Adding New Touch Drivers

### Step 1: Create Touch Driver Class

Create `src/app/drivers/my_touch_driver.h` and `.cpp`:

```cpp
#include "../touch_driver.h"
#include <MyTouchLib.h>

class MyTouchDriver : public TouchDriver {
private:
    MyTouchLib touch;
    uint16_t cal_x_min, cal_x_max;
    uint16_t cal_y_min, cal_y_max;
    
public:
    MyTouchDriver(uint8_t sda, uint8_t scl) : touch(sda, scl) {
        cal_x_min = 0;
        cal_x_max = 4095;
        cal_y_min = 0;
        cal_y_max = 4095;
    }
    
    void init() override {
        touch.begin();
    }
    
    bool isTouched() override {
        return touch.touched();
    }
    
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) override {
        if (!touch.touched()) return false;
        
        uint16_t raw_x, raw_y;
        touch.readData(&raw_x, &raw_y);
        
        // Map raw coordinates to screen coordinates
        *x = map(raw_x, cal_x_min, cal_x_max, 0, DISPLAY_WIDTH - 1);
        *y = map(raw_y, cal_y_min, cal_y_max, 0, DISPLAY_HEIGHT - 1);
        
        return true;
    }
    
    void setCalibration(uint16_t x_min, uint16_t x_max, 
                        uint16_t y_min, uint16_t y_max) override {
        cal_x_min = x_min;
        cal_x_max = x_max;
        cal_y_min = y_min;
        cal_y_max = y_max;
    }
    
    void setRotation(uint8_t rotation) override {
        touch.setRotation(rotation);
    }
};
```

### Step 2: Add Touch Driver Constant

In `src/app/board_config.h`:

```cpp
#define TOUCH_DRIVER_NONE      0
#define TOUCH_DRIVER_XPT2046   1
#define TOUCH_DRIVER_FT6236    2
#define TOUCH_DRIVER_MY_TOUCH  3  // Add new driver ID
```

### Step 3: Include in Compilation

Touch driver implementations are compiled via `src/app/touch_drivers.cpp` (a sketch-root translation unit).

To add a new touch driver implementation (`src/app/drivers/my_touch_driver.cpp`), include it conditionally in `src/app/touch_drivers.cpp`:

```cpp
// src/app/touch_drivers.cpp
#if HAS_TOUCH
    #if TOUCH_DRIVER == TOUCH_DRIVER_XPT2046
        #include "drivers/xpt2046_driver.cpp"
    #elif TOUCH_DRIVER == TOUCH_DRIVER_MY_TOUCH
        #include "drivers/my_touch_driver.cpp"
    #else
        #error "Unknown TOUCH_DRIVER"
    #endif
#endif
```

### Step 4: Configure in Board Override

In `src/boards/my-board/board_overrides.h`:

```cpp
#define HAS_TOUCH true
#define TOUCH_DRIVER TOUCH_DRIVER_MY_TOUCH

// Touch pins
#define TOUCH_SDA 21
#define TOUCH_SCL 22
#define TOUCH_IRQ 36

// Calibration values (get from calibration sketch)
#define TOUCH_CAL_X_MIN 200
#define TOUCH_CAL_X_MAX 3900
#define TOUCH_CAL_Y_MIN 250
#define TOUCH_CAL_Y_MAX 3700
```

### Step 5: Update TouchManager

In `src/app/touch_manager.cpp`, add initialization:

```cpp
void TouchManager::init() {
    #if TOUCH_DRIVER == TOUCH_DRIVER_XPT2046
        driver = new XPT2046_Driver(TOUCH_CS, TOUCH_IRQ);
    #elif TOUCH_DRIVER == TOUCH_DRIVER_MY_TOUCH
        driver = new MyTouchDriver(TOUCH_SDA, TOUCH_SCL);
    #endif
    
    driver->init();
    driver->setCalibration(TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, 
                          TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX);
    // ... rest of init
}
```

## Adding New Screens

### Step 1: Create Screen Files

Create `src/app/screens/my_screen.h`:

```cpp
#ifndef MY_SCREEN_H
#define MY_SCREEN_H

#include "screen.h"
#include <lvgl.h>

class MyScreen : public Screen {
private:
    lv_obj_t* screen;
    lv_obj_t* label;
    
public:
    MyScreen();
    ~MyScreen();
    
    void create() override;
    void destroy() override;
    void show() override;
    void hide() override;
    void update() override;
};

#endif
```

Create `src/app/screens/my_screen.cpp`:

```cpp
#include "my_screen.h"
#include "../board_config.h"

MyScreen::MyScreen() : screen(nullptr), label(nullptr) {}

MyScreen::~MyScreen() {
    destroy();
}

void MyScreen::create() {
    if (screen) return;
    
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    label = lv_label_create(screen);
    lv_label_set_text(label, "My Screen");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void MyScreen::destroy() {
    if (screen) {
        lv_obj_del(screen);
        screen = nullptr;
        label = nullptr;
    }
}

void MyScreen::show() {
    if (screen) {
        lv_scr_load(screen);
    }
}

void MyScreen::hide() {
    // LVGL handles screen switching
}

void MyScreen::update() {
    // Update dynamic content here
}
```

### Step 2: Add to DisplayManager

In `display_manager.h`:

```cpp
#include "screens/my_screen.h"

class DisplayManager {
private:
    MyScreen myScreen;
    
public:
    void showMyScreen();
};
```

In `display_manager.cpp`:

```cpp
void DisplayManager::init() {
    myScreen.create();
    // ...
}

void DisplayManager::showMyScreen() {
    // Deferred pattern - just set flag, no mutex needed
    pendingScreen = &myScreen;
    // Actual switch happens in lvglTask on next frame
}
```

### Step 3: Compile Screen

Add to `src/app/screens.cpp`:

```cpp
#include "screens/my_screen.cpp"
```

## Multi-Board Display & Touch Support

### Board-Specific Configuration

Each board can define display and touch settings in `src/boards/[board-name]/board_overrides.h`:

**Display Configuration:**

```cpp
// Enable display
#define HAS_DISPLAY true

// Display driver selection
#define DISPLAY_DRIVER_ILI9341_2  // Variant with inversion support

// Display dimensions
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ROTATION 1  // 0=portrait, 1=landscape, 2=portrait_flip, 3=landscape_flip

// Pin configuration (HSPI)
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1   // -1 = no reset pin
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// Display-specific fixes
#define DISPLAY_INVERSION_ON true
#define DISPLAY_NEEDS_GAMMA_FIX true
#define DISPLAY_COLOR_ORDER_BGR true
#define TFT_SPI_FREQUENCY 55000000  // 55 MHz

// LVGL buffer size (board-specific optimization)
#undef LVGL_BUFFER_SIZE
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)
```

**Touch Configuration:**

```cpp
// Enable touch
#define HAS_TOUCH true
#define TOUCH_DRIVER TOUCH_DRIVER_XPT2046

// Touch pins (VSPI - separate from display)
#define TOUCH_CS   33
#define TOUCH_SCLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_IRQ  36

// Calibration values (from calibration sketch)
#define TOUCH_CAL_X_MIN 300
#define TOUCH_CAL_X_MAX 3900
#define TOUCH_CAL_Y_MIN 200
#define TOUCH_CAL_Y_MAX 3700
```

**Conditional Compilation:**

Both display and touch are optional - boards can have:
- ✅ Display + Touch (e.g., CYD boards)
- ✅ Display only (e.g., dev boards with TFT shields)
- ✅ Neither (headless operation)

```cpp
// Headless board
#define HAS_DISPLAY false
#define HAS_TOUCH false
```

### Round Display Support

All included screens are designed for **240x240 minimum round displays**:

- All text uses `LV_ALIGN_CENTER` for horizontal/vertical centering
- Widgets positioned with Y offsets from center
- Important content kept within ±90px of center
- Full-width elements (gradients, bars) work on rectangular displays too

**Layout Guidelines:**
- Use centered alignment: `lv_obj_align(obj, LV_ALIGN_CENTER, 0, y_offset)`
- Keep critical content within ±90px from center (Y=0)
- Test on both 240x240 round and 320x240 rectangular displays

### Build System Integration

The build system automatically detects board-specific display configurations:

```bash
./build.sh cyd-v2  # Builds with CYD display config
./build.sh esp32-nodisplay       # Builds without display (HAS_DISPLAY=false)
```

Each board compiles with its own display driver and settings.

## Performance Considerations

### Memory Usage

- **DisplayDriver HAL:** +64 bytes RAM (vtable), +640 bytes flash
- **LVGL buffers:** `DISPLAY_WIDTH * 10 * 2` bytes (e.g., 6.4 KB for 320x240)
- **Per screen:** ~200-500 bytes (depends on widget count)
- **Total:** ~50-60 KB for display subsystem

### Rendering Performance

**Typical metrics (320x240 @ 40 MHz SPI):**
- Full screen refresh: ~30-40ms (24 buffer flushes)
- LVGL task overhead: ~1-2% CPU (5ms interval)
- Virtual call overhead: <0.01% (negligible)

**Optimization tips:**
- Use larger LVGL buffers if RAM allows (reduces flushes)
- Increase SPI speed to 80 MHz if display supports it
- Minimize widget updates in `update()` method
- Use LVGL's dirty rectangle optimization (automatic)

### LVGL Configuration

Key settings in `src/app/lv_conf.h`:

```cpp
#define LV_COLOR_DEPTH 16              // RGB565
#define LV_MEM_SIZE (48 * 1024U)       // 48 KB LVGL heap
#define LV_FONT_MONTSERRAT_14 1        // Enable fonts
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_THEME_DEFAULT_DARK 1        // Dark theme enabled
```

## File Organization

```
src/app/
├── display_driver.h              # HAL interface
├── display_manager.h/cpp         # Manager (owns everything)
├── screens.cpp                   # Compilation unit
├── lv_conf.h                     # LVGL configuration
├── drivers/
│   └── tft_espi_driver.h/cpp    # TFT_eSPI implementation
└── screens/
    ├── screen.h                  # Base class
    ├── splash_screen.h/cpp       # Boot screen
    ├── info_screen.h/cpp         # Device info screen
    └── test_screen.h/cpp         # Display test/calibration
```

## Best Practices

1. **Always use the HAL interface** - Don't access TFT_eSPI directly
2. **Keep screens stateless** - Reload data in `update()`, don't cache
3. **Test on round displays** - Verify 240x240 compatibility
4. **Use LVGL themes** - Leverage default theme for consistent styling
5. **Protect LVGL calls** - Use `lock()`/`unlock()` from outside rendering task
6. **Optimize update()** - Only update changed values, avoid full redraws
7. **Follow naming conventions** - snake_case for files, PascalCase for classes

## Touch Support

### Overview

Touch input is supported through a TouchDriver HAL interface following the same pattern as DisplayDriver. This allows different touch controllers to be used without changing application code.

**Supported Controllers:**
- XPT2046 (resistive touch - via TFT_eSPI)
- FT6236 (capacitive touch - future support)

### Touch Driver HAL

```cpp
// src/app/touch_driver.h
class TouchDriver {
public:
    virtual ~TouchDriver() = default;
    
    virtual void init() = 0;
    virtual bool isTouched() = 0;
    virtual bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) = 0;
    virtual void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) = 0;
    virtual void setRotation(uint8_t rotation) = 0;
};
```

### XPT2046 Implementation

The XPT2046_Driver wraps TFT_eSPI's touch extensions:

```cpp
// Uses TFT_eSPI's getTouch() and setTouch() methods
// Calibration values from board configuration
// Automatic rotation handling
```

**Hardware Setup (CYD boards):**
- Touch controller on separate VSPI bus
- 5-pin configuration: IRQ, MOSI, MISO, CLK, CS
- Calibration values: (300-3900, 200-3700) - from macsbug.wordpress.com

### Touch Manager

TouchManager integrates touch hardware with LVGL's input device system:

```cpp
// Initialize touch (after DisplayManager)
touch_manager_init();

// LVGL automatically polls touch via registered input device
// No manual update() calls needed
```

**Key Features:**
- LVGL input device registration
- Calibration from board config
- Rotation matching display
- Thread-safe (called from LVGL task)

### Enabling Touch for a Board

**Step 1: Board Configuration**

In `src/boards/[board-name]/board_overrides.h`:

```cpp
// Enable touch support
#define HAS_TOUCH true
#define TOUCH_DRIVER TOUCH_DRIVER_XPT2046

// TFT_eSPI Touch Controller Pins (required for TFT_eSPI extensions)
#define TOUCH_CS 33
#define TOUCH_SCLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_IRQ 36

// XPT2046 Touch Pins (for documentation)
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Calibration values (determine via calibration procedure)
#define TOUCH_CAL_X_MIN 300
#define TOUCH_CAL_X_MAX 3900
#define TOUCH_CAL_Y_MIN 200
#define TOUCH_CAL_Y_MAX 3700
```

**Step 2: Application Integration**

In `app.ino` (after display initialization):

```cpp
#if HAS_TOUCH
#include "touch_manager.h"

void setup() {
    // ... display init ...
    
    #if HAS_TOUCH
    touch_manager_init();  // Initialize after display
    #endif
}
#endif
```

### Adding Touch Events to Screens

LVGL handles touch events automatically once input device is registered. Add interactive widgets:

```cpp
// In screen's create() method:

// Button example
lv_obj_t* btn = lv_btn_create(screen);
lv_obj_add_event_cb(btn, button_callback, LV_EVENT_CLICKED, this);

// Slider example
lv_obj_t* slider = lv_slider_create(screen);
lv_obj_add_event_cb(slider, slider_callback, LV_EVENT_VALUE_CHANGED, this);
```

### Touch Calibration

To determine calibration values for a new board:

1. Use TFT_eSPI's calibration sketch
2. Record min/max raw values for X and Y
3. Add to board_overrides.h as TOUCH_CAL_* defines
4. Rebuild and test touch accuracy

**Default values (XPT2046 on CYD):**
- X range: 300 to 3900
- Y range: 200 to 3700

### File Organization

```
src/app/
├── touch_driver.h              # HAL interface
├── touch_manager.h/cpp         # Manager + LVGL integration
├── drivers/
│   └── xpt2046_driver.h/cpp   # XPT2046 implementation
```

### Architecture Benefits

✅ **Same HAL pattern as display** - Consistent abstraction
✅ **Easy controller swapping** - Change via TOUCH_DRIVER define
✅ **LVGL integration** - Automatic event handling
✅ **Board-specific calibration** - Values in board_overrides.h
✅ **Zero application changes** - Touch works transparently

## Future Enhancements

- LovyanGFX driver implementation
- FT6236 capacitive touch driver
- Touch gestures (swipe, pinch, long-press)
- Screen navigation with buttons
- Settings screen for WiFi configuration
- Graph widgets for sensor data visualization
- Multi-language support with LVGL's text engine
