#include "prov.h"
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#include "prov_logic.h"
#include "wifi_mgr.h"
// secrets.h SEBELUM config.h: AP_PASS dipertahankan #ifndef di config.h --
// kalau secrets.h (lapangan) mendefinisikannya duluan, default "bepgateway"
// di config.h otomatis dilewati (pola sama dengan OTA_ED25519_PUBKEY_B64 di
// src/task_ota.cpp).
#include "secrets.h"
#include "config.h"
#include "timeutil.h"

// ---------------------------------------------------------------------------
// Semua state provisioning di-cache di RAM setelah provInit()/provSaveWifi()/
// provSaveAp() -- provTick() dan handler HTTP (web.cpp) TIDAK PERNAH membaca
// NVS langsung, supaya tetap cepat (loop() diawasi task watchdog 120 dtk).
// ---------------------------------------------------------------------------
static char s_gw_code[PROV_GATEWAY_CODE_LEN + 1] = "";
static char s_sta_ssid[33] = "";
static char s_sta_pass[64] = "";
static char s_mdns_host[64] = PROV_MDNS_DEFAULT;
static char s_ap_ssid[40] = "";           // "BEP-CONNECT-" (12) + 6 kode + NUL = 19 char
static char s_ap_pass[64] = AP_PASS;

static bool s_sta_static = false;
static IPAddress s_sta_ip, s_sta_gw, s_sta_mask, s_sta_dns1, s_sta_dns2;

static DNSServer s_dns;
static bool s_ap_active = false;
static bool s_prev_connected = false;
static uint32_t s_connected_since_ms = 0;

static bool s_mdns_started = false;
static uint32_t s_mdns_next_try_ms = 0;

static uint32_t s_reset_press_start_ms = 0;
static bool s_reset_triggered = false;

static bool s_reboot_pending = false;
static uint32_t s_reboot_at_ms = 0;

// ---------------------------------------------------------------------------
// TEMUAN REVIEW 23 Sep 2026: sebelum handleClient() dipindah ke task_web
// (lihat web.h), SEMUA handler HTTP di web.cpp berjalan di loop() -- task
// YANG SAMA dengan provTick() -- jadi akses tanpa lock ke state di bawah ini
// aman (satu task, tidak pernah tumpang tindih). Sekarang handler HTTP bisa
// berjalan BERSAMAAN dengan provTick() (task berbeda), jadi field yang
// ditulis salah satu dan dibaca yang lain butuh mutex:
//   - s_ap_active/s_ap_ssid/s_ap_pass: ditulis provTick() (toggle AP) DAN
//     provSaveAp() (handler HTTP web.cpp, sekarang task_web); dibaca
//     provTick() (WiFi.softAP()) DAN provApActive()/provApSsid() (task_web,
//     fillSysInfo dari task_web/loop()).
//   - s_mdns_host: ditulis provSaveWifi() (task_web) DAN dibaca provTick()
//     (MDNS.begin()) serta provMdnsHostname() (task_web, fillSysInfo).
//   - s_reboot_pending/s_reboot_at_ms: SEPASANG (dibaca bersamaan di
//     provTick()) -- ditulis provScheduleReboot() (dipanggil handler HTTP
//     web.cpp, sekarang task_web), dibaca provTick() (loop()).
// s_gw_code (write-once di provInit(), SEBELUM task_web ada) dan
// s_sta_ssid/s_sta_pass/s_sta_static/IP statis (ditulis provSaveWifi()/
// provForgetWifi() tapi HANYA DIBACA sekali di main.cpp::setup(), juga
// SEBELUM task_web dibuat -- lihat wifiInit(provStaSsid(), provStaPass()))
// SENGAJA tidak dikunci -- tidak ada pembaca setelah task_web berjalan.
// ---------------------------------------------------------------------------
static SemaphoreHandle_t s_mtx = nullptr;

static uint32_t espRand32() { return esp_random(); }

static void defaultApSsid(char out[40]) {
    snprintf(out, 40, "BEP-CONNECT-%s", s_gw_code);
}

