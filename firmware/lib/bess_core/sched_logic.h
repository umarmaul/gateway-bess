#ifndef SCHED_LOGIC_H
#define SCHED_LOGIC_H

#include <stdint.h>
#include <stddef.h>

// sched_logic -- logika MURNI sub-proyek F (jadwal harian + auto-control SOC).
// Satu window harian tersimpan per gateway (bukan array jadwal). Integrasi
// ESP32 (NVS app_cfg, task periodik yang mengantrekan command internal ke
// task_cmd) ada di src/sched.cpp + src/task_auto.cpp -- file ini hanya
// parsing/validasi/clamp dan mesin keputusan, tanpa menyentuh waktu nyata,
// NVS, atau Modbus, supaya bisa diuji native (tabel kasus lengkap, termasuk
// window lintas tengah malam dan tz).
//
// Kebijakan (spec §F, menyelaraskan keputusan owner 23 Juli "gateway tak
// pernah auto-enable sendiri" pada gateway-v2):
//   (a) Proteksi SOC otonom = DISABLE-ONLY, berlaku SELALU (dengan atau
//       tanpa jadwal aktif): BESS mengekspor (active_power_kw>0) dan
//       soc<=soc_stop_pct dan sedang running -> disable.
//   (b) Jadwal = instruksi eksplisit (bukan keputusan otonom "kapan boleh
//       nyala sendiri"): hanya bertindak saat enabled DAN waktu tersinkron
//       (ts!=0). Aksi terjadi SEKALI per transisi (edge masuk/keluar
//       window), bukan tiap siklus evaluasi -- operator boleh mematikan di
//       tengah window tanpa dinyalakan ulang terus-menerus, dan kegagalan
//       syarat (fault/comm_lost/SOC rendah) saat edge masuk window TIDAK
//       di-retry sampai window itu berakhir dan dimulai lagi.

struct SchedConfig {
    bool enabled;
    int start_min;            // menit sejak 00:00 waktu lokal (tz_offset_min), 0..1439
    int end_min;               // 0..1439; end<=start berarti window melewati tengah malam
    float soc_stop_pct;        // proteksi disable-only, 0..99
    float soc_recovery_pct;    // ambang boleh auto-enable, (soc_stop_pct+1)..100
    float power_w;             // setpoint saat ENABLE_WITH_POWER; +=ekspor (lihat commands.h planPowerPct
                                // untuk pemangkasan ±120% rated -- terjadi saat EKSEKUSI, bukan saat disimpan,
                                // karena rated power device belum tentu diketahui saat command diterima)
    int tz_offset_min;         // menit dari UTC, -720..840
};

// enabled=false, soc_stop_pct=10, soc_recovery_pct=20, power_w=0,
// tz_offset_min=0, start_min=end_min=0 (window kosong sampai diset).
SchedConfig schedDefaultConfig();

// Parse "HH:MM" (persis 5 karakter, HH 00-23, MM 00-59) -> menit 0..1439.
// false = format tak valid (out tak diubah).
bool schedParseHHMM(const char* s, int& out_min);

// Format menit 0..1439 (nilai di luar rentang dilipat modulo 1440 secara
// defensif) ke "HH:MM" + NUL. out harus >= 6 byte.
void schedFormatHHMM(int minutes, char out[6]);

// Input mentah utk set_schedule (MQTT) / POST /api/auto/config (HTTP) --
// SEMUA field opsional (partial update): has_* menandai field itu dikirim
// pemanggil; field yang tak dikirim mempertahankan nilai `cur` di
// schedApplySetInput. start_min/end_min di sini HARUS sudah lolos
// schedParseHHMM (pemanggil menolak "bad_value" sendiri kalau parse gagal --
// lihat komentar di src/task_cmd.cpp / src/web.cpp).
struct SchedSetInput {
    bool has_enabled;      bool enabled;
    bool has_start;        int start_min;
    bool has_end;          int end_min;
    bool has_soc_stop;     float soc_stop_pct;
    bool has_soc_recovery; float soc_recovery_pct;
    bool has_power;        float power_w;
    bool has_tz;           int tz_offset_min;
};

enum class SchedSetResult { ACCEPTED, CLAMPED, REJECTED };

