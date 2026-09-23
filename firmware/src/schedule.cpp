#include "schedule.h"
#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Cache RAM + mutex -- dibaca/ditulis dari 3 task berbeda: loop()/web.cpp
// (GET/POST /api/auto/config), task_cmd (command set_schedule +
// schedNotifyManualOverride), task_auto (schedGetConfig tiap ~5 dtk). Pola
// sama dengan task_ota.cpp::s_info_mtx.
// ---------------------------------------------------------------------------
static SchedConfig s_cfg;
static SemaphoreHandle_t s_mtx = nullptr;

static bool loadFromNvs(SchedConfig& out) {
    out = schedDefaultConfig();
    Preferences p;
    if (!p.begin("app_cfg", true)) return false;   // belum pernah disimpan -- default berlaku
    bool has = p.isKey("sched_en");
    if (has) {
        out.enabled = p.getBool("sched_en", out.enabled);
        out.start_min = p.getInt("sched_start", out.start_min);
        out.end_min = p.getInt("sched_end", out.end_min);
        out.soc_stop_pct = p.getFloat("sched_stop", out.soc_stop_pct);
        out.soc_recovery_pct = p.getFloat("sched_rec", out.soc_recovery_pct);
        out.power_w = p.getFloat("sched_pw", out.power_w);
        out.tz_offset_min = p.getInt("sched_tz", out.tz_offset_min);
    }
    p.end();
    return has;
}

static bool saveToNvs(const SchedConfig& c) {
    Preferences p;
    if (!p.begin("app_cfg", false)) {
        Serial.println("[sched] NVS 'app_cfg' tak terbuka -- jadwal TIDAK tersimpan");
        return false;
    }
    p.putBool("sched_en", c.enabled);
    p.putInt("sched_start", c.start_min);
    p.putInt("sched_end", c.end_min);
    p.putFloat("sched_stop", c.soc_stop_pct);
    p.putFloat("sched_rec", c.soc_recovery_pct);
    p.putFloat("sched_pw", c.power_w);
    p.putInt("sched_tz", c.tz_offset_min);
    p.end();
    return true;
}

void schedInit() {
    // Sebelum s_mtx dibuat: hanya setup() yang berjalan (task lain belum
    // di-start), jadi akses tanpa lock di sini aman.
    loadFromNvs(s_cfg);
    s_mtx = xSemaphoreCreateMutex();
    char sh[6], eh[6];
    schedFormatHHMM(s_cfg.start_min, sh);
    schedFormatHHMM(s_cfg.end_min, eh);
    Serial.printf("[sched] dimuat: enabled=%d %s..%s stop=%.0f%% rec=%.0f%% pw=%.0fW tz=%d\n",
                  (int)s_cfg.enabled, sh, eh, s_cfg.soc_stop_pct, s_cfg.soc_recovery_pct,
                  s_cfg.power_w, s_cfg.tz_offset_min);
}

SchedConfig schedGetConfig() {
    if (!s_mtx) return s_cfg;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    SchedConfig c = s_cfg;
    xSemaphoreGive(s_mtx);
    return c;
}

SchedSetResult schedApplyAndSave(const SchedSetInput& in, SchedConfig& out) {
    SchedSetResult r;
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    r = schedApplySetInput(s_cfg, in, out);
    s_cfg = out;
    if (s_mtx) xSemaphoreGive(s_mtx);
    if (!saveToNvs(out))
        Serial.println("[sched] WARNING konfigurasi berubah di RAM tapi gagal tersimpan ke NVS");
    return r;
}

void schedNotifyManualOverride() {
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool was_enabled = s_cfg.enabled;
    if (was_enabled) s_cfg.enabled = false;
    SchedConfig snapshot = s_cfg;
    if (s_mtx) xSemaphoreGive(s_mtx);
    if (!was_enabled) return;   // sudah nonaktif -- tak perlu tulis NVS
    if (saveToNvs(snapshot)) Serial.println("[sched] jadwal dinonaktifkan (command manual)");
    else Serial.println("[sched] WARNING gagal menyimpan penonaktifan jadwal ke NVS");
}
