#include "web_portal_config.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "board_config.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#include <ArduinoJson.h>
#include <WiFi.h>

#include <math.h>
#include <stdlib.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// /api/config body accumulator (chunk-safe)
static portMUX_TYPE g_config_post_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool in_progress;
    uint32_t started_ms;
    size_t total;
    size_t received;
    uint8_t* buf;
} g_config_post = {false, 0, 0, 0, nullptr};

#if HAS_MQTT
// AsyncWebServer callbacks run on the AsyncTCP task. Defer MQTT reconnect to main loop.
static volatile bool g_pending_mqtt_reconnect_request = false;
#endif

static void config_post_reset() {
    if (g_config_post.buf) {
        heap_caps_free(g_config_post.buf);
        g_config_post.buf = nullptr;
    }
    g_config_post.in_progress = false;
    g_config_post.total = 0;
    g_config_post.received = 0;
    g_config_post.started_ms = 0;
}

bool web_portal_config_take_mqtt_reconnect_request() {
    #if HAS_MQTT
    if (g_pending_mqtt_reconnect_request) {
        g_pending_mqtt_reconnect_request = false;
        return true;
    }
    #endif
    return false;
}

void web_portal_config_loop() {
    // Cleanup stuck /api/config uploads.
    const uint32_t now = millis();
    portENTER_CRITICAL(&g_config_post_mux);
    const bool stale = g_config_post.in_progress && g_config_post.started_ms && (now - g_config_post.started_ms > WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS);
    if (stale) {
        config_post_reset();
    }
    portEXIT_CRITICAL(&g_config_post_mux);

    if (stale) {
        LOGW("Portal", "Config upload timed out (loop cleanup)");
    }
}

// ===== ENERGY MONITOR HELPERS =====
static bool parse_color_hex_rgb(const JsonVariantConst &v, uint32_t *out_rgb) {
    if (!out_rgb) return false;

    // Accept: "#RRGGBB", "RRGGBB", "0xRRGGBB", or numeric.
    if (v.is<uint32_t>()) {
        *out_rgb = ((uint32_t)v.as<uint32_t>()) & 0xFFFFFF;
        return true;
    }
    if (!v.is<const char*>()) return false;
    const char *s = v.as<const char*>();
    if (!s) return false;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '#') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    char *endp = nullptr;
    unsigned long val = strtoul(s, &endp, 16);
    if (!endp || endp == s) return false;
    *out_rgb = ((uint32_t)val) & 0xFFFFFF;
    return true;
}

static void format_color_hex_rgb(uint32_t rgb, char out[8]) {
    snprintf(out, 8, "#%06lX", (unsigned long)(rgb & 0xFFFFFF));
}

static float mkw_to_kw(int32_t mkw) {
    return (float)mkw / 1000.0f;
}

static int32_t kw_to_mkw(float kw) {
    if (!(kw >= 0.0f)) return 0;
    // clamp to 0..100kW for sanity
    if (kw > 100.0f) kw = 100.0f;
    return (int32_t)lroundf(kw * 1000.0f);
}

