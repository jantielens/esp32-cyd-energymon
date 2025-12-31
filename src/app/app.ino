#include "../version.h"
#include "board_config.h"
#include "config_manager.h"
#include "web_portal.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "device_telemetry.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <lwip/netif.h>

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_TOUCH
#include "touch_manager.h"
#endif

// Configuration
DeviceConfig device_config;
bool config_loaded = false;

#if HAS_MQTT
MqttManager mqtt_manager;
#endif

// WiFi retry settings
const unsigned long WIFI_BACKOFF_BASE = 3000; // 3 seconds base (DHCP typically needs 2-3s)

// Heartbeat interval
const unsigned long HEARTBEAT_INTERVAL = 60000; // 60 seconds
unsigned long lastHeartbeat = 0;

// WiFi watchdog for connection monitoring
const unsigned long WIFI_CHECK_INTERVAL = 10000; // 10 seconds
unsigned long lastWiFiCheck = 0;

// WiFi event handlers for connection lifecycle monitoring
void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Logger.logMessage("WiFi", "Connected to AP - waiting for IP");
}

void onWiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Logger.logMessagef("WiFi", "Got IP: %s", WiFi.localIP().toString().c_str());
}

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  uint8_t reason = info.wifi_sta_disconnected.reason;
  Logger.logMessagef("WiFi", "Disconnected - reason: %d", reason);

  // Common disconnect reasons:
  // 2 = AUTH_EXPIRE, 3 = AUTH_LEAVE, 4 = ASSOC_EXPIRE
  // 8 = ASSOC_LEAVE, 15 = 4WAY_HANDSHAKE_TIMEOUT
  // 201 = NO_AP_FOUND, 202 = AUTH_FAIL, 205 = HANDSHAKE_TIMEOUT
}


