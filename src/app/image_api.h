/*
 * Image API - Web-based Image Upload and Display
 * 
 * Portable module for uploading and displaying JPEG images via REST API.
 * Uses backend adapter pattern for integration with any display system.
 * 
 * Supports two upload modes:
 * 1. Full image upload (deferred decode in main loop)
 * 2. Stripped upload (synchronous decode, memory efficient)
 */

#pragma once

#include "board_config.h"

#if HAS_IMAGE_API

#include <stddef.h>
#include <stdint.h>

class AsyncWebServer;

// Backend adapter interface for connecting to display system
// Implement these three hooks to integrate with your display pipeline
struct ImageApiBackend {
    void (*hide_current_image)();  // Hide/dismiss current image
    bool (*start_strip_session)(int width, int height, unsigned long timeout_ms, unsigned long start_time);
    bool (*decode_strip)(const uint8_t* jpeg_data, size_t jpeg_size, uint8_t strip_index, bool output_bgr565);
};

// Configuration structure (can be populated from board_config.h)
struct ImageApiConfig {
    int lcd_width = 0;
    int lcd_height = 0;
    size_t max_image_size_bytes = IMAGE_API_MAX_SIZE_BYTES;
    size_t decode_headroom_bytes = IMAGE_API_DECODE_HEADROOM_BYTES;
    unsigned long default_timeout_ms = IMAGE_API_DEFAULT_TIMEOUT_MS;
    unsigned long max_timeout_ms = IMAGE_API_MAX_TIMEOUT_MS;
};

// Initialize image API with configuration and backend adapter
void image_api_init(const ImageApiConfig& cfg, const ImageApiBackend& backend);

// Register REST API routes with web server
// Endpoints:
//   POST   /api/display/image          - Upload full JPEG (deferred decode)
//   POST   /api/display/image_url      - Queue HTTP/HTTPS JPEG download (deferred download+decode)
//   DELETE /api/display/image          - Dismiss current image
//   POST   /api/display/image/strips   - Upload JPEG strip (synchronous)
void image_api_register_routes(AsyncWebServer* server);

// Process pending image operations (call from main loop)
// ota_in_progress: true if OTA update is in progress (skips processing)
void image_api_process_pending(bool ota_in_progress);

#endif // HAS_IMAGE_API
