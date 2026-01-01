/*
 * Configuration Manager Implementation
 * 
 * Uses ESP32 Preferences library (NVS wrapper) for persistent storage.
 * Stores configuration in "device_cfg" namespace.
 */

#include "config_manager.h"
#include "board_config.h"
#include "web_assets.h"
#include "log_manager.h"
#include <Preferences.h>
#include <nvs_flash.h>

// NVS namespace
#define CONFIG_NAMESPACE "device_cfg"

// Preferences keys
#define KEY_WIFI_SSID      "wifi_ssid"
#define KEY_WIFI_PASS      "wifi_pass"
#define KEY_DEVICE_NAME    "device_name"
#define KEY_FIXED_IP       "fixed_ip"
#define KEY_SUBNET_MASK    "subnet_mask"
#define KEY_GATEWAY        "gateway"
#define KEY_DNS1           "dns1"
#define KEY_DNS2           "dns2"
#define KEY_DUMMY          "dummy"
#define KEY_MQTT_HOST      "mqtt_host"
#define KEY_MQTT_PORT      "mqtt_port"
#define KEY_MQTT_USER      "mqtt_user"
#define KEY_MQTT_PASS      "mqtt_pass"
#define KEY_MQTT_INTERVAL  "mqtt_int"
#define KEY_MQTT_SOLAR_TOPIC "mqtt_sol_t"
#define KEY_MQTT_GRID_TOPIC  "mqtt_grd_t"
#define KEY_MQTT_SOLAR_PATH  "mqtt_sol_p"
#define KEY_MQTT_GRID_PATH   "mqtt_grd_p"
#define KEY_ENERGY_SOLAR_BAR_MAX_KW "en_sol_m"
#define KEY_ENERGY_HOME_BAR_MAX_KW  "en_hom_m"
#define KEY_ENERGY_GRID_BAR_MAX_KW  "en_grd_m"

// Energy monitor colors/thresholds (per category)
#define KEY_EN_SOL_CG "es_cg"
#define KEY_EN_SOL_CO "es_co"
#define KEY_EN_SOL_CA "es_ca"
#define KEY_EN_SOL_CW "es_cw"
#define KEY_EN_SOL_T0 "es_t0"
#define KEY_EN_SOL_T1 "es_t1"
#define KEY_EN_SOL_T2 "es_t2"

#define KEY_EN_HOM_CG "eh_cg"
#define KEY_EN_HOM_CO "eh_co"
#define KEY_EN_HOM_CA "eh_ca"
#define KEY_EN_HOM_CW "eh_cw"
#define KEY_EN_HOM_T0 "eh_t0"
#define KEY_EN_HOM_T1 "eh_t1"
#define KEY_EN_HOM_T2 "eh_t2"

#define KEY_EN_GRD_CG "eg_cg"
#define KEY_EN_GRD_CO "eg_co"
#define KEY_EN_GRD_CA "eg_ca"
#define KEY_EN_GRD_CW "eg_cw"
#define KEY_EN_GRD_T0 "eg_t0"
#define KEY_EN_GRD_T1 "eg_t1"
#define KEY_EN_GRD_T2 "eg_t2"
#define KEY_BACKLIGHT_BRIGHTNESS "bl_bright"
#if HAS_DISPLAY
#define KEY_SCREEN_SAVER_ENABLED "ss_en"
#define KEY_SCREEN_SAVER_TIMEOUT "ss_to"
#define KEY_SCREEN_SAVER_FADE_OUT "ss_fo"
#define KEY_SCREEN_SAVER_FADE_IN "ss_fi"
#define KEY_SCREEN_SAVER_WAKE_TOUCH "ss_wt"
#endif
#define KEY_MAGIC          "magic"

static Preferences preferences;

