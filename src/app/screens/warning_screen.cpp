#include "screens/warning_screen.h"
#include "log_manager.h"
#include "png_assets.h"

#include <esp_system.h>
#include <math.h>

namespace {
constexpr int32_t kWarningIconSizePx = 100;
constexpr uint32_t kWarningMoveIntervalMs = 60000;
}

WarningScreen::WarningScreen(DeviceConfig* cfg)
    : screen(nullptr), icon(nullptr), moveTimer(nullptr), pulseTimer(nullptr), config(cfg), pulsePhase(0), pulseDir(1) {}

WarningScreen::~WarningScreen() {
    destroy();
}

void WarningScreen::create() {
    if (screen) return;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    icon = lv_img_create(screen);
    lv_img_set_src(icon, &img_warning);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_img_opa(icon, LV_OPA_COVER, 0);

    moveTimer = lv_timer_create(WarningScreen::moveTimerCb, kWarningMoveIntervalMs, this);
    lv_timer_pause(moveTimer);

    pulseTimer = lv_timer_create(WarningScreen::pulseTimerCb, 40, this);
    lv_timer_pause(pulseTimer);

    moveIcon();
}

void WarningScreen::destroy() {
    if (moveTimer) {
        lv_timer_del(moveTimer);
        moveTimer = nullptr;
    }

    if (pulseTimer) {
        lv_timer_del(pulseTimer);
        pulseTimer = nullptr;
    }

    if (screen) {
        lv_obj_del(screen);
        screen = nullptr;
        icon = nullptr;
    }
}

void WarningScreen::show() {
    if (!screen) create();
    if (screen) {
        lv_scr_load(screen);
    }
    moveIcon();
    pulsePhase = 0;
    pulseDir = 1;
    if (moveTimer) {
        lv_timer_resume(moveTimer);
    }
    if (pulseTimer) {
        lv_timer_resume(pulseTimer);
    }
}

void WarningScreen::hide() {
    if (moveTimer) {
        lv_timer_pause(moveTimer);
    }
    if (pulseTimer) {
        lv_timer_pause(pulseTimer);
    }
    pulsePhase = 0;
    pulseDir = 1;
    if (icon) {
        lv_obj_set_style_img_opa(icon, LV_OPA_COVER, 0);
    }
}

void WarningScreen::update() {
    // No-op: timer handles movement.
}

void WarningScreen::moveTimerCb(lv_timer_t* timer) {
    if (!timer) return;
    WarningScreen* self = static_cast<WarningScreen*>(timer->user_data);
    if (!self) return;
    self->moveIcon();
}

void WarningScreen::pulseTimerCb(lv_timer_t* timer) {
    if (!timer) return;
    WarningScreen* self = static_cast<WarningScreen*>(timer->user_data);
    if (!self) return;
    self->pulseTick();
}

void WarningScreen::moveIcon() {
    if (!icon) return;

    const int32_t max_x = (LV_HOR_RES > kWarningIconSizePx) ? (LV_HOR_RES - kWarningIconSizePx) : 0;
    const int32_t max_y = (LV_VER_RES > kWarningIconSizePx) ? (LV_VER_RES - kWarningIconSizePx) : 0;

    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();
    const int32_t x = (max_x > 0) ? (int32_t)(r1 % (uint32_t)(max_x + 1)) : 0;
    const int32_t y = (max_y > 0) ? (int32_t)(r2 % (uint32_t)(max_y + 1)) : 0;

    lv_obj_set_pos(icon, x, y);
}

void WarningScreen::pulseTick() {
    if (!icon) return;

    const uint32_t tick_ms = pulseTimer ? pulseTimer->period : 40;
    uint16_t cycle_ms = config ? config->energy_alarm_pulse_cycle_ms : 2000;
    if (cycle_ms < 200) cycle_ms = 200;
    if (cycle_ms > 10000) cycle_ms = 10000;

    const float step_f = (255.0f * 2.0f * (float)tick_ms) / (float)cycle_ms;
    uint8_t step = (uint8_t)lroundf(step_f);
    if (step < 1) step = 1;

    int next = (int)pulsePhase + (int)pulseDir * (int)step;
    if (next >= 255) {
        next = 255;
        pulseDir = -1;
    } else if (next <= 0) {
        next = 0;
        pulseDir = 1;
    }

    pulsePhase = (uint8_t)next;

    uint8_t peak_pct = config ? config->energy_alarm_pulse_peak_pct : 100;
    if (peak_pct > 100) peak_pct = 100;
    const uint16_t scaledMix16 = (uint16_t)pulsePhase * (uint16_t)peak_pct / 100u;
    const uint8_t mix = (scaledMix16 > 255u) ? 255u : (uint8_t)scaledMix16;

    lv_obj_set_style_img_opa(icon, mix, 0);
}
