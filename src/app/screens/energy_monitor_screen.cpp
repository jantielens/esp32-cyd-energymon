#include "energy_monitor_screen.h"

#include "log_manager.h"
#include "../energy_monitor.h"
#include "../board_config.h"
#include "../png_assets.h"

#include <math.h>

static int32_t kw_to_mkw_round(float kw) {
    const float scaled = kw * 1000.0f;
    return (int32_t)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

static bool is_triggered_t2(const EnergyCategoryColorConfig* cfg, float kw, bool use_abs) {
    if (!cfg) return false;
    if (isnan(kw)) return false;

    const float v_kw = use_abs ? fabsf(kw) : kw;
    const int32_t mkw = kw_to_mkw_round(v_kw);
    return mkw >= cfg->threshold_mkw[2];
}

static bool is_cleared_t2(const EnergyCategoryColorConfig* cfg, float kw, bool use_abs, int32_t clear_hysteresis_mkw) {
    if (!cfg) return true;
    if (isnan(kw)) return true;

    const float v_kw = use_abs ? fabsf(kw) : kw;
    const int32_t mkw = kw_to_mkw_round(v_kw);
    int32_t clear_threshold = cfg->threshold_mkw[2] - clear_hysteresis_mkw;
    if (use_abs && clear_threshold < 0) clear_threshold = 0;
    return mkw < clear_threshold;
}

static lv_color_t contrast_remap_for_bg(lv_color_t intended, lv_color_t bg, uint8_t bg_strength_255) {
    // Hard-coded contrast policy:
    // If intended is too close to the pulsing background near its peak, blend toward white.
    const uint8_t kStart = 160;          // start remapping after ~63% into the pulse
    const uint8_t kLowContrast = 170;    // smaller => more aggressive remap

    if (bg_strength_255 <= kStart) return intended;

    const uint32_t c32 = lv_color_to32(intended);
    const uint8_t r = (uint8_t)((c32 >> 16) & 0xFFu);
    const uint8_t g = (uint8_t)((c32 >> 8) & 0xFFu);
    const uint8_t b = (uint8_t)(c32 & 0xFFu);

    const uint32_t b32 = lv_color_to32(bg);
    const uint8_t br = (uint8_t)((b32 >> 16) & 0xFFu);
    const uint8_t bgc = (uint8_t)((b32 >> 8) & 0xFFu);
    const uint8_t bb = (uint8_t)(b32 & 0xFFu);

    const int dr = (int)r - (int)br;
    const int dg = (int)g - (int)bgc;
    const int db = (int)b - (int)bb;
    const int dist = (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);

    if (dist >= kLowContrast) return intended;

    const uint8_t mix = (uint8_t)(((uint16_t)(bg_strength_255 - kStart) * 255u) / (uint16_t)(255u - kStart));
    return lv_color_mix(lv_color_white(), intended, mix);
}

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

    // Timer drives the alarm animation (background + contrast remap).
    // Start paused; it will be resumed when a T2 breach is detected.
    if (!alarmTimer) {
        alarmTimer = lv_timer_create(EnergyMonitorScreen::alarmTimerCb, 40 /*ms*/, this);
        lv_timer_pause(alarmTimer);
    }
}

