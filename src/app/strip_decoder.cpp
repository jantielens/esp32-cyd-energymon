/*
 * Strip Decoder Implementation
 * 
 * Decodes JPEG strips using TJpgDec and writes directly to LCD via DisplayDriver.
 * Performs RGB→BGR color swap during pixel write.
 */

#include "board_config.h"

#if HAS_IMAGE_API

#include "strip_decoder.h"
#include "display_driver.h"
#include "log_manager.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
    #include <esp_heap_caps.h>
#endif

// Use the ESP-ROM TJpgDec types/signatures. This matches the ROM-provided jd_prepare/jd_decomp
// symbols used by the ESP32 Arduino core.
//
// Note: not all Arduino-ESP32 installs ship headers for every ESP32-family target.
// We select based on the IDF target macro when available, and fall back to
// __has_include for toolchains that don't define CONFIG_IDF_TARGET_*.
#if defined(CONFIG_IDF_TARGET_ESP32)
    #include <esp32/rom/tjpgd.h>
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    #if __has_include(<esp32s2/rom/tjpgd.h>)
        #include <esp32s2/rom/tjpgd.h>
    #else
        #error "Missing <esp32s2/rom/tjpgd.h> in this Arduino-ESP32 install"
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #include <esp32s3/rom/tjpgd.h>
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
    #if __has_include(<esp32c2/rom/tjpgd.h>)
        #include <esp32c2/rom/tjpgd.h>
    #else
        #error "Missing <esp32c2/rom/tjpgd.h> in this Arduino-ESP32 install"
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    #include <esp32c3/rom/tjpgd.h>
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
    #include <esp32c5/rom/tjpgd.h>
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #include <esp32c6/rom/tjpgd.h>
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    #if __has_include(<esp32h2/rom/tjpgd.h>)
        #include <esp32h2/rom/tjpgd.h>
    #else
        #error "Missing <esp32h2/rom/tjpgd.h> in this Arduino-ESP32 install"
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
    #if __has_include(<esp32p4/rom/tjpgd.h>)
        #include <esp32p4/rom/tjpgd.h>
    #else
        #error "Missing <esp32p4/rom/tjpgd.h> in this Arduino-ESP32 install"
    #endif
#else
    // Fallback for unknown targets - try common variants
    #if __has_include(<esp32/rom/tjpgd.h>)
        #include <esp32/rom/tjpgd.h>
    #elif __has_include(<esp32s3/rom/tjpgd.h>)
        #include <esp32s3/rom/tjpgd.h>
    #elif __has_include(<esp32c6/rom/tjpgd.h>)
        #include <esp32c6/rom/tjpgd.h>
    #elif __has_include(<esp32c5/rom/tjpgd.h>)
        #include <esp32c5/rom/tjpgd.h>
    #elif __has_include(<esp32c3/rom/tjpgd.h>)
        #include <esp32c3/rom/tjpgd.h>
    #elif __has_include(<esp32s2/rom/tjpgd.h>)
        #include <esp32s2/rom/tjpgd.h>
    #elif __has_include(<esp32c2/rom/tjpgd.h>)
        #include <esp32c2/rom/tjpgd.h>
    #elif __has_include(<esp32h2/rom/tjpgd.h>)
        #include <esp32h2/rom/tjpgd.h>
    #elif __has_include(<esp32p4/rom/tjpgd.h>)
        #include <esp32p4/rom/tjpgd.h>
    #else
        #error "Unsupported ESP32 target for TJpgDec (no ROM tjpgd.h found)"
    #endif
#endif

// Input buffer context for TJpgDec
struct JpegInputContext {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

// Output context for TJpgDec
struct JpegOutputContext {
    StripDecoder* decoder;
    DisplayDriver* driver;
    int strip_y_offset;
    uint16_t* line_buffer;  // Buffer for one line of pixels
    int buffer_width;
    int lcd_width;
    int lcd_height;
    bool output_bgr565;     // true=BGR565, false=RGB565

