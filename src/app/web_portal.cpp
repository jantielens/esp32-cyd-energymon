/*
 * Web Configuration Portal Implementation
 * 
 * Async web server with captive portal support.
 * Serves static files and provides REST API for configuration.
 */

// AsyncTCP task stack sizing:
// - The AsyncTCP library is compiled as a separate translation unit.
// - Defining CONFIG_ASYNC_TCP_STACK_SIZE in this file does NOT reliably affect the library build.
// - To override it, define CONFIG_ASYNC_TCP_STACK_SIZE in src/boards/<board>/board_overrides.h.
//   The build script propagates this allowlisted define into library builds.

#include "web_portal.h"
#include "config_manager.h"
#include "log_manager.h"
#include "board_config.h"
#include "device_telemetry.h"
#include "project_branding.h"
#include "../version.h"
#include "psram_json_allocator.h"
#include "web_portal_routes.h"
#include "web_portal_auth.h"
#include "web_portal_config.h"
#include "web_portal_cors.h"
#include "web_portal_state.h"
#include "web_portal_firmware.h"
#include "web_portal_ap.h"

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_IMAGE_API
#include "image_api.h"
#endif

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void log_async_tcp_stack_watermark_once() {
    static bool logged = false;
    if (logged) return;
    logged = true;

    // AsyncTCP task name varies by library/core version; try a few common ones.
    TaskHandle_t task = xTaskGetHandle("async_tcp");
    if (!task) task = xTaskGetHandle("async_tcp_task");
    if (!task) task = xTaskGetHandle("AsyncTCP");
    if (!task) return;

    const UBaseType_t high_water_words = uxTaskGetStackHighWaterMark(task);
    const unsigned high_water_bytes = (unsigned)high_water_words * (unsigned)sizeof(StackType_t);

    #ifdef CONFIG_ASYNC_TCP_STACK_SIZE
        LOGI("Portal", "AsyncTCP stack watermark: %u bytes (CONFIG_ASYNC_TCP_STACK_SIZE=%u)", high_water_bytes, (unsigned)CONFIG_ASYNC_TCP_STACK_SIZE);
    #else
        LOGI("Portal", "AsyncTCP stack watermark: %u bytes (CONFIG_ASYNC_TCP_STACK_SIZE not set)", high_water_bytes);
    #endif
}


// Web server on port 80 (pointer to avoid constructor issues)
AsyncWebServer *server = nullptr;

// State
static DeviceConfig *current_config = nullptr;
static bool ota_in_progress = false;

bool web_portal_is_ap_mode_active() {
    return web_portal_is_ap_mode();
}

DeviceConfig* web_portal_get_current_config() {
    return current_config;
}

void web_portal_set_ota_in_progress(bool in_progress) {
    ota_in_progress = in_progress;
}

// ===== Basic Auth (optional; STA/full mode only) =====
// (Basic auth gate moved to web_portal_auth.cpp)

#if HAS_IMAGE_API && HAS_DISPLAY
// AsyncWebServer callbacks run on the AsyncTCP task; never touch LVGL/display from there.
// Use this flag to defer "hide current image / return" operations to the main loop.
static volatile bool pending_image_hide_request = false;
#endif

// ===== PUBLIC API =====

