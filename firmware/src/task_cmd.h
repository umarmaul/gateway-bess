#pragma once
#include <stddef.h>

// task_cmd.h — eksekusi command MQTT (enable/disable/set_power/set_schedule) + ack
void taskCmdStart();
void taskCmdSubmit(const char* json, size_t n);   // dipanggil dari event MQTT (copy ke queue)

// sub-proyek F: dipanggil task_auto untuk mengeksekusi aksi jadwal/auto-SOC
// lewat jalur command yang SAMA (Modbus + safety + ack), TANPA menonaktifkan
// jadwal (beda dari taskCmdSubmit, yang dianggap datang dari cloud/operator
// dan MEMATIKAN jadwal untuk enable/disable/set_output -- lihat schedule.h).
void taskCmdSubmitInternal(const char* json, size_t n);

// sub-proyek H: dipanggil src/web_dashboard.cpp (POST /api/command) DARI
// task_web (lihat web.h/web.cpp -- SEBELUM temuan review 23 Sep 2026 ini
// dipanggil dari loop(), sekarang handleClient() punya task sendiri) --
// BUKAN dari task esp-mqtt (taskCmdSubmit, buffer rc_ext) atau task_auto
// (taskCmdSubmitInternal, buffer rc_int). Buffer statis terpisah (rc_web)
// supaya ketiga jalur submit aman dipanggil dari task masing-masing tanpa
// saling menimpa -- invariant ini masih berlaku sama persis: task_web
// SATU-SATUNYA pemanggil taskCmdSubmitWeb (WebServer bawaan memproses satu
// koneksi per handleClient(), jadi tetap satu pemanggil pada satu waktu).
// Command dari web dianggap MANUAL (seperti cloud) -- menonaktifkan jadwal,
// sama seperti taskCmdSubmit (lihat schedule.h).
//
// Beda dari dua jalur lain: mengembalikan HASIL seketika (true = masuk
// antrean utama) karena HTTP punya respons sinkron yang bisa memberi tahu
// operator langsung ("503 antrean penuh") -- TIDAK memakai jalur luapan
// 1-slot yang dipakai MQTT/internal (itu ada supaya command yang tak punya
// respons sinkron tetap kebagian ack "queue_full"; command web sudah
// mendapat kepastian lewat kode status HTTP itu sendiri).
bool taskCmdSubmitWeb(const char* json, size_t n);

// sub-proyek H: 8 ack terakhir (ring buffer, lihat lib/bess_core/ack_ring.h)
// sebagai satu array JSON `[terbaru, ..., terlama]`. Aman dipanggil dari
// task mana pun -- mutex pendek, snapshot disalin lalu dibangun di luar
// lock. Return 0 kalau `cap` tidak cukup (caller boleh jatuh ke "[]").
size_t taskCmdGetAcksJson(char* out, size_t cap);
