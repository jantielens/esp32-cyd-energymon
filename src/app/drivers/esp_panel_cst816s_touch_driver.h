#ifndef ESP_PANEL_CST816S_TOUCH_DRIVER_H
#define ESP_PANEL_CST816S_TOUCH_DRIVER_H

#include "../touch_driver.h"

#include <ESP_Panel_Library.h>

class ESPPanel_CST816S_TouchDriver : public TouchDriver {
public:
    ESPPanel_CST816S_TouchDriver();
    ~ESPPanel_CST816S_TouchDriver() override;

    void init() override;

    bool isTouched() override;
    bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;

    void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
    void setRotation(uint8_t rotation) override;

private:
    ESP_PanelTouch* touch;
    uint8_t rotation;

    bool calibrationEnabled;
    uint16_t calXMin;
    uint16_t calXMax;
    uint16_t calYMin;
    uint16_t calYMax;

    void applyRotation(uint16_t& x, uint16_t& y) const;
};

#endif // ESP_PANEL_CST816S_TOUCH_DRIVER_H
