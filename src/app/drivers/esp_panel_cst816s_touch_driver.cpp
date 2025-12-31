#include "esp_panel_cst816s_touch_driver.h"

#include "../board_config.h"
#include "../log_manager.h"

ESPPanel_CST816S_TouchDriver::ESPPanel_CST816S_TouchDriver()
    : touch(nullptr), rotation(0), calibrationEnabled(false),
      calXMin(0), calXMax(0), calYMin(0), calYMax(0) {}

ESPPanel_CST816S_TouchDriver::~ESPPanel_CST816S_TouchDriver() {
    if (touch) {
        delete touch;
        touch = nullptr;
    }
}

void ESPPanel_CST816S_TouchDriver::init() {
    Logger.logLine("ESP_Panel: Initializing CST816S touch");

    auto* touch_bus = new ESP_PanelBus_I2C(TOUCH_I2C_SCL, TOUCH_I2C_SDA, ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG());
    touch_bus->configI2cFreqHz(400000);
    touch_bus->begin();

    touch = new ESP_PanelTouch_CST816S(touch_bus, DISPLAY_WIDTH, DISPLAY_HEIGHT, TOUCH_RST, TOUCH_INT);
    touch->init();
    touch->begin();
}

bool ESPPanel_CST816S_TouchDriver::isTouched() {
    uint16_t x, y;
    return getTouch(&x, &y, nullptr);
}

bool ESPPanel_CST816S_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
    if (pressure) *pressure = 0;
    if (!touch || !x || !y) return false;

    ESP_PanelTouchPoint point;
    int count = touch->readPoints(&point, 1);
    if (count <= 0) return false;

    uint16_t tx = (uint16_t)point.x;
    uint16_t ty = (uint16_t)point.y;

    if (calibrationEnabled && calXMax > calXMin && calYMax > calYMin) {
        uint32_t cx = tx;
        uint32_t cy = ty;
        if (cx < calXMin) cx = calXMin;
        if (cx > calXMax) cx = calXMax;
        if (cy < calYMin) cy = calYMin;
        if (cy > calYMax) cy = calYMax;

        // Map to display coordinates
        tx = (uint16_t)((cx - calXMin) * (uint32_t)(DISPLAY_WIDTH - 1) / (uint32_t)(calXMax - calXMin));
        ty = (uint16_t)((cy - calYMin) * (uint32_t)(DISPLAY_HEIGHT - 1) / (uint32_t)(calYMax - calYMin));
    }

    applyRotation(tx, ty);

    *x = tx;
    *y = ty;
    return true;
}

void ESPPanel_CST816S_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
    calibrationEnabled = true;
    calXMin = x_min;
    calXMax = x_max;
    calYMin = y_min;
    calYMax = y_max;
}

void ESPPanel_CST816S_TouchDriver::setRotation(uint8_t r) {
    rotation = r & 0x03;
}

void ESPPanel_CST816S_TouchDriver::applyRotation(uint16_t& x, uint16_t& y) const {
    // For a square 360x360 panel, this is mostly about coordinate consistency.
    // Rotation matches DisplayManager's DISPLAY_ROTATION.
    uint16_t w = DISPLAY_WIDTH;
    uint16_t h = DISPLAY_HEIGHT;

    switch (rotation) {
        case 0:
            return;
        case 1: {
            uint16_t nx = y;
            uint16_t ny = (uint16_t)(w - 1 - x);
            x = nx;
            y = ny;
            return;
        }
        case 2:
            x = (uint16_t)(w - 1 - x);
            y = (uint16_t)(h - 1 - y);
            return;
        case 3: {
            uint16_t nx = (uint16_t)(h - 1 - y);
            uint16_t ny = x;
            x = nx;
            y = ny;
            return;
        }
        default:
            return;
    }
}