static void set_energy_defaults(EnergyCategoryColorConfig* cfg) {
    if (!cfg) return;
    cfg->color_good_rgb = 0x00FF00;      // green
    cfg->color_ok_rgb = 0xFFFFFF;        // white
    cfg->color_attention_rgb = 0xFFA500; // orange
    cfg->color_warning_rgb = 0xFF0000;   // red
    cfg->threshold_mkw[0] = 500;   // 0.5 kW
    cfg->threshold_mkw[1] = 1500;  // 1.5 kW
    cfg->threshold_mkw[2] = 3000;  // 3.0 kW
}

static void normalize_energy_thresholds(EnergyCategoryColorConfig* cfg) {
    if (!cfg) return;
    if (cfg->threshold_mkw[0] < 0) cfg->threshold_mkw[0] = 0;
    if (cfg->threshold_mkw[1] < 0) cfg->threshold_mkw[1] = 0;
    if (cfg->threshold_mkw[2] < 0) cfg->threshold_mkw[2] = 0;

    // Require monotonic order; if invalid, reset to defaults.
    if (cfg->threshold_mkw[0] > cfg->threshold_mkw[1] || cfg->threshold_mkw[1] > cfg->threshold_mkw[2]) {
        set_energy_defaults(cfg);
    }
}

// Initialize NVS
void config_manager_init() {
    Logger.logBegin("Config NVS Init");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Logger.logLinef("NVS init error (%d) - erasing NVS", (int)err);
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        Logger.logLinef("NVS init FAILED (%d)", (int)err);
        Logger.logEnd("FAILED");
        return;
    }

    Logger.logEnd("OK");
}

// Get default device name with unique chip ID
String config_manager_get_default_device_name() {
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i = i + 8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    char name[32];
    snprintf(name, sizeof(name), PROJECT_DISPLAY_NAME " %04X", (uint16_t)(chipId & 0xFFFF));
    return String(name);
}

// Sanitize device name for mDNS (lowercase, alphanumeric + hyphens only)
void config_manager_sanitize_device_name(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len == 0) return;
    
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < max_len - 1; i++) {
        char c = input[i];
        
        // Convert to lowercase
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }
        
        // Keep alphanumeric and convert spaces/special chars to hyphens
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            output[j++] = c;
        } else if (c == ' ' || c == '_' || c == '-') {
            // Don't add hyphen if previous char was already a hyphen
            if (j > 0 && output[j-1] != '-') {
                output[j++] = '-';
            }
        }
    }
    
    // Remove trailing hyphen if present
    if (j > 0 && output[j-1] == '-') {
        j--;
    }
    
    output[j] = '\0';
}