// Initialize web portal
void web_portal_init(DeviceConfig *config) {
    LOGI("Portal", "Init start");
    
    current_config = config;
    LOGI("Portal", "Config ptr: %p, backlight_brightness: %d", 
                    current_config, current_config->backlight_brightness);
    
    // Create web server instance (avoid global constructor issues)
    if (server == nullptr) {
        yield();
        delay(100);
        
        server = new AsyncWebServer(80);
        
        yield();
        delay(100);
    }

    // CORS default headers for GitHub Pages (if repo slug is available).
    web_portal_add_default_cors_headers();

    // Routes (factored out for maintainability)
    web_portal_register_routes(server);
    
    // Image API integration (if enabled)
    #if HAS_IMAGE_API && HAS_DISPLAY
    LOGI("Portal", "Initializing image API");
    
    // Setup backend adapter
    ImageApiBackend backend;
    backend.hide_current_image = []() {
        #if HAS_DISPLAY
        // Called from AsyncTCP task and sometimes from the main loop.
        // Always defer actual display/LVGL operations to the main loop.
        pending_image_hide_request = true;
        #endif
    };
    
    backend.start_strip_session = [](int width, int height, unsigned long timeout_ms, unsigned long start_time) -> bool {
        #if HAS_DISPLAY
        (void)start_time;
        DirectImageScreen* screen = display_manager_get_direct_image_screen();
        if (!screen) {
            LOGE("IMG", "No direct image screen");
            return false;
        }
        
        // Now called from main loop with proper task context
        // Show the DirectImageScreen first
        display_manager_show_direct_image();

        // Screen-affecting action counts as explicit activity and should wake.
        screen_saver_manager_notify_activity(true);
        
        // Configure timeout and start session
        screen->set_timeout(timeout_ms);
        screen->begin_strip_session(width, height);
        return true;
        #else
        return false;
        #endif
    };
    
    backend.decode_strip = [](const uint8_t* jpeg_data, size_t jpeg_size, uint8_t strip_index, bool output_bgr565) -> bool {
        #if HAS_DISPLAY
        DirectImageScreen* screen = display_manager_get_direct_image_screen();
        if (!screen) {
            LOGE("IMG", "No direct image screen");
            return false;
        }
        
        // Now called from main loop - safe to decode
        return screen->decode_strip(jpeg_data, jpeg_size, strip_index, output_bgr565);
        #else
        return false;
        #endif
    };
    
    // Setup configuration
    ImageApiConfig image_cfg;

    // Use the display driver's coordinate space (fast path for direct image writes).
    // This intentionally avoids LVGL calls and any DISPLAY_ROTATION heuristics.
    image_cfg.lcd_width = DISPLAY_WIDTH;
    image_cfg.lcd_height = DISPLAY_HEIGHT;

    #if HAS_DISPLAY
        if (displayManager && displayManager->getDriver()) {
            image_cfg.lcd_width = displayManager->getDriver()->width();
            image_cfg.lcd_height = displayManager->getDriver()->height();
        }
    #endif
    
    image_cfg.max_image_size_bytes = IMAGE_API_MAX_SIZE_BYTES;
    image_cfg.decode_headroom_bytes = IMAGE_API_DECODE_HEADROOM_BYTES;
    image_cfg.default_timeout_ms = IMAGE_API_DEFAULT_TIMEOUT_MS;
    image_cfg.max_timeout_ms = IMAGE_API_MAX_TIMEOUT_MS;
    
    // Initialize and register routes
    LOGI("Portal", "Calling image_api_init...");
    image_api_init(image_cfg, backend);
    LOGI("Portal", "Calling image_api_register_routes...");
    image_api_register_routes(server, portal_auth_gate);
    LOGI("Portal", "Image API initialized");
    #endif // HAS_IMAGE_API && HAS_DISPLAY
    
    // Captive portal 404 handler
    web_portal_ap_register_not_found(server);
    
    // Start server
    yield();
    delay(100);
    server->begin();

    log_async_tcp_stack_watermark_once();
    LOGI("Portal", "Init complete");
}

// Handle web server (call in loop)
void web_portal_handle() {
    web_portal_ap_handle();

    web_portal_config_loop();
}

// Check if OTA update is in progress
bool web_portal_ota_in_progress() {
    return ota_in_progress;
}

#if HAS_IMAGE_API
// Process pending image uploads (call from main loop)
void web_portal_process_pending_images() {
    // If the image API asked us to hide/dismiss the current image (or recover
    // from a failure), do it from the main loop so DisplayManager can safely
    // clear direct-image mode.
    #if HAS_DISPLAY
    if (pending_image_hide_request) {
        pending_image_hide_request = false;
        display_manager_return_to_previous_screen();
    }
    #endif

    image_api_process_pending(ota_in_progress);
}
#endif
