#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include "board_config.h"

#if HAS_MQTT

// PubSubClient uses MQTT_MAX_PACKET_SIZE at compile time
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024
#endif

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "config_manager.h"

class MqttManager {
public:
    MqttManager();

    void begin(const DeviceConfig *config, const char *friendly_name, const char *sanitized_name);
    void loop();

    bool enabled() const;
    bool publishEnabled() const;
    bool connected();

    // Publish helpers
    bool publish(const char *topic, const char *payload, bool retained);
    bool publishJson(const char *topic, JsonDocument &doc, bool retained);

    // Immediate publish API (topic is full topic string)
    bool publishImmediate(const char *topic, const char *payload, bool retained);

    // Topic helpers
    const char *baseTopic() const { return _base_topic; }
    const char *availabilityTopic() const { return _availability_topic; }
    const char *healthStateTopic() const { return _health_state_topic; }

    const char *friendlyName() const { return _friendly_name; }
    const char *sanitizedName() const { return _sanitized_name; }

    // PubSubClient callback entrypoint (called by a C-style trampoline).
    void handleIncomingMessage(const char *topic, const uint8_t *payload, unsigned int length);

    // Request a reconnect so updated config (host/credentials/topics) takes effect.
    // Safe to call from the main loop.
    void requestReconnect();

private:
    void ensureConnected();
    void publishAvailability(bool online);
    void publishDiscoveryOncePerBoot();
    void publishHealthNow();
    void publishHealthIfDue();

    void subscribeEnergyMonitorTopics();

    bool connectEnabled() const;
    uint16_t resolvedPort() const;

    WiFiClient _net;
    PubSubClient _client;

    const DeviceConfig *_config = nullptr;
    char _friendly_name[CONFIG_DEVICE_NAME_MAX_LEN] = {0};
    char _sanitized_name[CONFIG_DEVICE_NAME_MAX_LEN] = {0};

    char _base_topic[96] = {0};
    char _availability_topic[128] = {0};
    char _health_state_topic[128] = {0};

    bool _discovery_published_this_boot = false;

    unsigned long _last_reconnect_attempt_ms = 0;
    unsigned long _last_health_publish_ms = 0;

    bool _energy_subscriptions_active = false;
};

// Global helper: request reconnect on the active MqttManager instance (if any).
// Safe to call from the main loop.
void mqtt_manager_request_reconnect();

#endif // HAS_MQTT

#endif // MQTT_MANAGER_H
