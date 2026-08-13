#include "wifi_mgr.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "secrets.h"

static uint32_t next_try_ms = 0;
static uint32_t backoff_ms = 4000;

void wifiInit() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_country_code("ID", true);      // kanal 1-13 (pelajaran reason=203)
    WiFi.setSleep(false);                       // modem sleep OFF (latensi + EMI)
    WiFi.setAutoReconnect(false);               // wifiTick satu-satunya driver
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void wifiTick() {
    if (WiFi.status() == WL_CONNECTED) { backoff_ms = 4000; return; }
    uint32_t now = millis();
    if (now >= next_try_ms) {
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
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
