#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* lvgl_heap_malloc(size_t size);
void* lvgl_heap_realloc(void* ptr, size_t size);
void  lvgl_heap_free(void* ptr);

#ifdef __cplusplus
}
#endif