// Terapkan `in` di atas `cur`, hasil di `out`. soc_stop_pct dipangkas ke
// 0..99; soc_recovery_pct dipangkas ke 0..100 LALU dipaksa naik ke
// (soc_stop_pct final)+1 bila masih di bawahnya (invariant recovery>stop
// selalu berlaku di config tersimpan, walau field itu sendiri tak dikirim
// pemanggil kali ini); tz_offset_min dipangkas -720..840; start_min/end_min
// dilipat modulo 1440 (jaga-jaga input di luar kontrak, mis. dari test).
// Mengembalikan CLAMPED bila ada nilai yang berubah akibat pemangkasan di
// atas, selain itu ACCEPTED. REJECTED TIDAK PERNAH dikembalikan fungsi ini
// (setiap kombinasi numerik bisa diselamatkan lewat clamp) -- dipakai
// pemanggil untuk kasus yang harus ditolak SEBELUM fungsi ini dipanggil
// (format HH:MM tak valid, power_w NaN).
SchedSetResult schedApplySetInput(const SchedConfig& cur, const SchedSetInput& in, SchedConfig& out);

// Builder ack command `set_schedule`:
// {"id","cmd":"set_schedule","result","detail","applied":{enabled,start_hhmm,
// end_hhmm,soc_stop_pct,soc_recovery_pct,power_w,tz_offset_min},"ts"}.
// ts 0 (belum sinkron) diteruskan tetap 0 (kontrak sama dengan buildAckJson).
size_t schedBuildAck(const char* id, const char* result, const char* detail,
                      const SchedConfig& applied, uint32_t ts, char* out, size_t cap);

// true kalau epoch_utc (waktu UTC nyata, BUKAN local) jatuh di dalam window
// harian cfg (setelah digeser tz_offset_min). epoch_utc==0 (belum sinkron
// NTP) -> false. start_min==end_min -> window kosong (tak pernah true) --
// konvensi dipilih sengaja, dikomentari di sched_logic.cpp.
bool schedInWindow(const SchedConfig& cfg, uint32_t epoch_utc);

// Snapshot data device yang dibutuhkan keputusan -- diisi task_auto dari
// g_state.bess (src/state.h) + waktu NTP, TANPA menyentuh state global di
// sini (supaya fungsi ini tetap murni/testable).
struct SchedInputs {
    float soc_pct;
    float active_power_kw;   // >0 = ekspor/discharge (kontrak sama dgn data.bess.active_power_kw)
    bool running;
    bool fault;
    bool comm_lost;
    uint32_t ts;              // epoch UTC; 0 = waktu belum tersinkron NTP
};

// true kalau SOC cukup DAN device sehat untuk auto-enable (dipakai juga utk
// field data.auto.battery_ready di telemetri, independen dari jadwal aktif
// atau tidak).
bool schedBatteryReady(const SchedConfig& cfg, const SchedInputs& in);

enum class SchedAction { NONE, ENABLE_WITH_POWER, DISABLE };

// Memori antar-panggilan (satu instance per gateway, dipegang task_auto) --
// dipakai semata untuk mendeteksi TRANSISI window (edge), supaya aksi
// jadwal terjadi sekali per transisi, bukan tiap siklus evaluasi (5 dtk).
struct SchedMemo {
    bool have_prev = false;   // false = evaluasi pertama (atau baru saja di-reset, lihat di bawah)
    bool prev_in_window = false;
};

// Fungsi keputusan MURNI -- dipanggil tiap siklus (task_auto, ~5 dtk).
// Urutan evaluasi (lihat komentar kebijakan (a)/(b) di atas struct):
//  1. Proteksi SOC disable-only -- SELALU dicek lebih dulu, walau jadwal
//     nonaktif atau waktu belum sinkron. Tidak menyentuh `memo`.
//  2. Kalau jadwal nonaktif: `memo.have_prev` di-reset ke false (supaya
//     saat jadwal diaktifkan lagi nanti, evaluasi berikutnya diperlakukan
//     sebagai "pertama kali" -- window saat itu dievaluasi fresh, bukan
//     dibandingkan ke status window dari sebelum jadwal dimatikan).
//  3. Kalau waktu belum sinkron (ts==0): TIDAK bertindak sama sekali, DAN
//     `memo` TIDAK disentuh (beda dari #2) -- supaya kalau waktu cuma
//     sempat desync sebentar di tengah window, status "sedang di dalam
//     window" tidak hilang dan tidak memicu enable ulang begitu waktu
//     sinkron lagi.
//  4. Evaluasi window + deteksi edge: masuk window (dan siap: SOC>=recovery,
//     !fault, !comm_lost) -> ENABLE_WITH_POWER; keluar window -> DISABLE;
//     selain edge -> NONE (termasuk kasus "gagal siap saat edge masuk" --
//     TIDAK di-retry sampai transisi berikutnya, karena `memo` tetap
//     diperbarui ke `win` walau aksinya NONE).
SchedAction schedDecide(const SchedConfig& cfg, const SchedInputs& in, SchedMemo& memo);

#endif
