# ESP32 CYD Energy Monitor

Energy Monitor firmware for ESP32 CYD-class devices with LVGL UI, MQTT ingestion, and a configurable T2 breathing warning indicator. Designed for local development with `arduino-cli` and the project’s build scripts.

## Features

- **Energy Monitor Screen** with PNG icons and per-category colors/thresholds
- **T2 Breathing Warning** overlay when a configurable threshold is exceeded
- **MQTT Ingestion** for solar/grid readings (JSON path supported)
- **Web Portal** for configuration (topics, scaling, thresholds, warning settings)
- **Multi-Board Builds** with board overrides and optional custom partitions

## Quick Start

### 1. Setup

```bash
./setup.sh
```

### 2. Build

```bash
./build.sh
./build.sh cyd-v2
```

### 3. Upload + Monitor

```bash
./upload.sh cyd-v2
./monitor.sh

# Or all-in-one:
./bum.sh cyd-v2
```

## MQTT Configuration

Configure MQTT in the Home page of the portal:

- **MQTT Enabled**: turn on publishing/subscription.
- **Solar Topic / Grid Topic**: topic strings to subscribe to.
- **JSON Path**: dot notation into JSON payload (e.g., `data.power`).
- **Scaling**: set the input units and scaling to kW.

Typical payload options:

- **Plain number**: `1234` (watts)
- **JSON**: `{ "power": 1234 }` with JSON Path `power`

The UI converts inputs to kW and applies category thresholds to colorize each value.

## Energy Monitor UI

The Energy Monitor screen provides:

- Solar, Home, and Grid metrics
- Colorized thresholds per category
- Breathing warning overlay for the T2 threshold

## CYD v2 Partition Scheme

The CYD v2 build uses a larger app partition to fit the UI and assets:

- `PartitionScheme=huge_app` is set in `config.project.sh` for `cyd-v2`

The first flash after changing partition tables should be performed over USB.

## Project Structure

Key files and folders:

- [src/app/app.ino](src/app/app.ino) — Main sketch
- [src/app/energy_monitor.cpp](src/app/energy_monitor.cpp) — Energy state and MQTT inputs
- [src/app/screens/energy_monitor_screen.cpp](src/app/screens/energy_monitor_screen.cpp) — LVGL screen
- [src/app/web/home.html](src/app/web/home.html) — Portal UI for energy settings
- [src/app/web/portal.js](src/app/web/portal.js) — Portal logic for energy fields
- [config.project.sh](config.project.sh) — Project-specific overrides

## Documentation

- [Web Portal Guide](docs/web-portal.md)
- [Build and Release Process](docs/build-and-release-process.md)
- [Scripts Reference](docs/scripts.md)
- [WSL Development Guide](docs/wsl-development.md)
- [Home Assistant + MQTT](docs/home-assistant-mqtt.md)

## License

MIT. See [LICENSE](LICENSE).
