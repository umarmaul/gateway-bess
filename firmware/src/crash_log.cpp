#include "crash_log.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <string.h>

static bool loadFromNvs(CrashInfo& out) {
    Preferences p;
    if (!p.begin("crash", true)) return false;
    bool ok = p.isKey("pc");
    if (ok) {
        out.present = true;
        p.getString("task", out.task, sizeof(out.task));
        out.pc = p.getUInt("pc", 0);
        out.mcause = p.getUInt("mcause", 0);
        out.boot_count = p.getUInt("boot", 0);
    }
    p.end();
    return ok;
}

void crashLogInit(uint32_t boot_count, CrashInfo& out) {
    out = CrashInfo{};
    if (esp_core_dump_image_check() == ESP_OK) {
        esp_core_dump_summary_t sum;
        if (esp_core_dump_get_summary(&sum) == ESP_OK) {
            Preferences p;
            if (p.begin("crash", false)) {
                char task[sizeof(sum.exc_task) + 1] = {};
                memcpy(task, sum.exc_task, sizeof(sum.exc_task));
                p.putString("task", task);
                p.putUInt("pc", sum.exc_pc);
                p.putUInt("mcause", sum.ex_info.mcause);
                p.putUInt("boot", boot_count);
                p.end();
                Serial.printf("[crash] coredump ditemukan: task=%s pc=0x%08lX mcause=%lu\n",
                              task, (unsigned long)sum.exc_pc,
                              (unsigned long)sum.ex_info.mcause);
                // Hapus hanya kalau ringkasannya sudah aman di NVS.
                esp_core_dump_image_erase();
            } else {
                Serial.println("[crash] coredump ada tapi NVS 'crash' tak terbuka — image dibiarkan");
            }
        } else {
            Serial.println("[crash] coredump ada tapi ringkasan gagal dibaca — image dibiarkan");
        }
    }
    loadFromNvs(out);
}