    // Optional batch buffer to reduce LCD transactions.
    // Holds a small rectangle (typically 8-16 rows) of converted RGB565 pixels.
    uint16_t* batch_buffer;
    int batch_capacity_pixels;
    int batch_max_rows;
};

// TJpgDec uses a single opaque device pointer for the entire decode session.
// Both the input function and output function must be able to access their
// respective state through the same pointer.
struct JpegSessionContext {
    JpegInputContext input;
    JpegOutputContext output;
};

// TJpgDec input function - read from memory buffer
// Signature must match ROM: UINT (*)(JDEC*, BYTE*, UINT)
static UINT jpeg_input_func(JDEC* jd, BYTE* buff, UINT nbyte) {
    JpegSessionContext* session = (JpegSessionContext*)jd->device;
    if (!session) return 0;

    JpegInputContext* ctx = &session->input;
    if (!ctx->data) return 0;
    if (ctx->pos >= ctx->size) return 0;

    const size_t remaining = ctx->size - ctx->pos;
    const size_t requested = (size_t)nbyte;
    const size_t to_read = (requested < remaining) ? requested : remaining;

    if (buff && to_read > 0) {
        memcpy(buff, ctx->data + ctx->pos, to_read);
    }

    ctx->pos += to_read;
    return (UINT)to_read;
}

// TJpgDec output function - convert RGB888→(BGR565 or RGB565) and write to LCD
static UINT jpeg_output_func(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegSessionContext* session = (JpegSessionContext*)jd->device;
    JpegOutputContext* ctx = session ? &session->output : nullptr;
    uint8_t* src = (uint8_t*)bitmap;
    
    if (!ctx || !ctx->line_buffer || !ctx->driver) {
        Logger.logMessage("StripDecoder", "ERROR: Invalid context or line_buffer or driver");
        return 0;
    }
    
    const int rect_w = rect->right - rect->left + 1;
    const int rect_h = rect->bottom - rect->top + 1;

    // Bounds check
    if (rect_w <= 0 || rect_h <= 0 || rect_w > ctx->buffer_width) {
        Logger.logMessagef("StripDecoder", "ERROR: Invalid rect (w=%d h=%d, buffer_width=%d)", rect_w, rect_h, ctx->buffer_width);
        return 0;
    }

    // Target LCD coordinates for the whole rect
    const int lcd_x = rect->left;
    const int lcd_y = ctx->strip_y_offset + rect->top;
    if (lcd_x < 0 || lcd_y < 0 || lcd_x + rect_w > ctx->lcd_width || lcd_y + rect_h > ctx->lcd_height) {
        Logger.logMessagef("StripDecoder", "ERROR: Invalid LCD rect: x=%d y=%d w=%d h=%d (LCD: %dx%d)",
                          lcd_x, lcd_y, rect_w, rect_h, ctx->lcd_width, ctx->lcd_height);
        return 0;
    }

    const int rect_pixels = rect_w * rect_h;
    const bool can_batch = ctx->batch_buffer &&
                           (ctx->batch_max_rows > 1) &&
                           (rect_h <= ctx->batch_max_rows) &&
                           (rect_pixels <= ctx->batch_capacity_pixels);

    if (can_batch) {
        // Convert entire rect into contiguous RGB565 pixels
        uint16_t* dst = ctx->batch_buffer;
        for (int row = 0; row < rect_h; row++) {
            for (int col = 0; col < rect_w; col++) {
                uint8_t r = *src++;
                uint8_t g = *src++;
                uint8_t b = *src++;

                if (ctx->output_bgr565) {
                    dst[row * rect_w + col] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
                } else {
                    dst[row * rect_w + col] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                }
            }
        }

        // Single LCD transaction for the whole rect
        ctx->driver->startWrite();
        ctx->driver->setAddrWindow(lcd_x, lcd_y, rect_w, rect_h);
        ctx->driver->pushColors(dst, rect_pixels, true);
        ctx->driver->endWrite();

        // Yield periodically to prevent watchdog timeouts.
        if ((lcd_y & 0x03) == 0) {
            taskYIELD();
        }

        return 1;
    }

    // Fallback: process each line (higher overhead but lower RAM).
    for (int y = rect->top; y <= rect->bottom; y++) {
        // Convert RGB888 to BGR565 or RGB565 for this line
        for (int x = 0; x < rect_w; x++) {
            uint8_t r = *src++;
            uint8_t g = *src++;
            uint8_t b = *src++;

            if (ctx->output_bgr565) {
                ctx->line_buffer[x] = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
            } else {
                ctx->line_buffer[x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            }
        }

        const int line_lcd_y = ctx->strip_y_offset + y;
        ctx->driver->startWrite();
        ctx->driver->setAddrWindow(lcd_x, line_lcd_y, rect_w, 1);
        ctx->driver->pushColors(ctx->line_buffer, rect_w, true);
        ctx->driver->endWrite();

        if ((line_lcd_y & 0x03) == 0) {
            taskYIELD();
        }
    }
    
    return 1;  // Continue decoding
}

// TJpgDec work buffer size (recommended minimum)
static const size_t TJPGD_WORK_BUFFER_SIZE = 4096;

StripDecoder::StripDecoder() : driver(nullptr), width(0), height(0), lcd_width(0), lcd_height(0), current_y(0) {
}

StripDecoder::~StripDecoder() {
    end();
}

void StripDecoder::setDisplayDriver(DisplayDriver* drv) {
    driver = drv;
}

void StripDecoder::free_buffers() {
    if (batch_buffer) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            heap_caps_free(batch_buffer);
        #else
            free(batch_buffer);
        #endif
        batch_buffer = nullptr;
    }
    batch_capacity_pixels = 0;
    batch_max_rows = 0;

    if (line_buffer) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            heap_caps_free(line_buffer);
        #else
            free(line_buffer);
        #endif
        line_buffer = nullptr;
    }
    line_buffer_width = 0;

