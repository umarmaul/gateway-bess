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

void setup() {
    Serial.begin(115200);           // USB-CDC (COM3)
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
