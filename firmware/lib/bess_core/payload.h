#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdint.h>
#include <stddef.h>
#include "bess_data.h"

struct SysInfo {
    char gw[13];              // MAC 12 hex + NUL
    const char* fw_version;   // "bess-0.1.0"
    uint32_t uptime_ms, seq, ts;   // ts mentah; buildTelemetryJson yang menolkan
    int rssi;
    const char* ssid;
    char ip[16];
};

// Builds telemetry JSON per spec §6.1
// Returns number of bytes written (excluding NUL terminator), or 0 on error
size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap);

#endif
