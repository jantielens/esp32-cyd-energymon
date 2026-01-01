/*
 * Web Configuration Portal Implementation
 * 
 * Async web server with captive portal support.
 * Serves static files and provides REST API for configuration.
 */

// Increase AsyncTCP task stack size to prevent overflow
// Default is 8192, increase to 16384 for web assets
#define CONFIG_ASYNC_TCP_STACK_SIZE 16384

#include "web_portal.h"
#include "web_assets.h"
#include "config_manager.h"
#include "log_manager.h"
#include "board_config.h"
#include "device_telemetry.h"
#include "../version.h"

#if HAS_MQTT
#include "mqtt_manager.h"
#endif

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_IMAGE_API
#include "image_api.h"
#endif

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>


// Forward declarations
void handleRoot(AsyncWebServerRequest *request);
void handleHome(AsyncWebServerRequest *request);
void handleNetwork(AsyncWebServerRequest *request);
void handleFirmware(AsyncWebServerRequest *request);
void handleCSS(AsyncWebServerRequest *request);
void handleJS(AsyncWebServerRequest *request);
void handleGetConfig(AsyncWebServerRequest *request);
void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleDeleteConfig(AsyncWebServerRequest *request);
void handleSetDisplayBrightness(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleGetVersion(AsyncWebServerRequest *request);
void handleGetMode(AsyncWebServerRequest *request);
void handleGetHealth(AsyncWebServerRequest *request);
void handleReboot(AsyncWebServerRequest *request);
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleGetDisplaySleep(AsyncWebServerRequest *request);
void handlePostDisplaySleep(AsyncWebServerRequest *request);
void handlePostDisplayWake(AsyncWebServerRequest *request);
void handlePostDisplayActivity(AsyncWebServerRequest *request);

// Web server on port 80 (pointer to avoid constructor issues)
AsyncWebServer *server = nullptr;

// DNS server for captive portal (port 53)
DNSServer dnsServer;

// AP configuration
#define DNS_PORT 53
#define CAPTIVE_PORTAL_IP IPAddress(192, 168, 4, 1)

// State
static bool ap_mode_active = false;
static DeviceConfig *current_config = nullptr;
static bool ota_in_progress = false;
static size_t ota_progress = 0;
static size_t ota_total = 0;

#if HAS_MQTT
// AsyncWebServer callbacks run on the AsyncTCP task. Defer MQTT reconnect to main loop.
static volatile bool pending_mqtt_reconnect_request = false;
#endif

#if HAS_IMAGE_API && HAS_DISPLAY
// AsyncWebServer callbacks run on the AsyncTCP task; never touch LVGL/display from there.
// Use this flag to defer "hide current image / return" operations to the main loop.
static volatile bool pending_image_hide_request = false;
#endif

// ===== WEB SERVER HANDLERS =====

static AsyncWebServerResponse *begin_gzipped_asset_response(
    AsyncWebServerRequest *request,
    const char *content_type,
    const uint8_t *content_gz,
    size_t content_gz_len,
    const char *cache_control
) {
    // Prefer the PROGMEM-aware response helper to avoid accidental heap copies.
    // All generated assets live in flash as `const uint8_t[] PROGMEM`.
    AsyncWebServerResponse *response = request->beginResponse_P(
        200,
        content_type,
        content_gz,
        content_gz_len
    );

    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Vary", "Accept-Encoding");
    if (cache_control && strlen(cache_control) > 0) {
        response->addHeader("Cache-Control", cache_control);
    }
    return response;
}

// Handle root - redirect to network.html in AP mode, serve home in full mode
void handleRoot(AsyncWebServerRequest *request) {
    if (ap_mode_active) {
        // In AP mode, redirect to network configuration page
        request->redirect("/network.html");
    } else {
        // In full mode, serve home page
        AsyncWebServerResponse *response = begin_gzipped_asset_response(
            request,
            "text/html",
            home_html_gz,
            home_html_gz_len,
            "no-store"
        );
        request->send(response);
    }
}

// Serve home page
void handleHome(AsyncWebServerRequest *request) {
    if (ap_mode_active) {
        // In AP mode, redirect to network configuration page
        request->redirect("/network.html");
        return;
    }
    AsyncWebServerResponse *response = begin_gzipped_asset_response(
        request,
        "text/html",
        home_html_gz,
        home_html_gz_len,
        "no-store"
    );
    request->send(response);
}

// Serve network configuration page
void handleNetwork(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = begin_gzipped_asset_response(
        request,
        "text/html",
        network_html_gz,
        network_html_gz_len,
        "no-store"
    );
    request->send(response);
}

// Serve firmware update page
void handleFirmware(AsyncWebServerRequest *request) {
    if (ap_mode_active) {
        // In AP mode, redirect to network configuration page
        request->redirect("/network.html");
        return;
    }
    AsyncWebServerResponse *response = begin_gzipped_asset_response(
        request,
        "text/html",
        firmware_html_gz,
        firmware_html_gz_len,
        "no-store"
    );
    request->send(response);
}

// Serve CSS
void handleCSS(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = begin_gzipped_asset_response(
        request,
        "text/css",
        portal_css_gz,
        portal_css_gz_len,
        "public, max-age=600"
    );
    request->send(response);
}

// Serve JavaScript
void handleJS(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = begin_gzipped_asset_response(
        request,
        "application/javascript",
        portal_js_gz,
        portal_js_gz_len,
        "public, max-age=600"
    );
    request->send(response);
}

// GET /api/mode - Return portal mode (core vs full)
void handleGetMode(AsyncWebServerRequest *request) {
    
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{\"mode\":\"");
    response->print(ap_mode_active ? "core" : "full");
    response->print("\",\"ap_active\":");
    response->print(ap_mode_active ? "true" : "false");
    response->print("}");
    request->send(response);
}

// GET /api/config - Return current configuration
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
    
    if (!current_config) {
        request->send(500, "application/json", "{\"error\":\"Config not initialized\"}");
        return;
    }
    
    // Create JSON response (don't include passwords)
    StaticJsonDocument<4096> doc;
    doc["wifi_ssid"] = current_config->wifi_ssid;
    doc["wifi_password"] = ""; // Don't send password
    doc["device_name"] = current_config->device_name;
    
    // Sanitized name for display
    char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
    config_manager_sanitize_device_name(current_config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);
    doc["device_name_sanitized"] = sanitized;
    
    // Fixed IP settings
    doc["fixed_ip"] = current_config->fixed_ip;
    doc["subnet_mask"] = current_config->subnet_mask;
    doc["gateway"] = current_config->gateway;
    doc["dns1"] = current_config->dns1;
    doc["dns2"] = current_config->dns2;
    
    // Dummy setting
    doc["dummy_setting"] = current_config->dummy_setting;

    // MQTT settings (password not returned)
    doc["mqtt_host"] = current_config->mqtt_host;
    doc["mqtt_port"] = current_config->mqtt_port;
    doc["mqtt_username"] = current_config->mqtt_username;
    doc["mqtt_password"] = "";
    doc["mqtt_interval_seconds"] = current_config->mqtt_interval_seconds;

    // Energy Monitor MQTT subscription settings
    doc["mqtt_topic_solar"] = current_config->mqtt_topic_solar;
    doc["mqtt_topic_grid"] = current_config->mqtt_topic_grid;
    doc["mqtt_solar_value_path"] = current_config->mqtt_solar_value_path;
    doc["mqtt_grid_value_path"] = current_config->mqtt_grid_value_path;

    // Energy Monitor UI scaling (kW)
    doc["energy_solar_bar_max_kw"] = current_config->energy_solar_bar_max_kw;
    doc["energy_home_bar_max_kw"] = current_config->energy_home_bar_max_kw;
    doc["energy_grid_bar_max_kw"] = current_config->energy_grid_bar_max_kw;

    // Energy Monitor per-category colors + thresholds
    {
        char c[8];

        format_color_hex_rgb(current_config->energy_solar_colors.color_good_rgb, c);
        doc["energy_solar_color_good"] = c;
        format_color_hex_rgb(current_config->energy_solar_colors.color_ok_rgb, c);
        doc["energy_solar_color_ok"] = c;
        format_color_hex_rgb(current_config->energy_solar_colors.color_attention_rgb, c);
        doc["energy_solar_color_attention"] = c;
        format_color_hex_rgb(current_config->energy_solar_colors.color_warning_rgb, c);
        doc["energy_solar_color_warning"] = c;
        doc["energy_solar_threshold_0_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[0]);
        doc["energy_solar_threshold_1_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[1]);
        doc["energy_solar_threshold_2_kw"] = mkw_to_kw(current_config->energy_solar_colors.threshold_mkw[2]);

        format_color_hex_rgb(current_config->energy_home_colors.color_good_rgb, c);
        doc["energy_home_color_good"] = c;
        format_color_hex_rgb(current_config->energy_home_colors.color_ok_rgb, c);
        doc["energy_home_color_ok"] = c;
        format_color_hex_rgb(current_config->energy_home_colors.color_attention_rgb, c);
        doc["energy_home_color_attention"] = c;
        format_color_hex_rgb(current_config->energy_home_colors.color_warning_rgb, c);
        doc["energy_home_color_warning"] = c;
        doc["energy_home_threshold_0_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[0]);
        doc["energy_home_threshold_1_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[1]);
        doc["energy_home_threshold_2_kw"] = mkw_to_kw(current_config->energy_home_colors.threshold_mkw[2]);

        format_color_hex_rgb(current_config->energy_grid_colors.color_good_rgb, c);
        doc["energy_grid_color_good"] = c;
        format_color_hex_rgb(current_config->energy_grid_colors.color_ok_rgb, c);
        doc["energy_grid_color_ok"] = c;
        format_color_hex_rgb(current_config->energy_grid_colors.color_attention_rgb, c);
        doc["energy_grid_color_attention"] = c;
        format_color_hex_rgb(current_config->energy_grid_colors.color_warning_rgb, c);
        doc["energy_grid_color_warning"] = c;
        doc["energy_grid_threshold_0_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[0]);
        doc["energy_grid_threshold_1_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[1]);
        doc["energy_grid_threshold_2_kw"] = mkw_to_kw(current_config->energy_grid_colors.threshold_mkw[2]);
    }
    
    // Display settings
    doc["backlight_brightness"] = current_config->backlight_brightness;

    #if HAS_DISPLAY
    // Screen saver settings
    doc["screen_saver_enabled"] = current_config->screen_saver_enabled;
    doc["screen_saver_timeout_seconds"] = current_config->screen_saver_timeout_seconds;
    doc["screen_saver_fade_out_ms"] = current_config->screen_saver_fade_out_ms;
    doc["screen_saver_fade_in_ms"] = current_config->screen_saver_fade_in_ms;
    doc["screen_saver_wake_on_touch"] = current_config->screen_saver_wake_on_touch;
    #endif

    if (doc.overflowed()) {
        Logger.logMessage("Portal", "ERROR: /api/config JSON overflow (StaticJsonDocument too small)");
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Response too large\"}");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

// POST /api/config - Save new configuration
void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {

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
    
    if (!current_config) {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Config not initialized\"}");
        return;
    }
    
    // Parse JSON body
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        Logger.logMessagef("Portal", "JSON parse error: %s", error.c_str());
        if (error == DeserializationError::NoMemory) {
            request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
            return;
        }
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    // Partial update: only update fields that are present in the request
    // This allows different pages to update only their relevant fields
    
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

    // Energy Monitor MQTT settings
    if (doc.containsKey("mqtt_topic_solar")) {
        strlcpy(current_config->mqtt_topic_solar, doc["mqtt_topic_solar"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    }
    if (doc.containsKey("mqtt_topic_grid")) {
        strlcpy(current_config->mqtt_topic_grid, doc["mqtt_topic_grid"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    }
    if (doc.containsKey("mqtt_solar_value_path")) {
        strlcpy(current_config->mqtt_solar_value_path, doc["mqtt_solar_value_path"] | "", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }
    if (doc.containsKey("mqtt_grid_value_path")) {
        strlcpy(current_config->mqtt_grid_value_path, doc["mqtt_grid_value_path"] | "", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }

    // Energy Monitor UI scaling (kW)
    auto read_kw = [&](const char* key, float* out) {
        if (!doc.containsKey(key) || !out) return;
        float v;
        if (doc[key].is<const char*>()) {
            const char* s = doc[key];
            v = s ? (float)atof(s) : 0.0f;
        } else {
            v = (float)(doc[key] | 0.0f);
        }

        // Clamp to sane values (kW)
        if (!(v > 0.0f)) v = 3.0f;
        if (v > 100.0f) v = 100.0f;
        *out = v;
    };

    read_kw("energy_solar_bar_max_kw", &current_config->energy_solar_bar_max_kw);
    read_kw("energy_home_bar_max_kw", &current_config->energy_home_bar_max_kw);
    read_kw("energy_grid_bar_max_kw", &current_config->energy_grid_bar_max_kw);

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
        return;
    }
    if (!update_category("energy_home", &current_config->energy_home_colors)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Home thresholds must be increasing\"}");
        return;
    }
    if (!update_category("energy_grid", &current_config->energy_grid_colors)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Grid thresholds must be increasing\"}");
        return;
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
        
        Logger.logLinef("Config: Backlight brightness set to %d%%", brightness);
        
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
    
    current_config->magic = CONFIG_MAGIC;
    
    // Validate config
    if (!config_manager_is_valid(current_config)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid configuration\"}");
        return;
    }
    
    // Save to NVS
    if (config_manager_save(current_config)) {
        Logger.logMessage("Portal", "Config saved");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");

                #if HAS_MQTT
                // If the request is "save only", apply MQTT changes at runtime (no reboot).
                // We only need to reconnect if connection parameters or subscription topics changed.
                if (request->hasParam("no_reboot")) {
                    bool mqtt_reconnect_needed = false;
                    if (strcmp(prev_mqtt_host, current_config->mqtt_host) != 0) mqtt_reconnect_needed = true;
                    if (prev_mqtt_port != current_config->mqtt_port) mqtt_reconnect_needed = true;
                    if (strcmp(prev_mqtt_username, current_config->mqtt_username) != 0) mqtt_reconnect_needed = true;
                    if (strcmp(prev_mqtt_password, current_config->mqtt_password) != 0) mqtt_reconnect_needed = true;
                    if (strcmp(prev_mqtt_topic_solar, current_config->mqtt_topic_solar) != 0) mqtt_reconnect_needed = true;
                    if (strcmp(prev_mqtt_topic_grid, current_config->mqtt_topic_grid) != 0) mqtt_reconnect_needed = true;

                    if (mqtt_reconnect_needed) {
                        pending_mqtt_reconnect_request = true;
                    }
                }
                #endif
        
        // Check for no_reboot parameter
        if (!request->hasParam("no_reboot")) {
            Logger.logMessage("Portal", "Rebooting device");
            // Schedule reboot after response is sent
            delay(100);
            ESP.restart();
        }
    } else {
        Logger.logMessage("Portal", "Config save failed");
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save\"}");
    }
}

// DELETE /api/config - Reset configuration
void handleDeleteConfig(AsyncWebServerRequest *request) {
    
    if (config_manager_reset()) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration reset\"}");
        
        // Schedule reboot after response is sent
        delay(100);
        ESP.restart();
    } else {
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to reset\"}");
    }
}

// GET /api/info - Get device information
void handleGetVersion(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{\"version\":\"");
    response->print(FIRMWARE_VERSION);
    response->print("\",\"build_date\":\"");
    response->print(BUILD_DATE);
    response->print("\",\"build_time\":\"");
    response->print(BUILD_TIME);
    response->print("\",\"chip_model\":\"");
    response->print(ESP.getChipModel());
    response->print("\",\"chip_revision\":");
    response->print(ESP.getChipRevision());
    response->print(",\"chip_cores\":");
    response->print(ESP.getChipCores());
    response->print(",\"cpu_freq\":");
    response->print(ESP.getCpuFreqMHz());
    response->print(",\"flash_chip_size\":");
    response->print(ESP.getFlashChipSize());
    response->print(",\"psram_size\":");
    response->print(ESP.getPsramSize());
    response->print(",\"free_heap\":");
    response->print(ESP.getFreeHeap());
    response->print(",\"sketch_size\":");
    response->print(device_telemetry_sketch_size());
    response->print(",\"free_sketch_space\":");
    response->print(device_telemetry_free_sketch_space());
    response->print(",\"mac_address\":\"");
    response->print(WiFi.macAddress());
    response->print("\",\"wifi_hostname\":\"");
    response->print(WiFi.getHostname());
    response->print("\",\"mdns_name\":\"");
    response->print(WiFi.getHostname());
    response->print(".local\",\"hostname\":\"");
    response->print(WiFi.getHostname());
    response->print("\",\"project_name\":\"");
    response->print(PROJECT_NAME);
    response->print("\",\"project_display_name\":\"");
    response->print(PROJECT_DISPLAY_NAME);
    response->print("\",\"has_mqtt\":");
    response->print(HAS_MQTT ? "true" : "false");    response->print(",\"has_backlight\":");
    response->print(HAS_BACKLIGHT ? "true" : "false");
    
    #if HAS_DISPLAY
    // Display screen information
    response->print(",\"has_display\":true");

    // Display resolution (driver coordinate space for direct writes / image upload)
    int display_coord_width = DISPLAY_WIDTH;
    int display_coord_height = DISPLAY_HEIGHT;
    if (displayManager && displayManager->getDriver()) {
        display_coord_width = displayManager->getDriver()->width();
        display_coord_height = displayManager->getDriver()->height();
    }
    response->print(",\"display_coord_width\":");
    response->print(display_coord_width);
    response->print(",\"display_coord_height\":");
    response->print(display_coord_height);
    
    // Get available screens
    size_t screen_count = 0;
    const ScreenInfo* screens = display_manager_get_available_screens(&screen_count);
    
    response->print(",\"available_screens\":[");
    for (size_t i = 0; i < screen_count; i++) {
        if (i > 0) response->print(",");
        response->print("{\"id\":\"");
        response->print(screens[i].id);
        response->print("\",\"name\":\"");
        response->print(screens[i].display_name);
        response->print("\"}");
    }
    response->print("]");
    
    // Get current screen
    const char* current_screen = display_manager_get_current_screen_id();
    response->print(",\"current_screen\":");
    if (current_screen) {
        response->print("\"");
        response->print(current_screen);
        response->print("\"");
    } else {
        response->print("null");
    }
    #else
    response->print(",\"has_display\":false");
    #endif
    
    response->print("}");
    request->send(response);
}

// GET /api/health - Get device health statistics
void handleGetHealth(AsyncWebServerRequest *request) {
    StaticJsonDocument<1024> doc;

    device_telemetry_fill_api(doc);

    if (doc.overflowed()) {
        Logger.logMessage("Portal", "ERROR: /api/health JSON overflow (StaticJsonDocument too small)");
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Response too large\"}");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

// POST /api/reboot - Reboot device without saving
void handleReboot(AsyncWebServerRequest *request) {
    Logger.logMessage("API", "POST /api/reboot");
    
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting device...\"}");
    
    // Schedule reboot after response is sent
    delay(100);
    Logger.logMessage("Portal", "Rebooting");
    ESP.restart();
}

// PUT /api/display/brightness - Set backlight brightness immediately (no persist)
void handleSetDisplayBrightness(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only handle the complete request (index == 0 && index + len == total)
    if (index != 0 || index + len != total) {
        return;
    }
    
    // Parse JSON body
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    // Get brightness value (0-100)
    if (!doc.containsKey("brightness")) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing brightness value\"}");
        return;
    }
    
    uint8_t brightness = (uint8_t)(doc["brightness"] | 100);
    if (brightness > 100) brightness = 100;
    
    #if HAS_DISPLAY
    // Update the in-RAM target brightness (does not persist to NVS).
    // This keeps the screen saver target consistent with what the user sees.
    if (current_config) {
        current_config->backlight_brightness = brightness;
    }

    // Edge case: if the screen saver is dimming/asleep/fading, directly setting the
    // backlight would show the UI again without updating the screen saver state.
    // Easiest fix: when not Awake, route through the screen saver wake path.
    const ScreenSaverState state = screen_saver_manager_get_status().state;
    if (state != ScreenSaverState::Awake) {
        screen_saver_manager_wake();
    } else {
        display_manager_set_backlight_brightness(brightness);
        screen_saver_manager_notify_activity(false);
    }

    #endif
    
    // Return success
    char response[64];
    snprintf(response, sizeof(response), "{\"success\":true,\"brightness\":%d}", brightness);
    request->send(200, "application/json", response);
}

// GET /api/display/sleep - Screen saver status snapshot
void handleGetDisplaySleep(AsyncWebServerRequest *request) {
    #if HAS_DISPLAY
    ScreenSaverStatus status = screen_saver_manager_get_status();

    StaticJsonDocument<256> doc;
    doc["enabled"] = status.enabled;
    doc["state"] = (uint8_t)status.state;
    doc["current_brightness"] = status.current_brightness;
    doc["target_brightness"] = status.target_brightness;
    doc["seconds_until_sleep"] = status.seconds_until_sleep;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
    #else
    request->send(404, "application/json", "{\"success\":false,\"message\":\"No display\"}");
    #endif
}

// POST /api/display/sleep - Sleep now
void handlePostDisplaySleep(AsyncWebServerRequest *request) {
    #if HAS_DISPLAY
    Logger.logMessage("API", "POST /api/display/sleep");
    screen_saver_manager_sleep_now();
    request->send(200, "application/json", "{\"success\":true}");
    #else
    request->send(404, "application/json", "{\"success\":false,\"message\":\"No display\"}");
    #endif
}

// POST /api/display/wake - Wake now
void handlePostDisplayWake(AsyncWebServerRequest *request) {
    #if HAS_DISPLAY
    Logger.logMessage("API", "POST /api/display/wake");
    screen_saver_manager_wake();
    request->send(200, "application/json", "{\"success\":true}");
    #else
    request->send(404, "application/json", "{\"success\":false,\"message\":\"No display\"}");
    #endif
}

// POST /api/display/activity - Reset idle timer; optionally wake
void handlePostDisplayActivity(AsyncWebServerRequest *request) {
    #if HAS_DISPLAY
    bool wake = false;
    if (request->hasParam("wake")) {
        wake = (request->getParam("wake")->value() == "1");
    }
    Logger.logMessagef("API", "POST /api/display/activity (wake=%d)", (int)wake);
    screen_saver_manager_notify_activity(wake);
    request->send(200, "application/json", "{\"success\":true}");
    #else
    request->send(404, "application/json", "{\"success\":false,\"message\":\"No display\"}");
    #endif
}

#if HAS_DISPLAY
// PUT /api/display/screen - Switch to a different screen (runtime only, no persist)
void handleSetDisplayScreen(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only handle the complete request (index == 0 && index + len == total)
    if (index != 0 || index + len != total) {
        return;
    }
    
    // Parse JSON body
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    // Get screen ID
    if (!doc.containsKey("screen")) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing screen ID\"}");
        return;
    }
    
    const char* screen_id = doc["screen"];
    if (!screen_id || strlen(screen_id) == 0) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid screen ID\"}");
        return;
    }
    
    Logger.logMessagef("API", "PUT /api/display/screen: %s", screen_id);
    
    // Switch to requested screen
    bool success = false;
    display_manager_show_screen(screen_id, &success);

    // Screen-affecting action counts as explicit activity and should wake.
    if (success) {
        screen_saver_manager_notify_activity(true);
    }
    
    if (success) {
        // Build success response with new screen ID
        char response[96];
        snprintf(response, sizeof(response), "{\"success\":true,\"screen\":\"%s\"}", screen_id);
        request->send(200, "application/json", response);
    } else {
        request->send(404, "application/json", "{\"success\":false,\"message\":\"Screen not found\"}");
    }
}
#endif

