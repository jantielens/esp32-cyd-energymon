/*
 * Strip Decoder for Memory-Efficient Image Display
 * 
 * Decodes individual JPEG strips and writes directly to LCD hardware.
 * Uses TJpgDec/tjpgd from ESP32 ROM for baseline JPEG decode.
 * 
 * Memory usage: ~20KB constant regardless of image size
 *   - Strip buffer: ~2KB (temporary per strip)
 *   - Decode buffer: ~13KB for 280px width × 16px height
 *   - TJpgDec work area: ~3KB
 */

#pragma once

#include "board_config.h"

#if HAS_IMAGE_API

#include <Arduino.h>

// Forward declaration
class DisplayDriver;

class StripDecoder {
public:
    StripDecoder();
    ~StripDecoder();
    
    // Set display driver for LCD writes
    void setDisplayDriver(DisplayDriver* drv);
    
    // Initialize decoder for new image session
    // image_width: total image width in pixels
    // image_height: total image height in pixels
    // lcd_width: LCD panel width (for bounds checking)
    // lcd_height: LCD panel height (for bounds checking)
    void begin(int image_width, int image_height, int lcd_width, int lcd_height);
    
    // Decode and display a single JPEG strip
    // jpeg_data: pointer to JPEG data for this strip
    // jpeg_size: size of JPEG data in bytes
    // strip_index: index of this strip (0-based)
    // output_bgr565: true to pack pixels as BGR565 (legacy strip behavior),
    //               false to pack pixels as RGB565 (useful when LCD is in RGB mode)
    // Returns: true on success, false on failure
    bool decode_strip(const uint8_t* jpeg_data, size_t jpeg_size, int strip_index, bool output_bgr565 = true);
    
    // Complete image session and cleanup
    void end();
    
    // Get current Y position (for progress tracking)
    int get_current_y() const { return current_y; }
    
private:
    void free_buffers();
    bool ensure_buffers();

    DisplayDriver* driver;  // Display driver for LCD writes
    int width;              // Image width
    int height;             // Image height
    int lcd_width;          // LCD panel width
    int lcd_height;         // LCD panel height
    int current_y;          // Current Y position in image

    // Per-session reusable buffers (allocated in begin(), freed in end()).
    void* work_buffer = nullptr;
    size_t work_buffer_size = 0;

    uint16_t* line_buffer = nullptr;
    int line_buffer_width = 0;

    uint16_t* batch_buffer = nullptr;
    int batch_max_rows = 0;
    int batch_capacity_pixels = 0;
    
    // Note: Strip height is auto-detected from JPEG during decode (not hardcoded)
    // Typical values: 8, 16, 32, or 64 pixels (configurable in encoder)
};

#endif // HAS_IMAGE_API