void EnergyMonitorScreen::destroy() {
    if (screen) {
        if (alarmTimer) {
            lv_timer_del(alarmTimer);
            alarmTimer = nullptr;
        }
        alarmState = AlarmState::Off;
        alarmPhase = 0;
        alarmDir = 1;
        alarmPeakColor = lv_color_make(255, 0, 0);
        alarmClearStartMs = 0;

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

void EnergyMonitorScreen::alarmTimerCb(lv_timer_t* t) {
    EnergyMonitorScreen* self = (EnergyMonitorScreen*)t->user_data;
    if (self) self->alarmTick();
}

void EnergyMonitorScreen::alarmTick() {
    if (!screen || !background) return;
    if (alarmState == AlarmState::Off) return;

    const uint32_t tick_ms = alarmTimer ? alarmTimer->period : 40;
    uint16_t cycle_ms = config ? config->energy_alarm_pulse_cycle_ms : 2000;
    if (cycle_ms < 200) cycle_ms = 200;
    if (cycle_ms > 10000) cycle_ms = 10000;

    // 0->255 in half a cycle.
    const float step_f = (255.0f * 2.0f * (float)tick_ms) / (float)cycle_ms;
    uint8_t step_active = (uint8_t)lroundf(step_f);
    if (step_active < 1) step_active = 1;

    uint8_t step_exit = (uint8_t)(step_active + (step_active / 2)); // ~1.5x faster
    if (step_exit < step_active) step_exit = step_active;

    const uint8_t step = (alarmState == AlarmState::Exiting) ? step_exit : step_active;

    int next = (int)alarmPhase + (int)alarmDir * (int)step;
    if (next >= 255) {
        next = 255;
        alarmDir = -1;
    } else if (next <= 0) {
        next = 0;
        // If we're exiting and reached dark, stop the alarm cleanly.
        if (alarmState == AlarmState::Exiting) {
            alarmState = AlarmState::Off;
            alarmPhase = 0;
            alarmPeakColor = lv_color_make(255, 0, 0);
            alarmClearStartMs = 0;
            if (alarmTimer) lv_timer_pause(alarmTimer);
            applyNormalStyles();
            return;
        }
        alarmDir = 1;
    }

    alarmPhase = (uint8_t)next;
    applyAlarmStyles();
}

void EnergyMonitorScreen::applyNormalStyles() {
    if (!screen || !background) return;

    // Normal background
    lv_obj_set_style_bg_color(background, lv_color_black(), 0);

    // Apply cached intended colors.
    if (solar_icon) lv_obj_set_style_img_recolor(solar_icon, intendedSolarColor, 0);
    if (home_icon) lv_obj_set_style_img_recolor(home_icon, intendedHomeColor, 0);
    if (grid_icon) lv_obj_set_style_img_recolor(grid_icon, intendedGridColor, 0);

    if (solar_value) lv_obj_set_style_text_color(solar_value, intendedSolarColor, 0);
    if (home_value) lv_obj_set_style_text_color(home_value, intendedHomeColor, 0);
    if (grid_value) lv_obj_set_style_text_color(grid_value, intendedGridColor, 0);
    if (solar_unit) lv_obj_set_style_text_color(solar_unit, intendedSolarColor, 0);
    if (home_unit) lv_obj_set_style_text_color(home_unit, intendedHomeColor, 0);
    if (grid_unit) lv_obj_set_style_text_color(grid_unit, intendedGridColor, 0);

    if (solar_bar_fill) lv_obj_set_style_bg_color(solar_bar_fill, intendedSolarColor, 0);
    if (home_bar_fill) lv_obj_set_style_bg_color(home_bar_fill, intendedHomeColor, 0);
    if (grid_bar_fill) lv_obj_set_style_bg_color(grid_bar_fill, intendedGridColor, 0);

    // Arrows are colored in update() based on direction; keep their current glyph logic,
    // but ensure their colors follow the same intended palette.
    if (arrow1) lv_obj_set_style_text_color(arrow1, intendedSolarColor, 0);
    if (arrow2) lv_obj_set_style_text_color(arrow2, intendedGridColor, 0);
}

void EnergyMonitorScreen::applyAlarmStyles() {
    if (!screen || !background) return;

    uint8_t peak_pct = config ? config->energy_alarm_pulse_peak_pct : 100;
    if (peak_pct > 100) peak_pct = 100;
    const uint16_t scaledMix16 = (uint16_t)alarmPhase * (uint16_t)peak_pct / 100u;
    const uint8_t mix = (scaledMix16 > 255u) ? 255u : (uint8_t)scaledMix16;

    // Background: dark -> peak color -> dark.
    const lv_color_t bg = lv_color_mix(alarmPeakColor, lv_color_black(), mix);
    lv_obj_set_style_bg_color(background, bg, 0);

    // Remap only the categories that are actually causing the alarm (>= T2).
    // Non-alarm categories keep their intended color even at full-red peak.
    const lv_color_t solar = alarmSolar ? contrast_remap_for_bg(intendedSolarColor, bg, mix) : intendedSolarColor;
    const lv_color_t home = alarmHome ? contrast_remap_for_bg(intendedHomeColor, bg, mix) : intendedHomeColor;
    const lv_color_t grid = alarmGrid ? contrast_remap_for_bg(intendedGridColor, bg, mix) : intendedGridColor;

    if (solar_icon) lv_obj_set_style_img_recolor(solar_icon, solar, 0);
    if (home_icon) lv_obj_set_style_img_recolor(home_icon, home, 0);
    if (grid_icon) lv_obj_set_style_img_recolor(grid_icon, grid, 0);

    if (solar_value) lv_obj_set_style_text_color(solar_value, solar, 0);
    if (home_value) lv_obj_set_style_text_color(home_value, home, 0);
    if (grid_value) lv_obj_set_style_text_color(grid_value, grid, 0);
    if (solar_unit) lv_obj_set_style_text_color(solar_unit, solar, 0);
    if (home_unit) lv_obj_set_style_text_color(home_unit, home, 0);
    if (grid_unit) lv_obj_set_style_text_color(grid_unit, grid, 0);

    if (solar_bar_fill) lv_obj_set_style_bg_color(solar_bar_fill, solar, 0);
    if (home_bar_fill) lv_obj_set_style_bg_color(home_bar_fill, home, 0);
    if (grid_bar_fill) lv_obj_set_style_bg_color(grid_bar_fill, grid, 0);

    if (arrow1) lv_obj_set_style_text_color(arrow1, solar, 0);
    if (arrow2) lv_obj_set_style_text_color(arrow2, grid, 0);
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

    // Cache intended colors for the timer-driven alarm renderer.
    intendedSolarColor = solar_color;
    intendedHomeColor = home_color;
    intendedGridColor = grid_color;

    const bool prevSolarAlarm = alarmSolar;
    const bool prevHomeAlarm = alarmHome;
    const bool prevGridAlarm = alarmGrid;

    int32_t clear_hyst = config ? config->energy_alarm_clear_hysteresis_mkw : 100;
    if (clear_hyst < 0) clear_hyst = 0;

    // Per-category T2 alarm state with hysteresis (anti-flicker).
    const EnergyCategoryColorConfig* solar_cfg = config ? &config->energy_solar_colors : nullptr;
    const EnergyCategoryColorConfig* home_cfg = config ? &config->energy_home_colors : nullptr;
    const EnergyCategoryColorConfig* grid_cfg = config ? &config->energy_grid_colors : nullptr;

    alarmSolar = prevSolarAlarm
        ? !is_cleared_t2(solar_cfg, solar_kw, true, clear_hyst)
        : is_triggered_t2(solar_cfg, solar_kw, true);
    alarmHome = prevHomeAlarm
        ? !is_cleared_t2(home_cfg, home_kw, true, clear_hyst)
        : is_triggered_t2(home_cfg, home_kw, true);
    alarmGrid = prevGridAlarm
        ? !is_cleared_t2(grid_cfg, grid_kw, false, clear_hyst)
        : is_triggered_t2(grid_cfg, grid_kw, false);

    const bool alarmWanted = alarmSolar || alarmHome || alarmGrid;

    if (alarmWanted) {
        alarmClearStartMs = 0;
        if (alarmState == AlarmState::Off || alarmState == AlarmState::Exiting) {
            if (alarmState == AlarmState::Off) {
                // Latch peak background color: warning color of the first category that triggers.
                if (alarmSolar && !prevSolarAlarm && solar_cfg) alarmPeakColor = lv_color_from_rgb_u32(solar_cfg->color_warning_rgb);
                else if (alarmHome && !prevHomeAlarm && home_cfg) alarmPeakColor = lv_color_from_rgb_u32(home_cfg->color_warning_rgb);
                else if (alarmGrid && !prevGridAlarm && grid_cfg) alarmPeakColor = lv_color_from_rgb_u32(grid_cfg->color_warning_rgb);
                else if (alarmSolar && solar_cfg) alarmPeakColor = lv_color_from_rgb_u32(solar_cfg->color_warning_rgb);
                else if (alarmHome && home_cfg) alarmPeakColor = lv_color_from_rgb_u32(home_cfg->color_warning_rgb);
                else if (alarmGrid && grid_cfg) alarmPeakColor = lv_color_from_rgb_u32(grid_cfg->color_warning_rgb);
            }
            alarmState = AlarmState::Active;
            alarmDir = 1;
            if (alarmTimer) lv_timer_resume(alarmTimer);
        }
    } else {
        if (alarmState == AlarmState::Active) {
            uint16_t clear_delay = config ? config->energy_alarm_clear_delay_ms : 800;
            if (clear_delay > 60000) clear_delay = 60000;

            if (clear_delay == 0) {
                alarmState = AlarmState::Exiting;
                alarmDir = -1;
                alarmClearStartMs = 0;
                if (alarmTimer) lv_timer_resume(alarmTimer);
            } else {
                if (alarmClearStartMs == 0) alarmClearStartMs = now;
                if ((uint32_t)(now - alarmClearStartMs) >= (uint32_t)clear_delay) {
                    alarmState = AlarmState::Exiting;
                    alarmDir = -1;
                    alarmClearStartMs = 0;
                    if (alarmTimer) lv_timer_resume(alarmTimer);
                }
            }
        }
    }

    // Apply colors. If the alarm is active/exiting, the timer owns the visual styles
    // (background pulse + contrast remap) so we only update cached intended colors.
    if (alarmState == AlarmState::Off) {
        applyNormalStyles();
    } else {
        applyAlarmStyles();
    }

    // Arrow visibility/direction
    if (arrow1) {
        // Color will be handled by applyNormalStyles/applyAlarmStyles.
        if (!isnan(solar_kw) && solar_kw >= 0.01f) {
            lv_obj_clear_flag(arrow1, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(arrow1, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (arrow2) {
        // Color will be handled by applyNormalStyles/applyAlarmStyles.
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
