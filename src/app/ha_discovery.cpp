#include "ha_discovery.h"

#include "board_config.h"

#if HAS_MQTT

#include "mqtt_manager.h"
#include "web_assets.h" // PROJECT_DISPLAY_NAME
#include "../version.h" // FIRMWARE_VERSION
#include <ArduinoJson.h>

static bool publish_sensor_config(
    MqttManager &mqtt,
    const char *object_id,
    const char *name_suffix,
    const char *value_template,
    const char *unit_of_measurement,
    const char *device_class,
    const char *state_class,
    const char *entity_category
);

static bool publish_binary_sensor_config(
    MqttManager &mqtt,
    const char *object_id,
    const char *name_suffix,
    const char *value_template,
    const char *device_class,
    const char *entity_category
);

void ha_discovery_publish_health(MqttManager &mqtt) {
    // Notes:
    // - Single JSON publish model: all entities share the same stat_t.
    // - value_template extracts fields from the JSON payload.

    publish_sensor_config(mqtt, "uptime", "Uptime", "{{ value_json.uptime_seconds }}", "s", "duration", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "reset_reason", "Reset Reason", "{{ value_json.reset_reason }}", "", "", "", "diagnostic");

    publish_sensor_config(mqtt, "cpu_usage", "CPU Usage", "{{ value_json.cpu_usage }}", "%", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "cpu_temperature", "Core Temp", "{{ value_json.cpu_temperature }}", "°C", "temperature", "measurement", "diagnostic");

    publish_sensor_config(mqtt, "heap_free", "Free Heap", "{{ value_json.heap_free }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "heap_min", "Min Free Heap", "{{ value_json.heap_min }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "heap_largest", "Largest Heap Block", "{{ value_json.heap_largest }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "heap_fragmentation", "Heap Fragmentation", "{{ value_json.heap_fragmentation }}", "%", "", "measurement", "diagnostic");

    publish_sensor_config(mqtt, "heap_internal_free", "Internal Heap Free", "{{ value_json.heap_internal_free }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "heap_internal_min", "Internal Heap Min", "{{ value_json.heap_internal_min }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "heap_internal_largest", "Internal Heap Largest", "{{ value_json.heap_internal_largest }}", "B", "", "measurement", "diagnostic");

    publish_sensor_config(mqtt, "psram_free", "PSRAM Free", "{{ value_json.psram_free }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "psram_min", "PSRAM Min Free", "{{ value_json.psram_min }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "psram_largest", "PSRAM Largest Block", "{{ value_json.psram_largest }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "psram_fragmentation", "PSRAM Fragmentation", "{{ value_json.psram_fragmentation }}", "%", "", "measurement", "diagnostic");

    publish_sensor_config(mqtt, "flash_used", "Flash Used", "{{ value_json.flash_used }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "flash_total", "Flash Total", "{{ value_json.flash_total }}", "B", "", "measurement", "diagnostic");

    publish_binary_sensor_config(mqtt, "fs_mounted", "FS Mounted", "{{ 'ON' if value_json.fs_mounted else 'OFF' }}", "", "diagnostic");
    publish_sensor_config(mqtt, "fs_used_bytes", "FS Used", "{{ value_json.fs_used_bytes }}", "B", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "fs_total_bytes", "FS Total", "{{ value_json.fs_total_bytes }}", "B", "", "measurement", "diagnostic");

    #if HAS_DISPLAY
    publish_sensor_config(mqtt, "display_fps", "Display FPS", "{{ value_json.display_fps }}", "fps", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "display_lv_timer_us", "Display LV Timer", "{{ value_json.display_lv_timer_us }}", "us", "", "measurement", "diagnostic");
    publish_sensor_config(mqtt, "display_present_us", "Display Present", "{{ value_json.display_present_us }}", "us", "", "measurement", "diagnostic");
    #endif

    publish_sensor_config(mqtt, "wifi_rssi", "WiFi RSSI", "{{ value_json.wifi_rssi }}", "dBm", "signal_strength", "measurement", "diagnostic");

    // =====================================================================
    // USER-EXTEND: Add your own Home Assistant entities here
    // =====================================================================
    // To add new sensors (e.g. ambient temperature + humidity), you typically:
    //   1) Add JSON fields to device_telemetry_fill_mqtt() in device_telemetry.cpp
    //   2) Add matching discovery entries below (value_template must match keys)
    //
    // Example (commented out): External temperature/humidity
    // (These will show up under the normal Sensors category in Home Assistant.)
    // publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
    // publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
}

static bool publish_binary_sensor_config(
    MqttManager &mqtt,
    const char *object_id,
    const char *name_suffix,
    const char *value_template,
    const char *device_class,
    const char *entity_category
) {
    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s/%s/config", mqtt.sanitizedName(), object_id);

    StaticJsonDocument<768> doc;

    // Use base topic shortcut to keep discovery payload small
    doc["~"] = mqtt.baseTopic();

    // Friendly name in payload
    // Keep entity name short; HA already groups entities under the device name.
    doc["name"] = name_suffix;

    // Provide a stable object_id that includes the device name once.
    char ha_object_id[96];
    snprintf(ha_object_id, sizeof(ha_object_id), "%s_%s", mqtt.sanitizedName(), object_id);
    doc["object_id"] = ha_object_id;

    if (entity_category && strlen(entity_category) > 0) {
        doc["entity_category"] = entity_category;
    }

    // Stable unique id (sanitized name + object id)
    char uniq_id[96];
    snprintf(uniq_id, sizeof(uniq_id), "%s_%s", mqtt.sanitizedName(), object_id);
    doc["uniq_id"] = uniq_id;

    doc["stat_t"] = "~/health/state";
    doc["val_tpl"] = value_template;

    // MQTT binary_sensor expects ON/OFF payloads.
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";

    // Availability
    doc["avty_t"] = "~/availability";
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (device_class && strlen(device_class) > 0) {
        doc["dev_cla"] = device_class;
    }

    // Device block (kept minimal)
    JsonObject dev = doc["dev"].to<JsonObject>();
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(mqtt.sanitizedName());
    dev["name"] = mqtt.friendlyName();
    dev["mdl"] = PROJECT_DISPLAY_NAME;
    dev["sw"] = FIRMWARE_VERSION;

    if (doc.overflowed()) {
        return false;
    }

    return mqtt.publishJson(topic, doc, true);
}

static bool publish_sensor_config(
    MqttManager &mqtt,
    const char *object_id,
    const char *name_suffix,
    const char *value_template,
    const char *unit_of_measurement,
    const char *device_class,
    const char *state_class,
    const char *entity_category = nullptr
) {
    char topic[160];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", mqtt.sanitizedName(), object_id);

    StaticJsonDocument<768> doc;

    // Use base topic shortcut to keep discovery payload small
    doc["~"] = mqtt.baseTopic();

    // Friendly name in payload
    // Keep entity name short; HA already groups entities under the device name.
    // This also avoids HA generating entity_id values that repeat the device name.
    doc["name"] = name_suffix;

    // Provide a stable object_id that includes the device name once.
    // HA uses this to generate entity_id like: sensor.<sanitized>_<object_id>.
    char ha_object_id[96];
    snprintf(ha_object_id, sizeof(ha_object_id), "%s_%s", mqtt.sanitizedName(), object_id);
    doc["object_id"] = ha_object_id;

    if (entity_category && strlen(entity_category) > 0) {
        doc["entity_category"] = entity_category;
    }

    // Stable unique id (sanitized name + object id)
    char uniq_id[96];
    snprintf(uniq_id, sizeof(uniq_id), "%s_%s", mqtt.sanitizedName(), object_id);
    doc["uniq_id"] = uniq_id;

    doc["stat_t"] = "~/health/state";
    doc["val_tpl"] = value_template;

    // Availability
    doc["avty_t"] = "~/availability";
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (unit_of_measurement && strlen(unit_of_measurement) > 0) {
        doc["unit_of_meas"] = unit_of_measurement;
    }
    if (device_class && strlen(device_class) > 0) {
        doc["dev_cla"] = device_class;
    }
    if (state_class && strlen(state_class) > 0) {
        doc["stat_cla"] = state_class;
    }

    // Device block (kept minimal)
    JsonObject dev = doc["dev"].to<JsonObject>();
    JsonArray ids = dev["ids"].to<JsonArray>();
    ids.add(mqtt.sanitizedName());
    dev["name"] = mqtt.friendlyName();
    dev["mdl"] = PROJECT_DISPLAY_NAME;
    dev["sw"] = FIRMWARE_VERSION;

    if (doc.overflowed()) {
        // Payload too large for this StaticJsonDocument size.
        // mqtt.publishJson() will also fail if serialization exceeds MQTT_MAX_PACKET_SIZE.
        return false;
    }

    return mqtt.publishJson(topic, doc, true);
}

#endif // HAS_MQTT
