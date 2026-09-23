#pragma once
#include "payload.h"   // AutoInfo

// task_auto.h -- evaluator periodik jadwal + auto-SOC (sub-proyek F).
// Tiap ~5 dtk: snapshot g_state.bess + waktu NTP -> schedDecide() (murni,
// lib/bess_core/sched_logic.h) -> eksekusi aksi dengan mengantrekan command
// INTERNAL ke task_cmd (taskCmdSubmitInternal) supaya jalur Modbus + safety
// + ack SAMA dengan command dari cloud, TANPA menonaktifkan jadwal (beda
// dari command manual eksternal -- lihat task_cmd.cpp).
void taskAutoStart();

// Snapshot untuk blok data.auto di telemetri. Aman dipanggil dari loop().
void autoGetInfo(AutoInfo& out);
