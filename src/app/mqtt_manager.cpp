#include "mqtt_manager.h"

#include "board_config.h"

#if HAS_MQTT

#include "ha_discovery.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "energy_monitor.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

static MqttManager* s_mqtt_manager_instance = nullptr;

static float parse_float_from_payload(const uint8_t* payload, unsigned int length, bool* ok) {
    if (ok) *ok = false;
    if (!payload || length == 0) return NAN;

    // Fast path: parse a bare number without allocating/decoding JSON.
    // Copy a small prefix to a temporary buffer and parse with strtod.
    char tmp[64];
    unsigned int i = 0;
    while (i < length && isspace((int)payload[i])) i++;
    unsigned int j = 0;
    while (i < length && j < (sizeof(tmp) - 1)) {
        char c = (char)payload[i];
        if (c == '\0' || isspace((int)c) || c == ',' || c == '}' || c == ']') break;
        tmp[j++] = c;
        i++;
    }
    tmp[j] = 0;

    if (j > 0) {
        char* endp = nullptr;
        double v = strtod(tmp, &endp);
        if (endp && endp != tmp) {
            if (ok) *ok = true;
            return (float)v;
        }
    }

    // Fallback: if payload is a JSON number, parse it as JSON.
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (!err && doc.is<float>()) {
        if (ok) *ok = true;
        return doc.as<float>();
    }

    return NAN;
}

static float parse_value_using_path(const uint8_t* payload, unsigned int length, const char* value_path, bool* ok) {
    if (ok) *ok = false;

    if (!value_path || strlen(value_path) == 0 || strcmp(value_path, ".") == 0) {
        return parse_float_from_payload(payload, length, ok);
    }

    // Minimal implementation: value_path is a top-level JSON key.
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) return NAN;

    if (!doc.containsKey(value_path)) return NAN;
    JsonVariant v = doc[value_path];
    if (v.is<float>() || v.is<int>() || v.is<long>() || v.is<double>()) {
        if (ok) *ok = true;
        return v.as<float>();
    }

    return NAN;
}

static void mqtt_message_trampoline(char* topic, uint8_t* payload, unsigned int length) {
    if (!s_mqtt_manager_instance) return;
    s_mqtt_manager_instance->handleIncomingMessage(topic, payload, length);
}

void mqtt_manager_request_reconnect() {
    if (!s_mqtt_manager_instance) return;
    s_mqtt_manager_instance->requestReconnect();
}

MqttManager::MqttManager() : _client(_net) {}

void MqttManager::begin(const DeviceConfig *config, const char *friendly_name, const char *sanitized_name) {
    _config = config;

    // Register callback for subscriptions (Energy Monitor).
    s_mqtt_manager_instance = this;
    _client.setCallback(mqtt_message_trampoline);

    if (friendly_name) {
        strlcpy(_friendly_name, friendly_name, sizeof(_friendly_name));
    }
    if (sanitized_name) {
        strlcpy(_sanitized_name, sanitized_name, sizeof(_sanitized_name));
    }

    // Safety: if sanitization produced an empty string, fall back to a stable default.
    if (strlen(_sanitized_name) == 0) {
        strlcpy(_sanitized_name, "esp32", sizeof(_sanitized_name));
    }

    snprintf(_base_topic, sizeof(_base_topic), "devices/%s", _sanitized_name);
    snprintf(_availability_topic, sizeof(_availability_topic), "%s/availability", _base_topic);
    snprintf(_health_state_topic, sizeof(_health_state_topic), "%s/health/state", _base_topic);

    _client.setBufferSize(MQTT_MAX_PACKET_SIZE);

    _discovery_published_this_boot = false;
    _last_reconnect_attempt_ms = 0;
    _last_health_publish_ms = 0;
    _energy_subscriptions_active = false;
    _last_energy_subscribe_attempt_ms = 0;
}

