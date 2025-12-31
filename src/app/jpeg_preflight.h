/*
 * JPEG Preflight Validation
 * 
 * Lightweight JPEG header scanner to validate TJpgDec compatibility.
 * Fails fast with descriptive errors instead of cryptic decode failures.
 */

#pragma once

#include "board_config.h"

#if HAS_IMAGE_API

#include <stddef.h>
#include <stdint.h>

// Validates a full-frame JPEG against exact dimensions.
// Returns true if the JPEG header looks compatible with TJpgDec, 
// else writes a human-friendly error message to err buffer.
bool jpeg_preflight_tjpgd_supported(
    const uint8_t* data,
    size_t size,
    int expected_width,
    int expected_height,
    char* err,
    size_t err_sz
);

// Validates a JPEG fragment (strip) against expected width and height bounds.
// max_height is typically the remaining image height for this fragment.
// panel_max_height is the display panel height cap.
bool jpeg_preflight_tjpgd_fragment_supported(
    const uint8_t* data,
    size_t size,
    int expected_width,
    int max_height,
    int panel_max_height,
    char* err,
    size_t err_sz
);

#endif // HAS_IMAGE_API
