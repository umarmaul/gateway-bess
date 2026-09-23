#include "sched_logic.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "timeutil.h"

SchedConfig schedDefaultConfig() {
    SchedConfig c{};
    c.enabled = false;
    c.start_min = 0;
    c.end_min = 0;
    c.soc_stop_pct = 10.0f;
    c.soc_recovery_pct = 20.0f;
    c.power_w = 0.0f;
    c.tz_offset_min = 0;
    return c;
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool schedParseHHMM(const char* s, int& out_min) {
    if (!s) return false;
    // Format ketat "HH:MM" -- persis 5 karakter, dua digit jam dua digit
    // menit. Tidak menerima jam 1-digit ("7:00") supaya parser tetap simpel
    // dan cocok dengan apa yang dikirim balik schedFormatHHMM (round-trip).
    if (strlen(s) != 5) return false;
    if (!isDigit(s[0]) || !isDigit(s[1]) || s[2] != ':' || !isDigit(s[3]) || !isDigit(s[4]))
        return false;
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return false;
    out_min = h * 60 + m;
    return true;
}

static int wrapMinutes(int m) {
    int r = m % 1440;
    if (r < 0) r += 1440;
    return r;
}

void schedFormatHHMM(int minutes, char out[6]) {
    int m = wrapMinutes(minutes);
    snprintf(out, 6, "%02d:%02d", m / 60, m % 60);
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

SchedSetResult schedApplySetInput(const SchedConfig& cur, const SchedSetInput& in, SchedConfig& out) {
    out = cur;
    bool clamped = false;

    if (in.has_enabled) out.enabled = in.enabled;

    if (in.has_start) {
        int wrapped = wrapMinutes(in.start_min);
        if (wrapped != in.start_min) clamped = true;
        out.start_min = wrapped;
    }
    if (in.has_end) {
        int wrapped = wrapMinutes(in.end_min);
        if (wrapped != in.end_min) clamped = true;
        out.end_min = wrapped;
    }

    if (in.has_soc_stop) {
        float v = clampf(in.soc_stop_pct, 0.0f, 99.0f);
        if (v != in.soc_stop_pct) clamped = true;
        out.soc_stop_pct = v;
    }

    // soc_recovery_pct: dipangkas ke 0..100 dulu (bila dikirim), LALU
    // invariant recovery > stop DIPAKSAKAN terhadap soc_stop_pct FINAL --
    // walau field recovery itu sendiri tak dikirim pemanggil kali ini (mis.
    // operator hanya menaikkan soc_stop_pct, recovery lama yang tersimpan
    // bisa jadi sudah di bawah stop baru).
    float recovery_in = in.has_soc_recovery ? clampf(in.soc_recovery_pct, 0.0f, 100.0f) : cur.soc_recovery_pct;
    if (in.has_soc_recovery && recovery_in != in.soc_recovery_pct) clamped = true;
    float min_recovery = out.soc_stop_pct + 1.0f;
    if (recovery_in < min_recovery) { recovery_in = clampf(min_recovery, 0.0f, 100.0f); clamped = true; }
    out.soc_recovery_pct = recovery_in;

    if (in.has_power) out.power_w = in.power_w;   // tidak dipangkas di sini -- lihat komentar sched_logic.h

    if (in.has_tz) {
        int v = clampi(in.tz_offset_min, -720, 840);
        if (v != in.tz_offset_min) clamped = true;
        out.tz_offset_min = v;
    }

    return clamped ? SchedSetResult::CLAMPED : SchedSetResult::ACCEPTED;
}

size_t schedBuildAck(const char* id, const char* result, const char* detail,
                      const SchedConfig& applied, uint32_t ts, char* out, size_t cap) {
    JsonDocument doc;
    doc["id"] = id;
    doc["cmd"] = "set_schedule";
    doc["result"] = result;
    doc["detail"] = detail;
    JsonObject ap = doc["applied"].to<JsonObject>();
    ap["enabled"] = applied.enabled;
    char start_hhmm[6], end_hhmm[6];
    schedFormatHHMM(applied.start_min, start_hhmm);
    schedFormatHHMM(applied.end_min, end_hhmm);
    ap["start_hhmm"] = start_hhmm;
    ap["end_hhmm"] = end_hhmm;
    ap["soc_stop_pct"] = applied.soc_stop_pct;
    ap["soc_recovery_pct"] = applied.soc_recovery_pct;
    ap["power_w"] = applied.power_w;
    ap["tz_offset_min"] = applied.tz_offset_min;
    doc["ts"] = tsOrZero(ts);
    return serializeJson(doc, out, cap);
}

bool schedInWindow(const SchedConfig& cfg, uint32_t epoch_utc) {
    if (epoch_utc == 0) return false;
    if (cfg.start_min == cfg.end_min) return false;   // window kosong (konvensi -- lihat sched_logic.h)
    // int64 supaya tz_offset_min negatif tidak underflow uint32 sebelum modulo.
    int64_t local = (int64_t)epoch_utc + (int64_t)cfg.tz_offset_min * 60;
    int64_t day_sec = local % 86400;
    if (day_sec < 0) day_sec += 86400;
    int minutes = (int)(day_sec / 60);
    if (cfg.start_min < cfg.end_min)
        return minutes >= cfg.start_min && minutes < cfg.end_min;
    // window lintas tengah malam (start > end)
    return minutes >= cfg.start_min || minutes < cfg.end_min;
}

bool schedBatteryReady(const SchedConfig& cfg, const SchedInputs& in) {
    return in.soc_pct >= cfg.soc_recovery_pct && !in.fault && !in.comm_lost;
}

SchedAction schedDecide(const SchedConfig& cfg, const SchedInputs& in, SchedMemo& memo) {
    // (a) proteksi SOC disable-only -- SELALU dicek lebih dulu, terlepas
    // dari status jadwal/waktu. `running` mencegah disable berulang percuma
    // kalau BESS sudah mati. Memicu DISABLE juga menghapus pending_enable
    // (TEMUAN REVIEW 23 Sep 2026) -- kalau BESS baru saja dipaksa mati oleh
    // proteksi SOC, jadwal tidak boleh langsung mencoba menyalakannya lagi
    // di siklus berikutnya hanya karena masih "menunggu siap" dari sebelumnya.
    bool exporting = in.active_power_kw > 0.0f;
    if (exporting && in.running && in.soc_pct <= cfg.soc_stop_pct) {
        memo.pending_enable = false;
        return SchedAction::DISABLE;
    }

    // (b) jadwal -- hanya bertindak saat enabled DAN waktu tersinkron.
    if (!cfg.enabled) {
        memo.have_prev = false;       // reset: reaktivasi nanti dievaluasi fresh
        memo.pending_enable = false;
        return SchedAction::NONE;
    }
    if (in.ts == 0) return SchedAction::NONE;   // memo SENGAJA tidak disentuh -- lihat sched_logic.h

    bool win = schedInWindow(cfg, in.ts);
    SchedAction action = SchedAction::NONE;
    bool edge_in = win && !(memo.have_prev && memo.prev_in_window);
    bool edge_out = !win && memo.have_prev && memo.prev_in_window;
    bool ready = schedBatteryReady(cfg, in);
    if (edge_in) {
        if (ready) {
            action = SchedAction::ENABLE_WITH_POWER;
            memo.pending_enable = false;
        } else {
            // Belum siap saat edge -- TEMUAN REVIEW 23 Sep 2026: ditandai
            // pending, DICOBA LAGI tiap siklus (lihat cabang di bawah)
            // selama masih di window yang sama, bukan menunggu window
            // berikutnya.
            memo.pending_enable = true;
        }
    } else if (edge_out) {
        action = SchedAction::DISABLE;
        memo.pending_enable = false;
    } else if (win && memo.pending_enable) {
        // Bukan edge, tapi masih di dalam window DAN belum pernah berhasil
        // enable di window ini -- coba lagi. Begitu sudah pernah enable
        // (pending_enable sudah false), cabang ini tidak pernah masuk lagi
        // sampai window berikutnya -- operator boleh mematikan BESS manual
        // di tengah window tanpa jadwal menyalakannya ulang.
        if (ready) {
            action = SchedAction::ENABLE_WITH_POWER;
            memo.pending_enable = false;
        }
    }
    memo.prev_in_window = win;
    memo.have_prev = true;
    return action;
}
