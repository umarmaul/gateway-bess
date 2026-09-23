#pragma once
#include "sched_logic.h"

// schedule.h -- integrasi ESP32 sub-proyek F (jadwal + auto-SOC): NVS `app_cfg`
// (namespace yang SAMA dihapus factory reset sub-proyek E, lihat
// prov.cpp::updateFactoryResetButton). Evaluasi murni ada di
// lib/bess_core/sched_logic.h -- file ini hanya persist + cache RAM
// (mutex-protected, dibaca dari beberapa task: web.cpp/loop(), task_cmd,
// task_auto).

void schedInit();               // panggil sekali di setup(), muat dari NVS app_cfg (atau default)
SchedConfig schedGetConfig();   // snapshot cache RAM saat ini (TIDAK baca NVS langsung)

// Terapkan input parsial di atas config saat ini, simpan ke NVS app_cfg,
// perbarui cache RAM. Satu jalur dipakai bersama oleh command MQTT
// set_schedule (task_cmd.cpp) dan HTTP POST /api/auto/config (web.cpp) --
// satu kontrak persist untuk keduanya.
SchedSetResult schedApplyAndSave(const SchedSetInput& in, SchedConfig& out);

// Dipanggil task_cmd.cpp saat command MANUAL (bukan dari task_auto/internal)
// enable/disable/set_output/set_power diterima -- nonaktifkan jadwal supaya
// intervensi operator tidak ditimpa auto-enable/disable berikutnya (paritas
// pola tim: manual override menang sampai jadwal diset ulang eksplisit).
// No-op (tanpa tulis NVS) kalau jadwal memang sudah nonaktif.
void schedNotifyManualOverride();
