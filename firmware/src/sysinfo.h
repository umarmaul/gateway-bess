#pragma once
#include "payload.h"

// sysinfo.h -- SATU fungsi pengisi SysInfo untuk KEDUA konsumen: telemetri
// MQTT (main.cpp::loop()) dan GET /api/data (sub-proyek H, src/web_dashboard.cpp).
// Sebelum ini, main.cpp::loop() mengisi SysInfo sendiri inline -- dashboard H
// butuh persis payload yang sama (satu builder, satu kontrak, lihat
// lib/bess_core/payload.h), jadi pengisiannya dipindah ke sini.
//
// `seq` SENGAJA TIDAK diisi oleh fillSysInfo() -- itu milik telemetri MQTT
// (g_state.seq di-increment HANYA oleh main.cpp::loop() saat mengirim
// telemetri). /api/data memakai nilai g_state.seq SAAT INI tanpa increment
// (lihat CLAUDE.md tugas sub-proyek H). Pemanggil mengisi si.seq sendiri
// (dengan/tanpa increment, sesuai kebutuhan) SETELAH fillSysInfo() kembali.

// Panggil SEKALI di setup(), setelah reset_reason/boot_count/crash diketahui
// (setelah crashLogInit()). Field ini tetap sama sepanjang boot ini, jadi
// disimpan sekali alih-alih dibaca ulang tiap panggilan fillSysInfo().
// `reset_reason` disalin sebagai POINTER apa adanya -- caller (main.cpp)
// menyimpannya di buffer `static`, jadi umurnya = seluruh program.
void sysInfoInit(const char* reset_reason, uint32_t boot_count, const CrashInfo& crash);

// Isi seluruh field SysInfo KECUALI `seq` (lihat catatan di atas). Aman
// dipanggil dari loop() maupun handler HTTP sinkron (web_dashboard.cpp) --
// semua sumber data (WiFi, otaGetInfo, autoGetInfo, provApActive/Mdns)
// sudah non-blocking/mutex-pendek.
void fillSysInfo(SysInfo& si);
