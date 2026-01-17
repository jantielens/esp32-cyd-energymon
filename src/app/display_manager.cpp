#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"

#include <esp_timer.h>

// Include selected display driver header.
// Driver implementations are compiled via src/app/display_drivers.cpp.
#if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
#include "drivers/tft_espi_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789V2
#include "drivers/st7789v2_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
#include "drivers/arduino_gfx_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ESP_PANEL
#include "drivers/esp_panel_st77916_driver.h"
#endif

#include <SPI.h>

static portMUX_TYPE g_splash_status_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_perf_mux = portMUX_INITIALIZER_UNLOCKED;

static DisplayPerfStats g_perf = {0, 0, 0};
static bool g_perf_ready = false;
static uint32_t g_perf_window_start_ms = 0;
static uint16_t g_perf_frames_in_window = 0;

// Global instance
DisplayManager* displayManager = nullptr;

DisplayManager::DisplayManager(DeviceConfig* cfg) 
    : driver(nullptr), config(cfg), currentScreen(nullptr), previousScreen(nullptr), pendingScreen(nullptr), 
      infoScreen(cfg, this), testScreen(this),
      #if HAS_IMAGE_API
      directImageScreen(this),
      #endif
                lvglTaskHandle(nullptr), lvglMutex(nullptr), screenCount(0), buf(nullptr), flushPending(false), directImageActive(false), pendingSplashStatusSet(false) {
        pendingSplashStatus[0] = '\0';
    // Instantiate selected display driver
    #if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
    driver = new TFT_eSPI_Driver();
    #elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789V2
    driver = new ST7789V2_Driver();
    #elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
    driver = new Arduino_GFX_Driver();
    #elif DISPLAY_DRIVER == DISPLAY_DRIVER_ESP_PANEL
    driver = new ESPPanel_ST77916_Driver();
    #else
    #error "No display driver selected or unknown driver type"
    #endif
    
    // Create mutex for thread-safe LVGL access
    lvglMutex = xSemaphoreCreateMutex();
    
    // Initialize screen registry (exclude splash - it's boot-specific)
    availableScreens[0] = {"info", "Info Screen", &infoScreen};
    availableScreens[1] = {"test", "Test Screen", &testScreen};
    #if HAS_IMAGE_API
    // Optional LVGL image screen (JPEG -> RGB565 -> lv_img).
    // Included under HAS_IMAGE_API for simplicity. To reduce firmware size,
    // disable LVGL image support via LV_USE_IMG=0 / LV_USE_IMG_TRANSFORM=0 in src/app/lv_conf.h.
    #if LV_USE_IMG
    availableScreens[2] = {"lvgl_image", "LVGL Image", &lvglImageScreen};
    screenCount = 3;
    #else
    screenCount = 2;
    #endif
    #else
    screenCount = 2;
    #endif
    
    #if HAS_IMAGE_API
    // Register DirectImageScreen (optional, only shown via API)
    // Not added to navigation menu - shown programmatically
    #endif
}

DisplayManager::~DisplayManager() {
    // Stop rendering task
    if (lvglTaskHandle) {
        vTaskDelete(lvglTaskHandle);
        lvglTaskHandle = nullptr;
    }
    
    if (currentScreen) {
        currentScreen->hide();
    }
    
    splashScreen.destroy();
    infoScreen.destroy();
    testScreen.destroy();
    
    #if HAS_IMAGE_API
    directImageScreen.destroy();
    #if LV_USE_IMG
    lvglImageScreen.destroy();
    #endif
    #endif
    
    // Delete display driver
    if (driver) {
        delete driver;
        driver = nullptr;
    }
    
    // Delete mutex
    if (lvglMutex) {
        vSemaphoreDelete(lvglMutex);
        lvglMutex = nullptr;
    }
    
    // Free LVGL buffer
    if (buf) {
        heap_caps_free(buf);
        buf = nullptr;
    }
}

const char* DisplayManager::getScreenIdForInstance(const Screen* screen) const {
    if (!screen) return nullptr;

    // Splash is boot-specific and intentionally not part of availableScreens.
    if (screen == &splashScreen) {
        return "splash";
    }

    #if HAS_IMAGE_API
    // Direct image mode is API-driven and intentionally not part of availableScreens.
    if (screen == &directImageScreen) {
        return "direct_image";
    }
    #endif

    // Registered runtime screens.
    for (size_t i = 0; i < screenCount; i++) {
        if (availableScreens[i].instance == screen) {
            return availableScreens[i].id;
        }
    }

    return nullptr;
}

