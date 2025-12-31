// ============================================================================
// Screen Compilation Unit
// ============================================================================
// Single compilation unit for all screen implementations.
// This ensures screen .cpp files are compiled with correct build flags
// while keeping them organized in the screens/ subdirectory.
//
// Note: Display driver .cpp files are included in display_manager.cpp,
// not here, since the display manager is responsible for driver lifecycle.

#include "board_config.h"

#if HAS_DISPLAY

// Include all screen implementations
#include "screens/splash_screen.cpp"
#include "screens/info_screen.cpp"
#include "screens/test_screen.cpp"

#if HAS_IMAGE_API
#include "screens/direct_image_screen.cpp"
#include "screens/lvgl_image_screen.cpp"
#endif

#endif // HAS_DISPLAY

// ============================================================================
// Touch Driver Implementations
// ============================================================================
// Touch driver implementations are compiled via src/app/touch_drivers.cpp.
