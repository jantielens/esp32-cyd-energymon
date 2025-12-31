#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_IMAGE_API

#include <lvgl.h>

#if LV_USE_IMG

#include "screen.h"
#include <stddef.h>
#include <stdint.h>

class LvglImageScreen : public Screen {
public:
    LvglImageScreen();
    ~LvglImageScreen() override;

    void create() override;
    void destroy() override;
    void show() override;
    void hide() override;
    void update() override;

    // Takes ownership of `pixels` (allocated with heap_caps_malloc/malloc).
    // Pixels are expected to be RGB565, w*h.
    bool setImageRgb565(uint16_t* pixels, int w, int h);

    void clearImage();

private:
    lv_obj_t* scr = nullptr;
    lv_obj_t* box = nullptr;
    lv_obj_t* img = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* placeholder = nullptr;

    lv_img_dsc_t img_dsc{};
    uint16_t* pixel_buf = nullptr;
    size_t pixel_buf_bytes = 0;

    void freePixelBuf();
};

#endif // LV_USE_IMG

#endif // HAS_DISPLAY && HAS_IMAGE_API
