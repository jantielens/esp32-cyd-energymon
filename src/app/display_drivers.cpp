#include "board_config.h"

#if HAS_DISPLAY

// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes the required driver implementation includes
// so manager code stays focused on logic.

#if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
#include "drivers/tft_espi_driver.cpp"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789V2
#include "drivers/st7789v2_driver.cpp"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
#include "drivers/arduino_gfx_driver.cpp"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ESP_PANEL
#include "drivers/esp_panel_st77916_driver.cpp"
#else
#error "No display driver selected or unknown driver type"
#endif

#endif // HAS_DISPLAY