void setup()
{
  // Initialize log manager (wraps Serial for web streaming)
  Logger.begin(115200);
  delay(1000);

  // Register WiFi event handlers for connection lifecycle
  WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(onWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  Logger.logBegin("System Boot");
  Logger.logLinef("Firmware: v%s", FIRMWARE_VERSION);
  Logger.logLinef("Chip: %s (Rev %d)", ESP.getChipModel(), ESP.getChipRevision());
  Logger.logLinef("CPU: %d MHz", ESP.getCpuFreqMHz());
  Logger.logLinef("Flash: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
  Logger.logLinef("MAC: %s", WiFi.macAddress().c_str());
  #if HAS_BUILTIN_LED
  Logger.logLinef("LED: GPIO%d (active %s)", LED_PIN, LED_ACTIVE_HIGH ? "HIGH" : "LOW");
  #endif
  // Example: Call board-specific function if available
  // #ifdef HAS_CUSTOM_IDENTIFIER
  // Logger.logLinef("Board: %s", board_get_custom_identifier());
  // #endif
  Logger.logEnd();

  // Baseline memory snapshot as early as possible.
  device_telemetry_log_memory_snapshot("boot");

  // Initialize device_config with sensible defaults
  // (Important: must happen before display_manager_init uses the config)
  memset(&device_config, 0, sizeof(DeviceConfig));
  device_config.backlight_brightness = 100;  // Default to full brightness
  device_config.mqtt_port = 0;
  device_config.mqtt_interval_seconds = 0;

  #if HAS_DISPLAY
  // Screen saver defaults (v1)
  device_config.screen_saver_enabled = false;
  device_config.screen_saver_timeout_seconds = 300;
  device_config.screen_saver_fade_out_ms = 800;
  device_config.screen_saver_fade_in_ms = 400;
  #if HAS_TOUCH
  device_config.screen_saver_wake_on_touch = true;
  #else
  device_config.screen_saver_wake_on_touch = false;
  #endif
  #endif

  // Initialize board-specific hardware
  #if HAS_BUILTIN_LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH); // LED off initially
  #endif

  #if HAS_DISPLAY
  display_manager_init(&device_config);
  display_manager_set_splash_status("Loading config...");
  #endif

  #if HAS_TOUCH
  // Initialize touch after display is ready
  touch_manager_init();
  #endif

  // Initialize configuration manager
  #if HAS_DISPLAY
  display_manager_set_splash_status("Init NVS...");
  #endif
  config_manager_init();

  // Cache flash/sketch metadata early to avoid concurrent access from different tasks later
  // (e.g., MQTT publish + web API calls).
  device_telemetry_init();

  // Start CPU monitoring background task
  device_telemetry_start_cpu_monitoring();

  // Try to load saved configuration
  #if HAS_DISPLAY
  display_manager_set_splash_status("Reading config...");
  #endif
  config_loaded = config_manager_load(&device_config);

  if (!config_loaded) {
    // No config found - set default device name
    String default_name = config_manager_get_default_device_name();
    strlcpy(device_config.device_name, default_name.c_str(), CONFIG_DEVICE_NAME_MAX_LEN);
    device_config.magic = CONFIG_MAGIC;
  }

  // Re-apply brightness from loaded config (display was initialized before config load)
  #if HAS_DISPLAY && HAS_BACKLIGHT
  Logger.logLinef("Main: Applying loaded brightness: %d%%", device_config.backlight_brightness);
  display_manager_set_backlight_brightness(device_config.backlight_brightness);
  #endif

  #if HAS_DISPLAY
  // Initialize screen saver manager after config is loaded.
  screen_saver_manager_init(&device_config);
  #endif

  // Start WiFi BEFORE initializing web server (critical for ESP32-C3)
  #if HAS_DISPLAY
  display_manager_set_splash_status("Connecting WiFi...");
  #endif

  if (!config_loaded) {
    Logger.logMessage("Main", "No config - starting AP mode");
    web_portal_start_ap();
  } else {
    Logger.logMessage("Main", "Config loaded - connecting to WiFi");
    if (connect_wifi()) {
      start_mdns();
    } else {
      // Hard reset retry - WiFi hardware may be in bad state
      Logger.logMessage("Main", "WiFi failed - attempting hard reset");
      Logger.logBegin("WiFi Hard Reset");
      WiFi.mode(WIFI_OFF);
      delay(1000);  // Longer delay to fully reset hardware
      WiFi.mode(WIFI_STA);
      delay(500);
      Logger.logEnd("Reset complete");

      // One more attempt after hard reset
      if (connect_wifi()) {
        start_mdns();
      } else {
        Logger.logMessage("Main", "WiFi failed after reset - fallback to AP");
        web_portal_start_ap();
      }
    }
  }

  // Initialize web portal AFTER WiFi is started
  web_portal_init(&device_config);

  #if HAS_MQTT
  // Initialize MQTT manager (will only connect/publish when configured)
  char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
  config_manager_sanitize_device_name(device_config.device_name, sanitized, sizeof(sanitized));
  mqtt_manager.begin(&device_config, device_config.device_name, sanitized);
  #endif

  lastHeartbeat = millis();
  Logger.logMessage("Main", "Setup complete");

  // Snapshot after all subsystems are initialized.
  device_telemetry_log_memory_snapshot("setup");

  #if HAS_DISPLAY
  // Show splash for minimum duration to ensure visibility
  display_manager_set_splash_status("Ready!");
  delay(2000);  // 2 seconds to see splash + status updates

  // Navigate to info screen
  display_manager_show_info();

  // Start the screen saver inactivity timer after the first runtime screen is visible.
  // This avoids counting boot + splash time as "inactivity".
  screen_saver_manager_notify_activity(false);
  #endif
}

void loop()
{
  #if HAS_DISPLAY
  screen_saver_manager_loop();
  #endif

  #if HAS_TOUCH
  touch_manager_loop();
  #endif

  // Handle web portal (DNS for captive portal)
  web_portal_handle();

  #if HAS_IMAGE_API
  // Process pending image uploads (deferred decoding)
  web_portal_process_pending_images();
  #endif

  #if HAS_MQTT
  mqtt_manager.loop();
  #endif

  unsigned long currentMillis = millis();

  // WiFi watchdog - monitor connection and reconnect if needed
  // Only run if we're not in AP mode (AP mode is the fallback, should stay active)
  if (config_loaded && !web_portal_is_ap_mode() && currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED && strlen(device_config.wifi_ssid) > 0) {
      Logger.logMessage("WiFi Watchdog", "Connection lost - attempting reconnect");
      if (connect_wifi()) {
        start_mdns();
      }
    }
    lastWiFiCheck = currentMillis;
  }

  // Check if it's time for heartbeat
  if (currentMillis - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) {
      Logger.logQuickf("Heartbeat", "Up: %ds | Heap: %d | WiFi: %s (%s)",
        currentMillis / 1000, ESP.getFreeHeap(),
        WiFi.localIP().toString().c_str(), WiFi.getHostname());
    } else {
      Logger.logQuickf("Heartbeat", "Up: %ds | Heap: %d | WiFi: Disconnected",
        currentMillis / 1000, ESP.getFreeHeap());
    }

    lastHeartbeat = currentMillis;

    // Keep a consistent memory line in the logs to quantify before/after changes.
    device_telemetry_log_memory_snapshot("hb");
  }

  delay(10);
}

