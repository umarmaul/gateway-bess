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
#include <esp_task_wdt.h>

// Seukuran buffer baca esp-mqtt: semua pesan yang lolos penjaga "pesan
// terpotong" di mqtt_link muat utuh. oversize tetap dijaga untuk berjaga-jaga
// kalau kedua konstanta suatu saat tak sinkron — lebih baik ditolak jujur
// daripada dipotong lalu dijawab bad_json.
struct RawCmd { char json[CMD_JSON_MAX + 1]; size_t len; bool oversize; };
static QueueHandle_t q;
static QueueHandle_t q_luapan;   // 1 slot: perintah yang ditolak karena antrean penuh

void taskCmdSubmit(const char* json, size_t n) {
    // static: ~2 KB terlalu besar untuk stack task esp-mqtt. Aman karena
    // fungsi ini hanya dipanggil dari satu task (event handler esp-mqtt) dan
    // xQueueSend menyalin isinya sebelum kembali.
    static RawCmd rc;
    rc.oversize = n > CMD_JSON_MAX;
    rc.len = rc.oversize ? 0 : n;
    memcpy(rc.json, json, rc.len);
    rc.json[rc.len] = 0;
    if (xQueueSend(q, &rc, 0) == pdTRUE) return;
    // Antrean utama penuh. JSON tidak boleh di-parse di sini (ini task jaringan
    // esp-mqtt), jadi payload mentah dititipkan ke slot luapan; task_cmd yang
    // mem-parse id-nya dan membalas queue_full.
    if (xQueueSend(q_luapan, &rc, 0) != pdTRUE)
        Serial.println("[cmd] dibuang: antrean utama dan luapan penuh");
}

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

static void sendAck(const Command& c, const char* result, const char* detail,
                    float pct = NAN, float w = NAN) {
    static char buf[512];
    size_t n = buildAckJson(c, result, detail, pct, w, nowTs(), buf, sizeof(buf));
    bool sent = mqttPublishAck(buf, n);
    Serial.printf("[cmd] %s -> %s %s%s\n", c.name, result, detail,
                  sent ? "" : " (ack TIDAK terkirim: mqtt putus)");
}

static bool waitStatusBit(int bit, bool want, uint32_t timeout_ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        esp_task_wdt_reset();
        uint16_t w; uint8_t exc;
        if (mbReadRegs(BESS_NODE, REG_STATUS, 1, &w, &exc) == MB_OK &&
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
        esp_task_wdt_reset();
        st = mbWrite5(BESS_NODE, REG_ONOFF, on, &exc);
        // Hanya exception busy (06) yang layak diulang. MB_OK selesai; exception
        // lain, timeout, CRC, dan frame cacat semuanya berarti bus tidak akan
        // membaik dengan diulang 20x — keluar segera supaya slot antrean tidak
        // tersandera sampai puluhan detik saat bus benar-benar mati.
        if (st != MB_EXCEPTION || exc != 6) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (st != MB_OK) { sendAck(c, "rejected", "bess_no_ack"); return; }
    // bukti nyata: bit Run (6) untuk on, bit Shutdown (11) untuk off
    bool okBit = on ? waitStatusBit(6, true, 10000) : waitStatusBit(11, true, 10000);
    // Tulisan FC5 SUDAH diterima device (echo sah) — yang tidak muncul hanya
    // bukti transisinya dalam 10 dtk. Itu bukan "rejected": device mungkin
    // masih/sudah berpindah state. Laporkan "timeout" supaya cloud membaca
    // status di telemetri alih-alih menganggap perintah tidak terjadi.
    if (okBit) sendAck(c, "accepted", "");
    else sendAck(c, "timeout", "status_timeout");
}

static void doSetPower(const Command& c) {
    if (!c.has_power) { sendAck(c, "rejected", "bad_value"); return; }
    if (isnan(c.power_w)) { sendAck(c, "rejected", "bad_value"); return; }
    stateLock();
    bool lost = g_state.bess.comm_lost;
    float rated_w = g_state.bess.rated_kw * 1000.0f;
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    float pct = 0.0f;
    bool clamped = false;
    if (!planPowerPct(c.power_w, rated_w, pct, clamped)) {
        sendAck(c, "rejected", "bess_no_ack");   // rated belum diketahui
        return;
    }
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
    float applied_pct = raw / 10.0f;
    sendAck(c, clamped ? "clamped" : "accepted", "",
            applied_pct, applied_pct / 100.0f * rated_w);
}

// Pesan melebihi CMD_JSON_MAX tidak bisa di-parse (id-nya pun tak diketahui),
// jadi ack-nya ber-id kosong — tetap lebih jujur daripada bad_json.
static bool rejectOversize(const RawCmd& r) {
    if (!r.oversize) return false;
    Command c{};
    c.type = Command::BAD_JSON;
    sendAck(c, "rejected", "payload_too_large");
    return true;
}

static void run(void*) {
    esp_task_wdt_add(nullptr);
    // static: dua RawCmd (~4 KB) terlalu besar untuk stack task ini.
    static RawCmd rc, luapan;
    for (;;) {
        esp_task_wdt_reset();
        // Timeout (bukan portMAX_DELAY) supaya watchdog tetap diberi makan
        // saat antrean sepi berjam-jam.
        if (xQueueReceive(q, &rc, pdMS_TO_TICKS(5000)) != pdTRUE) continue;
        // Kuras luapan lebih dulu supaya cloud mendapat jawaban secepat mungkin
        while (xQueueReceive(q_luapan, &luapan, 0) == pdTRUE) {
            if (rejectOversize(luapan)) continue;
            Command lc;
            parseCommand(luapan.json, luapan.len, lc);
            sendAck(lc, "rejected", "queue_full");
        }
        if (rejectOversize(rc)) continue;
        Command c;
        parseCommand(rc.json, rc.len, c);
        // BESS adalah node tunggal. Sebelumnya target diabaikan diam-diam,
        // sehingga perintah untuk node lain dijalankan di node ini.
        if ((c.type == Command::ENABLE || c.type == Command::DISABLE ||
             c.type == Command::SET_POWER) && c.target != 1) {
            sendAck(c, "rejected", "bad_value");
            continue;
        }
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
    q_luapan = xQueueCreate(1, sizeof(RawCmd));
    xTaskCreate(run, "task_cmd", 6144, nullptr, 2, nullptr);
}
