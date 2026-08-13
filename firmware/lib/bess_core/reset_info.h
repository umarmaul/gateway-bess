#ifndef RESET_INFO_H
#define RESET_INFO_H

#include <stddef.h>

// Nilai cermin dari esp_reset_reason_t (ESP-IDF). Dicocokkan lewat static_assert
// di src/main.cpp supaya ketidakcocokan ketahuan saat kompilasi, bukan di lapangan.
#define RESET_UNKNOWN    0
#define RESET_POWERON    1
#define RESET_EXT        2
#define RESET_SW         3
#define RESET_PANIC      4
#define RESET_INT_WDT    5
#define RESET_TASK_WDT   6
#define RESET_WDT        7
#define RESET_DEEPSLEEP  8
#define RESET_BROWNOUT   9
#define RESET_SDIO       10
#define RESET_USB        11
#define RESET_JTAG       12

// Menulis nama alasan reset ke out (selalu NUL-terminated), mengembalikan out.
// Nilai tak dikenal menjadi "UNKNOWN_<angka>" supaya tidak ada informasi hilang.
const char* resetReasonName(int reason, char* out, size_t cap);

#endif