    if (work_buffer) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            heap_caps_free(work_buffer);
        #else
            free(work_buffer);
        #endif
        work_buffer = nullptr;
    }
    work_buffer_size = 0;
}

bool StripDecoder::ensure_buffers() {
    if (width <= 0) {
        return false;
    }

    // Work buffer (fixed size)
    if (!work_buffer) {
        work_buffer_size = TJPGD_WORK_BUFFER_SIZE;
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            work_buffer = heap_caps_malloc(work_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        #else
            work_buffer = malloc(work_buffer_size);
        #endif
        if (!work_buffer) {
            Logger.logMessage("StripDecoder", "ERROR: Failed to allocate TJpgDec work buffer");
            return false;
        }
    }

    // Line buffer (width-dependent)
    if (!line_buffer || line_buffer_width != width) {
        if (line_buffer) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
                heap_caps_free(line_buffer);
            #else
                free(line_buffer);
            #endif
            line_buffer = nullptr;
        }

        const size_t bytes = (size_t)width * sizeof(uint16_t);
        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            line_buffer = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        #else
            line_buffer = (uint16_t*)malloc(bytes);
        #endif
        if (!line_buffer) {
            Logger.logMessage("StripDecoder", "ERROR: Failed to allocate line buffer");
            return false;
        }
        line_buffer_width = width;
    }

    // Optional batch buffer (width- and config-dependent)
    const int desired_rows = (int)IMAGE_STRIP_BATCH_MAX_ROWS;
    if (desired_rows <= 1) {
        // Batching disabled
        if (batch_buffer) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
                heap_caps_free(batch_buffer);
            #else
                free(batch_buffer);
            #endif
            batch_buffer = nullptr;
        }
        batch_capacity_pixels = 0;
        batch_max_rows = 0;
        return true;
    }

    const size_t batch_bytes = (size_t)width * (size_t)desired_rows * sizeof(uint16_t);
    const bool needs_new_batch = (!batch_buffer) || (batch_max_rows != desired_rows) || (batch_capacity_pixels != (width * desired_rows));
    if (needs_new_batch) {
        if (batch_buffer) {
            #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
                heap_caps_free(batch_buffer);
            #else
                free(batch_buffer);
            #endif
            batch_buffer = nullptr;
        }

        #if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
            // Prefer PSRAM when available to avoid pressuring internal RAM.
            batch_buffer = (uint16_t*)heap_caps_malloc(batch_bytes, MALLOC_CAP_SPIRAM);
            if (!batch_buffer) {
                batch_buffer = (uint16_t*)heap_caps_malloc(batch_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
        #else
            batch_buffer = (uint16_t*)malloc(batch_bytes);
        #endif

        if (batch_buffer) {
            batch_max_rows = desired_rows;
            batch_capacity_pixels = width * desired_rows;
        } else {
            // Batch buffer is optional; fall back to line-by-line flush.
            batch_max_rows = 0;
            batch_capacity_pixels = 0;
        }
    }

    return true;
}

void StripDecoder::begin(int image_width, int image_height, int lcd_w, int lcd_h) {
    width = image_width;
    height = image_height;
    lcd_width = lcd_w;
    lcd_height = lcd_h;
    current_y = 0;
    
    Logger.logMessagef("StripDecoder", "Begin decode: %dx%d image on %dx%d LCD", width, height, lcd_width, lcd_height);

    // Allocate per-session buffers once and reuse across strips.
    // If allocation fails, decoding will fail early in decode_strip().
    (void)ensure_buffers();
}

bool StripDecoder::decode_strip(const uint8_t* jpeg_data, size_t jpeg_size, int strip_index, bool output_bgr565) {
    if (!driver) {
        Logger.logMessage("StripDecoder", "ERROR: No display driver set");
        return false;
    }

    if (!ensure_buffers()) {
        Logger.logMessage("StripDecoder", "ERROR: Decoder buffers not available");
        return false;
    }
    
    Logger.logBegin("Strip");
    
    // Buffers are allocated once per session (begin/end) to reduce heap churn.
    const int kBatchMaxRows = batch_max_rows;
    
    JDEC jdec;
    JRESULT res;
    
    // Setup session context (shared between input and output callbacks)
    JpegSessionContext session_ctx;
    session_ctx.input.data = jpeg_data;
    session_ctx.input.size = jpeg_size;
    session_ctx.input.pos = 0;

    session_ctx.output.decoder = this;
    session_ctx.output.driver = driver;
    session_ctx.output.strip_y_offset = current_y;
    session_ctx.output.line_buffer = line_buffer;
    session_ctx.output.buffer_width = width;
    session_ctx.output.lcd_width = lcd_width;
    session_ctx.output.lcd_height = lcd_height;
    session_ctx.output.output_bgr565 = output_bgr565;
    session_ctx.output.batch_buffer = batch_buffer;
    session_ctx.output.batch_capacity_pixels = batch_buffer ? (width * kBatchMaxRows) : 0;
    session_ctx.output.batch_max_rows = batch_buffer ? kBatchMaxRows : 0;
    
    // Prepare decoder
    res = jd_prepare(&jdec, jpeg_input_func, work_buffer, (UINT)work_buffer_size, &session_ctx);
    
    if (res != JDR_OK) {
        Logger.logLinef("ERROR: jd_prepare failed: %d", res);
        Logger.logEnd();
        return false;
    }
    
    // Decompress and output to LCD
    res = jd_decomp(&jdec, jpeg_output_func, 0);  // 0 = 1:1 scale
    
    if (res != JDR_OK) {
        Logger.logLinef("ERROR: jd_decomp failed: %d", res);
        Logger.logEnd();
        return false;
    }

    // Buffered drivers (e.g., Arduino_GFX canvas) require an explicit present()
    // to flush the accumulated pixels to the physical panel.
    if (driver->renderMode() == DisplayDriver::RenderMode::Buffered) {
        driver->present();
    }
    
    // Move Y position for next strip
    current_y += jdec.height;
    
    return true;
}

void StripDecoder::end() {
    Logger.logMessagef("StripDecoder", "Complete at Y=%d", current_y);

    // Free session buffers so the heap can recover between image sessions.
    free_buffers();

    current_y = 0;
    width = 0;
    height = 0;
    lcd_width = 0;
    lcd_height = 0;
}

#endif // HAS_IMAGE_API