// ---------------------------------------------------------------------------
// provInit — dipanggil sekali di setup(), SEBELUM wifiInit()
// ---------------------------------------------------------------------------
void provInit() {
    s_mtx = xSemaphoreCreateMutex();   // lihat catatan thread-safety di atas
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    // gateway_code: dibangkitkan sekali, dipertahankan lintas reboot (NVS
    // 'device_id') -- identitas AP fallback yang stabil per perangkat.
    Preferences dev;
    if (dev.begin("device_id", false)) {
        if (dev.isKey("gateway_code")) {
            dev.getString("gateway_code", s_gw_code, sizeof(s_gw_code));
        } else {
            provGenGatewayCode(espRand32, s_gw_code);
            dev.putString("gateway_code", s_gw_code);
            Serial.printf("[prov] gateway_code baru dibangkitkan: %s\n", s_gw_code);
        }
        dev.end();
    } else {
        // NVS 'device_id' tak terbuka -- tetap jalan dengan kode sekali-pakai
        // per boot (lebih baik daripada provisioning mati total), tapi ini
        // artinya SSID AP fallback berubah tiap reboot -- sinyal untuk operator.
        provGenGatewayCode(espRand32, s_gw_code);
        Serial.println("[prov] WARNING NVS 'device_id' tak terbuka -- gateway_code TIDAK persisten");
    }
    defaultApSsid(s_ap_ssid);

    // wifi_cfg: kredensial STA + mDNS + IP statis + override AP. Kosong ->
    // fallback WIFI_SSID/WIFI_PASS (secrets.h), sesuai kontrak "bench tetap
    // jalan tanpa provisioning".
    Preferences p;
    bool opened = p.begin("wifi_cfg", true);
    char ssid_nvs[33] = "";
    if (opened) p.getString("ssid", ssid_nvs, sizeof(ssid_nvs));

    if (ssid_nvs[0]) {
        strncpy(s_sta_ssid, ssid_nvs, sizeof(s_sta_ssid) - 1);
        p.getString("pass", s_sta_pass, sizeof(s_sta_pass));

        char mdns_nvs[64] = "";
        p.getString("mdns", mdns_nvs, sizeof(mdns_nvs));
        if (mdns_nvs[0]) { strncpy(s_mdns_host, mdns_nvs, sizeof(s_mdns_host) - 1); s_mdns_host[sizeof(s_mdns_host) - 1] = 0; }

        bool want_static = p.getBool("sta_static", false);
        if (want_static) {
            char ip[16] = "", gw[16] = "", mask[16] = "", d1[16] = "", d2[16] = "";
            p.getString("sta_ip", ip, sizeof(ip));
            p.getString("sta_gw", gw, sizeof(gw));
            p.getString("sta_mask", mask, sizeof(mask));
            p.getString("sta_dns1", d1, sizeof(d1));
            p.getString("sta_dns2", d2, sizeof(d2));
            uint8_t a_ip[4], a_gw[4], a_mask[4], a_d1[4], a_d2[4];
            bool ok_ip = provParseIPv4(ip, a_ip) && provParseIPv4(gw, a_gw) &&
                         provParseIPv4(mask, a_mask) && provParseIPv4(d1, a_d1) &&
                         provParseIPv4(d2, a_d2);
            if (ok_ip) {
                s_sta_ip = IPAddress(a_ip[0], a_ip[1], a_ip[2], a_ip[3]);
                s_sta_gw = IPAddress(a_gw[0], a_gw[1], a_gw[2], a_gw[3]);
                s_sta_mask = IPAddress(a_mask[0], a_mask[1], a_mask[2], a_mask[3]);
                s_sta_dns1 = IPAddress(a_d1[0], a_d1[1], a_d1[2], a_d1[3]);
                s_sta_dns2 = IPAddress(a_d2[0], a_d2[1], a_d2[2], a_d2[3]);
                s_sta_static = true;
            } else {
                // IP statis tersimpan tapi korup/tak lengkap -- gagal AMAN ke
                // DHCP, jangan sampai gateway tak pernah konek karena ini.
                Serial.println("[prov] IP statis di NVS tak valid -- fallback DHCP");
            }
        }

        char apssid_nvs[33] = "";
        p.getString("ap_ssid", apssid_nvs, sizeof(apssid_nvs));
        if (apssid_nvs[0]) { strncpy(s_ap_ssid, apssid_nvs, sizeof(s_ap_ssid) - 1); s_ap_ssid[sizeof(s_ap_ssid) - 1] = 0; }

        char appass_nvs[64] = "";
        p.getString("ap_pass", appass_nvs, sizeof(appass_nvs));
        if (appass_nvs[0]) { strncpy(s_ap_pass, appass_nvs, sizeof(s_ap_pass) - 1); s_ap_pass[sizeof(s_ap_pass) - 1] = 0; }
    } else {
        strncpy(s_sta_ssid, WIFI_SSID, sizeof(s_sta_ssid) - 1);
        strncpy(s_sta_pass, WIFI_PASS, sizeof(s_sta_pass) - 1);
        Serial.println("[prov] NVS wifi_cfg kosong -- pakai WIFI_SSID/WIFI_PASS dari secrets.h");
    }
    if (opened) p.end();

    Serial.printf("[prov] gateway_code=%s ap_ssid=%s mdns=%s.local sta_static=%d\n",
                  s_gw_code, s_ap_ssid, s_mdns_host, (int)s_sta_static);
}

