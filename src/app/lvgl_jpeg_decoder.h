#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_IMAGE_API

#include <lvgl.h>

#if LV_USE_IMG

#include <stddef.h>
#include <stdint.h>

// Decode a baseline JPEG into an RGB565 pixel buffer.
//
// - Allocates the output buffer with heap_caps_malloc/malloc (caller owns).
// - Returns false with a short error string on failure.
// - output_bgr565 is not supported here; output is always RGB565.
bool lvgl_jpeg_decode_to_rgb565(
    const uint8_t* jpeg,
    size_t jpeg_size,
    uint16_t** out_pixels,
    int* out_w,
    int* out_h,
    int* out_scale_used,
    char* err,
    size_t err_len
);

#endif // LV_USE_IMG

#endif // HAS_DISPLAY && HAS_IMAGE_API
