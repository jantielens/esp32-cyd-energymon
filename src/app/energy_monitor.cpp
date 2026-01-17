#include "energy_monitor.h"

#include "config_manager.h"

#include <math.h>

// FreeRTOS critical section for cross-task access.
// (LVGL task reads; Arduino loop / MQTT callback writes.)
static portMUX_TYPE s_energy_mux = portMUX_INITIALIZER_UNLOCKED;
static EnergyMonitorState s_state;

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

void energy_monitor_init() {
    portENTER_CRITICAL(&s_energy_mux);
    s_state.solar_value = NAN;
    s_state.grid_value = NAN;
    s_state.solar_updated = false;
    s_state.grid_updated = false;
    s_state.solar_update_ms = 0;
    s_state.grid_update_ms = 0;
    portEXIT_CRITICAL(&s_energy_mux);
}

void energy_monitor_set_solar(float value, uint32_t now_ms) {
    portENTER_CRITICAL(&s_energy_mux);
    s_state.solar_value = value;
    s_state.solar_updated = true;
    s_state.solar_update_ms = now_ms;
    portEXIT_CRITICAL(&s_energy_mux);
}

void energy_monitor_set_grid(float value, uint32_t now_ms) {
    portENTER_CRITICAL(&s_energy_mux);
    s_state.grid_value = value;
    s_state.grid_updated = true;
    s_state.grid_update_ms = now_ms;
    portEXIT_CRITICAL(&s_energy_mux);
}

EnergyMonitorState energy_monitor_get_state(bool clear_updates) {
    EnergyMonitorState copy;
    portENTER_CRITICAL(&s_energy_mux);
    copy = s_state;
    if (clear_updates) {
        s_state.solar_updated = false;
        s_state.grid_updated = false;
    }
    portEXIT_CRITICAL(&s_energy_mux);
    return copy;
}

bool energy_monitor_has_warning(const DeviceConfig* config) {
    if (!config) return false;

    const EnergyMonitorState st = energy_monitor_get_state(false /*clear_updates*/);
    const float solar_kw = st.solar_value;
    const float grid_kw = st.grid_value;
    float home_kw = NAN;
    if (!isnan(solar_kw) && !isnan(grid_kw)) {
        home_kw = solar_kw + grid_kw;
    }

    if (is_triggered_t2(&config->energy_solar_colors, solar_kw, true /*use_abs*/)) return true;
    if (is_triggered_t2(&config->energy_home_colors, home_kw, true /*use_abs*/)) return true;
    if (is_triggered_t2(&config->energy_grid_colors, grid_kw, false /*use_abs*/)) return true;

    return false;
}
