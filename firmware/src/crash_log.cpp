#include "crash_log.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_core_dump.h>
#include <esp_task_wdt.h>
#include <string.h>

// Daftar task yang tak memberi makan watchdog, ditulis dari ISR TWDT sesaat
// sebelum panic. RTC_NOINIT bertahan melewati reset software/panic (tapi tidak
// power-on), sehingga boot berikutnya bisa membacanya. Coredump TASK_WDT sendiri
// merekam task yang SEDANG jalan saat interrupt (biasanya IDLE), bukan yang macet.
#define WDT_MAGIC 0xB355D0C5u
static RTC_NOINIT_ATTR uint32_t s_wdt_magic;
static RTC_NOINIT_ATTR char s_wdt_tasks[sizeof(CrashInfo::wdt_tasks)];

static void onWdtLine(void*, const char* msg) {
    // esp_task_wdt_print_triggered_tasks memanggil ini per potong teks:
    // judul, "\n - ", nama task, " (CPU n)". Yang disimpan hanya nama task.
    if (msg[0] == '\n' || msg[0] == ' ' || !strncmp(msg, "Task watchdog", 13)) return;
    wdtTaskListAppend(s_wdt_tasks, sizeof(s_wdt_tasks), msg);
}

extern "C" void esp_task_wdt_isr_user_handler(void) {
    s_wdt_tasks[0] = 0;
    esp_task_wdt_print_triggered_tasks(onWdtLine, nullptr, nullptr);
    s_wdt_magic = WDT_MAGIC;
}

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
        p.getString("wdt", out.wdt_tasks, sizeof(out.wdt_tasks));
    }
    p.end();
    return ok;
}

static bool saveToNvs(const char* task, uint32_t pc, uint32_t mcause,
                      uint32_t boot, const char* wdt) {
    Preferences p;
    if (!p.begin("crash", false)) return false;
    p.putString("task", task);
    p.putUInt("pc", pc);
    p.putUInt("mcause", mcause);
    p.putUInt("boot", boot);
    p.putString("wdt", wdt);
    p.end();
    return true;
}

void crashLogInit(uint32_t boot_count, bool reset_by_task_wdt, CrashInfo& out) {
    out = CrashInfo{};
    char wdt[sizeof(s_wdt_tasks)] = "";
    if (reset_by_task_wdt && s_wdt_magic == WDT_MAGIC) {
        memcpy(wdt, s_wdt_tasks, sizeof(wdt));
        wdt[sizeof(wdt) - 1] = 0;
        Serial.printf("[crash] task watchdog: task macet = %s\n", wdt[0] ? wdt : "?");
    }
    s_wdt_magic = 0;   // sekali pakai: jangan terbaca lagi di reset berikutnya

    bool saved = false;
    if (esp_core_dump_image_check() == ESP_OK) {
        esp_core_dump_summary_t sum;
        if (esp_core_dump_get_summary(&sum) == ESP_OK) {
            char task[sizeof(sum.exc_task) + 1] = {};
            memcpy(task, sum.exc_task, sizeof(sum.exc_task));
            Serial.printf("[crash] coredump ditemukan: task=%s pc=0x%08lX mcause=%lu\n",
                          task, (unsigned long)sum.exc_pc,
                          (unsigned long)sum.ex_info.mcause);
            saved = saveToNvs(task, sum.exc_pc, sum.ex_info.mcause, boot_count, wdt);
            // Hapus hanya kalau ringkasannya sudah aman di NVS.
            if (saved) esp_core_dump_image_erase();
            else Serial.println("[crash] NVS 'crash' tak terbuka — image coredump dibiarkan");
        } else {
            Serial.println("[crash] coredump ada tapi ringkasan gagal dibaca — image dibiarkan");
        }
    }
    // Reset TASK_WDT tanpa coredump yang terbaca: nama task macet tetap dicatat.
    if (!saved && wdt[0]) saveToNvs("?", 0, 0, boot_count, wdt);
    loadFromNvs(out);
}