// ---------------------------------------------------------------------------
// provTick — dipanggil tiap loop()
// ---------------------------------------------------------------------------
void provTick() {
    // ---- AP fallback: nyala saat STA putus, tetap PROV_AP_AFTER_CONNECT_MS
    // sesudah STA tersambung (paritas NetworkManager.cpp::updateWiFiRecoveryAp). ----
    bool connected = wifiConnected();
    if (connected && !s_prev_connected) s_connected_since_ms = millis();
    s_prev_connected = connected;
    uint32_t ms_since = connected ? (millis() - s_connected_since_ms) : 0;
    bool want_ap = provApShouldBeOn(connected, ms_since, PROV_AP_AFTER_CONNECT_MS);
    // s_ap_ssid/s_ap_pass disalin ke lokal DI BAWAH LOCK sebelum dipakai di luar
    // lock (WiFi.softAP() dkk tak perlu menahan mutex) -- provSaveAp() (task_web)
    // bisa menulis keduanya bersamaan (lihat catatan thread-safety di atas).
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool was_active = s_ap_active;
    bool changing = want_ap != was_active;
    if (changing) s_ap_active = want_ap;
    char ap_ssid[40], ap_pass[64];
    strncpy(ap_ssid, s_ap_ssid, sizeof(ap_ssid) - 1); ap_ssid[sizeof(ap_ssid) - 1] = 0;
    strncpy(ap_pass, s_ap_pass, sizeof(ap_pass) - 1); ap_pass[sizeof(ap_pass) - 1] = 0;
    xSemaphoreGive(s_mtx);
    if (changing) {
        if (want_ap) {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(ap_ssid, ap_pass);
            s_dns.start(53, "*", WiFi.softAPIP());
            Serial.printf("[prov] AP fallback ON: %s ip=%s\n", ap_ssid,
                          WiFi.softAPIP().toString().c_str());
        } else {
            s_dns.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            Serial.println("[prov] AP fallback OFF (STA stabil > 5 menit)");
        }
    }
    if (want_ap) s_dns.processNextRequest();

    // ---- mDNS: retry MDNS.begin() tiap PROV_MDNS_RETRY_MS selama STA connected ----
    if (connected) {
        if (!s_mdns_started && timeAfter(millis(), s_mdns_next_try_ms)) {
            char mdns_host[64];
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            strncpy(mdns_host, s_mdns_host, sizeof(mdns_host) - 1);
            xSemaphoreGive(s_mtx);
            mdns_host[sizeof(mdns_host) - 1] = 0;
            if (MDNS.begin(mdns_host)) {
                MDNS.addService("http", "tcp", 80);
                s_mdns_started = true;
                Serial.printf("[prov] mDNS aktif: %s.local\n", mdns_host);
            } else {
                Serial.println("[prov] MDNS.begin gagal -- coba lagi 5 dtk");
            }
            s_mdns_next_try_ms = millis() + PROV_MDNS_RETRY_MS;
        }
    } else {
        s_mdns_started = false;   // IP STA berubah pasca reconnect -> perlu MDNS.begin() ulang
    }

    // ---- tombol factory reset (GPIO9/BOOT, aktif LOW, tahan 8 dtk) ----
    bool pressed = digitalRead(PIN_BOOT_BUTTON) == LOW;
    if (pressed) {
        if (s_reset_press_start_ms == 0) s_reset_press_start_ms = millis();
        else if (!s_reset_triggered && millis() - s_reset_press_start_ms >= PROV_FACTORY_RESET_HOLD_MS) {
            s_reset_triggered = true;
            Serial.println("[prov] FACTORY RESET: tombol BOOT ditahan 8 dtk -- hapus wifi_cfg+app_cfg, reboot");
            Preferences p;
            if (p.begin("wifi_cfg", false)) { p.clear(); p.end(); }
            if (p.begin("app_cfg", false)) { p.clear(); p.end(); }
            // identitas (device_id/gateway_code), mqtt_ota, boot, crash SENGAJA
            // dipertahankan -- lihat kontrak di prov.h / README.
            delay(100);
            esp_restart();
        }
    } else {
        s_reset_press_start_ms = 0;
        s_reset_triggered = false;
    }

    // ---- reboot terjadwal (pasca simpan config via HTTP) ----
    // Dibaca sebagai PASANGAN di bawah lock -- provScheduleReboot() (dipanggil
    // handler HTTP, sekarang task_web) menulis keduanya; tanpa lock, provTick()
    // bisa melihat s_reboot_pending sudah true tapi s_reboot_at_ms masih nilai
    // lama (dari panggilan schedule SEBELUMNYA, kalau ada) sebelum tertulis.
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool reboot_pending = s_reboot_pending;
    uint32_t reboot_at_ms = s_reboot_at_ms;
    xSemaphoreGive(s_mtx);
    if (reboot_pending && timeAfter(millis(), reboot_at_ms)) {
        Serial.println("[prov] reboot (config WiFi berubah)");
        delay(50);
        esp_restart();
    }
}

