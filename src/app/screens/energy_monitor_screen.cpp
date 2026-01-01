#include "energy_monitor_screen.h"

#include "log_manager.h"
#include "../energy_monitor.h"
#include "../board_config.h"
#include "../png_assets.h"

#include <math.h>

EnergyMonitorScreen::EnergyMonitorScreen(DeviceConfig* deviceConfig, DisplayManager* manager)
    : config(deviceConfig), displayMgr(manager) {}

EnergyMonitorScreen::~EnergyMonitorScreen() {
    destroy();
}

void EnergyMonitorScreen::create() {
    if (screen) return;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    background = lv_obj_create(screen);
    lv_obj_set_size(background, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(background, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(background, 0, 0);
    lv_obj_set_style_border_width(background, 0, 0);
    lv_obj_set_style_radius(background, 0, 0);
    lv_obj_set_style_bg_color(background, lv_color_black(), 0);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t col_dx = (int32_t)(LV_HOR_RES / 3);
    const int32_t arrow_dx = col_dx / 2;

    // Icons row
    solar_icon = lv_img_create(background);
    lv_img_set_src(solar_icon, &img_sun);
    lv_obj_set_style_img_recolor(solar_icon, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(solar_icon, LV_OPA_COVER, 0);
    lv_obj_align(solar_icon, LV_ALIGN_TOP_MID, -col_dx, 15);

    arrow1 = lv_label_create(background);
    lv_label_set_text(arrow1, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(arrow1, lv_color_white(), 0);
    lv_obj_align(arrow1, LV_ALIGN_TOP_MID, -arrow_dx, 25);
    lv_obj_add_flag(arrow1, LV_OBJ_FLAG_HIDDEN);

    home_icon = lv_img_create(background);
    lv_img_set_src(home_icon, &img_home);
    lv_obj_set_style_img_recolor(home_icon, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(home_icon, LV_OPA_COVER, 0);
    lv_obj_align(home_icon, LV_ALIGN_TOP_MID, 0, 15);

    arrow2 = lv_label_create(background);
    lv_label_set_text(arrow2, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(arrow2, lv_color_white(), 0);
    lv_obj_align(arrow2, LV_ALIGN_TOP_MID, arrow_dx, 25);
    lv_obj_add_flag(arrow2, LV_OBJ_FLAG_HIDDEN);

    grid_icon = lv_img_create(background);
    lv_img_set_src(grid_icon, &img_grid);
    lv_obj_set_style_img_recolor(grid_icon, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(grid_icon, LV_OPA_COVER, 0);
    lv_obj_align(grid_icon, LV_ALIGN_TOP_MID, col_dx, 15);

    // Values row
    solar_value = lv_label_create(background);
    lv_label_set_text(solar_value, "--");
    lv_obj_set_style_text_font(solar_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(solar_value, lv_color_white(), 0);
    lv_obj_align(solar_value, LV_ALIGN_TOP_MID, -col_dx, 80);

    home_value = lv_label_create(background);
    lv_label_set_text(home_value, "--");
    lv_obj_set_style_text_font(home_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(home_value, lv_color_white(), 0);
    lv_obj_align(home_value, LV_ALIGN_TOP_MID, 0, 80);

    grid_value = lv_label_create(background);
    lv_label_set_text(grid_value, "--");
    lv_obj_set_style_text_font(grid_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(grid_value, lv_color_white(), 0);
    lv_obj_align(grid_value, LV_ALIGN_TOP_MID, col_dx, 80);

    // Units row
    solar_unit = lv_label_create(background);
    lv_label_set_text(solar_unit, "kW");
    lv_obj_set_style_text_font(solar_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(solar_unit, lv_color_white(), 0);
    lv_obj_align(solar_unit, LV_ALIGN_TOP_MID, -col_dx, 115);

    home_unit = lv_label_create(background);
    lv_label_set_text(home_unit, "kW");
    lv_obj_set_style_text_font(home_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(home_unit, lv_color_white(), 0);
    lv_obj_align(home_unit, LV_ALIGN_TOP_MID, 0, 115);

    grid_unit = lv_label_create(background);
    lv_label_set_text(grid_unit, "kW");
    lv_obj_set_style_text_font(grid_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(grid_unit, lv_color_white(), 0);
    lv_obj_align(grid_unit, LV_ALIGN_TOP_MID, col_dx, 115);

    // Bar charts (manual: bg + fill) - LV_USE_BAR is disabled in lv_conf.h
    const int32_t bar_width = 12;
    const int32_t bar_height = 100;
    const int32_t bar_y = 140;
    const lv_color_t bar_bg_color = lv_color_make(0x33, 0x33, 0x33);

    auto init_bar = [&](lv_obj_t** bg_out, lv_obj_t** fill_out, int32_t x_off) {
        lv_obj_t* bg = lv_obj_create(background);
        lv_obj_set_size(bg, bar_width, bar_height);
        lv_obj_align(bg, LV_ALIGN_TOP_MID, x_off, bar_y);
        lv_obj_set_style_pad_all(bg, 0, 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        lv_obj_set_style_radius(bg, 0, 0);
        lv_obj_set_style_bg_color(bg, bar_bg_color, 0);
        lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* fill = lv_obj_create(bg);
        lv_obj_set_size(fill, bar_width, 0);
        lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);

        *bg_out = bg;
        *fill_out = fill;
    };

    init_bar(&solar_bar_bg, &solar_bar_fill, -col_dx);
    init_bar(&home_bar_bg, &home_bar_fill, 0);
    init_bar(&grid_bar_bg, &grid_bar_fill, col_dx);
}

void EnergyMonitorScreen::destroy() {
    if (screen) {
        lv_obj_del(screen);
        screen = nullptr;

        background = nullptr;
        solar_icon = nullptr;
        home_icon = nullptr;
        grid_icon = nullptr;
        arrow1 = nullptr;
        arrow2 = nullptr;
        solar_value = nullptr;
        home_value = nullptr;
        grid_value = nullptr;
        solar_unit = nullptr;
        home_unit = nullptr;
        grid_unit = nullptr;
        solar_bar_bg = nullptr;
        solar_bar_fill = nullptr;
        home_bar_bg = nullptr;
        home_bar_fill = nullptr;
        grid_bar_bg = nullptr;
        grid_bar_fill = nullptr;
    }
}

void EnergyMonitorScreen::show() {
    if (screen) {
        lv_scr_load(screen);
    }
}

void EnergyMonitorScreen::hide() {
    // LVGL handles screen switching
}

static void set_kw_label(lv_obj_t* label, float kw) {
    if (!label) return;

    if (isnan(kw)) {
        lv_label_set_text(label, "--");
        return;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", (double)kw);
    lv_label_set_text(label, buf);
}

static void set_kw_bar(lv_obj_t* fill, int32_t bar_width_px, int32_t bar_height_px, float kw, int32_t max_watts) {
    if (!fill) return;

    if (isnan(kw)) {
        lv_obj_set_size(fill, bar_width_px, 0);
        lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
        return;
    }

    if (max_watts <= 0) max_watts = 3000;

    int32_t watts = (int32_t)(fabs((double)kw) * 1000.0);
    if (watts < 0) watts = 0;
    if (watts > max_watts) watts = max_watts;

    int32_t fill_h = (int32_t)((watts * (int64_t)bar_height_px) / max_watts);
    if (watts > 0 && fill_h == 0) fill_h = 1;

    lv_obj_set_size(fill, bar_width_px, fill_h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static lv_color_t lv_color_from_rgb_u32(uint32_t rgb) {
    rgb &= 0xFFFFFFu;
    return lv_color_make((uint8_t)((rgb >> 16) & 0xFFu), (uint8_t)((rgb >> 8) & 0xFFu), (uint8_t)(rgb & 0xFFu));
}

static lv_color_t pick_category_color(const EnergyCategoryColorConfig* cfg, float kw, bool use_abs) {
    if (!cfg) return lv_color_white();
    if (isnan(kw)) return lv_color_white();

    const float v_kw = use_abs ? fabsf(kw) : kw;
    const float scaled = v_kw * 1000.0f;
    int32_t mkw = (int32_t)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));

    const int32_t t0 = cfg->threshold_mkw[0];
    const int32_t t1 = cfg->threshold_mkw[1];
    const int32_t t2 = cfg->threshold_mkw[2];

    uint32_t rgb = cfg->color_ok_rgb;
    if (mkw < t0) rgb = cfg->color_good_rgb;
    else if (mkw < t1) rgb = cfg->color_ok_rgb;
    else if (mkw < t2) rgb = cfg->color_attention_rgb;
    else rgb = cfg->color_warning_rgb;

    return lv_color_from_rgb_u32(rgb);
}

void EnergyMonitorScreen::update() {
    if (!screen) return;

    // Prefer event-driven updates (when values arrive), but also refresh periodically
    // so placeholders update if needed.
    const uint32_t now = millis();
    const uint32_t kFallbackRefreshMs = 500;

    EnergyMonitorState st = energy_monitor_get_state(true /*clear_updates*/);
    bool shouldRefresh = st.solar_updated || st.grid_updated;

    if (!shouldRefresh) {
        if (lastRenderMs != 0 && (uint32_t)(now - lastRenderMs) < kFallbackRefreshMs) {
            return;
        }
    }

    lastRenderMs = now;

    const float solar_kw = st.solar_value;
    const float grid_kw = st.grid_value;
    float home_kw = NAN;
    if (!isnan(solar_kw) && !isnan(grid_kw)) {
        home_kw = solar_kw + grid_kw;
    }

    set_kw_label(solar_value, solar_kw);
    set_kw_label(home_value, home_kw);
    set_kw_label(grid_value, grid_kw);

    const lv_color_t solar_color = pick_category_color(config ? &config->energy_solar_colors : nullptr, solar_kw, true /*use_abs*/);
    const lv_color_t home_color = pick_category_color(config ? &config->energy_home_colors : nullptr, home_kw, true /*use_abs*/);
    const lv_color_t grid_color = pick_category_color(config ? &config->energy_grid_colors : nullptr, grid_kw, false /*use_abs*/);

    // Apply colors to icons
    if (solar_icon) lv_obj_set_style_img_recolor(solar_icon, solar_color, 0);
    if (home_icon) lv_obj_set_style_img_recolor(home_icon, home_color, 0);
    if (grid_icon) lv_obj_set_style_img_recolor(grid_icon, grid_color, 0);

    // Apply colors to value/unit labels
    if (solar_value) lv_obj_set_style_text_color(solar_value, solar_color, 0);
    if (home_value) lv_obj_set_style_text_color(home_value, home_color, 0);
    if (grid_value) lv_obj_set_style_text_color(grid_value, grid_color, 0);
    if (solar_unit) lv_obj_set_style_text_color(solar_unit, solar_color, 0);
    if (home_unit) lv_obj_set_style_text_color(home_unit, home_color, 0);
    if (grid_unit) lv_obj_set_style_text_color(grid_unit, grid_color, 0);

    // Apply colors to bar fills
    if (solar_bar_fill) lv_obj_set_style_bg_color(solar_bar_fill, solar_color, 0);
    if (home_bar_fill) lv_obj_set_style_bg_color(home_bar_fill, home_color, 0);
    if (grid_bar_fill) lv_obj_set_style_bg_color(grid_bar_fill, grid_color, 0);

    // Arrow visibility/direction
    if (arrow1) {
        lv_obj_set_style_text_color(arrow1, solar_color, 0);
        if (!isnan(solar_kw) && solar_kw >= 0.01f) {
            lv_obj_clear_flag(arrow1, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(arrow1, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (arrow2) {
        lv_obj_set_style_text_color(arrow2, grid_color, 0);
        if (isnan(grid_kw)) {
            lv_obj_add_flag(arrow2, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(arrow2, grid_kw > 0.0f ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
            lv_obj_clear_flag(arrow2, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const int32_t bar_width = 12;
    const int32_t bar_height = 100;

    float solar_max_kw = (config && config->energy_solar_bar_max_kw > 0.0f) ? config->energy_solar_bar_max_kw : 3.0f;
    float home_max_kw = (config && config->energy_home_bar_max_kw > 0.0f) ? config->energy_home_bar_max_kw : 3.0f;
    float grid_max_kw = (config && config->energy_grid_bar_max_kw > 0.0f) ? config->energy_grid_bar_max_kw : 3.0f;

    int32_t solar_max_w = (int32_t)(solar_max_kw * 1000.0f);
    int32_t home_max_w = (int32_t)(home_max_kw * 1000.0f);
    int32_t grid_max_w = (int32_t)(grid_max_kw * 1000.0f);

    if (solar_max_w <= 0) solar_max_w = 3000;
    if (home_max_w <= 0) home_max_w = 3000;
    if (grid_max_w <= 0) grid_max_w = 3000;

    set_kw_bar(solar_bar_fill, bar_width, bar_height, solar_kw, solar_max_w);
    set_kw_bar(home_bar_fill, bar_width, bar_height, home_kw, home_max_w);
    set_kw_bar(grid_bar_fill, bar_width, bar_height, grid_kw, grid_max_w);
}
