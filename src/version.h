#ifndef VERSION_H
#define VERSION_H

#include "app/log_manager.h"

// Firmware version information
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

// Build date (automatically set by compiler)
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// Convert version to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define VERSION_STRING TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH)
#define FIRMWARE_VERSION VERSION_STRING

// Function to print version information
inline void printVersionInfo() {
  Logger.println("=== Firmware Version ===");
  Logger.print("Version: ");
  Logger.println(VERSION_STRING);
  Logger.print("Build Date: ");
  Logger.println(BUILD_DATE);
  Logger.print("Build Time: ");
  Logger.println(BUILD_TIME);
  Logger.println("========================");
}

#endif // VERSION_H
