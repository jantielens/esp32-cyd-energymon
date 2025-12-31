#include "lvgl_heap.h"

#include <esp_heap_caps.h>

extern "C" void* lvgl_heap_malloc(size_t size) {
    if (size == 0) return nullptr;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void* lvgl_heap_realloc(void* ptr, size_t size) {
    if (ptr == nullptr) return lvgl_heap_malloc(size);
    if (size == 0) {
        lvgl_heap_free(ptr);
        return nullptr;
    }

    // Keep LVGL allocations in internal 8-bit RAM to avoid placing LVGL objects in PSRAM.
    // If this fails, LVGL will handle the OOM.
    return heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void lvgl_heap_free(void* ptr) {
    if (!ptr) return;
    heap_caps_free(ptr);
}
