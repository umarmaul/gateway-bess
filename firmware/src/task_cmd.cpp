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
#include "task_ota.h"
#include "schedule.h"
#include "ack_ring.h"
#include <esp_task_wdt.h>

// Seukuran buffer baca esp-mqtt: semua pesan yang lolos penjaga "pesan
// terpotong" di mqtt_link muat utuh. oversize tetap dijaga untuk berjaga-jaga
// kalau kedua konstanta suatu saat tak sinkron — lebih baik ditolak jujur
// daripada dipotong lalu dijawab bad_json.
// internal=true kalau ini command yang diantrekan task_auto (bukan cloud) --
// lihat schedule.h: command internal TIDAK menonaktifkan jadwal.
struct RawCmd { char json[CMD_JSON_MAX + 1]; size_t len; bool oversize; bool internal; };
static QueueHandle_t q;
// 1 slot: perintah yang ditolak karena antrean penuh. Invariant yang menjamin
// ack queue_full tidak menggantung: slot ini HANYA terisi saat antrean utama
// penuh, jadi antrean utama pasti berisi dan run() pasti bangun lalu menguras
// luapan sebelum perintah berikutnya.
static QueueHandle_t q_luapan;

static void submit(const char* json, size_t n, bool internal) {
    // static: ~2 KB terlalu besar untuk stack pemanggil (event handler
    // esp-mqtt untuk taskCmdSubmit, task_auto untuk taskCmdSubmitInternal).
    // Storage static terpisah per fungsi pemanggil (lihat definisi di bawah)
    // -- aman karena tiap fungsi hanya dipanggil dari SATU task masing-masing
    // dan xQueueSend menyalin isinya sebelum kembali.
    static RawCmd rc_ext, rc_int;
    RawCmd& rc = internal ? rc_int : rc_ext;
    rc.internal = internal;
    rc.oversize = n > CMD_JSON_MAX;
    rc.len = rc.oversize ? 0 : n;
    memcpy(rc.json, json, rc.len);
    rc.json[rc.len] = 0;
    if (xQueueSend(q, &rc, 0) == pdTRUE) return;
    // Antrean utama penuh. JSON tidak boleh di-parse di sini (task jaringan
    // esp-mqtt atau task_auto), jadi payload mentah dititipkan ke slot
    // luapan; task_cmd yang mem-parse id-nya dan membalas queue_full.
    if (xQueueSend(q_luapan, &rc, 0) != pdTRUE)
        Serial.println("[cmd] dibuang: antrean utama dan luapan penuh");
}

void taskCmdSubmit(const char* json, size_t n) { submit(json, n, false); }
void taskCmdSubmitInternal(const char* json, size_t n) { submit(json, n, true); }

// sub-proyek H: jalur submit KETIGA, aman dipanggil dari loop() (web_dashboard.cpp).
// Buffer statis SENDIRI (rc_web) -- terpisah dari rc_ext (submit(), hanya aman
// dari task esp-mqtt) dan rc_int (task_auto) supaya loop() tidak pernah
// menimpa buffer yang sedang disalin xQueueSend dari task lain. TIDAK lewat
// q_luapan (lihat task_cmd.h): antrean utama penuh -> false seketika, caller
// HTTP membalas 503 sendiri.
bool taskCmdSubmitWeb(const char* json, size_t n) {
    static RawCmd rc_web;
    rc_web.internal = false;   // command web = manual, seperti cloud (menonaktifkan jadwal)
    rc_web.oversize = n > CMD_JSON_MAX;
    rc_web.len = rc_web.oversize ? 0 : n;
    memcpy(rc_web.json, json, rc_web.len);
    rc_web.json[rc_web.len] = 0;
    return xQueueSend(q, &rc_web, 0) == pdTRUE;
}

// sub-proyek H: ring buffer 8 ack terakhir untuk GET /api/acks. Ditulis dari
// task_cmd sendiri (satu-satunya penulis, jadi push tak butuh lock ketat),
// dibaca dari task lain (web_dashboard.cpp, lewat loop()) -- mutex melindungi
// pembaca dari membaca struct yang sedang ditulis separuh jalan.
static AckRing s_ack_ring;
static SemaphoreHandle_t s_ack_mtx;

static void ackRingPushLocked(const char* json, size_t n) {
    if (!s_ack_mtx) return;
    xSemaphoreTake(s_ack_mtx, portMAX_DELAY);
    ackRingPush(s_ack_ring, json, n);
    xSemaphoreGive(s_ack_mtx);
}

size_t taskCmdGetAcksJson(char* out, size_t cap) {
    AckRing copy;
    if (!s_ack_mtx) { ackRingInit(copy); }
    else {
        xSemaphoreTake(s_ack_mtx, portMAX_DELAY);
        copy = s_ack_ring;
        xSemaphoreGive(s_ack_mtx);
    }
    return ackRingBuildJson(copy, out, cap);
}

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