// LVGL flush callback
void DisplayManager::flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    DisplayManager* mgr = (DisplayManager*)disp->user_data;

    // When DirectImageScreen is active, the JPEG decoder writes directly to the LCD.
    // Avoid concurrent SPI/TFT_eSPI access from LVGL flushes (can cause WDT/deadlocks).
    #if HAS_IMAGE_API
    if (mgr && (mgr->directImageActive || mgr->currentScreen == &mgr->directImageScreen)) {
        lv_disp_flush_ready(disp);
        return;
    }
    #endif
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    mgr->driver->startWrite();
    mgr->driver->setAddrWindow(area->x1, area->y1, w, h);
    mgr->driver->pushColors((uint16_t *)&color_p->full, w * h, true);
    mgr->driver->endWrite();

    // Signal that the driver may need a post-render present() step.
    // For Direct render-mode drivers this is harmless (present() is a no-op).
    if (mgr) {
        mgr->flushPending = true;
    }
    
    lv_disp_flush_ready(disp);
}

bool DisplayManager::isInLvglTask() const {
    if (!lvglTaskHandle) return false;
    return xTaskGetCurrentTaskHandle() == lvglTaskHandle;
}

void DisplayManager::lockIfNeeded(bool& didLock) {
    if (isInLvglTask()) {
        didLock = false;
        return;
    }
    lock();
    didLock = true;
}

void DisplayManager::unlockIfNeeded(bool didLock) {
    if (didLock) {
        unlock();
    }
}

void DisplayManager::lock() {
    if (lvglMutex) {
        xSemaphoreTake(lvglMutex, portMAX_DELAY);
    }
}

void DisplayManager::unlock() {
    if (lvglMutex) {
        xSemaphoreGive(lvglMutex);
    }
}

