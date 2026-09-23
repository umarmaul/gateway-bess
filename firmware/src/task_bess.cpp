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
    const struct { const char* nama; uint16_t start, count; uint16_t* out; } blok[4] = {
        {"telem", REG_TELEM_START, REG_TELEM_COUNT, telem},
        {"alarm", REG_ALARM_START, REG_ALARM_COUNT, alst},
        {"pset",  REG_P_SET,       1,               pset},
        {"param", REG_PARAM_START, REG_PARAM_COUNT, param},
    };
    ok = true;
    for (int i = 0; i < 4; i++) {
        uint8_t exc = 0;
        MbStatus st = mbReadRegs(BESS_NODE, blok[i].start, blok[i].count, blok[i].out, &exc);
        if (st == MB_OK) continue;
        ok = false;
        if (st == MB_EXCEPTION) {
            // Device menjawab — lanjut ke blok berikutnya supaya kode exception
            // tiap blok tetap tercatat (alasan short-circuit lama dihapus).
            Serial.printf("[bess] blok %s: exception 0x%02X\n", blok[i].nama, exc);
            continue;
        }
        Serial.printf("[bess] blok %s: gagal (status %d)\n", blok[i].nama, (int)st);
        if (st == MB_TIMEOUT) {
            // Device diam total (3 percobaan tanpa satu byte pun). Blok sisanya
            // hampir pasti sama, dan mencobanya melipattigakan jendela deteksi
            // comm_lost (~8 -> ~25 dtk) sambil menahan mb_mtx dari task_cmd.
            if (i < 3) Serial.println("[bess] blok sisa dilewati (device tak menjawab)");
            break;
        }
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
