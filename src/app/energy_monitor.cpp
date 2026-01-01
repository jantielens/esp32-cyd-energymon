#include "energy_monitor.h"

#include <math.h>

// FreeRTOS critical section for cross-task access.
// (LVGL task reads; Arduino loop / MQTT callback writes.)
static portMUX_TYPE s_energy_mux = portMUX_INITIALIZER_UNLOCKED;
static EnergyMonitorState s_state;

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
