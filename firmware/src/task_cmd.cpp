#include "task_cmd.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "commands.h"
#include "config.h"
#include "mqtt_link.h"
#include "modbus_port.h"
#include "bess_decode.h"
#include "state.h"

struct RawCmd { char json[512]; size_t len; };
static QueueHandle_t q;

void taskCmdSubmit(const char* json, size_t n) {
    RawCmd rc{};
    rc.len = min(n, sizeof(rc.json) - 1);
    memcpy(rc.json, json, rc.len);
    xQueueSend(q, &rc, 0);
}

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

static void sendAck(const Command& c, const char* result, const char* detail,
                    float pct = NAN, float w = NAN) {
    static char buf[512];
    size_t n = buildAckJson(c, result, detail, pct, w, nowTs(), buf, sizeof(buf));
    mqttPublishAck(buf, n);
    Serial.printf("[cmd] %s -> %s %s\n", c.name, result, detail);
}

static bool waitStatusBit(int bit, bool want, uint32_t timeout_ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        uint16_t w; uint8_t exc;
        if (mbReadRegs(BESS_NODE, 2057, 1, &w, &exc) == MB_OK &&
            (bool)((w >> bit) & 1) == want)
            return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

static void doOnOff(const Command& c, bool on) {
    stateLock();
    bool lost = g_state.bess.comm_lost;
    bool fault = bessFault(g_state.bess);
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    if (on && fault) { sendAck(c, "rejected", "bess_fault"); return; }
    uint8_t exc = 0;
    MbStatus st = MB_TIMEOUT;
    for (int i = 0; i < 20; i++) {                    // busy (exc 6) → coba lagi
        st = mbWrite5(BESS_NODE, REG_ONOFF, on, &exc);
        if (st == MB_OK || (st == MB_EXCEPTION && exc != 6)) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (st != MB_OK) { sendAck(c, "rejected", "bess_no_ack"); return; }
    // bukti nyata: bit Run (6) untuk on, bit Shutdown (11) untuk off
    bool okBit = on ? waitStatusBit(6, true, 10000) : waitStatusBit(11, true, 10000);
    sendAck(c, okBit ? "accepted" : "rejected", okBit ? "" : "bess_no_ack");
}

static void doSetPower(const Command& c) {
    if (!c.has_power) { sendAck(c, "rejected", "bad_value"); return; }
    stateLock();
    bool lost = g_state.bess.comm_lost;
    float rated_w = g_state.bess.rated_kw * 1000.0f;
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    if (rated_w <= 0) { sendAck(c, "rejected", "bess_no_ack"); return; }
    float pct = c.power_w / rated_w * 100.0f;
    if (fabsf(pct) > 120.0f) { sendAck(c, "rejected", "bad_value"); return; }
    int16_t raw = (int16_t)lroundf(pct * 10.0f);
    uint8_t exc = 0;
    if (mbWrite6(BESS_NODE, REG_P_SET, (uint16_t)raw, &exc) != MB_OK) {
        sendAck(c, "rejected", exc == 6 ? "bess_busy" : "bess_no_ack");
        return;
    }
    uint16_t rb;
    if (mbReadRegs(BESS_NODE, REG_P_SET, 1, &rb, &exc) != MB_OK ||
        (int16_t)rb != raw) {
        sendAck(c, "rejected", "readback_mismatch");
        return;
    }
    sendAck(c, "accepted", "", raw / 10.0f, raw / 1000.0f * rated_w / 10.0f * 10.0f);
}

static void run(void*) {
    RawCmd rc;
    for (;;) {
        if (xQueueReceive(q, &rc, portMAX_DELAY) != pdTRUE) continue;
        Command c;
        parseCommand(rc.json, rc.len, c);
        switch (c.type) {
            case Command::ENABLE:  doOnOff(c, true); break;
            case Command::DISABLE: doOnOff(c, false); break;
            case Command::SET_POWER: doSetPower(c); break;
            case Command::BAD_JSON: sendAck(c, "rejected", "bad_json"); break;
            default: sendAck(c, "rejected", "unsupported_cmd"); break;
        }
    }
}

void taskCmdStart() {
    q = xQueueCreate(4, sizeof(RawCmd));
    xTaskCreate(run, "task_cmd", 6144, nullptr, 2, nullptr);
}
