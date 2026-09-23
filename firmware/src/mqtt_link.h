#pragma once
#include <stddef.h>

// mqtt_link.h — uplink telemetri + downlink command via esp-mqtt (Task 15)
void mqttInit(const char* gw);                        // konfigurasi + LWT (belum connect)
void mqttTick(bool wifi_up);                          // start client begitu WiFi pertama kali naik
void mqttTxStart();                                   // task mqtt_tx (panggil sebelum mqttInit)
bool mqttConnected();
// Keduanya hanya MENITIP ke task mqtt_tx dan kembali seketika (tak pernah
// menunggu lock esp-mqtt). false = tak bisa dititip (n tak valid / antrean penuh).
bool mqttEnqueueTelemetry(const char* json, size_t n); // latest wins, QoS1
bool mqttPublishAck(const char* json, size_t n);       // antre sampai terhubung, QoS1
