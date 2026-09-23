#pragma once
#include "payload.h"

// Dipanggil sekali di setup(), SESUDAH boot_count dinaikkan. Kalau flash
// menyimpan coredump dari crash sebelumnya, ringkasannya dipindah ke NVS
// (namespace "crash") lalu image-nya dihapus — supaya crash yang sama tidak
// dilaporkan ulang, dan crash berikutnya tidak menimpa bukti yang belum dibaca.
// Mengisi `out` dengan crash terakhir yang tercatat di NVS (bisa dari boot lama).
// reset_by_task_wdt: esp_reset_reason() == ESP_RST_TASK_WDT — nama task macet
// yang ditangkap hook ISR watchdog hanya dipercaya pada reset jenis itu.
void crashLogInit(uint32_t boot_count, bool reset_by_task_wdt, CrashInfo& out);