// POST /api/update - Handle OTA firmware upload
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // First chunk - initialize OTA
    if (index == 0) {

        
        Logger.logBegin("OTA Update");
        Logger.logLinef("File: %s", filename.c_str());
        Logger.logLinef("Size: %d bytes", request->contentLength());
        
        ota_in_progress = true;
        ota_progress = 0;
        ota_total = request->contentLength();
        
        // Check if filename ends with .bin
        if (!filename.endsWith(".bin")) {
            Logger.logEnd("Not a .bin file");
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Only .bin files are supported\"}");
            ota_in_progress = false;
            return;
        }
        
        // Get OTA partition size
        size_t updateSize = (ota_total > 0) ? ota_total : UPDATE_SIZE_UNKNOWN;
        size_t freeSpace = device_telemetry_free_sketch_space();
        
        Logger.logLinef("Free space: %d bytes", freeSpace);
        
        // Validate size before starting
        if (ota_total > 0 && ota_total > freeSpace) {
            Logger.logEnd("Firmware too large");
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Firmware too large\"}");
            ota_in_progress = false;
            return;
        }
        
        // Begin OTA update
        if (!Update.begin(updateSize, U_FLASH)) {
            Logger.logEnd("Begin failed");
            Update.printError(Serial);
            request->send(500, "application/json", "{\"success\":false,\"message\":\"OTA begin failed\"}");
            ota_in_progress = false;
            return;
        }
    }
    
    // Write chunk to flash
    if (len) {
        if (Update.write(data, len) != len) {
            Logger.logEnd("Write failed");
            Update.printError(Serial);
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Write failed\"}");
            ota_in_progress = false;
            return;
        }
        
        ota_progress += len;
        
        // Print progress every 10%
        static uint8_t last_percent = 0;
        uint8_t percent = (ota_progress * 100) / ota_total;
        if (percent >= last_percent + 10) {
            Logger.logLinef("Progress: %d%%", percent);
            last_percent = percent;
        }
    }
    
    // Final chunk - complete OTA
    if (final) {
        if (Update.end(true)) {
            Logger.logLinef("Written: %d bytes", ota_progress);
            Logger.logEnd("Success - rebooting");
            
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Update successful! Rebooting...\"}");
            
            delay(500);
            ESP.restart();
        } else {
            Logger.logEnd("Update failed");
            Update.printError(Serial);
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Update failed\"}");
        }
        
        ota_in_progress = false;
    }
}

