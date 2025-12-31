#ifndef DEVICE_TELEMETRY_H
#define DEVICE_TELEMETRY_H

#include <ArduinoJson.h>

struct DeviceMemorySnapshot {
	size_t heap_free_bytes;
	size_t heap_min_free_bytes;
	size_t heap_largest_free_block_bytes;
	size_t heap_internal_free_bytes;
	size_t heap_internal_min_free_bytes;
	size_t psram_free_bytes;
	size_t psram_min_free_bytes;
	size_t psram_largest_free_block_bytes;
};

// Initializes cached values used by device telemetry (safe to call multiple times).
// This exists to avoid re-entrant calls into ESP-IDF image helpers from different tasks.
void device_telemetry_init();

// Cached flash/sketch metadata helpers.
size_t device_telemetry_sketch_size();
size_t device_telemetry_free_sketch_space();

// Fill a JsonDocument with device telemetry for the web API (/api/health).
void device_telemetry_fill_api(JsonDocument &doc);

// Fill a JsonDocument with device telemetry optimized for MQTT publishing.
// Intentionally excludes volatile/low-value fields like IP address.
void device_telemetry_fill_mqtt(JsonDocument &doc);

// Get current CPU usage percentage (0-100).
// Thread-safe - reads cached value updated by background task.
int device_telemetry_get_cpu_usage();

// Get CPU usage min/max over the last 60 seconds.
void device_telemetry_get_cpu_minmax(int* out_min, int* out_max);

// Initialize CPU monitoring background task.
// Must be called once during setup.
void device_telemetry_start_cpu_monitoring();

// Capture a point-in-time memory snapshot (heap/internal heap/PSRAM).
DeviceMemorySnapshot device_telemetry_get_memory_snapshot();

// Convenience logging helper (single line) using LogManager.
void device_telemetry_log_memory_snapshot(const char *tag);

#endif // DEVICE_TELEMETRY_H
