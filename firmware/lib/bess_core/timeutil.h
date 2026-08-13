#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdint.h>

// Ambang "jam sudah masuk akal" — 2020-09-13. Sebelum NTP sinkron, time(nullptr)
// di ESP32 menghitung dari 1970 sehingga mengembalikan detik sejak boot.
#define TS_VALID_MIN 1600000000u

// Kembalikan epoch apa adanya kalau sudah sinkron, 0 kalau belum.
// 0 = "waktu tidak diketahui" (kontrak sama dengan BEPESP32_WiFi_Extension).
uint32_t tsOrZero(uint32_t epoch);

// Benar kalau now sudah mencapai/melewati deadline. Memakai selisih bertanda
// supaya tetap benar saat millis() berputar (hari ke-49).
bool timeAfter(uint32_t now, uint32_t deadline);

#endif