// ===== PUBLIC API =====

// Initialize web portal
void web_portal_init(DeviceConfig *config) {
    Logger.logBegin("Portal Init");
    
    current_config = config;
    Logger.logLinef("Portal config pointer: %p, backlight_brightness: %d", 
                    current_config, current_config->backlight_brightness);
    
    // Create web server instance (avoid global constructor issues)
    if (server == nullptr) {
        yield();
        delay(100);
        
        server = new AsyncWebServer(80);
        
        yield();
        delay(100);
    }

    // Page routes
    server->on("/", HTTP_GET, handleRoot);
    server->on("/home.html", HTTP_GET, handleHome);
    server->on("/network.html", HTTP_GET, handleNetwork);
    server->on("/firmware.html", HTTP_GET, handleFirmware);
    
    // Asset routes
    server->on("/portal.css", HTTP_GET, handleCSS);
    server->on("/portal.js", HTTP_GET, handleJS);
    
    // API endpoints
    server->on("/api/mode", HTTP_GET, handleGetMode);
    server->on("/api/config", HTTP_GET, handleGetConfig);
    
    server->on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest *request) {},
        NULL,
        handlePostConfig
    );
    
    server->on("/api/config", HTTP_DELETE, handleDeleteConfig);
    server->on("/api/info", HTTP_GET, handleGetVersion);
    server->on("/api/health", HTTP_GET, handleGetHealth);
    server->on("/api/reboot", HTTP_POST, handleReboot);
    
    #if HAS_DISPLAY
    // Display API endpoints
    server->on("/api/display/brightness", HTTP_PUT,
        [](AsyncWebServerRequest *request) {},
        NULL,
        handleSetDisplayBrightness
    );

    // Screen saver API endpoints
    server->on("/api/display/sleep", HTTP_GET, handleGetDisplaySleep);
    server->on("/api/display/sleep", HTTP_POST, handlePostDisplaySleep);
    server->on("/api/display/wake", HTTP_POST, handlePostDisplayWake);
    server->on("/api/display/activity", HTTP_POST, handlePostDisplayActivity);

    // Runtime-only screen switch
    server->on("/api/display/screen", HTTP_PUT,
        [](AsyncWebServerRequest *request) {},
        NULL,
        handleSetDisplayScreen
    );
    #endif
    
    // OTA upload endpoint
    server->on("/api/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        handleOTAUpload
    );
    
    // Image API integration (if enabled)
    #if HAS_IMAGE_API && HAS_DISPLAY
    Logger.logMessage("Portal", "Initializing image API");
    
    // Setup backend adapter
    ImageApiBackend backend;
    backend.hide_current_image = []() {
        #if HAS_DISPLAY
        // Called from AsyncTCP task and sometimes from the main loop.
        // Always defer actual display/LVGL operations to the main loop.
        pending_image_hide_request = true;
        #endif
    };
    
    backend.start_strip_session = [](int width, int height, unsigned long timeout_ms, unsigned long start_time) -> bool {
        #if HAS_DISPLAY
        (void)start_time;
        DirectImageScreen* screen = display_manager_get_direct_image_screen();
        if (!screen) {
            Logger.logMessage("ImageAPI", "ERROR: No direct image screen");
            return false;
        }
        
        // Now called from main loop with proper task context
        // Show the DirectImageScreen first
        display_manager_show_direct_image();

        // Screen-affecting action counts as explicit activity and should wake.
        screen_saver_manager_notify_activity(true);
        
        // Configure timeout and start session
        screen->set_timeout(timeout_ms);
        screen->begin_strip_session(width, height);
        return true;
        #else
        return false;
        #endif
    };
    
    backend.decode_strip = [](const uint8_t* jpeg_data, size_t jpeg_size, uint8_t strip_index, bool output_bgr565) -> bool {
        #if HAS_DISPLAY
        DirectImageScreen* screen = display_manager_get_direct_image_screen();
        if (!screen) {
            Logger.logMessage("ImageAPI", "ERROR: No direct image screen");
            return false;
        }
        
        // Now called from main loop - safe to decode
        return screen->decode_strip(jpeg_data, jpeg_size, strip_index, output_bgr565);
        #else
        return false;
        #endif
    };
    
    // Setup configuration
    ImageApiConfig image_cfg;

    // Use the display driver's coordinate space (fast path for direct image writes).
    // This intentionally avoids LVGL calls and any DISPLAY_ROTATION heuristics.
    image_cfg.lcd_width = DISPLAY_WIDTH;
    image_cfg.lcd_height = DISPLAY_HEIGHT;

    #if HAS_DISPLAY
        if (displayManager && displayManager->getDriver()) {
            image_cfg.lcd_width = displayManager->getDriver()->width();
            image_cfg.lcd_height = displayManager->getDriver()->height();
        }
    #endif
    
    image_cfg.max_image_size_bytes = IMAGE_API_MAX_SIZE_BYTES;
    image_cfg.decode_headroom_bytes = IMAGE_API_DECODE_HEADROOM_BYTES;
    image_cfg.default_timeout_ms = IMAGE_API_DEFAULT_TIMEOUT_MS;
    image_cfg.max_timeout_ms = IMAGE_API_MAX_TIMEOUT_MS;
    
    // Initialize and register routes
    Logger.logMessage("Portal", "Calling image_api_init...");
    image_api_init(image_cfg, backend);
    Logger.logMessage("Portal", "Calling image_api_register_routes...");
    image_api_register_routes(server);
    Logger.logMessage("Portal", "Image API initialized");
    #endif // HAS_IMAGE_API && HAS_DISPLAY
    
    // 404 handler
    server->onNotFound([](AsyncWebServerRequest *request) {
        // In AP mode, redirect to root for captive portal
        if (ap_mode_active) {
            request->redirect("/");
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
    
    // Start server
    yield();
    delay(100);
    server->begin();
    Logger.logEnd();
}

// Start AP mode with captive portal
void web_portal_start_ap() {
    Logger.logBegin("AP Mode");
    
    // Generate AP name with chip ID
    uint32_t chipId = 0;
    for(int i=0; i<17; i=i+8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    
    // Convert PROJECT_NAME to uppercase for AP SSID
    String apPrefix = String(PROJECT_NAME);
    apPrefix.toUpperCase();
    String apName = apPrefix + "-" + String(chipId, HEX);
    
    Logger.logLinef("SSID: %s", apName.c_str());
    
    // Configure AP
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(CAPTIVE_PORTAL_IP, CAPTIVE_PORTAL_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(apName.c_str());
    
    // Start DNS server for captive portal (redirect all DNS queries to our IP)
    dnsServer.start(DNS_PORT, "*", CAPTIVE_PORTAL_IP);
    
    WiFi.softAPsetHostname(apName.c_str());

    // Mark AP mode active so watchdog/DNS handling knows we're in captive portal
    ap_mode_active = true;

    Logger.logLinef("IP: %s", WiFi.softAPIP().toString().c_str());
    Logger.logEnd("Captive portal active");
}

// Stop AP mode
void web_portal_stop_ap() {
    if (ap_mode_active) {
        Logger.logMessage("Portal", "Stopping AP mode");
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        ap_mode_active = false;
    }
}

// Handle web server (call in loop)
void web_portal_handle() {
    if (ap_mode_active) {
        dnsServer.processNextRequest();
    }

    #if HAS_MQTT
    if (pending_mqtt_reconnect_request) {
        pending_mqtt_reconnect_request = false;
        mqtt_manager_request_reconnect();
    }
    #endif
}

// Check if in AP mode
bool web_portal_is_ap_mode() {
    return ap_mode_active;
}

// Check if OTA update is in progress
bool web_portal_ota_in_progress() {
    return ota_in_progress;
}

#if HAS_IMAGE_API
// Process pending image uploads (call from main loop)
void web_portal_process_pending_images() {
    // If the image API asked us to hide/dismiss the current image (or recover
    // from a failure), do it from the main loop so DisplayManager can safely
    // clear direct-image mode.
    #if HAS_DISPLAY
    if (pending_image_hide_request) {
        pending_image_hide_request = false;
        display_manager_return_to_previous_screen();
    }
    #endif

    image_api_process_pending(ota_in_progress);
}
#endif
