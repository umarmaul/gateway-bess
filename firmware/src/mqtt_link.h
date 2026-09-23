#pragma once
#include <stddef.h>

// mqtt_link.h — uplink telemetri + downlink command/OTA via esp-mqtt (Task 15 + sub-proyek G)
void mqttInit(const char* gw);                        // konfigurasi + LWT (belum connect)
void mqttTick(bool wifi_up);                          // start client begitu WiFi pertama kali naik
void mqttTxStart();                                   // task mqtt_tx (panggil sebelum mqttInit)
bool mqttConnected();

// Topic tujuan untuk mqttPublish -- SEMUA lewat antrean generik task mqtt_tx
// (tak pernah menunggu lock esp-mqtt langsung, lihat mqtt_link.cpp).
enum MqttTopic { MQTT_TOPIC_ACK, MQTT_TOPIC_OTA_ACK, MQTT_TOPIC_OTA_STATUS };

// Menitip pesan ke task mqtt_tx dan kembali seketika. Ditahan di antrean
// sampai MQTT terhubung; basi >TX_MAX_AGE_MS dibuang. retain diteruskan apa
// adanya ke esp-mqtt (status OTA retained, ack/ota_ack tidak).
// false = tak bisa dititip (n tak valid / antrean penuh).
bool mqttPublish(MqttTopic topic, const char* json, size_t n, bool retain);

// Pembungkus lama, dipertahankan: ack command, tidak retained.
bool mqttPublishAck(const char* json, size_t n);

// latest wins, QoS1, tak lewat antrean generik (telemetri jauh lebih besar
// dan hanya perlu nilai terakhir, bukan semua histori seperti ack/status).
bool mqttEnqueueTelemetry(const char* json, size_t n);
