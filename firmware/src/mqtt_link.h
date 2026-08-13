#pragma once
#include <stddef.h>

// mqtt_link.h — uplink telemetri + downlink command via esp-mqtt (Task 15)
void mqttInit(const char* gw);                        // connect + LWT + subscribe command
bool mqttConnected();
bool mqttEnqueueTelemetry(const char* json, size_t n); // esp_mqtt_client_enqueue QoS1
bool mqttPublishAck(const char* json, size_t n);       // esp_mqtt_client_enqueue QoS1
