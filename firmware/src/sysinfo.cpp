#include "sysinfo.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_system.h>
#include "config.h"
#include "wifi_mgr.h"
#include "prov.h"
#include "task_ota.h"
#include "task_auto.h"

static const char* s_reset_reason = "UNKNOWN";
static uint32_t s_boot_count = 0;
static CrashInfo s_crash{};

void sysInfoInit(const char* reset_reason, uint32_t boot_count, const CrashInfo& crash) {
    s_reset_reason = reset_reason;
    s_boot_count = boot_count;
    s_crash = crash;
}

void fillSysInfo(SysInfo& si) {
    wifiGw(si.gw);
    si.fw_version = FW_VERSION;
    si.last_reset_reason = s_reset_reason;
    si.boot_count = s_boot_count;
    si.free_heap = esp_get_free_heap_size();
    si.min_free_heap = esp_get_minimum_free_heap_size();
    si.crash = s_crash;
    si.uptime_ms = millis();
    si.ts = (uint32_t)time(nullptr);   // buildTelemetryJson yang menolkan (tsOrZero)
    si.rssi = WiFi.RSSI();
    si.ssid = wifiSsid();
    si.ap_active = provApActive();
    si.mdns = provMdnsHostname();
    snprintf(si.ip, sizeof(si.ip), "%s", WiFi.localIP().toString().c_str());
    otaGetInfo(si.ota);
    autoGetInfo(si.auto_info);
    // si.seq TIDAK diisi di sini -- lihat sysinfo.h.
}