// Connect to WiFi with exponential backoff
bool connect_wifi() {
  Logger.logBegin("WiFi Connection");
  Logger.logLinef("SSID: %s", device_config.wifi_ssid);

  // Helper: format BSSID as string
  auto format_bssid = [](const uint8_t *bssid, char *out, size_t out_len) {
    if (!out || out_len < 18) return;
    if (!bssid) {
      snprintf(out, out_len, "--:--:--:--:--:--");
      return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  };

  // Best-effort: when multiple APs share the same SSID, explicitly connect to the strongest one.
  auto select_strongest_ap = [&](const char *target_ssid, uint8_t out_bssid[6], int *out_channel, int *out_rssi) -> bool {
    if (!target_ssid || strlen(target_ssid) == 0) return false;

    // Clear prior results to avoid stale entries and reduce memory usage.
    WiFi.scanDelete();

    Logger.logBegin("WiFi Scan");
    const int16_t n = WiFi.scanNetworks();
    if (n < 0) {
      Logger.logEnd("Scan failed");
      return false;
    }

    int best_index = -1;
    int best_rssi = -1000;
    int matches = 0;

    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == target_ssid) {
        matches++;
        const int rssi = WiFi.RSSI(i);
        if (best_index < 0 || rssi > best_rssi) {
          best_index = i;
          best_rssi = rssi;
        }
      }
    }

    Logger.logLinef("Found %d networks (%d matching SSID)", (int)n, matches);

    if (best_index < 0) {
      Logger.logEnd("No matching SSID");
      WiFi.scanDelete();
      return false;
    }

    const uint8_t *best_bssid_ptr = WiFi.BSSID(best_index);
    const int best_channel = WiFi.channel(best_index);

    if (!best_bssid_ptr || best_channel <= 0) {
      Logger.logEnd("Missing BSSID/channel");
      WiFi.scanDelete();
      return false;
    }

    memcpy(out_bssid, best_bssid_ptr, 6);
    if (out_channel) *out_channel = best_channel;
    if (out_rssi) *out_rssi = best_rssi;

    char bssid_str[18];
    format_bssid(out_bssid, bssid_str, sizeof(bssid_str));
    Logger.logLinef("Selected AP: %s | Ch %d | RSSI %d dBm", bssid_str, best_channel, best_rssi);
    Logger.logEnd();

    // Free scan results to save memory.
    WiFi.scanDelete();
    return true;
  };

  // === WiFi Hardware Reset Sequence ===
  // Disable persistent WiFi config (we manage our own via NVS)
  WiFi.persistent(false);

  // Full WiFi reset to clear stale state and prevent hardware corruption
  WiFi.disconnect(true);  // Disconnect + erase stored credentials
  delay(100);
  WiFi.mode(WIFI_OFF);    // Turn off WiFi hardware
  delay(500);             // Wait for hardware to settle
  WiFi.mode(WIFI_STA);    // Back to station mode
  delay(100);

  // Disable WiFi sleep (power-save). Improves latency and often stability,
  // at the cost of higher power consumption.
  WiFi.setSleep(false);

  // Enable auto-reconnect at WiFi stack level
  WiFi.setAutoReconnect(true);

  // Prepare sanitized hostname
  char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
  config_manager_sanitize_device_name(device_config.device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);

  // Set WiFi hostname for mDNS and internal use
  // NOTE: ESP32's lwIP stack has limited DHCP Option 12 (hostname) support
  // The hostname may not always appear in router DHCP tables due to ESP-IDF/lwIP limitations
  // Use mDNS (.local) or NetBIOS for reliable device discovery instead
  if (strlen(sanitized) > 0) {
    // Set via WiFi library
    WiFi.setHostname(sanitized);
    Logger.logLinef("Hostname: %s", sanitized);

    // Also set via esp_netif API (for compatibility)
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
      esp_netif_set_hostname(netif, sanitized);
    }
  }

  // Configure fixed IP if provided
  if (strlen(device_config.fixed_ip) > 0) {
    Logger.logBegin("Fixed IP Config");

    IPAddress local_ip, gateway, subnet, dns1, dns2;

    if (!local_ip.fromString(device_config.fixed_ip)) {
      Logger.logEnd("Invalid IP address");
      Logger.logEnd("Connection failed");
      return false;
    }

    if (!subnet.fromString(device_config.subnet_mask)) {
      Logger.logEnd("Invalid subnet mask");
      Logger.logEnd("Connection failed");
      return false;
    }

    if (!gateway.fromString(device_config.gateway)) {
      Logger.logEnd("Invalid gateway");
      Logger.logEnd("Connection failed");
      return false;
    }

    // DNS1: use provided, or default to gateway
    if (strlen(device_config.dns1) > 0) {
      dns1.fromString(device_config.dns1);
    } else {
      dns1 = gateway;
    }

    // DNS2: optional
    if (strlen(device_config.dns2) > 0) {
      dns2.fromString(device_config.dns2);
    } else {
      dns2 = IPAddress(0, 0, 0, 0);
    }

    if (!WiFi.config(local_ip, gateway, subnet, dns1, dns2)) {
      Logger.logEnd("Configuration failed");
      Logger.logEnd("Connection failed");
      return false;
    }

    Logger.logLinef("IP: %s", device_config.fixed_ip);
    Logger.logEnd();
  }

  // Prefer the strongest AP when multiple BSSIDs exist for the same SSID.
  uint8_t best_bssid[6];
  int best_channel = 0;
  int best_rssi = 0;
  const bool has_best_ap = select_strongest_ap(device_config.wifi_ssid, best_bssid, &best_channel, &best_rssi);
  if (has_best_ap) {
    WiFi.begin(device_config.wifi_ssid, device_config.wifi_password, best_channel, best_bssid);
  } else {
    WiFi.begin(device_config.wifi_ssid, device_config.wifi_password);
  }

  // Try to connect with exponential backoff
  for (int attempt = 0; attempt < WIFI_MAX_ATTEMPTS; attempt++) {
    unsigned long backoff = WIFI_BACKOFF_BASE * (attempt + 1);
    unsigned long start = millis();

    Logger.logLinef("Attempt %d/%d (timeout %ds)", attempt + 1, WIFI_MAX_ATTEMPTS, backoff / 1000);

    while (millis() - start < backoff) {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        Logger.logLinef("IP: %s", WiFi.localIP().toString().c_str());
        Logger.logLinef("Hostname: %s", WiFi.getHostname());
        Logger.logLinef("MAC: %s", WiFi.macAddress().c_str());
        Logger.logLinef("Signal: %d dBm", WiFi.RSSI());
        Logger.logLine("");
        Logger.logLine("Access via:");
        Logger.logLinef("  http://%s", WiFi.localIP().toString().c_str());
        Logger.logLinef("  http://%s.local", WiFi.getHostname());
        Logger.logEnd("Connected");

        return true;
      }
      delay(100);
    }

    // Log detailed failure reason for diagnostics
    wl_status_t status = WiFi.status();
    if (status != WL_CONNECTED) {
      const char* reason =
        (status == WL_NO_SSID_AVAIL) ? "SSID not found" :
        (status == WL_CONNECT_FAILED) ? "Connect failed (wrong password?)" :
        (status == WL_CONNECTION_LOST) ? "Connection lost" :
        (status == WL_DISCONNECTED) ? "Disconnected" :
        "Unknown";
      Logger.logLinef("Status: %s (%d)", reason, status);
    }
  }

  Logger.logEnd("All attempts failed");
  return false;
}

