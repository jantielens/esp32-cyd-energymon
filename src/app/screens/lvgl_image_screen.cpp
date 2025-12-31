#include "board_config.h"

#if HAS_DISPLAY && HAS_IMAGE_API

#include "lvgl_image_screen.h"
#include "log_manager.h"

#if LV_USE_IMG

LvglImageScreen::LvglImageScreen() {
}

LvglImageScreen::~LvglImageScreen() {
    destroy();
}

void LvglImageScreen::create() {
    if (scr) return;

    scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Minimal visual indicator so it's obvious this screen is active.
    title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL Image");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    // Fixed display box so the image always occupies 200x200 on screen.
    // The decoded image may be smaller due to heap fragmentation; we zoom it to fit.
    box = lv_obj_create(scr);
    lv_obj_set_size(box, 200, 200);
    // Move the image down slightly so it doesn't overlap the title label.
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 16);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Ensure children are rendered flush to the box and get clipped cleanly.
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);

    // Prevent the image from bleeding past rounded corners (theme may apply radius).
    // LVGL v8 uses a style property to clip children to the object's rounded corners.
    lv_obj_set_style_clip_corner(box, true, 0);

    img = lv_img_create(box);
    lv_obj_center(img);

    placeholder = lv_label_create(box);
    lv_label_set_text(placeholder, "No image loaded");
    lv_obj_center(placeholder);

    // Default: no image.
    lv_img_set_src(img, nullptr);
}

void LvglImageScreen::destroy() {
    freePixelBuf();

    if (scr) {
        lv_obj_del(scr);
        scr = nullptr;
        box = nullptr;
        img = nullptr;
        title = nullptr;
        placeholder = nullptr;
    }
}

void LvglImageScreen::show() {
    if (!scr) create();
    lv_scr_load(scr);
}

void LvglImageScreen::hide() {
    // No-op
}

void LvglImageScreen::update() {
    // No-op
}

void LvglImageScreen::freePixelBuf() {
    if (pixel_buf) {
        heap_caps_free(pixel_buf);
        pixel_buf = nullptr;
        pixel_buf_bytes = 0;
    }
    memset(&img_dsc, 0, sizeof(img_dsc));
}

void LvglImageScreen::clearImage() {
    freePixelBuf();
    if (img) {
        lv_img_set_src(img, nullptr);
        #if LV_USE_IMG_TRANSFORM
        lv_img_set_zoom(img, 256);
        #endif
        if (box) lv_obj_center(img);
    }
    if (placeholder) {
        lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

bool LvglImageScreen::setImageRgb565(uint16_t* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) {
        return false;
    }

    if (!img) {
        // Screen not created yet; still accept and store.
        if (!scr) create();
    }

    // Replace any previous image.
    freePixelBuf();

    pixel_buf = pixels;
    pixel_buf_bytes = (size_t)w * (size_t)h * 2;

    img_dsc.header.always_zero = 0;
    img_dsc.header.w = (uint32_t)w;
    img_dsc.header.h = (uint32_t)h;
    img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    img_dsc.data_size = pixel_buf_bytes;
    img_dsc.data = (const uint8_t*)pixel_buf;

    lv_img_set_src(img, &img_dsc);

    // Keep it centered inside the fixed 200x200 box.
    if (box) lv_obj_center(img);

    #if LV_USE_IMG_TRANSFORM
    // Scale to fit within 200x200. For square album art this becomes exactly 200x200.
    // LVGL zoom: 256 = 1x.
    const uint32_t target = 200U;
    const uint32_t zoom_x = target * 256U / (uint32_t)w;
    const uint32_t zoom_y = target * 256U / (uint32_t)h;
    uint32_t zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y; // contain

    // Reasonable clamp: allow up to 16x for tiny decode outputs.
    if (zoom < 16) zoom = 16;
    if (zoom > 4096) zoom = 4096;
    lv_img_set_zoom(img, (uint16_t)zoom);
    #endif

    if (placeholder) {
        lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

#endif // LV_USE_IMG

#endif // HAS_DISPLAY && HAS_IMAGE_API
