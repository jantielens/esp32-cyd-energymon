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
