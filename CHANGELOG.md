# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2026-01-01

### Added
- Web portal with a modern, responsive UI for device setup and configuration
- Captive portal AP mode for first-time Wi‑Fi setup, with a Network page for SSID/password + device settings
- Firmware update page with OTA update support and factory reset
- On-device UI (LVGL) with a default Energy Monitor screen showing Solar / Home / Grid power
- MQTT integration to subscribe to Solar and Grid topics, with flexible payload parsing (number or JSON value path)
- Configurable Energy Monitor settings in the portal:
  - Per-category bar max (kW)
  - Per-category color + threshold mapping (including signed thresholds for Grid)
- Save-without-reboot support for MQTT-related config changes (applies after saving)

[1.0.0]: https://github.com/jantielens/esp32-cyd-energymon/releases/tag/v1.0.0

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
