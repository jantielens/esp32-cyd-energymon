#include "lvgl_heap.h"

#include <esp_heap_caps.h>

static inline bool psram_available() {
#if SOC_SPIRAM_SUPPORTED
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
    return false;
#endif
}

extern "C" void* lvgl_heap_malloc(size_t size) {
    if (size == 0) return nullptr;

    if (psram_available()) {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }

    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void* lvgl_heap_realloc(void* ptr, size_t size) {
    if (ptr == nullptr) return lvgl_heap_malloc(size);
    if (size == 0) {
        lvgl_heap_free(ptr);
        return nullptr;
    }

    if (psram_available()) {
        void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }

    // Fall back to internal 8-bit RAM.
    // If this fails, LVGL will handle the OOM.
    return heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void lvgl_heap_free(void* ptr) {
    if (!ptr) return;
    heap_caps_free(ptr);
}
