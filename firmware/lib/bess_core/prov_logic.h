#pragma once
#include <stdint.h>
#include <stddef.h>

// prov_logic -- logika MURNI sub-proyek E (provisioning). Validasi input,
// parsing IP, pembangkit gateway_code, dan evaluasi status AP: semuanya
// tanpa menyentuh WiFi/NVS/DNS supaya bisa diuji native. Integrasi ESP32
// (Preferences, WiFi, DNSServer, ESPmDNS) ada di src/prov.cpp.

#define PROV_GATEWAY_CODE_LEN   6     // char, di luar NUL

// SSID router (STA): 1..32 byte -- paritas BEPESP32_WiFi_Extension.
bool provValidSsid(const char* ssid);

// Password STA: 0..63 byte (kosong = jaringan terbuka, dibolehkan --
// keputusan operator, bukan sesuatu yang bisa gateway paksa).
bool provValidStaPass(const char* pass);

// Password AP fallback: WAJIB 8..63 byte -- kosong DILARANG.
// PENYIMPANGAN SADAR dari tim (yang membolehkan password AP kosong ATAU
// 8-63 byte): AP fallback gateway ini SELALU menyala sebagai jalur kendali
// darurat ke konverter 50 kW, jadi "AP tanpa password" bukan pilihan yang
// aman untuk ditinggalkan sebagai default maupun opsi operator.
bool provValidApPass(const char* pass);

// Hostname mDNS: lowercase [a-z0-9-], 1..63 byte, tak diawali/diakhiri '-'.
bool provValidMdnsHostname(const char* host);

// Parse IPv4 dotted-decimal "a.b.c.d" (tiap oktet 0..255). Parser sederhana
// (bukan validator RFC ketat -- leading zero diterima apa adanya), tapi
// menolak tegas: oktet >255, bukan tepat 4 oktet, karakter di luar digit/'.'.
// false = format tak valid (out tak diubah).
bool provParseIPv4(const char* s, uint8_t out[4]);

// Sumber acak disuntikkan supaya pembangkitan gateway_code deterministik di
// test (lihat src/prov.cpp untuk pembungkus esp_random() asli).
typedef uint32_t (*ProvRandFn)();

// Bangkitkan gateway_code 6 karakter A-Z0-9 dari rand_fn. out harus berupa
// buffer >= PROV_GATEWAY_CODE_LEN+1 byte (6 char + NUL).
void provGenGatewayCode(ProvRandFn rand_fn, char out[PROV_GATEWAY_CODE_LEN + 1]);

// Pembanding waktu-konstan untuk field `code` di endpoint provisioning --
// panjang input BUKAN rahasia (aman dibandingkan biasa), tapi ISI-nya iya,
// supaya waktu respons tidak membocorkan berapa karakter awal yang sudah
// cocok (pola sama dengan sodium_memcmp/crypto_verify). `stored` HARUS
// persis PROV_GATEWAY_CODE_LEN karakter + NUL (kontrak internal --
// gateway_code selalu dibangkitkan lewat provGenGatewayCode di atas).
bool provCodeEquals(const char* input, const char* stored);

// Status AP fallback (paritas pola tim, NetworkManager.cpp::updateWiFiRecoveryAp):
// AP menyala selama STA putus, dan TETAP menyala after_connect_ms sesudah STA
// tersambung (lalu mati). ms_since_connect diabaikan bila !sta_connected.
bool provApShouldBeOn(bool sta_connected, uint32_t ms_since_connect, uint32_t after_connect_ms);
