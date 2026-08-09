#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "state.h"
#include "wifi_mgr.h"

void setup() {
    Serial.begin(115200);           // USB-CDC (COM3)
    pinMode(PIN_LED_WIFI, OUTPUT);
    pinMode(PIN_LED_BESS, OUTPUT);
    stateInit();
    wifiInit();
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[boot] gateway-bess " FW_VERSION);
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
    delay(100);
}
