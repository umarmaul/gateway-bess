#include "task_bess.h"
#include <Arduino.h>
#include "config.h"
#include "modbus_port.h"
#include "bess_decode.h"
#include "state.h"

static void pollOnce(bool& ok) {
    uint16_t telem[REG_TELEM_COUNT], alst[REG_ALARM_COUNT], pset[1],
             param[REG_PARAM_COUNT];
    uint8_t exc;
    ok = mbReadRegs(BESS_NODE, REG_TELEM_START, REG_TELEM_COUNT, telem, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_ALARM_START, REG_ALARM_COUNT, alst, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_P_SET, 1, pset, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_PARAM_START, REG_PARAM_COUNT, param, &exc) == MB_OK;
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
    int fail = 0;
    for (;;) {
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
            stateLock();
            Serial.printf("[bess] %s p=%.1fkW soc=%.1f%% vdc=%.1fV status=0x%04X\n",
                          g_state.bess.comm_lost ? "COMM_LOST" : "OK",
                          g_state.bess.active_power_kw, g_state.bess.soc_pct,
                          g_state.bess.dc_voltage_v, g_state.bess.status_raw);
            stateUnlock();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void taskBessStart() {
    xTaskCreate(run, "task_bess", 4096, nullptr, 3, nullptr);
}
