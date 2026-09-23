#include "task_auto.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "schedule.h"
#include "sched_logic.h"
#include "state.h"
#include "task_cmd.h"
#include "bess_decode.h"
#include "timeutil.h"
#include "config.h"
#include <esp_task_wdt.h>

#define AUTO_PERIOD_MS 5000UL

static SemaphoreHandle_t s_info_mtx = nullptr;
static AutoInfo s_info{};

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

static void setInfo(const SchedConfig& cfg, bool win, bool ready,
                     const char* last_action, uint32_t last_action_ts) {
    if (!s_info_mtx) return;
    xSemaphoreTake(s_info_mtx, portMAX_DELAY);
    s_info.schedule_enabled = cfg.enabled;
    schedFormatHHMM(cfg.start_min, s_info.start_hhmm);
    schedFormatHHMM(cfg.end_min, s_info.end_hhmm);
    s_info.tz_offset_min = cfg.tz_offset_min;
    s_info.power_w = cfg.power_w;
    s_info.soc_stop_pct = cfg.soc_stop_pct;
    s_info.soc_recovery_pct = cfg.soc_recovery_pct;
    s_info.in_window = win;
    s_info.battery_ready = ready;
    strncpy(s_info.last_action, last_action, sizeof(s_info.last_action) - 1);
    s_info.last_action[sizeof(s_info.last_action) - 1] = 0;
    s_info.last_action_ts = last_action_ts;
    xSemaphoreGive(s_info_mtx);
}

void autoGetInfo(AutoInfo& out) {
    if (!s_info_mtx) { out = AutoInfo{}; return; }
    xSemaphoreTake(s_info_mtx, portMAX_DELAY);
    out = s_info;
    xSemaphoreGive(s_info_mtx);
}

// Command internal ke task_cmd -- id "auto-<ts>" supaya ack-nya terlihat
// jelas oleh cloud sebagai aksi otonom gateway, bukan command yang mereka
// kirim sendiri. Lewat taskCmdSubmitInternal (BUKAN taskCmdSubmit) supaya
// TIDAK menonaktifkan jadwal yang baru saja memicunya sendiri.
static void submitInternal(const char* cmd, bool with_power, float power_w) {
    char buf[160];
    char id[24];
    snprintf(id, sizeof(id), "auto-%lu", (unsigned long)nowTs());
    if (with_power)
        snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"cmd\":\"%s\",\"args\":{\"power_w\":%.1f}}",
                 id, cmd, (double)power_w);
    else
        snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"cmd\":\"%s\"}", id, cmd);
    taskCmdSubmitInternal(buf, strlen(buf));
}

static void run(void*) {
    esp_task_wdt_add(nullptr);
    s_info_mtx = xSemaphoreCreateMutex();
    static char last_action[24] = "none";
    static uint32_t last_action_ts = 0;
    SchedMemo memo{};   // hidup selama task ini hidup (tidak pernah di-restart)

    for (;;) {
        esp_task_wdt_reset();

        SchedConfig cfg = schedGetConfig();
        stateLock();
        BessData d = g_state.bess;
        stateUnlock();

        SchedInputs in{};
        in.soc_pct = d.soc_pct;
        in.active_power_kw = d.active_power_kw;
        in.running = bessRunning(d);
        in.fault = bessFault(d);
        in.comm_lost = d.comm_lost;
        in.ts = tsOrZero(nowTs());

        SchedAction action = schedDecide(cfg, in, memo);
        bool win = schedInWindow(cfg, in.ts);
        bool ready = schedBatteryReady(cfg, in);

        switch (action) {
            case SchedAction::ENABLE_WITH_POWER:
                Serial.printf("[auto] masuk window jadwal -- set_output %.1f W lalu enable\n", cfg.power_w);
                // set_output DULU, baru enable: setpoint daya sudah terpasang
                // saat BESS mulai berjalan (bukan menyala dulu di setpoint lama).
                submitInternal("set_output", true, cfg.power_w);
                submitInternal("enable", false, 0.0f);
                strncpy(last_action, "enable_with_power", sizeof(last_action) - 1);
                last_action[sizeof(last_action) - 1] = 0;
                last_action_ts = nowTs();
                break;
            case SchedAction::DISABLE:
                Serial.println("[auto] disable (proteksi SOC disable-only, atau keluar window jadwal)");
                submitInternal("disable", false, 0.0f);
                strncpy(last_action, "disable", sizeof(last_action) - 1);
                last_action[sizeof(last_action) - 1] = 0;
                last_action_ts = nowTs();
                break;
            case SchedAction::NONE: default: break;
        }

        setInfo(cfg, win, ready, last_action, last_action_ts);
        vTaskDelay(pdMS_TO_TICKS(AUTO_PERIOD_MS));
    }
}

void taskAutoStart() {
    xTaskCreate(run, "task_auto", 4096, nullptr, 1, nullptr);
}
