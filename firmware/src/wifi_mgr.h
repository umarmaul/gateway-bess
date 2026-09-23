#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// wifi_mgr -- STA WiFi + backoff reconnect. wifiTick() SATU-SATUNYA penggerak
// reconnect (setAutoReconnect(false), lihat pelajaran 24 Juli di CLAUDE.md).
// Kredensial disuntikkan dari prov.cpp (sub-proyek E: NVS wifi_cfg, fallback
// WIFI_SSID/WIFI_PASS secrets.h) -- wifi_mgr sendiri tidak menyentuh NVS.

// IP statis opsional -- panggil SEBELUM wifiInit() kalau prov.cpp punya
// konfigurasi sta_static aktif. Tidak dipanggil = DHCP (perilaku lama).
void wifiSetStaticIp(IPAddress ip, IPAddress gw, IPAddress mask, IPAddress dns1, IPAddress dns2);

// ssid/pass disalin ke buffer statis internal -- pemanggil tidak perlu
// menjaga umur string setelah panggilan ini kembali.
void wifiInit(const char* ssid, const char* pass);
void wifiTick();
bool wifiConnected();
void wifiGw(char out[13]);
const char* wifiSsid();   // SSID STA yang sedang dipakai (untuk telemetri, ganti macro WIFI_SSID lama)
