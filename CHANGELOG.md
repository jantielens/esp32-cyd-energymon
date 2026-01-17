## Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.0] - 2026-01-17

### Added
- Warning screensaver screen with pulsing warning icon that reuses alarm pulse settings

### Changed
- Screen saver behavior now shows the warning screen (backlight on) when a warning is active during sleep
- CYD v2 target now uses OTA-capable partitions (`PartitionScheme=ota_1_9mb`) via project config

### Fixed
- Build flag report now honors `config.project.sh` board overrides

## [1.0.0] - 2026-01-17

### Added
- CYD Energy Monitor UI with breathing T2 warning background and per-category colors/thresholds
- MQTT subscriptions for solar and grid readings with JSON path support
- Home page configuration for MQTT topics, thresholds, scaling, and warning behavior

### Changed
- Project branding and initial release packaging
- Compile-time flags documentation generation is enforced in CI (fails when generated doc differs)

## [0.0.24] - 2025-12-24

### Added
- Screen saver / burn-in prevention (v1): inactivity-based backlight fade-out and wake fade-in
- Display control API endpoints: `GET /api/display/sleep`, `POST /api/display/sleep`, `POST /api/display/wake`, `POST /api/display/activity`, `PUT /api/display/brightness`

### Changed
- Touch input now resets the screen saver idle timer during normal UI interaction
- Screen saver behavior suppresses touch input while dimming/asleep/fading in to avoid wake gestures clicking through into LVGL navigation

### Fixed
- Avoid potential boot hangs by deferring LVGL touch input device registration and retrying later

### Documentation
- Updated `README.md`, `docs/web-portal.md`, and `docs/display-touch-architecture.md` for screen saver config and APIs

## [0.0.23] - 2025-12-23

### Changed
- WiFi station mode disables power-save sleep to improve stability/latency