void handleGetConfig(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    DeviceConfig *current_config = web_portal_get_current_config();
    if (!current_config) {
        request->send(500, "application/json", "{\"error\":\"Config not initialized\"}");
        return;
    }

    // Create JSON response (don't include passwords)
    std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> doc = make_psram_json_doc(4096);
    if (doc && doc->capacity() > 0) {
        (*doc)["wifi_ssid"] = current_config->wifi_ssid;
        (*doc)["wifi_password"] = ""; // Don't send password
        (*doc)["device_name"] = current_config->device_name;

        // Sanitized name for display
        char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
        config_manager_sanitize_device_name(current_config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);
        (*doc)["device_name_sanitized"] = sanitized;

        // Fixed IP settings
        (*doc)["fixed_ip"] = current_config->fixed_ip;
        (*doc)["subnet_mask"] = current_config->subnet_mask;
        (*doc)["gateway"] = current_config->gateway;
        (*doc)["dns1"] = current_config->dns1;
        (*doc)["dns2"] = current_config->dns2;

        // Dummy setting
        (*doc)["dummy_setting"] = current_config->dummy_setting;

        // MQTT settings (password not returned)
        (*doc)["mqtt_host"] = current_config->mqtt_host;
        (*doc)["mqtt_port"] = current_config->mqtt_port;
        (*doc)["mqtt_username"] = current_config->mqtt_username;
        (*doc)["mqtt_password"] = "";
        (*doc)["mqtt_interval_seconds"] = current_config->mqtt_interval_seconds;

        // Energy Monitor MQTT subscription settings
        (*doc)["mqtt_topic_solar"] = current_config->mqtt_topic_solar;
        (*doc)["mqtt_topic_grid"] = current_config->mqtt_topic_grid;
        (*doc)["mqtt_solar_value_path"] = current_config->mqtt_solar_value_path;
        (*doc)["mqtt_grid_value_path"] = current_config->mqtt_grid_value_path;

        // Energy Monitor UI scaling (kW)
        (*doc)["energy_solar_bar_max_kw"] = current_config->energy_solar_bar_max_kw;
        (*doc)["energy_home_bar_max_kw"] = current_config->energy_home_bar_max_kw;
        (*doc)["energy_grid_bar_max_kw"] = current_config->energy_grid_bar_max_kw;

        // Energy Monitor warning behavior
        (*doc)["energy_alarm_pulse_cycle_ms"] = current_config->energy_alarm_pulse_cycle_ms;
        (*doc)["energy_alarm_pulse_peak_pct"] = current_config->energy_alarm_pulse_peak_pct;
        (*doc)["energy_alarm_clear_delay_ms"] = current_config->energy_alarm_clear_delay_ms;
        (*doc)["energy_alarm_clear_hysteresis_mkw"] = current_config->energy_alarm_clear_hysteresis_mkw;

        // Energy Monitor per-category colors + thresholds
        {
            char c[8];

            format_color_hex_rgb(current_config->energy_solar_colors.color_good_rgb, c);
            (*doc)["energy_solar_color_good"] = c;
            format_color_hex_rgb(current_config->energy_solar_colors.color_ok_rgb, c);
            (*doc)["energy_solar_color_ok"] = c;
            format_color_hex_rgb(current_config->energy_solar_colors.color_attention_rgb, c);
            (*doc)["energy_solar_color_attention"] = c;
            format_color_hex_rgb(current_config->energy_solar_colors.color_warning_rgb, c);
            (*doc)["energy_solar_color_warning"] = c;
            (*doc)["energy_solar_threshold_0_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[0]);
            (*doc)["energy_solar_threshold_1_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[1]);
            (*doc)["energy_solar_threshold_2_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[2]);

            format_color_hex_rgb(current_config->energy_home_colors.color_good_rgb, c);
            (*doc)["energy_home_color_good"] = c;
            format_color_hex_rgb(current_config->energy_home_colors.color_ok_rgb, c);
            (*doc)["energy_home_color_ok"] = c;
            format_color_hex_rgb(current_config->energy_home_colors.color_attention_rgb, c);
            (*doc)["energy_home_color_attention"] = c;
            format_color_hex_rgb(current_config->energy_home_colors.color_warning_rgb, c);
            (*doc)["energy_home_color_warning"] = c;
            (*doc)["energy_home_threshold_0_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[0]);
            (*doc)["energy_home_threshold_1_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[1]);
            (*doc)["energy_home_threshold_2_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[2]);

            format_color_hex_rgb(current_config->energy_grid_colors.color_good_rgb, c);
            (*doc)["energy_grid_color_good"] = c;
            format_color_hex_rgb(current_config->energy_grid_colors.color_ok_rgb, c);
            (*doc)["energy_grid_color_ok"] = c;
            format_color_hex_rgb(current_config->energy_grid_colors.color_attention_rgb, c);
            (*doc)["energy_grid_color_attention"] = c;
            format_color_hex_rgb(current_config->energy_grid_colors.color_warning_rgb, c);
            (*doc)["energy_grid_color_warning"] = c;
            (*doc)["energy_grid_threshold_0_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[0]);
            (*doc)["energy_grid_threshold_1_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[1]);
            (*doc)["energy_grid_threshold_2_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[2]);
        }

        // Web portal Basic Auth (password not returned)
        (*doc)["basic_auth_enabled"] = current_config->basic_auth_enabled;
        (*doc)["basic_auth_username"] = current_config->basic_auth_username;
        (*doc)["basic_auth_password"] = "";
        (*doc)["basic_auth_password_set"] = (strlen(current_config->basic_auth_password) > 0);

        // Display settings
        (*doc)["backlight_brightness"] = current_config->backlight_brightness;

        #if HAS_DISPLAY
        // Screen saver settings
        (*doc)["screen_saver_enabled"] = current_config->screen_saver_enabled;
        (*doc)["screen_saver_timeout_seconds"] = current_config->screen_saver_timeout_seconds;
        (*doc)["screen_saver_fade_out_ms"] = current_config->screen_saver_fade_out_ms;
        (*doc)["screen_saver_fade_in_ms"] = current_config->screen_saver_fade_in_ms;
        (*doc)["screen_saver_wake_on_touch"] = current_config->screen_saver_wake_on_touch;
        #endif

        if (doc->overflowed()) {
            LOGE("Portal", "/api/config JSON overflow");
        }
    }

    web_portal_send_json_chunked(request, doc);
}

void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    DeviceConfig *current_config = web_portal_get_current_config();
    if (!current_config) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Config not initialized\"}");
        return;
    }

    #if HAS_MQTT
    char prev_mqtt_host[CONFIG_MQTT_HOST_MAX_LEN] = {0};
    char prev_mqtt_username[CONFIG_MQTT_USERNAME_MAX_LEN] = {0};
    char prev_mqtt_password[CONFIG_MQTT_PASSWORD_MAX_LEN] = {0};
    char prev_mqtt_topic_solar[CONFIG_MQTT_TOPIC_MAX_LEN] = {0};
    char prev_mqtt_topic_grid[CONFIG_MQTT_TOPIC_MAX_LEN] = {0};
    uint16_t prev_mqtt_port = current_config->mqtt_port;

    strlcpy(prev_mqtt_host, current_config->mqtt_host, sizeof(prev_mqtt_host));
    strlcpy(prev_mqtt_username, current_config->mqtt_username, sizeof(prev_mqtt_username));
    strlcpy(prev_mqtt_password, current_config->mqtt_password, sizeof(prev_mqtt_password));
    strlcpy(prev_mqtt_topic_solar, current_config->mqtt_topic_solar, sizeof(prev_mqtt_topic_solar));
    strlcpy(prev_mqtt_topic_grid, current_config->mqtt_topic_grid, sizeof(prev_mqtt_topic_grid));
    #endif

    // Accumulate the full body (chunk-safe) then parse once.
    if (index == 0) {
        // If a previous upload got stuck, reset it.
        const uint32_t now = millis();
        portENTER_CRITICAL(&g_config_post_mux);
        const bool stale = g_config_post.in_progress && g_config_post.started_ms && (now - g_config_post.started_ms > WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS);
        portEXIT_CRITICAL(&g_config_post_mux);
        if (stale) {
            LOGW("Portal", "Config upload timed out; resetting state");
            portENTER_CRITICAL(&g_config_post_mux);
            config_post_reset();
            portEXIT_CRITICAL(&g_config_post_mux);
        }

        portENTER_CRITICAL(&g_config_post_mux);
        if (g_config_post.in_progress) {
            portEXIT_CRITICAL(&g_config_post_mux);
            request->send(409, "application/json", "{\"success\":false,\"message\":\"Config update already in progress\"}");
            return;
        }
        g_config_post.in_progress = true;
        g_config_post.started_ms = now;
        g_config_post.total = total;
        g_config_post.received = 0;
        g_config_post.buf = nullptr;
        portEXIT_CRITICAL(&g_config_post_mux);

        if (total == 0 || total > WEB_PORTAL_CONFIG_MAX_JSON_BYTES) {
            portENTER_CRITICAL(&g_config_post_mux);
            config_post_reset();
            portEXIT_CRITICAL(&g_config_post_mux);
            request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
            return;
        }

        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            portENTER_CRITICAL(&g_config_post_mux);
            config_post_reset();
            portEXIT_CRITICAL(&g_config_post_mux);
            request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
            return;
        }

        portENTER_CRITICAL(&g_config_post_mux);
        g_config_post.buf = buf;
        portEXIT_CRITICAL(&g_config_post_mux);
    }

    // Copy this chunk.
    portENTER_CRITICAL(&g_config_post_mux);
    const bool ok = g_config_post.in_progress && g_config_post.buf && g_config_post.total == total && (index + len) <= total;
    uint8_t* dst = g_config_post.buf;
    portEXIT_CRITICAL(&g_config_post_mux);

    if (!ok) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid upload state\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }

    memcpy(dst + index, data, len);

    portENTER_CRITICAL(&g_config_post_mux);
    const size_t new_received = index + len;
    if (new_received > g_config_post.received) {
        g_config_post.received = new_received;
    }
    const bool done = (g_config_post.received >= g_config_post.total);
    portEXIT_CRITICAL(&g_config_post_mux);

    if (!done) {
        // More chunks to come.
        return;
    }

    // Finalize buffer and parse.
    uint8_t* body = nullptr;
    size_t body_len = 0;
    portENTER_CRITICAL(&g_config_post_mux);
    body = g_config_post.buf;
    body_len = g_config_post.total;
    if (body) body[body_len] = 0;
    portEXIT_CRITICAL(&g_config_post_mux);

    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    DeserializationError error = deserializeJson(doc, body, body_len);

    if (error) {
        LOGE("Portal", "JSON parse error: %s", error.c_str());
        if (error == DeserializationError::NoMemory) {
            request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
            portENTER_CRITICAL(&g_config_post_mux);
            config_post_reset();
            portEXIT_CRITICAL(&g_config_post_mux);
            return;
        }
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }

    // Partial update: only update fields that are present in the request
    // This allows different pages to update only their relevant fields

    // Security hardening: never allow changing Basic Auth settings in AP/core mode.
    // Otherwise, an attacker near the device could wait for fallback AP mode and lock out the owner.
    if (web_portal_is_ap_mode_active() && (doc.containsKey("basic_auth_enabled") || doc.containsKey("basic_auth_username") || doc.containsKey("basic_auth_password"))) {
        request->send(403, "application/json", "{\"success\":false,\"message\":\"Basic Auth settings cannot be changed in AP mode\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }

    // WiFi SSID - only update if field exists in JSON
    if (doc.containsKey("wifi_ssid")) {
        strlcpy(current_config->wifi_ssid, doc["wifi_ssid"] | "", CONFIG_SSID_MAX_LEN);
    }

    // WiFi password - only update if provided and not empty
    if (doc.containsKey("wifi_password")) {
        const char* wifi_pass = doc["wifi_password"];
        if (wifi_pass && strlen(wifi_pass) > 0) {
            strlcpy(current_config->wifi_password, wifi_pass, CONFIG_PASSWORD_MAX_LEN);
        }
    }

    // Device name - only update if field exists
    if (doc.containsKey("device_name")) {
        const char* device_name = doc["device_name"];
        if (device_name && strlen(device_name) > 0) {
            strlcpy(current_config->device_name, device_name, CONFIG_DEVICE_NAME_MAX_LEN);
        }
    }

    // Fixed IP settings - only update if fields exist
    if (doc.containsKey("fixed_ip")) {
        strlcpy(current_config->fixed_ip, doc["fixed_ip"] | "", CONFIG_IP_STR_MAX_LEN);
    }
    if (doc.containsKey("subnet_mask")) {
        strlcpy(current_config->subnet_mask, doc["subnet_mask"] | "", CONFIG_IP_STR_MAX_LEN);
    }
    if (doc.containsKey("gateway")) {
        strlcpy(current_config->gateway, doc["gateway"] | "", CONFIG_IP_STR_MAX_LEN);
    }
    if (doc.containsKey("dns1")) {
        strlcpy(current_config->dns1, doc["dns1"] | "", CONFIG_IP_STR_MAX_LEN);
    }
    if (doc.containsKey("dns2")) {
        strlcpy(current_config->dns2, doc["dns2"] | "", CONFIG_IP_STR_MAX_LEN);
    }

    // Dummy setting - only update if field exists
    if (doc.containsKey("dummy_setting")) {
        strlcpy(current_config->dummy_setting, doc["dummy_setting"] | "", CONFIG_DUMMY_MAX_LEN);
    }

    // MQTT host
    if (doc.containsKey("mqtt_host")) {
        strlcpy(current_config->mqtt_host, doc["mqtt_host"] | "", CONFIG_MQTT_HOST_MAX_LEN);
    }

    // MQTT port (optional; 0 means default 1883)
    if (doc.containsKey("mqtt_port")) {
        if (doc["mqtt_port"].is<const char*>()) {
            const char* port_str = doc["mqtt_port"];
            current_config->mqtt_port = (uint16_t)atoi(port_str ? port_str : "0");
        } else {
            current_config->mqtt_port = (uint16_t)(doc["mqtt_port"] | 0);
        }
    }

    // MQTT username
    if (doc.containsKey("mqtt_username")) {
        strlcpy(current_config->mqtt_username, doc["mqtt_username"] | "", CONFIG_MQTT_USERNAME_MAX_LEN);
    }

    // MQTT password (only update if provided and not empty)
    if (doc.containsKey("mqtt_password")) {
        const char* mqtt_pass = doc["mqtt_password"];
        if (mqtt_pass && strlen(mqtt_pass) > 0) {
            strlcpy(current_config->mqtt_password, mqtt_pass, CONFIG_MQTT_PASSWORD_MAX_LEN);
        }
    }

    // MQTT interval seconds
    if (doc.containsKey("mqtt_interval_seconds")) {
        if (doc["mqtt_interval_seconds"].is<const char*>()) {
            const char* int_str = doc["mqtt_interval_seconds"];
            current_config->mqtt_interval_seconds = (uint16_t)atoi(int_str ? int_str : "0");
        } else {
            current_config->mqtt_interval_seconds = (uint16_t)(doc["mqtt_interval_seconds"] | 0);
        }
    }

    // Energy Monitor MQTT subscription settings
    if (doc.containsKey("mqtt_topic_solar")) {
        strlcpy(current_config->mqtt_topic_solar, doc["mqtt_topic_solar"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    }
    if (doc.containsKey("mqtt_topic_grid")) {
        strlcpy(current_config->mqtt_topic_grid, doc["mqtt_topic_grid"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    }
    if (doc.containsKey("mqtt_solar_value_path")) {
        strlcpy(current_config->mqtt_solar_value_path, doc["mqtt_solar_value_path"] | ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }
    if (doc.containsKey("mqtt_grid_value_path")) {
        strlcpy(current_config->mqtt_grid_value_path, doc["mqtt_grid_value_path"] | ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }
    if (strlen(current_config->mqtt_solar_value_path) == 0) {
        strlcpy(current_config->mqtt_solar_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }
    if (strlen(current_config->mqtt_grid_value_path) == 0) {
        strlcpy(current_config->mqtt_grid_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }

    // Energy Monitor UI scaling (kW)
    auto read_kw = [&](const char* key, float* out_kw) {
        if (!doc.containsKey(key) || !out_kw) return;
        float v;
        if (doc[key].is<const char*>()) {
            const char* s = doc[key];
            v = s ? (float)atof(s) : 0.0f;
        } else {
            v = (float)(doc[key] | 0.0f);
        }
        if (v < 0.0f) v = 0.0f;
        if (v > 100.0f) v = 100.0f;
        *out_kw = v;
    };

    read_kw("energy_solar_bar_max_kw", &current_config->energy_solar_bar_max_kw);
    read_kw("energy_home_bar_max_kw", &current_config->energy_home_bar_max_kw);
    read_kw("energy_grid_bar_max_kw", &current_config->energy_grid_bar_max_kw);

    // Energy Monitor warning behavior
    auto read_u16 = [&](const char* key, uint16_t* out, uint16_t minV, uint16_t maxV) {
        if (!doc.containsKey(key) || !out) return;
        uint32_t v;
        if (doc[key].is<const char*>()) {
            const char* s = doc[key];
            v = (uint32_t)atoi(s ? s : "0");
        } else {
            v = (uint32_t)(doc[key] | 0);
        }
        if (v < minV) v = minV;
        if (v > maxV) v = maxV;
        *out = (uint16_t)v;
    };

    auto read_i32 = [&](const char* key, int32_t* out, int32_t minV, int32_t maxV) {
        if (!doc.containsKey(key) || !out) return;
        int32_t v;
        if (doc[key].is<const char*>()) {
            const char* s = doc[key];
            v = (int32_t)atol(s ? s : "0");
        } else {
            v = (int32_t)(doc[key] | 0);
        }
        if (v < minV) v = minV;
        if (v > maxV) v = maxV;
        *out = v;
    };

    read_u16("energy_alarm_pulse_cycle_ms", &current_config->energy_alarm_pulse_cycle_ms, 200, 10000);
    if (doc.containsKey("energy_alarm_pulse_peak_pct")) {
        uint32_t v;
        if (doc["energy_alarm_pulse_peak_pct"].is<const char*>()) {
            const char* s = doc["energy_alarm_pulse_peak_pct"];
            v = (uint32_t)atoi(s ? s : "0");
        } else {
            v = (uint32_t)(doc["energy_alarm_pulse_peak_pct"] | 0);
        }
        if (v > 100) v = 100;
        current_config->energy_alarm_pulse_peak_pct = (uint8_t)v;
    }
    read_u16("energy_alarm_clear_delay_ms", &current_config->energy_alarm_clear_delay_ms, 0, 60000);
    read_i32("energy_alarm_clear_hysteresis_mkw", &current_config->energy_alarm_clear_hysteresis_mkw, 0, 100000);

    // Energy Monitor per-category colors + thresholds
    auto update_category = [&](const char* prefix, EnergyCategoryColorConfig* cfg) {
        if (!cfg) return true;

        // Colors
        {
            char key[48];
            uint32_t rgb;

            snprintf(key, sizeof(key), "%s_color_good", prefix);
            if (doc.containsKey(key) && parse_color_hex_rgb(doc[key], &rgb)) cfg->color_good_rgb = rgb;

            snprintf(key, sizeof(key), "%s_color_ok", prefix);
            if (doc.containsKey(key) && parse_color_hex_rgb(doc[key], &rgb)) cfg->color_ok_rgb = rgb;

            snprintf(key, sizeof(key), "%s_color_attention", prefix);
            if (doc.containsKey(key) && parse_color_hex_rgb(doc[key], &rgb)) cfg->color_attention_rgb = rgb;

            snprintf(key, sizeof(key), "%s_color_warning", prefix);
            if (doc.containsKey(key) && parse_color_hex_rgb(doc[key], &rgb)) cfg->color_warning_rgb = rgb;
        }

        // Thresholds (kW)
        bool any_threshold = false;
        int32_t t0 = cfg->threshold_mkw[0];
        int32_t t1 = cfg->threshold_mkw[1];
        int32_t t2 = cfg->threshold_mkw[2];

        auto read_threshold_kw = [&](const char* key, int32_t* out_mkw) {
            if (!doc.containsKey(key) || !out_mkw) return;
            float v;
            if (doc[key].is<const char*>()) {
                const char* s = doc[key];
                v = s ? (float)atof(s) : 0.0f;
            } else {
                v = (float)(doc[key] | 0.0f);
            }
            *out_mkw = kw_to_mkw(v);
            any_threshold = true;
        };

        char key0[48];
        char key1[48];
        char key2[48];
        snprintf(key0, sizeof(key0), "%s_threshold_0_kw", prefix);
        snprintf(key1, sizeof(key1), "%s_threshold_1_kw", prefix);
        snprintf(key2, sizeof(key2), "%s_threshold_2_kw", prefix);
        read_threshold_kw(key0, &t0);
        read_threshold_kw(key1, &t1);
        read_threshold_kw(key2, &t2);

        if (any_threshold) {
            if (t0 > t1 || t1 > t2) {
                return false;
            }
            cfg->threshold_mkw[0] = t0;
            cfg->threshold_mkw[1] = t1;
            cfg->threshold_mkw[2] = t2;
        }

        return true;
    };

    if (!update_category("energy_solar", &current_config->energy_solar_colors)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Solar thresholds must be increasing\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }
    if (!update_category("energy_home", &current_config->energy_home_colors)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Home thresholds must be increasing\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }
    if (!update_category("energy_grid", &current_config->energy_grid_colors)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Grid thresholds must be increasing\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }

    // Basic Auth enabled
    if (doc.containsKey("basic_auth_enabled")) {
        if (doc["basic_auth_enabled"].is<const char*>()) {
            const char* v = doc["basic_auth_enabled"];
            current_config->basic_auth_enabled = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
        } else {
            current_config->basic_auth_enabled = (bool)(doc["basic_auth_enabled"] | false);
        }
    }

    // Basic Auth username
    if (doc.containsKey("basic_auth_username")) {
        strlcpy(current_config->basic_auth_username, doc["basic_auth_username"] | "", CONFIG_BASIC_AUTH_USERNAME_MAX_LEN);
    }

    // Basic Auth password (only update if provided and not empty)
    if (doc.containsKey("basic_auth_password")) {
        const char* pass = doc["basic_auth_password"];
        if (pass && strlen(pass) > 0) {
            strlcpy(current_config->basic_auth_password, pass, CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN);
        }
    }

    // Display settings - backlight brightness (0-100%)
    if (doc.containsKey("backlight_brightness")) {
        uint8_t brightness;
        // Handle both string and integer values from form
        if (doc["backlight_brightness"].is<const char*>()) {
            const char* brightness_str = doc["backlight_brightness"];
            brightness = (uint8_t)atoi(brightness_str ? brightness_str : "100");
        } else {
            brightness = (uint8_t)(doc["backlight_brightness"] | 100);
        }

        if (brightness > 100) brightness = 100;
        current_config->backlight_brightness = brightness;

        LOGI("Config", "Backlight brightness set to %d%%", brightness);

        // Apply brightness immediately (will also be persisted when config saved)
        #if HAS_DISPLAY
        display_manager_set_backlight_brightness(brightness);

        // Edge case: if the device was in screen saver (backlight at 0), changing brightness
        // externally would light the screen without updating the screen saver state.
        // Treat this as explicit activity+wake so auto-sleep keeps working.
        screen_saver_manager_notify_activity(true);
        #endif
    }

    #if HAS_DISPLAY
    // Screen saver settings
    if (doc.containsKey("screen_saver_enabled")) {
        if (doc["screen_saver_enabled"].is<const char*>()) {
            const char* v = doc["screen_saver_enabled"];
            current_config->screen_saver_enabled = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
        } else {
            current_config->screen_saver_enabled = (bool)(doc["screen_saver_enabled"] | false);
        }
    }

    if (doc.containsKey("screen_saver_timeout_seconds")) {
        if (doc["screen_saver_timeout_seconds"].is<const char*>()) {
            const char* v = doc["screen_saver_timeout_seconds"];
            current_config->screen_saver_timeout_seconds = (uint16_t)atoi(v ? v : "0");
        } else {
            current_config->screen_saver_timeout_seconds = (uint16_t)(doc["screen_saver_timeout_seconds"] | 0);
        }
    }

    if (doc.containsKey("screen_saver_fade_out_ms")) {
        if (doc["screen_saver_fade_out_ms"].is<const char*>()) {
            const char* v = doc["screen_saver_fade_out_ms"];
            current_config->screen_saver_fade_out_ms = (uint16_t)atoi(v ? v : "0");
        } else {
            current_config->screen_saver_fade_out_ms = (uint16_t)(doc["screen_saver_fade_out_ms"] | 0);
        }
    }

    if (doc.containsKey("screen_saver_fade_in_ms")) {
        if (doc["screen_saver_fade_in_ms"].is<const char*>()) {
            const char* v = doc["screen_saver_fade_in_ms"];
            current_config->screen_saver_fade_in_ms = (uint16_t)atoi(v ? v : "0");
        } else {
            current_config->screen_saver_fade_in_ms = (uint16_t)(doc["screen_saver_fade_in_ms"] | 0);
        }
    }

    if (doc.containsKey("screen_saver_wake_on_touch")) {
        if (doc["screen_saver_wake_on_touch"].is<const char*>()) {
            const char* v = doc["screen_saver_wake_on_touch"];
            current_config->screen_saver_wake_on_touch = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
        } else {
            current_config->screen_saver_wake_on_touch = (bool)(doc["screen_saver_wake_on_touch"] | false);
        }
    }
    #endif

    #if HAS_MQTT
    const bool mqtt_changed = (prev_mqtt_port != current_config->mqtt_port) ||
                              (strcmp(prev_mqtt_host, current_config->mqtt_host) != 0) ||
                              (strcmp(prev_mqtt_username, current_config->mqtt_username) != 0) ||
                              (strcmp(prev_mqtt_password, current_config->mqtt_password) != 0) ||
                              (strcmp(prev_mqtt_topic_solar, current_config->mqtt_topic_solar) != 0) ||
                              (strcmp(prev_mqtt_topic_grid, current_config->mqtt_topic_grid) != 0);
    #endif

    current_config->magic = CONFIG_MAGIC;

    // Validate config
    if (!config_manager_is_valid(current_config)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid configuration\"}");
        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
        return;
    }

    // Save to NVS
    if (config_manager_save(current_config)) {
        LOGI("Portal", "Config saved");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");

        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);

        // Check for no_reboot parameter
        if (!request->hasParam("no_reboot")) {
            LOGI("Portal", "Rebooting device");
            // Schedule reboot after response is sent
            delay(100);
            ESP.restart();
        } else {
            #if HAS_MQTT
            if (mqtt_changed) {
                g_pending_mqtt_reconnect_request = true;
            }
            #endif
        }
    } else {
        LOGE("Portal", "Config save failed");
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save\"}");

        portENTER_CRITICAL(&g_config_post_mux);
        config_post_reset();
        portEXIT_CRITICAL(&g_config_post_mux);
    }
}

void handleDeleteConfig(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (config_manager_reset()) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration reset\"}");

        // Schedule reboot after response is sent
        delay(100);
        ESP.restart();
    } else {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to reset\"}");
    }
}
