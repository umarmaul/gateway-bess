#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdint.h>
#include <stddef.h>
#include "bess_data.h"
#include "ota_logic.h"

// Ringkasan crash terakhir (dari coredump di flash, disimpan ke NVS saat boot).
struct CrashInfo {
    bool present;
    char task[16];            // nama task yang crash (esp_core_dump_summary_t)
    uint32_t pc;              // program counter saat exception
    uint32_t mcause;          // penyebab trap RISC-V
    uint32_t boot_count;      // boot_count saat dump ditangkap (boot sesudah crash)
    char wdt_tasks[48];       // crash TASK_WDT: task yang tak memberi makan WDT ("" = bukan)
};

// Tambah `name` ke daftar dipisah koma di `buf`. Kalau tak muat, nama itu
// dibuang utuh (daftar tak pernah berisi nama terpotong). Aman dipanggil dari
// ISR: tanpa alokasi, tanpa printf.
void wdtTaskListAppend(char* buf, size_t cap, const char* name);

// Snapshot OTA (sub-proyek G) untuk blok data.ota di telemetri -- diisi
// task_ota (src/), murni data supaya builder tetap testable native.
struct OtaInfo {
    char state[16];                          // idle|downloading|verifying|restarting|installed|failed
    char id[OTA_MANIFEST_ID_MAX + 1];         // "" kalau belum pernah ada job
    char running_partition[16];               // label partisi yang SEDANG berjalan (app0/app1)
    bool pending_verify;                      // true = image ini masih PENDING_VERIFY (rollback aktif)
};

struct SysInfo {
    char gw[13];              // MAC 12 hex + NUL
    const char* fw_version;   // "bess-0.1.0"
    uint32_t uptime_ms, seq, ts;   // ts mentah; buildTelemetryJson yang menolkan
    int rssi;
    const char* ssid;
    char ip[16];
    bool ap_active;           // SoftAP fallback sedang menyala (sub-proyek E)
    const char* mdns;         // hostname mDNS yang sedang diiklankan (mis. "bep-bess-gateway")
    const char* last_reset_reason;   // hasil resetReasonName(), mis. "PANIC"
    uint32_t boot_count;             // pencacah monotonik di NVS
    uint32_t free_heap;              // esp_get_free_heap_size() saat telemetri dibangun
    uint32_t min_free_heap;          // low-water mark sejak boot
    CrashInfo crash;                 // present=false -> "last_crash": null
    OtaInfo ota;                     // state=="" -> builder melapor "idle" (defensif)
};

// Builds telemetry JSON per spec §6.1
// Returns number of bytes written (excluding NUL terminator), or 0 on error
size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap);

#endif
