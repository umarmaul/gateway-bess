#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "state.h"
#include "wifi_mgr.h"
#include "modbus_port.h"
#include "task_bess.h"
#include "payload.h"
#include "mqtt_link.h"
#include "task_cmd.h"
#include "secrets.h"        // WIFI_SSID (dipakai di blok telemetri)
#include <Preferences.h>
#include <esp_system.h>
#include "reset_info.h"

// Kalau ESP-IDF pernah menggeser nilai enum-nya, build gagal di sini —
// bukan diam-diam salah label di telemetri lapangan.
static_assert(ESP_RST_POWERON  == RESET_POWERON,  "nilai enum reset bergeser");
static_assert(ESP_RST_SW       == RESET_SW,       "nilai enum reset bergeser");
static_assert(ESP_RST_PANIC    == RESET_PANIC,    "nilai enum reset bergeser");
static_assert(ESP_RST_INT_WDT  == RESET_INT_WDT,  "nilai enum reset bergeser");
static_assert(ESP_RST_TASK_WDT == RESET_TASK_WDT, "nilai enum reset bergeser");
static_assert(ESP_RST_BROWNOUT == RESET_BROWNOUT, "nilai enum reset bergeser");

static char g_reset_reason[24] = "UNKNOWN";
static uint32_t g_boot_count = 0;

void setup() {
    Serial.begin(115200);           // USB-CDC (COM3)
    delay(200);                       // beri waktu USB-CDC siap sebelum baris pertama
    resetReasonName((int)esp_reset_reason(), g_reset_reason, sizeof(g_reset_reason));
    Preferences bootprefs;
    if (bootprefs.begin("boot", false)) {
        g_boot_count = bootprefs.getUInt("count", 0) + 1;
        bootprefs.putUInt("count", g_boot_count);
        bootprefs.end();
    }
    Serial.printf("[boot] reset=%s boot_count=%u heap=%u min_heap=%u\n",
                  g_reset_reason, g_boot_count,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size());
    pinMode(PIN_LED_WIFI, OUTPUT);
    pinMode(PIN_LED_BESS, OUTPUT);
    stateInit();
    wifiInit();
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[boot] gateway-bess " FW_VERSION);
    mbPortInit();
    taskBessStart();

    static char gw[13];
    wifiGw(gw);
    Serial.printf("[boot] gw=%s\n", gw);
    taskCmdStart();
    mqttInit(gw);
}

void loop() {
    wifiTick();
    digitalWrite(PIN_LED_WIFI, wifiConnected() ? HIGH : LOW);
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        Serial.printf("[wifi] %s rssi=%d ip=%s\n",
                      wifiConnected() ? "OK" : "putus", WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
    }
    static uint32_t last_telem = 0;
    // mqttConnected() ikut digerbang: kalau broker putus, jangan menumpuk
    // telemetri di outbox lalu memuntahkannya beruntun saat reconnect.
    if (millis() - last_telem > TELEMETRY_PERIOD_MS && wifiConnected() &&
        mqttConnected()) {
        last_telem = millis();
        static char json[8192];
        SysInfo si{};
        wifiGw(si.gw);
        si.fw_version = FW_VERSION;
        si.last_reset_reason = g_reset_reason;
        si.boot_count = g_boot_count;
        si.uptime_ms = millis();
        si.ts = (uint32_t)time(nullptr);   // buildTelemetryJson yang menolkan
        si.rssi = WiFi.RSSI();
        si.ssid = WIFI_SSID;
        snprintf(si.ip, sizeof(si.ip), "%s", WiFi.localIP().toString().c_str());
        stateLock();
        si.seq = ++g_state.seq;
        BessData snapshot = g_state.bess;
        stateUnlock();
        size_t n = buildTelemetryJson(si, snapshot, json, sizeof(json));
        mqttEnqueueTelemetry(json, n);
    }
    delay(100);
}