void MqttManager::subscribeEnergyMonitorTopics() {
    if (!_config) return;
    if (!_client.connected()) return;

    bool any = false;

    if (strlen(_config->mqtt_topic_solar) > 0) {
        bool ok = _client.subscribe(_config->mqtt_topic_solar);
        LOGI("MQTT", "Subscribe solar '%s': %s", _config->mqtt_topic_solar, ok ? "OK" : "FAIL");
        any = any || ok;
    }
    if (strlen(_config->mqtt_topic_grid) > 0) {
        bool ok = _client.subscribe(_config->mqtt_topic_grid);
        LOGI("MQTT", "Subscribe grid '%s': %s", _config->mqtt_topic_grid, ok ? "OK" : "FAIL");
        any = any || ok;
    }

    _energy_subscriptions_active = any;
}

void MqttManager::requestReconnect() {
    // Force PubSubClient to drop the current connection so ensureConnected()
    // uses the latest host/credentials/topics from _config.
    if (_client.connected()) {
        _client.disconnect();
    }

    _energy_subscriptions_active = false;
    _last_reconnect_attempt_ms = 0;
    _last_energy_subscribe_attempt_ms = 0;
}

void MqttManager::handleIncomingMessage(const char *topic, const uint8_t *payload, unsigned int length) {
    if (!_config) return;
    if (!topic || !payload || length == 0) return;

    uint32_t now = millis();

    if (strlen(_config->mqtt_topic_solar) > 0 && strcmp(topic, _config->mqtt_topic_solar) == 0) {
        bool ok = false;
        float v = parse_value_using_path(payload, length, _config->mqtt_solar_value_path, &ok);
        energy_monitor_set_solar(ok ? v : NAN, now);
        char buf[24];
        if (ok) {
            snprintf(buf, sizeof(buf), "%.3f", (double)v);
        } else {
            strlcpy(buf, "NAN", sizeof(buf));
        }
        LOGI("MQTT", "Energy solar update: %s -> %s", topic, buf);
        return;
    }

    if (strlen(_config->mqtt_topic_grid) > 0 && strcmp(topic, _config->mqtt_topic_grid) == 0) {
        bool ok = false;
        float v = parse_value_using_path(payload, length, _config->mqtt_grid_value_path, &ok);
        energy_monitor_set_grid(ok ? v : NAN, now);
        char buf[24];
        if (ok) {
            snprintf(buf, sizeof(buf), "%.3f", (double)v);
        } else {
            strlcpy(buf, "NAN", sizeof(buf));
        }
        LOGI("MQTT", "Energy grid update: %s -> %s", topic, buf);
        return;
    }
}

bool MqttManager::connectEnabled() const {
    if (!_config) return false;
    if (strlen(_config->mqtt_host) == 0) return false;
    return true;
}

uint16_t MqttManager::resolvedPort() const {
    if (!_config) return 1883;
    return _config->mqtt_port > 0 ? _config->mqtt_port : 1883;
}

bool MqttManager::enabled() const {
    // Enabled = we should connect to the broker.
    return connectEnabled();
}

bool MqttManager::publishEnabled() const {
    // Publishing health periodically is optional.
    if (!_config) return false;
    if (!connectEnabled()) return false;
    return _config->mqtt_interval_seconds > 0;
}

bool MqttManager::connected() {
    return _client.connected();
}

bool MqttManager::publish(const char *topic, const char *payload, bool retained) {
    if (!enabled() || !_client.connected()) return false;
    if (!topic || !payload) return false;

    return _client.publish(topic, payload, retained);
}

bool MqttManager::publishJson(const char *topic, JsonDocument &doc, bool retained) {
    if (!topic) return false;

    // Avoid heap allocations inside String by using a bounded buffer.
    char payload[MQTT_MAX_PACKET_SIZE];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0 || n >= sizeof(payload)) {
        LOGE("MQTT", "JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
        return false;
    }

    if (!enabled() || !_client.connected()) return false;
    return _client.publish(topic, (const uint8_t*)payload, (unsigned)n, retained);
}

bool MqttManager::publishImmediate(const char *topic, const char *payload, bool retained) {
    return publish(topic, payload, retained);
}

void MqttManager::publishAvailability(bool online) {
    if (!_client.connected()) return;
    _client.publish(_availability_topic, online ? "online" : "offline", true);
}

