#ifndef WARNING_SCREEN_H
#define WARNING_SCREEN_H

#include "screen.h"
#include "../config_manager.h"
#include <lvgl.h>

// ============================================================================
// Warning Screen
// ============================================================================
// Minimal black screen with a warning icon that moves periodically.

class WarningScreen : public Screen {
private:
    lv_obj_t* screen;
    lv_obj_t* icon;
    lv_timer_t* moveTimer;
    lv_timer_t* pulseTimer;

    DeviceConfig* config;
    uint8_t pulsePhase;
    int8_t pulseDir;

    static void moveTimerCb(lv_timer_t* timer);
    static void pulseTimerCb(lv_timer_t* timer);
    void moveIcon();
    void pulseTick();

public:
    explicit WarningScreen(DeviceConfig* config);
    ~WarningScreen();

    void create() override;
    void destroy() override;
    void show() override;
    void hide() override;
    void update() override;
};

#endif // WARNING_SCREEN_H
