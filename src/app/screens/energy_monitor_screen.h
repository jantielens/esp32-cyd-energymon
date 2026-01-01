#ifndef ENERGY_MONITOR_SCREEN_H
#define ENERGY_MONITOR_SCREEN_H

#include "screen.h"
#include "../config_manager.h"
#include <lvgl.h>

class DisplayManager;

class EnergyMonitorScreen : public Screen {
private:
    lv_obj_t* screen = nullptr;
    DeviceConfig* config = nullptr;
    DisplayManager* displayMgr = nullptr;

    uint32_t lastRenderMs = 0;

    lv_obj_t* background = nullptr;

    lv_obj_t* solar_icon = nullptr;
    lv_obj_t* home_icon = nullptr;
    lv_obj_t* grid_icon = nullptr;

    lv_obj_t* arrow1 = nullptr;
    lv_obj_t* arrow2 = nullptr;

    lv_obj_t* solar_value = nullptr;
    lv_obj_t* home_value = nullptr;
    lv_obj_t* grid_value = nullptr;

    lv_obj_t* solar_unit = nullptr;
    lv_obj_t* home_unit = nullptr;
    lv_obj_t* grid_unit = nullptr;

    lv_obj_t* solar_bar_bg = nullptr;
    lv_obj_t* solar_bar_fill = nullptr;
    lv_obj_t* home_bar_bg = nullptr;
    lv_obj_t* home_bar_fill = nullptr;
    lv_obj_t* grid_bar_bg = nullptr;
    lv_obj_t* grid_bar_fill = nullptr;

    // T2 Warning (v1): breathing background + contrast remapping.
    enum class AlarmState : uint8_t {
        Off = 0,
        Active,
        Exiting,
    };

    AlarmState alarmState = AlarmState::Off;
    lv_timer_t* alarmTimer = nullptr;
    uint8_t alarmPhase = 0;   // 0..255 (black -> red)
    int8_t alarmDir = 1;      // +1 to ramp up, -1 to ramp down

    // Which categories are currently responsible for the T2 alarm.
    // Used to avoid animating/remapping non-alarm categories.
    bool alarmSolar = false;
    bool alarmHome = false;
    bool alarmGrid = false;

    // Cache the latest intended (non-alarm) colors so the timer can re-apply
    // contrast-safe colors while the background is animating.
    lv_color_t intendedSolarColor = lv_color_white();
    lv_color_t intendedHomeColor = lv_color_white();
    lv_color_t intendedGridColor = lv_color_white();

    static void alarmTimerCb(lv_timer_t* t);
    void alarmTick();
    void applyNormalStyles();
    void applyAlarmStyles();

public:
    EnergyMonitorScreen(DeviceConfig* deviceConfig, DisplayManager* manager);
    ~EnergyMonitorScreen();

    void create() override;
    void destroy() override;
    void show() override;
    void hide() override;
    void update() override;
};

#endif // ENERGY_MONITOR_SCREEN_H