### Fixed
- WiFi connection now prefers the strongest AP when multiple BSSIDs share the same SSID (Fixes #25)
- Web portal reboot flow no longer triggers a double reboot; reboot overlay is more robust, including AP/core-mode guidance (Fixes #27)

---

## [0.0.22] - 2025-12-23

### Added
- LVGL PNG asset pipeline: `assets/png/*.png` → generated `src/app/png_assets.cpp/h` via `tools/png2lvgl_assets.py` (used by the splash screen)

---

## [0.0.21] - 2025-12-23

### Added
- `POST /api/display/image_url` to queue HTTP/HTTPS JPEG downloads for display (download+decode runs later in the main loop)
- Optional LVGL image screen (`lvgl_image`) when `HAS_IMAGE_API` is enabled
- Optional `LV_MEM_CUSTOM` LVGL allocator backed by `heap_caps_*` allocators (helps reduce internal heap fragmentation)
- `tools/portal_stress_test.py` for repeatable portal/API/image stress testing

### Changed
- Image API HTTP(S) download path is more robust (honors timeouts, reduces stack usage during header parsing)

### Fixed
- Hardened Image API request handling (abort-safe behavior; cleanup for interrupted/stuck uploads)

### Documentation
- Documented and logged a security warning for insecure TLS mode (`WiFiClientSecure::setInsecure()`)

### Removed
- Removed internal working doc `docs/memory-optimizations.md`

## [0.0.20] - 2025-12-21

### Added
- **Display + LVGL framework**
  - `DisplayDriver` / `DisplayManager` HAL with LVGL integration and render-mode support (`Direct` vs `Buffered`)
  - LVGL v8.4 screen framework with multiple screens (splash, info, test, direct image)
  - Deferred screen switching to avoid FPS drops during transitions
  - Optional LVGL performance monitor overlay
- **Touch input support**
  - `TouchDriver` / `TouchManager` HAL with LVGL input device registration and optional calibration
  - Board-configurable touch support via `HAS_TOUCH`
- **Display/touch drivers and backends**
  - TFT_eSPI display driver
  - Native SPI ST7789V2 display driver
  - Arduino_GFX display backend
  - ESP-Panel based display/touch drivers (where applicable)
  - XPT2046 resistive touch driver
  - AXS15231B touch backend wrapper (vendored implementation scoped under the driver)
- **Board driver selection and documentation**
  - Board overrides updated so boards explicitly select display/touch backends (including `cyd-v2`, `jc3248w535`, `jc3636w518`, and ESP32-C3 variants)
  - Board→driver documentation and generator (`src/app/drivers/README.md` + `tools/generate-board-driver-table.py`)
- **Image upload/display pipeline + tooling**
  - JPEG upload + decode flow (`image_api`) with JPEG preflight validation
  - Strip decoding and batching; async decode with screen preserved while processing
  - Tools: `tools/upload_image.py` and `tools/camera_to_esp32.py`
- **Docs**
  - New architecture docs: `docs/display-touch-architecture.md`, `docs/arduino-gfx-unification.md`

### Changed
- Build tooling is more robust (detects and flags previously-silent build failures)
- Telemetry and web portal behavior updated (including CPU usage monitoring with min/max tracking)

### Documentation
- Updated `README.md` and developer docs to reflect the new display/touch system and board targets
- Updated `docs/web-portal.md`, `docs/scripts.md`, and `docs/build-and-release-process.md`

---

## [0.0.19] - 2025-12-17

### Fixed
- Fixed board macro sanitization in build system to generate valid C++ identifiers
  - Board names with special characters (hyphens, dots, etc.) now properly converted to underscores
  - Macro pattern: `BOARD_<BOARDNAME>` where `<BOARDNAME>` is alphanumeric + underscore only
  - Example: `cyd-v2` → `BOARD_CYD_V2`, `board.name` → `BOARD_BOARD_NAME`
  - Resolves compilation errors when using `#if defined(BOARD_xxx)` with hyphenated board names
- Updated documentation to explain board macro sanitization behavior

---

## [0.0.18] - 2025-12-17

### Changed
- **BREAKING**: Reversed FQBN_TARGETS associative array mapping in `config.sh` (Resolves #21)
  - **Old format**: `["FQBN"]="board-name"` → **New format**: `["board-name"]="FQBN"`
  - Board name is now the key, FQBN is the value
  - Enables multiple board variants with the same FQBN (e.g., CYD display v2/v3)
  - Simplifies helper functions (`get_fqbn_for_board()` is now direct array lookup)
  - **Migration**: Swap keys and values in your `FQBN_TARGETS` array
- Updated build scripts, CI/CD workflows, and documentation to use new mapping format

---

## [0.0.17] - 2025-12-16

### Added
- Optional custom partition scheme example for ESP32-C3 to increase OTA app space
  - `partitions/partitions_ota_1_9mb.csv`
  - Example board target using `PartitionScheme=ota_1_9mb`
- Installer script to register custom partition schemes into the Arduino ESP32 core: `tools/install-custom-partitions.sh`
- Custom partitions documentation: `partitions/README.md`

### Changed
- `setup.sh` now runs the custom partitions installer (when present)
- CI workflows run the installer before compiling boards that use `PartitionScheme=...`

## [0.0.16] - 2025-12-16

### Added
- MQTT + Home Assistant integration (PubSubClient)
  - Home Assistant MQTT Discovery for device telemetry
  - Availability topic (LWT) and retained state publishing
  - Single JSON state topic with per-entity `value_template`
- MQTT settings in the Network page (host/port/credentials/publish interval)
- New developer guide: `docs/home-assistant-mqtt.md`

### Changed
- Refactored health/telemetry internals to `device_telemetry` (used by `/api/health` and MQTT publishing)
- `/api/info` now reports `has_mqtt` so the portal can hide MQTT settings for MQTT-disabled builds
- Documentation updated to reflect built-in MQTT/HA support and optional configuration

## [0.0.15] - 2025-12-16

### Fixed
- Web portal CSS now styles `input[type="number"]` fields consistently (Issue #16)

## [0.0.14] - 2025-12-11

### Fixed
- **Board Override System**: Fixed include guard conflicts and circular dependencies (Issue #14)
  - Renamed board-specific files from `board_config.h` → `board_overrides.h` to prevent naming conflicts
  - Implemented two-phase include pattern: board overrides loaded first, then defaults with `#ifndef` guards
  - Removed `#include_next` directive that caused infinite recursion errors
  - Removed non-functional `board_config.cpp` files (Arduino build system only compiles sketch directory)
  - Board overrides now working correctly for compile-time configuration

### Changed
- **Documentation**: Complete rewrite of board configuration documentation
  - Added real-world examples: buttons, battery monitors, different display drivers
  - Clarified that board overrides are for compile-time defines only
  - Documented conditional compilation pattern with `#if HAS_xxx` in application code
  - Updated README.md, copilot-instructions.md, and build-and-release-process.md
- **Board Override Pattern**: System now uses conditional compilation in main app instead of separate implementation files
  - Cleaner architecture with zero runtime overhead
  - All board-specific logic uses `#if` guards in `app.ino`
  - Compiler automatically eliminates unused code per board

### Removed
- `src/boards/<board>/board_config.cpp` - Not compiled by Arduino build system, was misleading

---

## [0.0.13] - 2025-12-11

### Added
- **Multi-Page Web Portal**: Refactored single-page portal to multi-page architecture
  - Home page: Custom settings and welcome message (Full Mode only)
  - Network page: WiFi, device, and network configuration (both modes)
  - Firmware page: OTA updates and factory reset (Full Mode only)
- **DRY Template System**: Build-time placeholder replacement for shared components
  - Shared templates: `_header.html`, `_nav.html`, `_footer.html`
  - Template fragments excluded from asset generation
- **Responsive Grid Layout**: 2-column design for desktop (≥768px breakpoint)
  - 900px max container width for better desktop utilization
  - Side-by-side sections on network and home pages
- **Health Monitoring Badge**: Integrated health widget into header as 7th badge
  - Orange background with green breathing dot, CPU %, and expand icon
  - Click to expand full health overlay with 11 metrics
  - Positioned top-right for better visibility
- **Floating Action Footer**: Redesigned action buttons as fixed bottom bar
  - Horizontal layout with Save & Reboot, Save, and Reboot buttons
  - Reduced vertical space usage (80px vs 100px)
  - Rounded top corners for modern appearance
- **Initial Setup Notice**: Added helpful message on network page for first-time setup

### Changed
- **Partial Configuration Updates**: Backend only updates fields present in JSON request
  - Each page updates only its own fields via `doc.containsKey()`
  - Prevents accidental clearing of settings from other pages
- **AP Mode Navigation**: Home/Firmware tabs hidden in Core Mode
  - Backend redirects to `/network.html` when in AP mode
  - Only Network page accessible during initial setup
- **Badge Loading Optimization**: Fixed widths and placeholders prevent layout shift
  - Format placeholders: `Firmware v-.-.-`, `--- rev -`, etc.
- **Scrollbar Layout**: Fixed layout shift with `html { overflow-y: scroll; }`
- **Page Margins**: Reduced body padding from 24px 16px to 12px 8px
- **Project Branding**: Simplified configuration names and project branding
- **Mobile Input Handling**: Enhanced keyboard overlap prevention on mobile devices

### Removed
- **Build Info Footer**: Removed from all pages (info now in header badges)
- **Duplicate CSS**: Cleaned up redundant styles for health widget
- **Floating Health Bubble**: Replaced with integrated header badge

### Performance
- **Web Assets**: 86,626 → 16,566 bytes gzipped (-81% compression)
- **Flash Usage**: ESP32: 82%, ESP32-C3: 81%, ESP32-C6: 84%

---

## [0.0.12] - 2025-11-28

### Fixed
- `build.sh` now injects board override include paths/macros and supports optional build profiles without requiring `get_build_props_for_board`.
- `src/app/board_config.h` automatically pulls board-specific overrides when `BOARD_HAS_OVERRIDE` is defined, with no app code changes.

---

## [0.0.11] - 2025-11-28

### Fixed
- Single-board workflows (e.g., `um.sh`) now accept an explicit board argument without treating it as a serial port (prevents uploads to a non-existent port named after the board)
- Captive portal startup now marks AP mode active so watchdog/DNS handling correctly recognizes captive portal state

## [0.0.10] - 2025-11-27

### Added
- **Board-Specific Configuration System**: Flexible optional override system for multi-board support
  - Default configuration in `src/app/board_config.h` used by all boards
  - Optional board-specific overrides in `src/boards/[board-name]/board_config.h`
  - Board-specific implementations in `src/boards/[board-name]/board_config.cpp`
  - Build system automatically detects and includes board overrides when present
  - Example: ESP32-C3 Super Mini with LED on GPIO8 (vs GPIO2 default)
  - Supports board-specific functions via feature flags (e.g., `HAS_CUSTOM_IDENTIFIER`)
  - Zero runtime overhead - all configuration resolved at compile time

### Changed
- **Project Structure**: Moved board overrides to `src/boards/` for better organization
- **Documentation**: Updated all docs to reflect board configuration system

---

## [0.0.9] - 2025-11-27

### Removed
- **Streaming Logs Feature**: Removed unstable web portal log streaming
  - Removed LogBuffer, `/api/logs` endpoint, and web log viewer
  - Simplified LogManager to serial-only output
  - Preserved all formatting features (nesting, indentation, timing)

### Changed
- **WiFi Connection**: Faster and more reliable connection handling
  - Reduced retry attempts: 5→3, optimized timeout: 5s→3s per attempt
  - Added hard reset retry before AP fallback (WiFi power cycle)
  - Total connection time: ~37.5s (was 75s)
  - Updated logMessage/logMessagef to single-line format

### Fixed
- Duplicate mDNS startup causing "Service already exists" error
- WiFi watchdog disrupting AP mode fallback
- ESP32-C6 FQBN configuration
- AP mode now stays active when WiFi connection fails

---

## [0.0.8] - 2025-11-26

### Fixed
- **CPU Usage Calculation**: Fixed CPU usage metric to show real-time usage instead of cumulative average
  - Changed from cumulative runtime to delta-based calculation between measurements
  - Added 100ms minimum interval to prevent measurement jitter
  - Now accurately reflects current CPU load, especially noticeable under stress testing
  - First measurement returns 0%, subsequent calls show accurate real-time usage percentage

---

## [0.0.7] - 2025-11-26

### Added
- **Gzip Compression for Web Assets**: All HTML, CSS, and JavaScript files are now automatically gzip compressed
  - Reduces flash storage usage by ~80% (64KB → 12KB for web assets)
  - Reduces bandwidth usage for web portal access
  - Automatic compression during build via `tools/minify-web-assets.sh`
  - Assets served with `Content-Encoding: gzip` header
  - Browser automatically decompresses (transparent to users)
  - Build output shows compression statistics (Original → Minified → Gzipped)

### Changed
- Modified `tools/minify-web-assets.sh` to add gzip compression step after minification
- Updated `web_portal.cpp` handlers to serve gzipped content with proper headers
- Web assets now stored as `uint8_t` byte arrays instead of string literals

---

## [0.0.6] - 2025-11-26

### Added
- **WiFi Stability Improvements**: Comprehensive WiFi connection reliability enhancements
  - WiFi hardware reset sequence on each connection attempt (OFF → STA mode cycle with delays)
  - Disabled persistent WiFi storage (`WiFi.persistent(false)`) to prevent NVS corruption
  - Automatic reconnection enabled at WiFi stack level (`WiFi.setAutoReconnect(true)`)
  - WiFi event handlers for connection lifecycle (connect, got IP, disconnect with reason codes)
  - WiFi watchdog in main loop (10-second interval) for automatic recovery from connection loss
  - Detailed connection diagnostics logging (SSID not found, wrong password, connection lost, etc.)
  - Automatic mDNS restart after reconnection

### Fixed
- **WiFi Connection Failures After OTA/Reboot**: Hardware reset sequence prevents WiFi radio state corruption that survives software reboots
- **No Recovery from WiFi Drops**: Watchdog + auto-reconnect + event handlers provide triple-layer protection for runtime connection stability
- **Limited WiFi Diagnostics**: Status reason logging helps identify connection failure causes

### Changed
- WiFi initialization now includes proper reset sequence: disconnect → OFF → delay → STA → delay
- Event-driven WiFi state management with callbacks instead of polling-only approach

---

## [0.0.5] - 2025-11-26

### Added
- **Project Branding System**: Centralized configuration for project identity
  - `PROJECT_NAME` and `PROJECT_DISPLAY_NAME` variables in `config.sh`
  - Template substitution in HTML files at build time (e.g., `{{PROJECT_DISPLAY_NAME}}`)
  - Auto-generated C++ defines in `src/app/project_branding.h` for firmware use (included by `web_assets.h`)
  - Branded AP SSID, device names, web portal titles, and release artifacts
- **Automated Release Workflow**: GitHub Actions workflow for automated releases
  - `.github/workflows/release.yml` - Tag-triggered release pipeline with branded artifact names
  - `tools/extract-changelog.sh` - Changelog parser for release notes
  - `create-release.sh` - Helper script for release preparation
  - Multi-board firmware artifacts with project name in filenames
  - SHA256 checksums generation
  - Pre-release support (tags with hyphens)
  - Lightweight releases with only .bin files (debug symbols in workflow artifacts)

### Changed
- **Documentation Restructure**: Renamed and expanded documentation
  - `docs/release-process.md` → `docs/build-and-release-process.md`
  - Added comprehensive project branding configuration guide
  - Updated README.md with branding customization steps
- **Build System**: Enhanced `minify-web-assets.sh` to accept and apply project name variables
- **Artifact Naming**: GitHub workflows now use `PROJECT_NAME` for consistent artifact naming
  - Build artifacts: `{PROJECT_NAME}-{board}` (e.g., `esp32-template-wifi-esp32`)
  - Release files: `{PROJECT_NAME}-{board}-v{version}.bin`
- Updated README.md with comprehensive release process documentation
- Streamlined release artifacts to include only firmware binaries and checksums

### Fixed
- AP SSID now uses configurable project name instead of hardcoded "ESP32-"
- Default device name now uses configurable display name instead of hardcoded "ESP32"

---

## [0.0.4] - 2025-11-26

### Added
- **Log Management System**: New structured logging with nested blocks and automatic timing
  - `LogManager` class: Print-compatible wrapper for Serial with log buffer integration
  - `LogBuffer`: Thread-safe circular buffer (50 entries, 200 chars each)
  - Automatic routing to both Serial output and web buffer
  - Indentation-based nested log blocks with elapsed time tracking
- **Web Logs Viewer**: New full-screen logs interface at `/logs`
  - Real-time polling of device logs (2s interval)
  - Syntax highlighting for error/warning/info messages
  - Auto-scroll toggle and connection controls
  - Formatted timestamps (HH:MM:SS.milliseconds since boot)
  - Link from main portal in Advanced section
- **Memory Optimizations**: AsyncResponseStream for JSON responses to reduce stack usage
- **Build Tool Enhancement**: `minify-web-assets.sh` now generates size constants for PROGMEM assets

### Changed
- Replaced all `Serial.print/println` calls with `Logger` methods throughout codebase
- Reduced heartbeat interval from 5s to 60s for less verbose logging
- Improved web portal asset serving with explicit size constants (avoids `strlen_P()` overhead)
- Enhanced AsyncTCP stack size to 16KB for large web assets
- Updated `config.sh` with detailed CDCOnBoot documentation for USB serial support

### Fixed
- Improved error handling in `config.sh` board name resolution
- Cleaned up duplicate CSS rules in portal.css

---

## [0.0.3] - 2025-11-26

### Added
- Enhanced mDNS service discovery with 5 additional TXT records following RFC 6763 DNS-SD best practices:
  - `ty`: device type classification (`iot-device`)
  - `mf`: manufacturer identifier (`ESP32-Tmpl`)
  - `features`: device capabilities (`wifi,http,api`)
  - `note`: human-readable description
  - `url`: direct configuration URL

### Fixed
- Fixed missing `API_VERSION` constant that caused 404 errors during device reconnection polling
- Fixed OTA polling starting prematurely during firmware upload instead of after completion

### Changed
- Improved reconnection polling strategy: 2s initial delay, then 3s intervals (total 122s timeout)
- Unified reboot dialog for all scenarios (save config, OTA update, manual reboot, factory reset)
- Consolidated reconnection logic from two separate functions into single `startReconnection()`
- Better user feedback with best-effort messaging and manual fallback addresses
- Enhanced timeout handling with troubleshooting hints
- Progress display now shows elapsed time during reconnection attempts
- Updated documentation to reflect 8-field mDNS TXT record structure

### Removed
- Removed duplicate OTA overlay dialog (now uses unified reboot dialog)
- Removed deprecated `startOTAReconnect()` function
- Removed NetBIOS references from documentation (functionality removed earlier)

---

## [0.0.2] - 2025-11-25

### Added
- Multi-board build support with configurable FQBN targets
- Board-specific build directories: `build/esp32/`, `build/esp32c3/`, etc.
- `PROJECT_NAME` configuration variable for consistent firmware naming
- Helper functions in `config.sh`: `get_board_name()`, `list_boards()`, `get_fqbn_for_board()`
- Board parameter support in `build.sh`, `upload.sh`, `upload-erase.sh`, `bum.sh`, `um.sh`
- GitHub Actions matrix build strategy for parallel board compilation
- ESP32-C3 Super Mini board configuration
- Separate artifact uploads per board in CI/CD

### Changed
- Build system now creates board-specific directories instead of flat `build/` structure
- Scripts require board name parameter when multiple boards are configured
- `./build.sh` without parameters now builds all configured boards
- CI/CD workflow generates separate artifacts for each board variant
- Updated documentation to reflect multi-board workflow

---

## [0.0.1] - 2025-11-25

### Added
- Initial release of ESP32 Arduino development template
- Headless build system using `arduino-cli` with local installation
- Build automation scripts with auto-detection of serial ports
- Library management system
- Web configuration portal with captive portal support
- REST API for device configuration and monitoring
- Real-time health monitoring widget
- OTA firmware updates via web interface
- NVS-based configuration storage
- WSL2 USB/IP support
- CI/CD pipeline with GitHub Actions
- Comprehensive documentation

---

## Template for Future Releases

```markdown
## [X.Y.Z] - YYYY-MM-DD

### Added
- New features

### Changed
- Changes to existing features

### Deprecated
- Features marked for removal

### Removed
- Removed features

### Fixed
- Bug fixes

### Security
- Security patches
```
