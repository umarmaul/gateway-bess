#include "task_bess.h"
#include <Arduino.h>
#include "config.h"
#include "modbus_port.h"
#include "bess_decode.h"
#include "state.h"
#include <esp_task_wdt.h>

static void pollOnce(bool& ok) {
    uint16_t telem[REG_TELEM_COUNT], alst[REG_ALARM_COUNT], pset[1],
             param[REG_PARAM_COUNT];
    uint8_t exc = 0;
    struct { const char* nama; MbStatus st; uint8_t exc; } blok[4];
    blok[0] = {"telem", mbReadRegs(BESS_NODE, REG_TELEM_START, REG_TELEM_COUNT, telem, &exc), exc};
    exc = 0;
    blok[1] = {"alarm", mbReadRegs(BESS_NODE, REG_ALARM_START, REG_ALARM_COUNT, alst, &exc), exc};
    exc = 0;
    blok[2] = {"pset", mbReadRegs(BESS_NODE, REG_P_SET, 1, pset, &exc), exc};
    exc = 0;
    blok[3] = {"param", mbReadRegs(BESS_NODE, REG_PARAM_START, REG_PARAM_COUNT, param, &exc), exc};

    ok = true;
    for (int i = 0; i < 4; i++) {
        if (blok[i].st == MB_OK) continue;
        ok = false;
        if (blok[i].st == MB_EXCEPTION)
            Serial.printf("[bess] blok %s: exception 0x%02X\n", blok[i].nama, blok[i].exc);
        else
            Serial.printf("[bess] blok %s: gagal (status %d)\n", blok[i].nama, (int)blok[i].st);
    }
    if (!ok) return;
    stateLock();
    BessData& d = g_state.bess;
    bessDecodeTelemetry(telem, d);
    bessDecodeAlarmStatus(alst, d);
    d.setpoint_pct = (int16_t)pset[0] / 10.0f;
    d.rated_kw = param[0] / 10.0f;               // 3146
    d.soc_pct = param[38] / 10.0f;               // 3184
    d.comm_lost = false;
    d.last_ok_ms = millis();
    stateUnlock();
}

static void run(void*) {
    esp_task_wdt_add(nullptr);
    int fail = 0;
    for (;;) {
        esp_task_wdt_reset();
        bool ok = false;
        pollOnce(ok);
        if (ok) {
            fail = 0;
            digitalWrite(PIN_LED_BESS, HIGH);
        } else if (++fail >= COMM_LOST_AFTER) {
            stateLock();
            g_state.bess.comm_lost = true;       // nilai lama dipertahankan
            stateUnlock();
            digitalWrite(PIN_LED_BESS, LOW);
        }
        static uint32_t lastlog = 0;
        if (millis() - lastlog > 5000) {
            lastlog = millis();
            // Salin dulu, cetak di luar lock: Serial.printf bisa memblokir
            // (buffer USB-CDC penuh) dan tak boleh menahan mutex state.
            stateLock();
            BessData s = g_state.bess;
            stateUnlock();
            Serial.printf("[bess] %s p=%.1fkW soc=%.1f%% vdc=%.1fV status=0x%04X\n",
                          s.comm_lost ? "COMM_LOST" : "OK", s.active_power_kw,
                          s.soc_pct, s.dc_voltage_v, s.status_raw);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void taskBessStart() {
    xTaskCreate(run, "task_bess", 4096, nullptr, 3, nullptr);
}
