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
#include <esp_task_wdt.h>

// Kalau ESP-IDF pernah menggeser nilai enum-nya, build gagal di sini —
// bukan diam-diam salah label di telemetri lapangan. Mencakup SEMUA
// konstanta yang dipakai tabel NAMA[] di reset_info.cpp (0-12), bukan
// cuma sebagian — supaya klaim di komentar ini benar-benar berlaku.
static_assert(ESP_RST_UNKNOWN  == RESET_UNKNOWN,  "nilai enum reset bergeser");
static_assert(ESP_RST_POWERON  == RESET_POWERON,  "nilai enum reset bergeser");
static_assert(ESP_RST_EXT      == RESET_EXT,      "nilai enum reset bergeser");
static_assert(ESP_RST_SW       == RESET_SW,       "nilai enum reset bergeser");
static_assert(ESP_RST_PANIC    == RESET_PANIC,    "nilai enum reset bergeser");
static_assert(ESP_RST_INT_WDT  == RESET_INT_WDT,  "nilai enum reset bergeser");
static_assert(ESP_RST_TASK_WDT == RESET_TASK_WDT, "nilai enum reset bergeser");
static_assert(ESP_RST_WDT      == RESET_WDT,      "nilai enum reset bergeser");
static_assert(ESP_RST_DEEPSLEEP == RESET_DEEPSLEEP, "nilai enum reset bergeser");
static_assert(ESP_RST_BROWNOUT == RESET_BROWNOUT, "nilai enum reset bergeser");
static_assert(ESP_RST_SDIO     == RESET_SDIO,     "nilai enum reset bergeser");
static_assert(ESP_RST_USB      == RESET_USB,      "nilai enum reset bergeser");
static_assert(ESP_RST_JTAG     == RESET_JTAG,     "nilai enum reset bergeser");

static char g_reset_reason[24] = "UNKNOWN";
static uint32_t g_boot_count = 0;

void setup() {
    Serial.begin(115200);           // USB-CDC (COM3)
    delay(200);                       // beri waktu USB-CDC siap sebelum baris pertama
    resetReasonName((int)esp_reset_reason(), g_reset_reason, sizeof(g_reset_reason));
    Preferences bootprefs;
    bool nvs_ok = bootprefs.begin("boot", false);
    if (nvs_ok) {
        g_boot_count = bootprefs.getUInt("count", 0) + 1;
        bootprefs.putUInt("count", g_boot_count);
        bootprefs.end();
    }
    // Kalau NVS gagal dibuka, boot_count diam di 0 — tanpa baris ini itu
    // tak terbedakan dari boot pertama yang genuine. Fitur ini ada justru
    // supaya masalah boot terlihat; jalur kegagalannya sendiri tak boleh diam.
    if (!nvs_ok) Serial.println("[boot] WARNING nvs_open_gagal boot_count=0 (bukan boot pertama, NVS 'boot' tak terbuka)");
    Serial.printf("[boot] reset=%s boot_count=%u heap=%u min_heap=%u\n",
                  g_reset_reason, g_boot_count,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size());
    // TWDT bawaan core aktif (panic=1) tapi TIDAK mengawasi task apa pun —
    // idle task tak didaftarkan dan tak ada task yang subscribe — sehingga
    // task macet tidak pernah terdeteksi dan TASK_WDT mustahil muncul di
    // last_reset_reason. Setel timeout longgar lalu daftarkan loop() di sini;
    // task_bess dan task_cmd mendaftarkan dirinya sendiri.
    esp_task_wdt_config_t wdt = {};
    wdt.timeout_ms = WDT_TIMEOUT_S * 1000;
    wdt.idle_core_mask = 0;
    wdt.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
    esp_task_wdt_add(nullptr);          // setup() dan loop() = loopTask yang sama
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
    esp_task_wdt_reset();
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
        si.free_heap = esp_get_free_heap_size();
        si.min_free_heap = esp_get_minimum_free_heap_size();
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
        if (n == 0) Serial.println("[mqtt] telemetri gagal dibangun (buffer kurang)");
        else if (!mqttEnqueueTelemetry(json, n)) Serial.println("[mqtt] telemetri gagal masuk outbox");
    }
    delay(100);
}
