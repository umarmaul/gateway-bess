#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// prov.h -- integrasi ESP32 sub-proyek E (provisioning): NVS wifi_cfg/device_id,
// SoftAP fallback + captive DNS, mDNS, tombol factory reset (GPIO9/BOOT).
// Logika murni (validasi input, parse IP, gateway_code, status AP) ada di
// lib/bess_core/prov_logic.h -- diuji native, dipakai di sini apa adanya.

void provInit();   // panggil SEKALI di setup(), SEBELUM wifiInit()
void provTick();   // panggil tiap loop(): DNS captive, AP on/off, retry mDNS, tombol reset, reboot terjadwal

// Kredensial STA efektif untuk wifiInit() -- dari NVS wifi_cfg kalau ada,
// fallback WIFI_SSID/WIFI_PASS (secrets.h) kalau NVS kosong (bench tetap
// jalan tanpa provisioning).
const char* provStaSsid();
const char* provStaPass();

struct ProvStaticIp {
    bool enabled;
    IPAddress ip, gw, mask, dns1, dns2;
};
// enabled=false berarti DHCP (perilaku lama) -- field IP lain tidak berarti.
ProvStaticIp provStaticIp();

// Untuk telemetri (SysInfo.ap_active / SysInfo.mdns, lihat main.cpp).
bool provApActive();
const char* provMdnsHostname();

// SSID AP fallback efektif (default "BEP-CONNECT-<gateway_code>", atau hasil
// override /api/wifi/ap) -- ditampilkan di /wifi walau AP sedang mati, supaya
// operator tahu apa yang harus dicari saat AP menyala nanti.
const char* provApSsid();

// 6 karakter A-Z0-9, ditampilkan di halaman /wifi (lihat src/web.cpp).
const char* provGatewayCode();

// Dipanggil src/web.cpp. Pembanding waktu-konstan terhadap gateway_code aktif.
bool provCheckCode(const char* code);

enum class ProvResult { OK, INVALID_SSID, INVALID_PASS, INVALID_MDNS, INVALID_IP, NVS_ERROR };
// Pesan `error` untuk balasan JSON 400 -- "" kalau r == OK.
const char* provResultError(ProvResult r);

// Validasi lalu simpan ke NVS `wifi_cfg`. TIDAK reboot sendiri -- pemanggil
// (web.cpp) yang memutuskan kapan reboot lewat provScheduleReboot() SETELAH
// respons HTTP terkirim ke klien.
ProvResult provSaveWifi(const char* ssid, const char* pass, const char* mdns,
                         bool sta_static, const char* sta_ip, const char* sta_gw,
                         const char* sta_mask, const char* sta_dns1, const char* sta_dns2);

// ap_ssid kosong = pertahankan default "BEP-CONNECT-<gateway_code>".
ProvResult provSaveAp(const char* ap_ssid, const char* ap_pass);

// Hapus kredensial router (ssid+pass) dari NVS wifi_cfg -- pengaturan AP
// TIDAK ikut terhapus (paritas pola tim: forget = lupakan router saja).
void provForgetWifi();

// Reboot esp_restart() setelah delay_ms, dieksekusi dari provTick() (BUKAN
// seketika) supaya respons HTTP yang memicunya sempat terkirim dulu.
void provScheduleReboot(uint32_t delay_ms);
