#include "board_config.h"

#if HAS_DISPLAY && HAS_IMAGE_API

#include "lvgl_jpeg_decoder.h"

#if LV_USE_IMG

#include <Arduino.h>
#include <esp_heap_caps.h>

// TJpgDec ROM header selection copied from StripDecoder for broad ESP32-family compatibility.
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
    // Fall back to generic include if present.
    #if __has_include(<rom/tjpgd.h>)
        #include <rom/tjpgd.h>
    #else
        #error "Missing TJpgDec ROM header (tjpgd.h)"
    #endif
#endif

struct JpegInputContext {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

struct JpegOutputContext {
    uint16_t* dst = nullptr;
    int dst_w = 0;
    int dst_h = 0;
};

struct JpegSessionContext {
    JpegInputContext input;
    JpegOutputContext output;
};

static UINT jpeg_input_func(JDEC* jd, BYTE* buff, UINT nbyte) {
    JpegSessionContext* session = (JpegSessionContext*)jd->device;
    if (!session) return 0;

    JpegInputContext* ctx = &session->input;
    if (!ctx->data || ctx->pos >= ctx->size) return 0;

    const size_t remaining = ctx->size - ctx->pos;
    const size_t requested = (size_t)nbyte;
    const size_t to_read = (requested < remaining) ? requested : remaining;

    if (buff && to_read > 0) {
        memcpy(buff, ctx->data + ctx->pos, to_read);
    }

    ctx->pos += to_read;
    return (UINT)to_read;
}

static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static UINT jpeg_output_to_rgb565(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegSessionContext* session = (JpegSessionContext*)jd->device;
    if (!session) return 0;

    JpegOutputContext* out = &session->output;
    if (!out->dst || out->dst_w <= 0 || out->dst_h <= 0) return 0;

    const int rect_w = rect->right - rect->left + 1;
    const int rect_h = rect->bottom - rect->top + 1;
    if (rect_w <= 0 || rect_h <= 0) return 0;

    // Bounds check against output buffer.
    if (rect->left < 0 || rect->top < 0) return 0;
    if (rect->right >= out->dst_w || rect->bottom >= out->dst_h) return 0;

    uint8_t* src = (uint8_t*)bitmap;
    for (int row = 0; row < rect_h; row++) {
        const int y = rect->top + row;
        uint16_t* dst_row = out->dst + (size_t)y * (size_t)out->dst_w + (size_t)rect->left;
        for (int col = 0; col < rect_w; col++) {
            const uint8_t r = *src++;
            const uint8_t g = *src++;
            const uint8_t b = *src++;
            dst_row[col] = pack_rgb565(r, g, b);
        }

        // Yield periodically to avoid watchdog issues on single-core boards.
        if ((y & 0x07) == 0) {
            taskYIELD();
        }
    }

    return 1;
}

static void* alloc_any_8bit(size_t bytes) {
    if (bytes == 0) return nullptr;

#if SOC_SPIRAM_SUPPORTED
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (p) return p;
#endif

    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool lvgl_jpeg_decode_to_rgb565(
    const uint8_t* jpeg,
    size_t jpeg_size,
    uint16_t** out_pixels,
    int* out_w,
    int* out_h,
    int* out_scale_used,
    char* err,
    size_t err_len
) {
    if (!out_pixels || !out_w || !out_h) return false;
    *out_pixels = nullptr;
    *out_w = 0;
    *out_h = 0;
    if (out_scale_used) *out_scale_used = -1;

    if (!jpeg || jpeg_size < 4) {
        if (err && err_len) snprintf(err, err_len, "Invalid JPEG buffer");
        return false;
    }

    // TJpgDec work area.
    static const size_t kWorkSize = 4096;
    bool work_is_caps = true;
    void* work = heap_caps_malloc(kWorkSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work) {
        work_is_caps = false;
        work = malloc(kWorkSize);
    }
    if (!work) {
        if (err && err_len) snprintf(err, err_len, "Out of memory (work buffer)");
        return false;
    }

    auto free_work = [&]() {
        if (!work) return;
        if (work_is_caps) heap_caps_free(work);
        else free(work);
        work = nullptr;
    };

    // Best-effort: try full-res first, then 1/2, 1/4, 1/8 if the heap is fragmented.
    // TJpgDec scale factors: 0=1/1, 1=1/2, 2=1/4, 3=1/8
    for (uint8_t scale = 0; scale <= 3; scale++) {
        JDEC jd;
        JpegSessionContext session;
        session.input.data = jpeg;
        session.input.size = jpeg_size;
        session.input.pos = 0;

        JRESULT prep = jd_prepare(&jd, jpeg_input_func, (void*)work, (UINT)kWorkSize, &session);
        if (prep != JDR_OK) {
            free_work();
            if (err && err_len) snprintf(err, err_len, "JPEG prepare failed (%d)", (int)prep);
            return false;
        }

        const int src_w = (int)jd.width;
        const int src_h = (int)jd.height;
        if (src_w <= 0 || src_h <= 0) {
            free_work();
            if (err && err_len) snprintf(err, err_len, "Invalid JPEG dimensions");
            return false;
        }

        const int div = 1 << scale;
        const int outw = (src_w + div - 1) / div;
        const int outh = (src_h + div - 1) / div;
        if (outw <= 0 || outh <= 0) {
            continue;
        }

        const size_t pixel_bytes = (size_t)outw * (size_t)outh * 2;
        uint16_t* pixels = (uint16_t*)alloc_any_8bit(pixel_bytes);
        if (!pixels) {
            // Try smaller scale.
            continue;
        }

        session.output.dst = pixels;
        session.output.dst_w = outw;
        session.output.dst_h = outh;

        JRESULT dec = jd_decomp(&jd, jpeg_output_to_rgb565, scale);
        if (dec != JDR_OK) {
            heap_caps_free(pixels);
            // Try smaller scale.
            continue;
        }

        free_work();
        *out_pixels = pixels;
        *out_w = outw;
        *out_h = outh;
        if (out_scale_used) *out_scale_used = (int)scale;
        return true;
    }

    free_work();
    if (err && err_len) snprintf(err, err_len, "Out of memory (no scale fits)");
    return false;
}

#endif // LV_USE_IMG

#endif // HAS_DISPLAY && HAS_IMAGE_API