// Load configuration from NVS
bool config_manager_load(DeviceConfig *config) {
    if (!config) {
        Logger.logMessage("Config", "Load failed: NULL pointer");
        return false;
    }
    
    Logger.logBegin("Config Load");

    if (!preferences.begin(CONFIG_NAMESPACE, true)) { // Read-only mode
        Logger.logEnd("Preferences begin failed");
        return false;
    }
    
    // Check magic number first
    uint32_t magic = preferences.getUInt(KEY_MAGIC, 0);
    if (magic != CONFIG_MAGIC) {
        preferences.end();
        Logger.logEnd("No config found");
        
        // Initialize defaults for fields that need sensible values even when no config exists
        config->backlight_brightness = 100;  // Default to full brightness
        config->mqtt_port = 0;
        config->mqtt_interval_seconds = 0;

        config->mqtt_topic_solar[0] = '\0';
        config->mqtt_topic_grid[0] = '\0';

        strlcpy(config->mqtt_solar_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
        strlcpy(config->mqtt_grid_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);

        // Energy monitor UI defaults (kW)
        config->energy_solar_bar_max_kw = 3.0f;
        config->energy_home_bar_max_kw = 3.0f;
        config->energy_grid_bar_max_kw = 3.0f;

        // Energy monitor colors/thresholds defaults
        set_energy_defaults(&config->energy_solar_colors);
        set_energy_defaults(&config->energy_home_colors);
        set_energy_defaults(&config->energy_grid_colors);

        #if HAS_DISPLAY
        // Screen saver defaults
        config->screen_saver_enabled = false;
        config->screen_saver_timeout_seconds = 300;
        config->screen_saver_fade_out_ms = 800;
        config->screen_saver_fade_in_ms = 400;
        #if HAS_TOUCH
        config->screen_saver_wake_on_touch = true;
        #else
        config->screen_saver_wake_on_touch = false;
        #endif
        #endif
        
        return false;
    }
    
    // Load WiFi settings
    preferences.getString(KEY_WIFI_SSID, config->wifi_ssid, CONFIG_SSID_MAX_LEN);
    preferences.getString(KEY_WIFI_PASS, config->wifi_password, CONFIG_PASSWORD_MAX_LEN);
    
    // Load device settings
    String default_name = config_manager_get_default_device_name();
    preferences.getString(KEY_DEVICE_NAME, config->device_name, CONFIG_DEVICE_NAME_MAX_LEN);
    if (strlen(config->device_name) == 0) {
        strlcpy(config->device_name, default_name.c_str(), CONFIG_DEVICE_NAME_MAX_LEN);
    }
    
    // Load fixed IP settings
    preferences.getString(KEY_FIXED_IP, config->fixed_ip, CONFIG_IP_STR_MAX_LEN);
    preferences.getString(KEY_SUBNET_MASK, config->subnet_mask, CONFIG_IP_STR_MAX_LEN);
    preferences.getString(KEY_GATEWAY, config->gateway, CONFIG_IP_STR_MAX_LEN);
    preferences.getString(KEY_DNS1, config->dns1, CONFIG_IP_STR_MAX_LEN);
    preferences.getString(KEY_DNS2, config->dns2, CONFIG_IP_STR_MAX_LEN);
    
    // Load dummy setting
    preferences.getString(KEY_DUMMY, config->dummy_setting, CONFIG_DUMMY_MAX_LEN);

    // Load MQTT settings (all optional)
    preferences.getString(KEY_MQTT_HOST, config->mqtt_host, CONFIG_MQTT_HOST_MAX_LEN);
    config->mqtt_port = preferences.getUShort(KEY_MQTT_PORT, 0);
    preferences.getString(KEY_MQTT_USER, config->mqtt_username, CONFIG_MQTT_USERNAME_MAX_LEN);
    preferences.getString(KEY_MQTT_PASS, config->mqtt_password, CONFIG_MQTT_PASSWORD_MAX_LEN);
    config->mqtt_interval_seconds = preferences.getUShort(KEY_MQTT_INTERVAL, 0);

    // Load Energy Monitor MQTT settings (all optional)
    preferences.getString(KEY_MQTT_SOLAR_TOPIC, config->mqtt_topic_solar, CONFIG_MQTT_TOPIC_MAX_LEN);
    preferences.getString(KEY_MQTT_GRID_TOPIC, config->mqtt_topic_grid, CONFIG_MQTT_TOPIC_MAX_LEN);
    preferences.getString(KEY_MQTT_SOLAR_PATH, config->mqtt_solar_value_path, CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    preferences.getString(KEY_MQTT_GRID_PATH, config->mqtt_grid_value_path, CONFIG_MQTT_VALUE_PATH_MAX_LEN);

    // Normalize empty paths to "." (direct numeric payloads)
    if (strlen(config->mqtt_solar_value_path) == 0) {
        strlcpy(config->mqtt_solar_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }
    if (strlen(config->mqtt_grid_value_path) == 0) {
        strlcpy(config->mqtt_grid_value_path, ".", CONFIG_MQTT_VALUE_PATH_MAX_LEN);
    }

    // Load Energy Monitor UI scaling (kW)
    config->energy_solar_bar_max_kw = preferences.getFloat(KEY_ENERGY_SOLAR_BAR_MAX_KW, 3.0f);
    config->energy_home_bar_max_kw = preferences.getFloat(KEY_ENERGY_HOME_BAR_MAX_KW, 3.0f);
    config->energy_grid_bar_max_kw = preferences.getFloat(KEY_ENERGY_GRID_BAR_MAX_KW, 3.0f);

    // Clamp to sane minimums (avoid divide-by-zero)
    if (!(config->energy_solar_bar_max_kw > 0.0f)) config->energy_solar_bar_max_kw = 3.0f;
    if (!(config->energy_home_bar_max_kw > 0.0f)) config->energy_home_bar_max_kw = 3.0f;
    if (!(config->energy_grid_bar_max_kw > 0.0f)) config->energy_grid_bar_max_kw = 3.0f;

    // Load Energy Monitor colors/thresholds
    set_energy_defaults(&config->energy_solar_colors);
    set_energy_defaults(&config->energy_home_colors);
    set_energy_defaults(&config->energy_grid_colors);

    config->energy_solar_colors.color_good_rgb = preferences.getUInt(KEY_EN_SOL_CG, config->energy_solar_colors.color_good_rgb) & 0xFFFFFF;
    config->energy_solar_colors.color_ok_rgb = preferences.getUInt(KEY_EN_SOL_CO, config->energy_solar_colors.color_ok_rgb) & 0xFFFFFF;
    config->energy_solar_colors.color_attention_rgb = preferences.getUInt(KEY_EN_SOL_CA, config->energy_solar_colors.color_attention_rgb) & 0xFFFFFF;
    config->energy_solar_colors.color_warning_rgb = preferences.getUInt(KEY_EN_SOL_CW, config->energy_solar_colors.color_warning_rgb) & 0xFFFFFF;
    config->energy_solar_colors.threshold_mkw[0] = preferences.getInt(KEY_EN_SOL_T0, config->energy_solar_colors.threshold_mkw[0]);
    config->energy_solar_colors.threshold_mkw[1] = preferences.getInt(KEY_EN_SOL_T1, config->energy_solar_colors.threshold_mkw[1]);
    config->energy_solar_colors.threshold_mkw[2] = preferences.getInt(KEY_EN_SOL_T2, config->energy_solar_colors.threshold_mkw[2]);
    normalize_energy_thresholds(&config->energy_solar_colors);

    config->energy_home_colors.color_good_rgb = preferences.getUInt(KEY_EN_HOM_CG, config->energy_home_colors.color_good_rgb) & 0xFFFFFF;
    config->energy_home_colors.color_ok_rgb = preferences.getUInt(KEY_EN_HOM_CO, config->energy_home_colors.color_ok_rgb) & 0xFFFFFF;
    config->energy_home_colors.color_attention_rgb = preferences.getUInt(KEY_EN_HOM_CA, config->energy_home_colors.color_attention_rgb) & 0xFFFFFF;
    config->energy_home_colors.color_warning_rgb = preferences.getUInt(KEY_EN_HOM_CW, config->energy_home_colors.color_warning_rgb) & 0xFFFFFF;
    config->energy_home_colors.threshold_mkw[0] = preferences.getInt(KEY_EN_HOM_T0, config->energy_home_colors.threshold_mkw[0]);
    config->energy_home_colors.threshold_mkw[1] = preferences.getInt(KEY_EN_HOM_T1, config->energy_home_colors.threshold_mkw[1]);
    config->energy_home_colors.threshold_mkw[2] = preferences.getInt(KEY_EN_HOM_T2, config->energy_home_colors.threshold_mkw[2]);
    normalize_energy_thresholds(&config->energy_home_colors);

    config->energy_grid_colors.color_good_rgb = preferences.getUInt(KEY_EN_GRD_CG, config->energy_grid_colors.color_good_rgb) & 0xFFFFFF;
    config->energy_grid_colors.color_ok_rgb = preferences.getUInt(KEY_EN_GRD_CO, config->energy_grid_colors.color_ok_rgb) & 0xFFFFFF;
    config->energy_grid_colors.color_attention_rgb = preferences.getUInt(KEY_EN_GRD_CA, config->energy_grid_colors.color_attention_rgb) & 0xFFFFFF;
    config->energy_grid_colors.color_warning_rgb = preferences.getUInt(KEY_EN_GRD_CW, config->energy_grid_colors.color_warning_rgb) & 0xFFFFFF;
    config->energy_grid_colors.threshold_mkw[0] = preferences.getInt(KEY_EN_GRD_T0, config->energy_grid_colors.threshold_mkw[0]);
    config->energy_grid_colors.threshold_mkw[1] = preferences.getInt(KEY_EN_GRD_T1, config->energy_grid_colors.threshold_mkw[1]);
    config->energy_grid_colors.threshold_mkw[2] = preferences.getInt(KEY_EN_GRD_T2, config->energy_grid_colors.threshold_mkw[2]);
    normalize_energy_thresholds(&config->energy_grid_colors);
    
    // Load display settings
    config->backlight_brightness = preferences.getUChar(KEY_BACKLIGHT_BRIGHTNESS, 100);
    Logger.logLinef("Loaded brightness: %d%%", config->backlight_brightness);

    #if HAS_DISPLAY
    // Load screen saver settings
    config->screen_saver_enabled = preferences.getBool(KEY_SCREEN_SAVER_ENABLED, false);
    config->screen_saver_timeout_seconds = preferences.getUShort(KEY_SCREEN_SAVER_TIMEOUT, 300);
    config->screen_saver_fade_out_ms = preferences.getUShort(KEY_SCREEN_SAVER_FADE_OUT, 800);
    config->screen_saver_fade_in_ms = preferences.getUShort(KEY_SCREEN_SAVER_FADE_IN, 400);
    #if HAS_TOUCH
    config->screen_saver_wake_on_touch = preferences.getBool(KEY_SCREEN_SAVER_WAKE_TOUCH, true);
    #else
    config->screen_saver_wake_on_touch = preferences.getBool(KEY_SCREEN_SAVER_WAKE_TOUCH, false);
    #endif
    #endif
    
    config->magic = magic;
    
    preferences.end();
    
    // Validate loaded config
    if (!config_manager_is_valid(config)) {
        Logger.logEnd("Invalid config");
        return false;
    }
    
    config_manager_print(config);
    Logger.logEnd();
    return true;
}

// Save configuration to NVS
bool config_manager_save(const DeviceConfig *config) {
    if (!config) {
        Logger.logMessage("Config", "Save failed: NULL pointer");
        return false;
    }
    
    if (!config_manager_is_valid(config)) {
        Logger.logMessage("Config", "Save failed: Invalid config");
        return false;
    }
    
    Logger.logBegin("Config Save");
    
    preferences.begin(CONFIG_NAMESPACE, false); // Read-write mode
    
    // Save WiFi settings
    preferences.putString(KEY_WIFI_SSID, config->wifi_ssid);
    preferences.putString(KEY_WIFI_PASS, config->wifi_password);
    
    // Save device settings
    preferences.putString(KEY_DEVICE_NAME, config->device_name);
    
    // Save fixed IP settings
    preferences.putString(KEY_FIXED_IP, config->fixed_ip);
    preferences.putString(KEY_SUBNET_MASK, config->subnet_mask);
    preferences.putString(KEY_GATEWAY, config->gateway);
    preferences.putString(KEY_DNS1, config->dns1);
    preferences.putString(KEY_DNS2, config->dns2);
    
    // Save dummy setting
    preferences.putString(KEY_DUMMY, config->dummy_setting);

    // Save MQTT settings
    preferences.putString(KEY_MQTT_HOST, config->mqtt_host);
    preferences.putUShort(KEY_MQTT_PORT, config->mqtt_port);
    preferences.putString(KEY_MQTT_USER, config->mqtt_username);
    preferences.putString(KEY_MQTT_PASS, config->mqtt_password);
    preferences.putUShort(KEY_MQTT_INTERVAL, config->mqtt_interval_seconds);

    // Save Energy Monitor MQTT settings
    preferences.putString(KEY_MQTT_SOLAR_TOPIC, config->mqtt_topic_solar);
    preferences.putString(KEY_MQTT_GRID_TOPIC, config->mqtt_topic_grid);

    const char *solar_path = (strlen(config->mqtt_solar_value_path) == 0) ? "." : config->mqtt_solar_value_path;
    const char *grid_path = (strlen(config->mqtt_grid_value_path) == 0) ? "." : config->mqtt_grid_value_path;
    preferences.putString(KEY_MQTT_SOLAR_PATH, solar_path);
    preferences.putString(KEY_MQTT_GRID_PATH, grid_path);

    // Save Energy Monitor UI scaling (kW)
    float solar_max = config->energy_solar_bar_max_kw > 0.0f ? config->energy_solar_bar_max_kw : 3.0f;
    float home_max = config->energy_home_bar_max_kw > 0.0f ? config->energy_home_bar_max_kw : 3.0f;
    float grid_max = config->energy_grid_bar_max_kw > 0.0f ? config->energy_grid_bar_max_kw : 3.0f;
    preferences.putFloat(KEY_ENERGY_SOLAR_BAR_MAX_KW, solar_max);
    preferences.putFloat(KEY_ENERGY_HOME_BAR_MAX_KW, home_max);
    preferences.putFloat(KEY_ENERGY_GRID_BAR_MAX_KW, grid_max);

    // Save Energy Monitor colors/thresholds
    preferences.putUInt(KEY_EN_SOL_CG, config->energy_solar_colors.color_good_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_SOL_CO, config->energy_solar_colors.color_ok_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_SOL_CA, config->energy_solar_colors.color_attention_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_SOL_CW, config->energy_solar_colors.color_warning_rgb & 0xFFFFFF);
    preferences.putInt(KEY_EN_SOL_T0, config->energy_solar_colors.threshold_mkw[0]);
    preferences.putInt(KEY_EN_SOL_T1, config->energy_solar_colors.threshold_mkw[1]);
    preferences.putInt(KEY_EN_SOL_T2, config->energy_solar_colors.threshold_mkw[2]);

    preferences.putUInt(KEY_EN_HOM_CG, config->energy_home_colors.color_good_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_HOM_CO, config->energy_home_colors.color_ok_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_HOM_CA, config->energy_home_colors.color_attention_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_HOM_CW, config->energy_home_colors.color_warning_rgb & 0xFFFFFF);
    preferences.putInt(KEY_EN_HOM_T0, config->energy_home_colors.threshold_mkw[0]);
    preferences.putInt(KEY_EN_HOM_T1, config->energy_home_colors.threshold_mkw[1]);
    preferences.putInt(KEY_EN_HOM_T2, config->energy_home_colors.threshold_mkw[2]);

    preferences.putUInt(KEY_EN_GRD_CG, config->energy_grid_colors.color_good_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_GRD_CO, config->energy_grid_colors.color_ok_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_GRD_CA, config->energy_grid_colors.color_attention_rgb & 0xFFFFFF);
    preferences.putUInt(KEY_EN_GRD_CW, config->energy_grid_colors.color_warning_rgb & 0xFFFFFF);
    preferences.putInt(KEY_EN_GRD_T0, config->energy_grid_colors.threshold_mkw[0]);
    preferences.putInt(KEY_EN_GRD_T1, config->energy_grid_colors.threshold_mkw[1]);
    preferences.putInt(KEY_EN_GRD_T2, config->energy_grid_colors.threshold_mkw[2]);
    
    // Save display settings
    Logger.logLinef("Saving brightness: %d%%", config->backlight_brightness);
    preferences.putUChar(KEY_BACKLIGHT_BRIGHTNESS, config->backlight_brightness);

    #if HAS_DISPLAY
    // Save screen saver settings
    preferences.putBool(KEY_SCREEN_SAVER_ENABLED, config->screen_saver_enabled);
    preferences.putUShort(KEY_SCREEN_SAVER_TIMEOUT, config->screen_saver_timeout_seconds);
    preferences.putUShort(KEY_SCREEN_SAVER_FADE_OUT, config->screen_saver_fade_out_ms);
    preferences.putUShort(KEY_SCREEN_SAVER_FADE_IN, config->screen_saver_fade_in_ms);
    preferences.putBool(KEY_SCREEN_SAVER_WAKE_TOUCH, config->screen_saver_wake_on_touch);
    #endif
    
    // Save magic number last (indicates valid config)
    preferences.putUInt(KEY_MAGIC, CONFIG_MAGIC);
    
    preferences.end();
    
    config_manager_print(config);
    Logger.logEnd();
    return true;
}

// Reset configuration (erase from NVS)
bool config_manager_reset() {
    Logger.logBegin("Config Reset");
    
    preferences.begin(CONFIG_NAMESPACE, false);
    bool success = preferences.clear();
    preferences.end();
    
    if (success) {
        Logger.logEnd();
    } else {
        Logger.logEnd("Failed to reset");
    }
    
    return success;
}

// Check if configuration is valid
bool config_manager_is_valid(const DeviceConfig *config) {
    if (!config) return false;
    if (config->magic != CONFIG_MAGIC) return false;
    if (strlen(config->wifi_ssid) == 0) return false;
    if (strlen(config->device_name) == 0) return false;
    return true;
}

// Print configuration (for debugging)
void config_manager_print(const DeviceConfig *config) {
    if (!config) return;
    
    Logger.logLinef("Device: %s", config->device_name);
    
    // Show sanitized name for mDNS
    char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
    config_manager_sanitize_device_name(config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);
    Logger.logLinef("mDNS: %s.local", sanitized);
    
    Logger.logLinef("WiFi SSID: %s", config->wifi_ssid);
    Logger.logLinef("WiFi Pass: %s", strlen(config->wifi_password) > 0 ? "***" : "(none)");
    
    if (strlen(config->fixed_ip) > 0) {
        Logger.logLinef("IP: %s", config->fixed_ip);
        Logger.logLinef("Subnet: %s", config->subnet_mask);
        Logger.logLinef("Gateway: %s", config->gateway);
        Logger.logLinef("DNS: %s, %s", config->dns1, strlen(config->dns2) > 0 ? config->dns2 : "(none)");
    } else {
        Logger.logLine("IP: DHCP");
    }

#if HAS_MQTT
    if (strlen(config->mqtt_host) > 0) {
        uint16_t port = config->mqtt_port > 0 ? config->mqtt_port : 1883;
        if (config->mqtt_interval_seconds > 0) {
            Logger.logLinef("MQTT: %s:%d (%ds)", config->mqtt_host, port, config->mqtt_interval_seconds);
        } else {
            Logger.logLinef("MQTT: %s:%d (publish disabled)", config->mqtt_host, port);
        }
        Logger.logLinef("MQTT User: %s", strlen(config->mqtt_username) > 0 ? config->mqtt_username : "(none)");
        Logger.logLinef("MQTT Pass: %s", strlen(config->mqtt_password) > 0 ? "***" : "(none)");
    } else {
        Logger.logLine("MQTT: disabled");
    }
#else
    // MQTT config can still exist in NVS, but the firmware has MQTT support compiled out.
    Logger.logLine("MQTT: disabled (feature not compiled into firmware)");
#endif
}
