#pragma once
#define WIFI_SSID     "isi-ssid"
#define WIFI_PASS     "isi-password"
#define MQTT_URI      "mqtt://mqtt-dev.bepbatt.id:1883"
#define MQTT_USER     "isi-user"
#define MQTT_PASSWD   "isi-pass"

// OTA (sub-proyek G): TIDAK perlu didefinisikan di sini untuk memakai broker
// tim (default di config.h sudah kunci publik tim). Untuk bench dengan kunci
// dev sendiri (mis. hasil `uv run --with cryptography python
// bess-sim/tools/ota_publish.py --gen-key dev_key.pem`), timpa dengan base64
// 32 byte kunci PUBLIK dev itu -- JANGAN commit private key ke mana pun:
// #define OTA_ED25519_PUBKEY_B64 "isi-base64-32-byte-kunci-publik-dev"