static void sendAck(const Command& c, const char* result, const char* detail,
                    float pct = NAN, float w = NAN) {
    static char buf[ACK_JSON_MAX];
    size_t n = buildAckJson(c, result, detail, pct, w, nowTs(), buf, sizeof(buf));
    ackRingPushLocked(buf, n);
    bool sent = mqttPublishAck(buf, n);
    Serial.printf("[cmd] %s -> %s %s%s\n", c.name, result, detail,
                  sent ? "" : " (ack DIBUANG: antrean mqtt_tx penuh)");
}

// set_schedule punya bentuk "applied" sendiri (config jadwal, bukan
// power_pct/power_w) -- builder terpisah di sched_logic (schedBuildAck),
// sama pola dengan otaBuildAck/otaBuildStatus milik sub-proyek G.
static void sendScheduleAck(const Command& c, const char* result, const char* detail,
                             const SchedConfig& applied) {
    static char buf[ACK_JSON_MAX];
    size_t n = schedBuildAck(c.id, result, detail, applied, nowTs(), buf, sizeof(buf));
    ackRingPushLocked(buf, n);
    bool sent = mqttPublishAck(buf, n);
    Serial.printf("[cmd] set_schedule -> %s %s%s\n", result, detail,
                  sent ? "" : " (ack DIBUANG: antrean mqtt_tx penuh)");
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

static void doOnOff(const Command& c, bool on, bool internal) {
    // Command MANUAL (cloud/operator) mematikan kontrol jadwal -- intervensi
    // manual menang sampai jadwal diset ulang eksplisit (paritas pola tim).
    // Command INTERNAL (dari task_auto sendiri, eksekusi jadwal) TIDAK boleh
    // mematikan jadwalnya sendiri. Dicek regardless hasil Modbus di bawah --
    // kontrak ack TIDAK berubah (lihat CLAUDE.md task ini), cukup log serial
    // + field schedule_enabled:false di telemetri berikutnya.
    if (!internal) schedNotifyManualOverride();
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

static void doSetPower(const Command& c, bool internal) {
    if (!internal) schedNotifyManualOverride();   // lihat komentar di doOnOff
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
        sendAck(c, "rejected", "rated_unknown");   // 3146 belum pernah terbaca
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

static void doSetSchedule(const Command& c) {
    if (c.sched_bad_input) {
        // start_hhmm/end_hhmm bukan "HH:MM" valid, atau power_w NaN --
        // ditolak TANPA menyentuh config tersimpan (schedApplyAndSave TIDAK
        // dipanggil sama sekali). "applied" melaporkan config SAAT INI (tak
        // ada yang berubah), bukan input yang ditolak.
        sendScheduleAck(c, "rejected", "bad_value", schedGetConfig());
        return;
    }
    SchedConfig applied{};
    SchedSetResult r = schedApplyAndSave(c.sched, applied);
    sendScheduleAck(c, r == SchedSetResult::CLAMPED ? "clamped" : "accepted", "", applied);
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
            // OTA aktif menang atas queue_full: job OTA yang sedang berjalan
            // lebih informatif buat cloud daripada "antreanmu penuh".
            sendAck(lc, "rejected", otaInProgress() ? "ota_in_progress" : "queue_full");
        }
        if (rejectOversize(rc)) continue;
        Command c;
        parseCommand(rc.json, rc.len, c);
        // Selama job OTA aktif, semua command biasa ditolak -- flash sedang
        // ditulis dan bus RS485/heap sebaiknya tidak dibagi dengan Modbus.
        if (otaInProgress()) { sendAck(c, "rejected", "ota_in_progress"); continue; }
        // BESS adalah node tunggal. Sebelumnya target diabaikan diam-diam,
        // sehingga perintah untuk node lain dijalankan di node ini.
        if ((c.type == Command::ENABLE || c.type == Command::DISABLE ||
             c.type == Command::SET_POWER) && c.target != 1) {
            sendAck(c, "rejected", "bad_value");
            continue;
        }
        switch (c.type) {
            case Command::ENABLE:  doOnOff(c, true, rc.internal); break;
            case Command::DISABLE: doOnOff(c, false, rc.internal); break;
            case Command::SET_POWER: doSetPower(c, rc.internal); break;
            case Command::SET_SCHEDULE: doSetSchedule(c); break;
            case Command::BAD_JSON: sendAck(c, "rejected", "bad_json"); break;
            default: sendAck(c, "rejected", "unsupported_cmd"); break;
        }
    }
}

void taskCmdStart() {
    q = xQueueCreate(4, sizeof(RawCmd));
    q_luapan = xQueueCreate(1, sizeof(RawCmd));
    s_ack_mtx = xSemaphoreCreateMutex();
    ackRingInit(s_ack_ring);
    xTaskCreate(run, "task_cmd", 6144, nullptr, 2, nullptr);
}