// Start mDNS service with enhanced TXT records
void start_mdns() {
  Logger.logBegin("mDNS");

  char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
  config_manager_sanitize_device_name(device_config.device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);

  if (strlen(sanitized) == 0) {
    Logger.logEnd("Empty hostname");
    return;
  }

  if (MDNS.begin(sanitized)) {
    Logger.logLinef("Name: %s.local", sanitized);

    // Add HTTP service
    MDNS.addService("http", "tcp", 80);

    // Add TXT records with device metadata (per RFC 6763)
    // Keep keys ≤9 chars, total TXT record <400 bytes

    // Core device identification
    MDNS.addServiceTxt("http", "tcp", "version", FIRMWARE_VERSION);
    MDNS.addServiceTxt("http", "tcp", "model", ESP.getChipModel());

    // MAC address (last 4 hex digits for identification)
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String mac_short = mac.substring(mac.length() - 4);
    MDNS.addServiceTxt("http", "tcp", "mac", mac_short.c_str());

    // Device type and manufacturer
    MDNS.addServiceTxt("http", "tcp", "ty", "iot-device");
    MDNS.addServiceTxt("http", "tcp", "mf", "ESP32-Tmpl");

    // Capabilities
    MDNS.addServiceTxt("http", "tcp", "features", "wifi,http,api");

    // User-friendly description
    MDNS.addServiceTxt("http", "tcp", "note", "WiFi Portal Device");

    // Configuration URL
    String config_url = "http://";
    config_url += sanitized;
    config_url += ".local";
    MDNS.addServiceTxt("http", "tcp", "url", config_url.c_str());

    Logger.logLine("TXT records: version, model, mac, ty, features");
    Logger.logEnd();
  } else {
    Logger.logEnd("Failed to start");
  }
}