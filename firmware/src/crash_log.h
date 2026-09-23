#pragma once
#include "payload.h"

// Dipanggil sekali di setup(), SESUDAH boot_count dinaikkan. Kalau flash
// menyimpan coredump dari crash sebelumnya, ringkasannya dipindah ke NVS
// (namespace "crash") lalu image-nya dihapus — supaya crash yang sama tidak
// dilaporkan ulang, dan crash berikutnya tidak menimpa bukti yang belum dibaca.
// Mengisi `out` dengan crash terakhir yang tercatat di NVS (bisa dari boot lama).
void crashLogInit(uint32_t boot_count, CrashInfo& out);