void MqttManager::publishDiscoveryOncePerBoot() {
    if (_discovery_published_this_boot) return;

    LOGI("MQTT", "Publishing HA discovery");
    ha_discovery_publish_health(*this);
    _discovery_published_this_boot = true;
}

void MqttManager::publishHealthNow() {
    if (!_client.connected()) return;

    StaticJsonDocument<768> doc;
    device_telemetry_fill_mqtt(doc);

    if (doc.overflowed()) {
        LOGE("MQTT", "Health JSON overflow (StaticJsonDocument too small)");
        return;
    }

    char payload[MQTT_MAX_PACKET_SIZE];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0 || n >= sizeof(payload)) {
        LOGE("MQTT", "Health JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
        return;
    }
    _client.publish(_health_state_topic, (const uint8_t*)payload, (unsigned)n, true);
}

void MqttManager::publishHealthIfDue() {
    if (!_client.connected()) return;
    if (!publishEnabled()) return;

    unsigned long now = millis();
    unsigned long interval_ms = (unsigned long)_config->mqtt_interval_seconds * 1000UL;

    if (_last_health_publish_ms == 0 || (now - _last_health_publish_ms) >= interval_ms) {
        StaticJsonDocument<768> doc;
        device_telemetry_fill_mqtt(doc);

        if (doc.overflowed()) {
            LOGE("MQTT", "Health JSON overflow (StaticJsonDocument too small)");
            return;
        }

        char payload[MQTT_MAX_PACKET_SIZE];
        size_t n = serializeJson(doc, payload, sizeof(payload));
        if (n == 0 || n >= sizeof(payload)) {
            LOGE("MQTT", "Health JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
            return;
        }

        bool ok = _client.publish(_health_state_topic, (const uint8_t*)payload, (unsigned)n, true);

        if (ok) {
            _last_health_publish_ms = now;
        }
    }
}

void MqttManager::ensureConnected() {
    if (!enabled()) return;
    if (WiFi.status() != WL_CONNECTED) return;

    if (_client.connected()) return;

    unsigned long now = millis();
    if (_last_reconnect_attempt_ms > 0 && (now - _last_reconnect_attempt_ms) < 5000) {
        return;
    }
    _last_reconnect_attempt_ms = now;

    _client.setServer(_config->mqtt_host, resolvedPort());

    // Client ID: sanitized name
    char client_id[96];
    snprintf(client_id, sizeof(client_id), "%s", _sanitized_name);

    bool has_user = strlen(_config->mqtt_username) > 0;
    bool has_pass = strlen(_config->mqtt_password) > 0;

    LOGI("MQTT", "Connecting to %s:%d", _config->mqtt_host, resolvedPort());

    bool connected = false;
    if (has_user) {
        const char *pass = has_pass ? _config->mqtt_password : "";
        connected = _client.connect(
            client_id,
            _config->mqtt_username,
            pass,
            _availability_topic,
            0,
            true,
            "offline"
        );
    } else {
        connected = _client.connect(
            client_id,
            _availability_topic,
            0,
            true,
            "offline"
        );
    }

    if (connected) {
        LOGI("MQTT", "Connected");
        publishAvailability(true);
        publishDiscoveryOncePerBoot();

        // Subscribe after connect so we receive Energy Monitor updates.
        subscribeEnergyMonitorTopics();

        // Publish a single retained state after connect so HA entities have values,
        // even when periodic publishing is disabled (interval = 0).
        publishHealthNow();

        // If periodic publishing is enabled, start interval timing from now.
        _last_health_publish_ms = millis();
    } else {
        LOGW("MQTT", "Connect failed (state %d)", _client.state());
        _energy_subscriptions_active = false;
    }
}

void MqttManager::loop() {
    if (!enabled()) return;

    ensureConnected();

    if (_client.connected()) {
        _client.loop();
        if (!_energy_subscriptions_active) {
            unsigned long now = millis();
            if (_last_energy_subscribe_attempt_ms == 0 || (now - _last_energy_subscribe_attempt_ms) >= 5000) {
                _last_energy_subscribe_attempt_ms = now;
                subscribeEnergyMonitorTopics();
            }
        }
        publishHealthIfDue();
    }
}

#endif // HAS_MQTT