const char* provStaSsid() { return s_sta_ssid; }
const char* provStaPass() { return s_sta_pass; }

ProvStaticIp provStaticIp() {
    ProvStaticIp r{};
    r.enabled = s_sta_static;
    r.ip = s_sta_ip; r.gw = s_sta_gw; r.mask = s_sta_mask; r.dns1 = s_sta_dns1; r.dns2 = s_sta_dns2;
    return r;
}

bool provApActive() {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool v = s_ap_active;
    xSemaphoreGive(s_mtx);
    return v;
}
// provMdnsHostname()/provApSsid() mengembalikan POINTER ke buffer modul (bukan
// salinan) -- panggilan ini sendiri tidak dikunci karena mengunci lalu
// mengembalikan pointer ke luar critical section tidak benar-benar menghapus
// risiko robek (caller bisa membaca buffer SAAT provSaveWifi()/provSaveAp()
// (task_web) sedang menulisnya lewat strncpy). Risiko ini SEMPIT (hanya
// selama ~1 dtk provScheduleReboot() sebelum esp_restart(), lihat
// provSaveWifi/provSaveAp) dan HANYA kosmetik (tampilan /wifi atau field
// data.network.mdns di satu payload telemetri/​dashboard, bukan apa pun yang
// memengaruhi keselamatan BESS/Modbus) -- didokumentasikan di README
// (§Kerangka web server) sebagai residual risk, bukan diperbaiki dengan
// mengubah SysInfo.mdns/payload.h (kontrak MQTT dipakai native test, di luar
// lingkup temuan task_web ini).
const char* provMdnsHostname() { return s_mdns_host; }
const char* provApSsid() { return s_ap_ssid; }
const char* provGatewayCode() { return s_gw_code; }

bool provCheckCode(const char* code) { return provCodeEquals(code ? code : "", s_gw_code); }

const char* provResultError(ProvResult r) {
    switch (r) {
        case ProvResult::OK:            return "";
        case ProvResult::INVALID_SSID:  return "invalid_ssid";
        case ProvResult::INVALID_PASS:  return "invalid_pass";
        case ProvResult::INVALID_MDNS:  return "invalid_mdns";
        case ProvResult::INVALID_IP:    return "invalid_ip";
        case ProvResult::NVS_ERROR:     return "nvs_error";
    }
    return "invalid";
}

