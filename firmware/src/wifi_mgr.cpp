#include "wifi_mgr.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "config.h"
#include "timeutil.h"

static uint32_t next_try_ms = 0;
static uint32_t backoff_ms = 4000;

static char s_ssid[33] = "";
static char s_pass[64] = "";

static bool s_static_ip = false;
static IPAddress s_ip, s_gw, s_mask, s_dns1, s_dns2;

void wifiSetStaticIp(IPAddress ip, IPAddress gw, IPAddress mask, IPAddress dns1, IPAddress dns2) {
    s_static_ip = true;
    s_ip = ip; s_gw = gw; s_mask = mask; s_dns1 = dns1; s_dns2 = dns2;
}

void wifiInit(const char* ssid, const char* pass) {
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1); s_ssid[sizeof(s_ssid) - 1] = 0;
    strncpy(s_pass, pass, sizeof(s_pass) - 1); s_pass[sizeof(s_pass) - 1] = 0;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_country_code("ID", true);      // kanal 1-13 (pelajaran reason=203)
    WiFi.setSleep(false);                       // modem sleep OFF (latensi + EMI)
    WiFi.setAutoReconnect(false);               // wifiTick satu-satunya driver
    if (s_static_ip) {
        if (!WiFi.config(s_ip, s_gw, s_mask, s_dns1, s_dns2))
            Serial.println("[wifi] WiFi.config (IP statis) gagal -- lanjut coba konek apa adanya");
    }
    WiFi.begin(s_ssid, s_pass);
    // Beri percobaan pertama jatah backoff penuh. Dulu next_try_ms=0 membuat
    // wifiTick() pertama langsung disconnect()+begin() lagi di tengah asosiasi
    // yang sedang berjalan — membuang ~4 dtk di setiap boot.
    next_try_ms = millis() + backoff_ms;
}

void wifiTick() {
    if (WiFi.status() == WL_CONNECTED) {
        backoff_ms = 4000;
        // Jaga next_try_ms tetap dekat dengan waktu sekarang. Kalau dibiarkan
        // basi berminggu-minggu, selisihnya melewati jendela 2^31 ms yang
        // dibutuhkan timeAfter() dan percobaan reconnect pertama sesudah putus
        // akan gagal dievaluasi — varian dari bug rollover yang sama.
        next_try_ms = millis();
        return;
    }
    uint32_t now = millis();
    if (timeAfter(now, next_try_ms)) {
        WiFi.disconnect();
        WiFi.begin(s_ssid, s_pass);
        next_try_ms = now + backoff_ms;
        backoff_ms = min(backoff_ms * 2, (uint32_t)60000);
    }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

void wifiGw(char out[13]) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char* wifiSsid() { return s_ssid; }
