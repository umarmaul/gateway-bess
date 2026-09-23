#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdint.h>
#include <stddef.h>
#include "bess_data.h"

// Ringkasan crash terakhir (dari coredump di flash, disimpan ke NVS saat boot).
struct CrashInfo {
    bool present;
    char task[16];            // nama task yang crash (esp_core_dump_summary_t)
    uint32_t pc;              // program counter saat exception
    uint32_t mcause;          // penyebab trap RISC-V
    uint32_t boot_count;      // boot_count saat dump ditangkap (boot sesudah crash)
};

struct SysInfo {
    char gw[13];              // MAC 12 hex + NUL
    const char* fw_version;   // "bess-0.1.0"
    uint32_t uptime_ms, seq, ts;   // ts mentah; buildTelemetryJson yang menolkan
    int rssi;
    const char* ssid;
    char ip[16];
    const char* last_reset_reason;   // hasil resetReasonName(), mis. "PANIC"
    uint32_t boot_count;             // pencacah monotonik di NVS
    uint32_t free_heap;              // esp_get_free_heap_size() saat telemetri dibangun
    uint32_t min_free_heap;          // low-water mark sejak boot
    CrashInfo crash;                 // present=false -> "last_crash": null
};

// Builds telemetry JSON per spec §6.1
// Returns number of bytes written (excluding NUL terminator), or 0 on error
size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap);

#endif
