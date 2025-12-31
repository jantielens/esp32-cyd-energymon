#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include <Arduino.h>
#include "board_config.h"

#if HAS_MQTT

class MqttManager;

// Publish Home Assistant MQTT discovery configuration for the health sensors.
// Intended to be called once per boot after MQTT connects.
void ha_discovery_publish_health(MqttManager &mqtt);

#endif // HAS_MQTT

#endif // HA_DISCOVERY_H
