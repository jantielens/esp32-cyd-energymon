/*
 * JPEG Preflight Validation Implementation
 */

#include "board_config.h"

#if HAS_IMAGE_API

#include "jpeg_preflight.h"
#include <stdio.h>

struct JpegSofInfo {
    bool found = false;
    bool progressive = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t components = 0;
    // Sampling factors (h,v) for component IDs 1(Y),2(Cb),3(Cr)
    uint8_t y_h = 0, y_v = 0;
    uint8_t cb_h = 0, cb_v = 0;
    uint8_t cr_h = 0, cr_v = 0;
};

static bool jpeg_parse_sof_best_effort(const uint8_t* data, size_t size, JpegSofInfo& out) {
    if (!data || size < 4) return false;
    // Must start with SOI
    if (!(data[0] == 0xFF && data[1] == 0xD8)) return false;

    size_t i = 2;
    while (i + 3 < size) {
        // Find marker prefix 0xFF
        if (data[i] != 0xFF) {
            i++;
            continue;
        }

        // Skip fill bytes 0xFF
        while (i < size && data[i] == 0xFF) i++;
        if (i >= size) break;

        const uint8_t marker = data[i++];

        // Standalone markers without length
        if (marker == 0xD8 || marker == 0xD9) continue; // SOI/EOI
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue; // TEM / RSTn

        // Start of Scan: header ends; no more metadata segments reliably parseable
        if (marker == 0xDA) break;

        if (i + 1 >= size) break;
        const uint16_t seg_len = (uint16_t)((data[i] << 8) | data[i + 1]);
        if (seg_len < 2) return false;
        if (i + seg_len > size) return false;

        // SOF0 (baseline DCT) or SOF2 (progressive DCT)
        if (marker == 0xC0 || marker == 0xC2) {
            out.found = true;
            out.progressive = (marker == 0xC2);
            if (seg_len < 8) return false;
            const size_t p = i + 2;
            // p+0: precision
            out.height = (uint16_t)((data[p + 1] << 8) | data[p + 2]);
            out.width  = (uint16_t)((data[p + 3] << 8) | data[p + 4]);
            out.components = data[p + 5];
            size_t cpos = p + 6;
            for (uint8_t c = 0; c < out.components; c++) {
                if (cpos + 2 >= i + seg_len) break;
                const uint8_t cid = data[cpos + 0];
                const uint8_t hv  = data[cpos + 1];
                const uint8_t h = hv >> 4;
                const uint8_t v = hv & 0x0F;
                if (cid == 1) { out.y_h = h; out.y_v = v; }
                else if (cid == 2) { out.cb_h = h; out.cb_v = v; }
                else if (cid == 3) { out.cr_h = h; out.cr_v = v; }
                cpos += 3;
            }
            return true;
        }

        // Move to next segment
        i += seg_len;
    }

    return out.found;
}

static bool jpeg_preflight_common(
    const JpegSofInfo& info,
    char* err,
    size_t err_sz
) {
    if (info.progressive) {
        snprintf(err, err_sz, "Unsupported JPEG: progressive encoding (use baseline JPEG)");
        return false;
    }

    // Allow grayscale
    if (info.components == 1) {
        return true;
    }

    // Only accept 3-component JPEG with standard sampling patterns
    if (info.components != 3) {
        snprintf(err, err_sz, "Unsupported JPEG: expected 1 (grayscale) or 3 components, got %u", (unsigned)info.components);
        return false;
    }

    // TJpgDec expects Cb/Cr to be 1x1
    if (!(info.cb_h == 1 && info.cb_v == 1 && info.cr_h == 1 && info.cr_v == 1)) {
        snprintf(err, err_sz, "Unsupported JPEG sampling: Cb/Cr must be 1x1 (got Cb %ux%u, Cr %ux%u)",
                 (unsigned)info.cb_h, (unsigned)info.cb_v, (unsigned)info.cr_h, (unsigned)info.cr_v);
        return false;
    }

    // Y can be 1x1 (4:4:4), 2x1 (4:2:2) or 2x2 (4:2:0). Reject uncommon layouts like 1x2.
    const bool y_ok = (info.y_h == 1 && info.y_v == 1) || (info.y_h == 2 && info.y_v == 1) || (info.y_h == 2 && info.y_v == 2);
    if (!y_ok) {
        snprintf(err, err_sz, "Unsupported JPEG sampling: Y must be 1x1, 2x1, or 2x2 (got %ux%u)", (unsigned)info.y_h, (unsigned)info.y_v);
        return false;
    }

    return true;
}

bool jpeg_preflight_tjpgd_supported(
    const uint8_t* data,
    size_t size,
    int expected_width,
    int expected_height,
    char* err,
    size_t err_sz
) {
    JpegSofInfo info;
    if (!jpeg_parse_sof_best_effort(data, size, info) || !info.found) {
        snprintf(err, err_sz, "Invalid JPEG header (missing SOF marker)");
        return false;
    }

    if ((int)info.width != expected_width || (int)info.height != expected_height) {
        snprintf(err, err_sz, "Unsupported JPEG dimensions: got %ux%u, expected %dx%d",
                 (unsigned)info.width, (unsigned)info.height, expected_width, expected_height);
        return false;
    }

    return jpeg_preflight_common(info, err, err_sz);
}

bool jpeg_preflight_tjpgd_fragment_supported(
    const uint8_t* data,
    size_t size,
    int expected_width,
    int max_height,
    int panel_max_height,
    char* err,
    size_t err_sz
) {
    JpegSofInfo info;
    if (!jpeg_parse_sof_best_effort(data, size, info) || !info.found) {
        snprintf(err, err_sz, "Invalid JPEG header (missing SOF marker)");
        return false;
    }

    if ((int)info.width != expected_width) {
        snprintf(err, err_sz, "Unsupported JPEG fragment width: got %u, expected %d", (unsigned)info.width, expected_width);
        return false;
    }

    const int h = (int)info.height;
    if (h <= 0 || h > max_height || h > panel_max_height) {
        snprintf(err, err_sz, "Unsupported JPEG fragment height: got %u (max %d)", (unsigned)info.height, max_height);
        return false;
    }

    return jpeg_preflight_common(info, err, err_sz);
}

#endif // HAS_IMAGE_API