bool DisplayManager::tryLock(uint32_t timeoutMs) {
    if (!lvglMutex) return false;
    return xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

// FreeRTOS task for continuous LVGL rendering
void DisplayManager::lvglTask(void* pvParameter) {
    DisplayManager* mgr = (DisplayManager*)pvParameter;
    
    LOGI("Display", "LVGL render task start (core %d)", xPortGetCoreID());
    
    while (true) {
        mgr->lock();

        // Apply any deferred splash status update.
        if (mgr->pendingSplashStatusSet) {
            char text[sizeof(mgr->pendingSplashStatus)];
            bool has = false;
            portENTER_CRITICAL(&g_splash_status_mux);
            if (mgr->pendingSplashStatusSet) {
                strlcpy(text, mgr->pendingSplashStatus, sizeof(text));
                mgr->pendingSplashStatusSet = false;
                has = true;
            }
            portEXIT_CRITICAL(&g_splash_status_mux);
            if (has) {
                mgr->splashScreen.setStatus(text);
            }
        }
        
        // Process pending screen switch (deferred from external calls)
        if (mgr->pendingScreen) {
            Screen* target = mgr->pendingScreen;
            if (mgr->currentScreen) {
                mgr->currentScreen->hide();
            }

            #if HAS_IMAGE_API
            // DirectImageScreen return behavior uses previousScreen.
            // Do not clobber it here for DirectImageScreen transitions; it's managed
            // explicitly in showDirectImage()/returnToPreviousScreen().
            if (mgr->pendingScreen != &mgr->directImageScreen) {
                mgr->previousScreen = mgr->currentScreen;
            }
            #else
            mgr->previousScreen = mgr->currentScreen;
            #endif
            mgr->currentScreen = target;
            mgr->currentScreen->show();
            mgr->pendingScreen = nullptr;

            #if HAS_IMAGE_API
            // Keep the flush gate in sync with the active screen.
            mgr->directImageActive = (mgr->currentScreen == &mgr->directImageScreen);
            #endif

            const char* screenId = mgr->getScreenIdForInstance(mgr->currentScreen);
            LOGI("Display", "Switched to %s", screenId ? screenId : "(unregistered)");
        }
        
        // Handle LVGL rendering (animations, timers, etc.)
        const uint64_t lv_start_us = esp_timer_get_time();
        uint32_t delayMs = lv_timer_handler();
        const uint32_t lv_timer_us = (uint32_t)(esp_timer_get_time() - lv_start_us);
        
        // Update current screen (data refresh)
        if (mgr->currentScreen) {
            mgr->currentScreen->update();
        }
        
        // Flush canvas buffer only when LVGL produced draw data.
        if (mgr->flushPending) {
            const uint32_t now_ms = millis();
            if (g_perf_window_start_ms == 0) {
                g_perf_window_start_ms = now_ms;
                g_perf_frames_in_window = 0;
            }

            uint64_t present_start_us = 0;
            if (mgr->driver->renderMode() == DisplayDriver::RenderMode::Buffered) {
                present_start_us = esp_timer_get_time();
                mgr->driver->present();
            }

            const uint32_t present_us = (present_start_us == 0) ? 0 : (uint32_t)(esp_timer_get_time() - present_start_us);
            g_perf_frames_in_window++;

            // Update published stats every ~1s.
            const uint32_t elapsed = now_ms - g_perf_window_start_ms;
            if (elapsed >= 1000) {
                const uint16_t fps = g_perf_frames_in_window;
                portENTER_CRITICAL(&g_perf_mux);
                g_perf.fps = fps;
                g_perf.lv_timer_us = lv_timer_us;
                g_perf.present_us = present_us;
                g_perf_ready = true;
                portEXIT_CRITICAL(&g_perf_mux);

                g_perf_window_start_ms = now_ms;
                g_perf_frames_in_window = 0;
            }

            mgr->flushPending = false;
        }
        
        mgr->unlock();
        
        // Sleep based on LVGL's suggested next timer deadline.
        // Clamp to keep UI responsive while avoiding busy looping on static screens.
        if (delayMs < 1) delayMs = 1;
        if (delayMs > 20) delayMs = 20;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}

bool display_manager_get_perf_stats(DisplayPerfStats* out) {
    if (!out) return false;
    bool ok = false;
    portENTER_CRITICAL(&g_perf_mux);
    ok = g_perf_ready;
    if (ok) {
        *out = g_perf;
    }
    portEXIT_CRITICAL(&g_perf_mux);
    return ok;
}

void DisplayManager::initHardware() {
    LOGI("Display", "Init start");
    
    // Initialize display driver
    driver->init();
    driver->setRotation(DISPLAY_ROTATION);
    
    // Apply saved brightness from config (or default to 100%)
    #if HAS_BACKLIGHT
    uint8_t brightness = config ? config->backlight_brightness : 100;
    if (brightness > 100) brightness = 100;
    driver->setBacklightBrightness(brightness);
    LOGI("Display", "Backlight: %d%%", brightness);
    #else
    // Turn on backlight (on/off only)
    driver->setBacklight(true);
    LOGI("Display", "Backlight: ON");
    #endif
    
    LOGI("Display", "Resolution: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    LOGI("Display", "Rotation: %d", DISPLAY_ROTATION);
    
    // Apply display-specific settings (inversion, gamma, etc.)
    driver->applyDisplayFixes();
    
    LOGI("Display", "Init complete");
}

void DisplayManager::initLVGL() {
    LOGI("Display", "LVGL init start");
    
    lv_init();
    
    // Allocate LVGL draw buffer.
    // Some QSPI panels/drivers require internal RAM for flush reliability.
    if (LVGL_BUFFER_PREFER_INTERNAL) {
        buf = (lv_color_t*)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf) {
            LOGW("Display", "Internal RAM alloc failed, trying PSRAM...");
            buf = (lv_color_t*)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        }
    } else {
        // Default: PSRAM first, fallback to internal.
        buf = (lv_color_t*)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (!buf) {
            LOGW("Display", "PSRAM alloc failed, trying internal RAM...");
            buf = (lv_color_t*)heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!buf) {
        LOGE("Display", "Failed to allocate LVGL buffer");
        return;
    }
    LOGI("Display", "Buffer allocated: %d bytes (%d pixels)", LVGL_BUFFER_SIZE * sizeof(lv_color_t), LVGL_BUFFER_SIZE);
    
    // Initialize default theme (dark mode with custom primary color)
    lv_theme_t* theme = lv_theme_default_init(
        NULL,                           // Display (use default)
        lv_color_hex(0x3399FF),        // Primary color (light blue)
        lv_color_hex(0x303030),        // Secondary color (dark gray)
        true,                           // Dark mode
        LV_FONT_DEFAULT                // Default font
    );
    lv_disp_set_theme(NULL, theme);
    LOGI("Display", "Theme: Default dark mode initialized");
    
    // Set up display buffer
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_BUFFER_SIZE);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = DisplayManager::flushCallback;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = this;  // Pass instance for callback
    
    // Apply driver-specific LVGL configuration (rotation, full refresh, etc.)
    driver->configureLVGL(&disp_drv, DISPLAY_ROTATION);
    
    lv_disp_drv_register(&disp_drv);
    
    LOGI("Display", "Buffer: %d pixels (%d lines)", LVGL_BUFFER_SIZE, LVGL_BUFFER_SIZE / DISPLAY_WIDTH);
    LOGI("Display", "LVGL init complete");
}

void DisplayManager::init() {
    // Initialize hardware (TFT + gamma fix)
    initHardware();
    
    // Initialize LVGL
    initLVGL();
    
    LOGI("Display", "Manager init start");
    
    // Create all screens
    splashScreen.create();
    infoScreen.create();
    testScreen.create();
    #if HAS_IMAGE_API
    #if LV_USE_IMG
    lvglImageScreen.create();
    #endif
    #endif
    
    LOGI("Display", "Screens created");
    
    // Show splash immediately
    showSplash();
    
    // Create LVGL rendering task
    // Stack size increased to 8KB for ESP32-S3 and larger displays
    // On dual-core: pin to Core 0 (Arduino loop runs on Core 1)
    // On single-core: runs on Core 0 (time-sliced with Arduino loop)
    #if CONFIG_FREERTOS_UNICORE
    xTaskCreate(lvglTask, "LVGL", 8192, this, 1, &lvglTaskHandle);
    LOGI("Display", "Rendering task created (single-core)");
    #else
    xTaskCreatePinnedToCore(lvglTask, "LVGL", 8192, this, 1, &lvglTaskHandle, 0);
    LOGI("Display", "Rendering task created (pinned to Core 0)");
    #endif
    
    LOGI("Display", "Manager init complete");
}

void DisplayManager::showSplash() {
    // Splash shown during init - can switch immediately (no task running yet)
    lock();
    if (currentScreen) {
        currentScreen->hide();
    }
    currentScreen = &splashScreen;
    currentScreen->show();
    unlock();
    LOGI("Display", "Switched to SplashScreen");
}

void DisplayManager::showInfo() {
    // Defer screen switch to lvglTask (non-blocking)
    pendingScreen = &infoScreen;
    LOGI("Display", "Queued switch to InfoScreen");
}

void DisplayManager::showTest() {
    // Defer screen switch to lvglTask (non-blocking)
    pendingScreen = &testScreen;
    LOGI("Display", "Queued switch to TestScreen");
}

#if HAS_IMAGE_API
void DisplayManager::showDirectImage() {
    // If we're already showing the DirectImageScreen, don't queue a redundant
    // LVGL screen switch (it would also risk clobbering previousScreen).
    if (currentScreen == &directImageScreen) {
        directImageActive = true;
        LOGI("Display", "Already on DirectImageScreen");
        return;
    }

    // Save current screen so we can return to it after timeout
    // If we're already on DirectImageScreen, don't overwrite previousScreen
    if (currentScreen && currentScreen != &directImageScreen) {
        previousScreen = currentScreen;
    }
    
    // Defer screen switch to lvglTask (non-blocking)
    // Immediately gate LVGL flushes so the decoder can safely write even before
    // the screen switch is processed by the LVGL task.
    // Also drop any pending buffered present() to avoid flushing stale LVGL content
    // over the direct-image content.
    flushPending = false;
    directImageActive = true;
    pendingScreen = &directImageScreen;
    LOGI("Display", "Queued switch to DirectImageScreen");
}

void DisplayManager::returnToPreviousScreen() {
    // Defer screen switch to lvglTask (non-blocking)
    // If no previous screen, default to info screen
    Screen* targetScreen = previousScreen ? previousScreen : &infoScreen;
    directImageActive = false;
    pendingScreen = targetScreen;
    previousScreen = nullptr;  // Clear previous screen reference
    LOGI("Display", "Queued return to previous screen");
}
#endif

void DisplayManager::setSplashStatus(const char* text) {
    // If called before the LVGL task exists (during early setup), update directly.
    // Otherwise, defer to the LVGL task to avoid cross-task LVGL calls.
    if (!lvglTaskHandle || isInLvglTask()) {
        bool didLock = false;
        lockIfNeeded(didLock);
        splashScreen.setStatus(text);
        unlockIfNeeded(didLock);
        return;
    }

    portENTER_CRITICAL(&g_splash_status_mux);
    strlcpy(pendingSplashStatus, text ? text : "", sizeof(pendingSplashStatus));
    pendingSplashStatusSet = true;
    portEXIT_CRITICAL(&g_splash_status_mux);
}

bool DisplayManager::showScreen(const char* screen_id) {
    if (!screen_id) return false;
    
    // Look up screen in registry
    for (size_t i = 0; i < screenCount; i++) {
        if (strcmp(availableScreens[i].id, screen_id) == 0) {
            // Defer screen switch to lvglTask (non-blocking)
            pendingScreen = availableScreens[i].instance;
            LOGI("Display", "Queued switch to screen: %s", screen_id);
            return true;
        }
    }
    
    LOGW("Display", "Screen not found: %s", screen_id);
    return false;
}

const char* DisplayManager::getCurrentScreenId() {
    // Return ID of current screen (nullptr if splash or unknown)
    for (size_t i = 0; i < screenCount; i++) {
        if (currentScreen == availableScreens[i].instance) {
            return availableScreens[i].id;
        }
    }
    return nullptr;  // Splash or unknown screen
}

const ScreenInfo* DisplayManager::getAvailableScreens(size_t* count) {
    if (count) *count = screenCount;
    return availableScreens;
}

// C-style interface for app.ino
void display_manager_init(DeviceConfig* config) {
    if (!displayManager) {
        displayManager = new DisplayManager(config);
        displayManager->init();
    }
}

void display_manager_show_splash() {
    if (displayManager) {
        displayManager->showSplash();
    }
}

void display_manager_show_info() {
    if (displayManager) {
        displayManager->showInfo();
    }
}

void display_manager_show_test() {
    if (displayManager) {
        displayManager->showTest();
    }
}

void display_manager_set_splash_status(const char* text) {
    if (displayManager) {
        displayManager->setSplashStatus(text);
    }
}

void display_manager_show_screen(const char* screen_id, bool* success) {
    bool result = false;
    if (displayManager) {
        result = displayManager->showScreen(screen_id);
    }
    if (success) *success = result;
}

const char* display_manager_get_current_screen_id() {
    if (displayManager) {
        return displayManager->getCurrentScreenId();
    }
    return nullptr;
}

const ScreenInfo* display_manager_get_available_screens(size_t* count) {
    if (displayManager) {
        return displayManager->getAvailableScreens(count);
    }
    if (count) *count = 0;
    return nullptr;
}

void display_manager_set_backlight_brightness(uint8_t brightness) {
    if (displayManager && displayManager->getDriver()) {
        displayManager->getDriver()->setBacklightBrightness(brightness);
    }
}

void display_manager_lock() {
    if (displayManager) {
        displayManager->lock();
    }
}

void display_manager_unlock() {
    if (displayManager) {
        displayManager->unlock();
    }
}

bool display_manager_try_lock(uint32_t timeout_ms) {
    if (!displayManager) return false;
    return displayManager->tryLock(timeout_ms);
}

#if HAS_IMAGE_API
void display_manager_show_direct_image() {
    if (displayManager) {
        displayManager->showDirectImage();
    }
}

DirectImageScreen* display_manager_get_direct_image_screen() {
    if (displayManager) {
        return displayManager->getDirectImageScreen();
    }
    return nullptr;
}

#if LV_USE_IMG
LvglImageScreen* display_manager_get_lvgl_image_screen() {
    if (displayManager) {
        return displayManager->getLvglImageScreen();
    }
    return nullptr;
}
#endif

void display_manager_return_to_previous_screen() {
    if (displayManager) {
        displayManager->returnToPreviousScreen();
    }
}
#endif // HAS_IMAGE_API

#endif // HAS_DISPLAY