ProvResult provSaveWifi(const char* ssid, const char* pass, const char* mdns,
                         bool sta_static, const char* sta_ip, const char* sta_gw,
                         const char* sta_mask, const char* sta_dns1, const char* sta_dns2) {
    if (!provValidSsid(ssid)) return ProvResult::INVALID_SSID;
    if (!provValidStaPass(pass)) return ProvResult::INVALID_PASS;
    if (!provValidMdnsHostname(mdns)) return ProvResult::INVALID_MDNS;
    uint8_t a_ip[4], a_gw[4], a_mask[4], a_d1[4], a_d2[4];
    if (sta_static) {
        if (!provParseIPv4(sta_ip, a_ip) || !provParseIPv4(sta_gw, a_gw) ||
            !provParseIPv4(sta_mask, a_mask) || !provParseIPv4(sta_dns1, a_d1) ||
            !provParseIPv4(sta_dns2, a_d2))
            return ProvResult::INVALID_IP;
    }

    Preferences p;
    if (!p.begin("wifi_cfg", false)) {
        Serial.println("[prov] NVS 'wifi_cfg' tak terbuka -- config TIDAK tersimpan");
        return ProvResult::NVS_ERROR;
    }
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.putString("mdns", mdns);
    p.putBool("sta_static", sta_static);
    if (sta_static) {
        p.putString("sta_ip", sta_ip);
        p.putString("sta_gw", sta_gw);
        p.putString("sta_mask", sta_mask);
        p.putString("sta_dns1", sta_dns1);
        p.putString("sta_dns2", sta_dns2);
    }
    p.end();

    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1); s_sta_ssid[sizeof(s_sta_ssid) - 1] = 0;
    strncpy(s_sta_pass, pass, sizeof(s_sta_pass) - 1); s_sta_pass[sizeof(s_sta_pass) - 1] = 0;
    // s_mdns_host dikunci: dibaca provTick()/provMdnsHostname() dari task lain
    // (lihat catatan thread-safety di atas).
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    strncpy(s_mdns_host, mdns, sizeof(s_mdns_host) - 1); s_mdns_host[sizeof(s_mdns_host) - 1] = 0;
    xSemaphoreGive(s_mtx);
    s_sta_static = sta_static;
    if (sta_static) {
        s_sta_ip = IPAddress(a_ip[0], a_ip[1], a_ip[2], a_ip[3]);
        s_sta_gw = IPAddress(a_gw[0], a_gw[1], a_gw[2], a_gw[3]);
        s_sta_mask = IPAddress(a_mask[0], a_mask[1], a_mask[2], a_mask[3]);
        s_sta_dns1 = IPAddress(a_d1[0], a_d1[1], a_d1[2], a_d1[3]);
        s_sta_dns2 = IPAddress(a_d2[0], a_d2[1], a_d2[2], a_d2[3]);
    }
    Serial.printf("[prov] wifi_cfg disimpan: ssid=%s mdns=%s sta_static=%d\n", ssid, mdns, (int)sta_static);
    return ProvResult::OK;
}

ProvResult provSaveAp(const char* ap_ssid, const char* ap_pass) {
    bool has_ssid = ap_ssid && ap_ssid[0];
    if (has_ssid && !provValidSsid(ap_ssid)) return ProvResult::INVALID_SSID;
    if (!provValidApPass(ap_pass)) return ProvResult::INVALID_PASS;

    Preferences p;
    if (!p.begin("wifi_cfg", false)) {
        Serial.println("[prov] NVS 'wifi_cfg' tak terbuka -- config AP TIDAK tersimpan");
        return ProvResult::NVS_ERROR;
    }
    if (has_ssid) p.putString("ap_ssid", ap_ssid);
    p.putString("ap_pass", ap_pass);
    p.end();

    // s_ap_ssid/s_ap_pass dikunci: dibaca provTick() (WiFi.softAP()) dari
    // loop() -- task berbeda dari task_web (lihat catatan thread-safety).
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (has_ssid) { strncpy(s_ap_ssid, ap_ssid, sizeof(s_ap_ssid) - 1); s_ap_ssid[sizeof(s_ap_ssid) - 1] = 0; }
    strncpy(s_ap_pass, ap_pass, sizeof(s_ap_pass) - 1); s_ap_pass[sizeof(s_ap_pass) - 1] = 0;
    xSemaphoreGive(s_mtx);
    Serial.printf("[prov] AP config disimpan: ap_ssid=%s\n", s_ap_ssid);
    return ProvResult::OK;
}

void provForgetWifi() {
    Preferences p;
    if (p.begin("wifi_cfg", false)) {
        p.remove("ssid");
        p.remove("pass");
        p.end();
    }
    s_sta_ssid[0] = 0;
    s_sta_pass[0] = 0;
    Serial.println("[prov] kredensial router dilupakan (AP config tetap)");
}

void provScheduleReboot(uint32_t delay_ms) {
    // Dikunci: ditulis dari handler HTTP (task_web sejak temuan review 23 Sep
    // 2026), dibaca sebagai PASANGAN oleh provTick() (loop()) -- lihat catatan
    // thread-safety di atas dan blok "reboot terjadwal" di provTick().
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_reboot_pending = true;
    s_reboot_at_ms = millis() + delay_ms;
    xSemaphoreGive(s_mtx);
}
