#include <unity.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "sched_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

// Epoch dasar: 2023-01-01T00:00:00Z (`date -u -d '2023-01-01 00:00:00' +%s`).
// Dipakai supaya kasus uji bisa ditulis sebagai "jam ke-N UTC pada hari itu"
// tanpa peduli tanggal kalender sungguhan -- yang penting epoch % 86400.
#define BASE 1672531200u
static uint32_t tsH(int hour) { return BASE + (uint32_t)hour * 3600u; }

static SchedConfig windowCfg(int start_min, int end_min, int tz = 0) {
    SchedConfig c = schedDefaultConfig();
    c.enabled = true;
    c.start_min = start_min;
    c.end_min = end_min;
    c.tz_offset_min = tz;
    c.soc_recovery_pct = 20.0f;
    c.soc_stop_pct = 10.0f;
    return c;
}

// ---------------------------------------------------------------------------
// schedParseHHMM / schedFormatHHMM
// ---------------------------------------------------------------------------
static void test_parse_hhmm_valid() {
    int m;
    TEST_ASSERT_TRUE(schedParseHHMM("17:00", m));
    TEST_ASSERT_EQUAL(1020, m);
    TEST_ASSERT_TRUE(schedParseHHMM("00:00", m));
    TEST_ASSERT_EQUAL(0, m);
    TEST_ASSERT_TRUE(schedParseHHMM("23:59", m));
    TEST_ASSERT_EQUAL(1439, m);
}

static void test_parse_hhmm_invalid() {
    int m = -1;
    TEST_ASSERT_FALSE(schedParseHHMM("24:00", m));    // jam di luar rentang
    TEST_ASSERT_FALSE(schedParseHHMM("17:60", m));    // menit di luar rentang
    TEST_ASSERT_FALSE(schedParseHHMM("1700", m));     // tanpa titik dua
    TEST_ASSERT_FALSE(schedParseHHMM("7:00", m));     // jam 1 digit -- format ketat HH:MM
    TEST_ASSERT_FALSE(schedParseHHMM("17:0", m));     // kurang 1 digit
    TEST_ASSERT_FALSE(schedParseHHMM("", m));
    TEST_ASSERT_FALSE(schedParseHHMM(nullptr, m));
    TEST_ASSERT_FALSE(schedParseHHMM("ab:cd", m));
}

static void test_format_hhmm() {
    char buf[6];
    schedFormatHHMM(0, buf);
    TEST_ASSERT_EQUAL_STRING("00:00", buf);
    schedFormatHHMM(1439, buf);
    TEST_ASSERT_EQUAL_STRING("23:59", buf);
    schedFormatHHMM(1020, buf);
    TEST_ASSERT_EQUAL_STRING("17:00", buf);
    schedFormatHHMM(1440, buf);           // defensif: lipat modulo 1440
    TEST_ASSERT_EQUAL_STRING("00:00", buf);
    schedFormatHHMM(-60, buf);            // defensif: negatif dilipat juga
    TEST_ASSERT_EQUAL_STRING("23:00", buf);
}

// ---------------------------------------------------------------------------
// schedDefaultConfig
// ---------------------------------------------------------------------------
static void test_default_config() {
    SchedConfig c = schedDefaultConfig();
    TEST_ASSERT_FALSE(c.enabled);
    TEST_ASSERT_EQUAL(0, c.start_min);
    TEST_ASSERT_EQUAL(0, c.end_min);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0f, c.soc_stop_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 20.0f, c.soc_recovery_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0f, c.power_w);
    TEST_ASSERT_EQUAL(0, c.tz_offset_min);
}

// ---------------------------------------------------------------------------
// schedInWindow -- normal, lintas tengah malam, tz, ts=0, window kosong
// ---------------------------------------------------------------------------
static void test_in_window_normal() {
    SchedConfig c = windowCfg(1020, 1260);   // 17:00..21:00, tz=0
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(18)));     // 18:00 -> di dalam
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(22)));    // 22:00 -> di luar
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(17)));     // tepat start -> inklusif
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(21)));    // tepat end -> eksklusif
}

static void test_in_window_lintas_tengah_malam() {
    SchedConfig c = windowCfg(1320, 360);    // 22:00..06:00 (wrap)
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(23)));     // 23:00 -> di dalam (sisi malam)
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(2)));      // 02:00 -> di dalam (sisi pagi)
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(12)));    // siang -> di luar
}

static void test_in_window_tz_offset_positif() {
    // WIB (+420 menit = +7 jam). 11:00 UTC == 18:00 WIB, di dalam window
    // lokal 17:00..21:00.
    SchedConfig c = windowCfg(1020, 1260, 420);
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(11)));
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(20)));    // 20:00 UTC == 03:00 WIB (+1 hari) -> di luar
}

static void test_in_window_tz_offset_negatif() {
    // UTC-5. 15:00 UTC == 10:00 lokal, di dalam window lokal 09:00..11:00.
    SchedConfig c = windowCfg(540, 660, -300);
    TEST_ASSERT_TRUE(schedInWindow(c, tsH(15)));
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(20)));
}

static void test_in_window_ts_nol() {
    SchedConfig c = windowCfg(0, 1439);      // window sepanjang hari (hampir)
    TEST_ASSERT_FALSE(schedInWindow(c, 0));  // waktu belum sinkron -> selalu false
}

static void test_in_window_start_sama_end_kosong() {
    SchedConfig c = windowCfg(600, 600);     // start==end -> window kosong (konvensi)
    TEST_ASSERT_FALSE(schedInWindow(c, tsH(10)));
}

// ---------------------------------------------------------------------------
// schedBatteryReady
// ---------------------------------------------------------------------------
static void test_battery_ready() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.comm_lost = false;
    TEST_ASSERT_TRUE(schedBatteryReady(c, in));
    in.soc_pct = 19.9f;
    TEST_ASSERT_FALSE(schedBatteryReady(c, in));    // di bawah recovery (20)
    in.soc_pct = 25.0f; in.fault = true;
    TEST_ASSERT_FALSE(schedBatteryReady(c, in));
    in.fault = false; in.comm_lost = true;
    TEST_ASSERT_FALSE(schedBatteryReady(c, in));
}

// ---------------------------------------------------------------------------
// schedDecide -- siklus penuh: edge masuk -> tetap di dalam (no-op) -> edge keluar
// ---------------------------------------------------------------------------
static void test_decide_siklus_normal() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18);   // masuk window, evaluasi pertama, siap -> enable
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.have_prev);
    TEST_ASSERT_TRUE(memo.prev_in_window);

    in.ts = tsH(19);   // masih di dalam window, bukan edge -> tidak retry
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));

    in.ts = tsH(22);   // keluar window -> disable, sekali
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.prev_in_window);

    in.ts = tsH(23);   // masih di luar -> tidak retry disable
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
}

// TEMUAN REVIEW 23 Sep 2026 (RENDAH): dulu "gagal siap saat edge TIDAK
// PERNAH di-retry sampai window berikutnya" -- sekarang di-retry tiap siklus
// selama masih di window yang sama (`memo.pending_enable`), lihat
// sched_logic.h. Nama test tetap merujuk kasus lama (fault saat edge) tapi
// assersi diperbarui ke perilaku retry baru.
static void test_decide_fault_saat_edge_lalu_retry_setelah_pulih() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.soc_pct = 25.0f; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18); in.fault = true;      // edge masuk, tapi fault -> belum enable, pending disimpan
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.prev_in_window); // window tetap ditandai "sudah di dalam"
    TEST_ASSERT_TRUE(memo.pending_enable);

    in.ts = tsH(19); in.fault = false;     // fault hilang, bukan edge lagi TAPI pending -> DI-RETRY
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);

    in.ts = tsH(20);                       // sudah enable sekali di window ini -> TIDAK retry lagi
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));

    in.ts = tsH(22);                       // keluar window -> disable (transisi berikutnya)
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo));

    in.ts = tsH(24 + 17); in.soc_pct = 25.0f;  // hari berikutnya, window baru -> fresh edge, siap -> enable
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
}

static void test_decide_comm_lost_saat_edge_lalu_retry_setelah_pulih() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18); in.comm_lost = true;
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    in.ts = tsH(19); in.comm_lost = false;   // pulih, bukan edge, TAPI pending -> retry berhasil
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));

    in.ts = tsH(20);                          // sudah enable -> tidak retry lagi
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
}

// Retry berbasis SOC murni (bukan fault/comm_lost) -- SOC naik pelan-pelan
// ke ambang recovery di dalam window yang sama.
static void test_decide_retry_saat_soc_naik_ke_recovery_dalam_window() {
    SchedConfig c = windowCfg(1020, 1260);   // soc_recovery_pct=20 (default helper)
    SchedMemo memo{};
    SchedInputs in{};
    in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18); in.soc_pct = 15.0f;        // edge masuk, SOC di bawah recovery -> pending
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    in.ts = tsH(18) + 1800; in.soc_pct = 18.0f;  // 30 menit kemudian, masih belum cukup
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    in.ts = tsH(19); in.soc_pct = 20.0f;        // SOC cukup (>=recovery) -> retry berhasil
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);
}

// Setelah enable pernah terjadi di window ini, jadwal TIDAK menyalakan ulang
// -- operator boleh mematikan BESS manual di tengah window.
static void test_decide_tidak_retry_setelah_enable_sekali_di_window() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18);   // edge masuk, langsung siap -> enable sekali
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);

    // "operator mematikan manual" tidak dimodelkan di level ini (schedDecide
    // tidak tahu status ON/OFF BESS sebenarnya) -- yang diuji: TANPA edge
    // baru dan TANPA pending, jadwal tidak pernah mengirim ENABLE lagi
    // selama masih di window yang sama, berapa kali pun dievaluasi.
    in.ts = tsH(19);
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    in.ts = tsH(20);
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
}

// Pending harus hilang tepat saat keluar window, walau tak pernah sempat
// enable sama sekali di window itu.
static void test_decide_pending_hilang_saat_keluar_window() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18); in.soc_pct = 5.0f;    // edge masuk, tak pernah siap -> pending disimpan
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    in.ts = tsH(22); in.soc_pct = 5.0f;    // keluar window -> disable, pending DIHAPUS
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);
}

// Pending harus hilang saat jadwal dinonaktifkan di tengah "menunggu siap".
static void test_decide_pending_hilang_saat_jadwal_dinonaktifkan() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;

    in.ts = tsH(18); in.soc_pct = 5.0f;
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    c.enabled = false;
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);
    TEST_ASSERT_FALSE(memo.have_prev);
}

// Proteksi SOC disable-only yang memicu DISABLE juga menghapus pending.
static void test_decide_proteksi_soc_menghapus_pending() {
    SchedConfig c = windowCfg(1020, 1260);   // soc_stop_pct=10 (default helper)
    SchedMemo memo{};
    SchedInputs in{};
    in.fault = false; in.comm_lost = false;

    // Masuk window belum siap -- TANPA ekspor/running dulu supaya proteksi
    // SOC tidak ikut memicu di tick ini (murni menguji jalur pending jadwal).
    in.running = false; in.active_power_kw = 0.0f;
    in.ts = tsH(18); in.soc_pct = 5.0f;
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_TRUE(memo.pending_enable);

    // Sekarang proteksi SOC memicu (running+exporting+soc rendah).
    in.running = true; in.active_power_kw = 5.0f;
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.pending_enable);
}

static void test_decide_jadwal_nonaktif_tidak_bertindak() {
    SchedConfig c = windowCfg(1020, 1260);
    c.enabled = false;
    SchedMemo memo{};
    memo.have_prev = true; memo.prev_in_window = true;   // sisa dari evaluasi sebelumnya
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;
    in.ts = tsH(18);      // waktu valid, di dalam window kalau saja enabled -- tapi tidak

    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    TEST_ASSERT_FALSE(memo.have_prev);    // direset -- evaluasi berikutnya (saat enabled) dianggap "pertama kali"

    c.enabled = true;      // operator aktifkan sekarang, masih di jam yg sama (di dalam window)
    TEST_ASSERT_TRUE(SchedAction::ENABLE_WITH_POWER == schedDecide(c, in, memo));
}

static void test_decide_waktu_belum_sinkron_tidak_bertindak_dan_memo_utuh() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    memo.have_prev = true; memo.prev_in_window = true;
    SchedInputs in{};
    in.soc_pct = 25.0f; in.fault = false; in.comm_lost = false; in.running = false; in.active_power_kw = 0.0f;
    in.ts = 0;    // belum sinkron

    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
    // BEDA dari kasus jadwal nonaktif: memo TIDAK direset (waktu cuma
    // sempat desync, bukan operator mematikan jadwal).
    TEST_ASSERT_TRUE(memo.have_prev);
    TEST_ASSERT_TRUE(memo.prev_in_window);
}

static void test_decide_proteksi_soc_disable_only() {
    SchedConfig c = windowCfg(1020, 1260);   // soc_stop=10 (default helper)
    SchedMemo memo{};
    SchedInputs in{};
    in.active_power_kw = 5.0f;    // ekspor
    in.soc_pct = 8.0f;            // <= soc_stop
    in.running = true;
    in.fault = false; in.comm_lost = false;
    in.ts = tsH(12);              // di LUAR window -- proteksi tetap berlaku ("walau jadwal off")
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo));

    // Berlaku juga saat jadwal nonaktif total & waktu belum sinkron.
    c.enabled = false;
    in.ts = 0;
    SchedMemo memo2{};
    TEST_ASSERT_TRUE(SchedAction::DISABLE == schedDecide(c, in, memo2));
}

static void test_decide_soc_rendah_saat_charging_tidak_disable() {
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.active_power_kw = -5.0f;   // charging (impor), bukan ekspor
    in.soc_pct = 3.0f;            // sangat rendah
    in.running = true;
    in.fault = false; in.comm_lost = false;
    in.ts = tsH(12);              // di luar window juga -- jadwal tak bertindak
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
}

static void test_decide_proteksi_soc_butuh_running() {
    // Ekspor + SOC rendah tapi BESS sudah tidak running (sudah off) -- tidak
    // perlu kirim disable lagi.
    SchedConfig c = windowCfg(1020, 1260);
    SchedMemo memo{};
    SchedInputs in{};
    in.active_power_kw = 5.0f;
    in.soc_pct = 5.0f;
    in.running = false;
    in.fault = false; in.comm_lost = false;
    in.ts = tsH(12);
    TEST_ASSERT_TRUE(SchedAction::NONE == schedDecide(c, in, memo));
}

// ---------------------------------------------------------------------------
// schedApplySetInput -- partial update + clamp
// ---------------------------------------------------------------------------
static void test_apply_partial_update_retain() {
    SchedConfig cur = schedDefaultConfig();
    cur.enabled = true; cur.start_min = 1020; cur.end_min = 1260;
    cur.soc_stop_pct = 10; cur.soc_recovery_pct = 20; cur.power_w = 1500; cur.tz_offset_min = 420;

    SchedSetInput in{};
    in.has_power = true; in.power_w = 2000;   // hanya ubah power_w
    SchedConfig out{};
    SchedSetResult r = schedApplySetInput(cur, in, out);
    TEST_ASSERT_TRUE(SchedSetResult::ACCEPTED == r);
    TEST_ASSERT_TRUE(out.enabled);
    TEST_ASSERT_EQUAL(1020, out.start_min);
    TEST_ASSERT_EQUAL(1260, out.end_min);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10, out.soc_stop_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 20, out.soc_recovery_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2000, out.power_w);
    TEST_ASSERT_EQUAL(420, out.tz_offset_min);
}

static void test_apply_semua_field_dalam_rentang_diterima() {
    SchedConfig cur = schedDefaultConfig();
    SchedSetInput in{};
    in.has_enabled = true; in.enabled = true;
    in.has_start = true; in.start_min = 1020;
    in.has_end = true; in.end_min = 1260;
    in.has_soc_stop = true; in.soc_stop_pct = 10;
    in.has_soc_recovery = true; in.soc_recovery_pct = 20;
    in.has_power = true; in.power_w = 1500;
    in.has_tz = true; in.tz_offset_min = 420;
    SchedConfig out{};
    SchedSetResult r = schedApplySetInput(cur, in, out);
    TEST_ASSERT_TRUE(SchedSetResult::ACCEPTED == r);
}

static void test_apply_clamp_soc_stop_atas_bawah() {
    SchedConfig cur = schedDefaultConfig();
    SchedSetInput in{};
    in.has_soc_stop = true; in.soc_stop_pct = 150;
    SchedConfig out{};
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 99, out.soc_stop_pct);

    in.soc_stop_pct = -5;
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0, out.soc_stop_pct);
}

static void test_apply_recovery_dipaksa_di_atas_stop_walau_tak_dikirim() {
    SchedConfig cur = schedDefaultConfig();
    cur.soc_stop_pct = 10; cur.soc_recovery_pct = 20;
    SchedSetInput in{};
    in.has_soc_stop = true; in.soc_stop_pct = 50;   // recovery TIDAK dikirim (retained 20)
    SchedConfig out{};
    SchedSetResult r = schedApplySetInput(cur, in, out);
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == r);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 50, out.soc_stop_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 51, out.soc_recovery_pct);   // dipaksa naik ke stop+1
}

static void test_apply_recovery_clamp_rentang_dan_batas_bawah_stop() {
    SchedConfig cur = schedDefaultConfig();
    cur.soc_stop_pct = 10; cur.soc_recovery_pct = 20;
    SchedSetInput in{};
    in.has_soc_recovery = true; in.soc_recovery_pct = 200;   // di luar 0..100
    SchedConfig out{};
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 100, out.soc_recovery_pct);

    in.soc_recovery_pct = 5;    // valid range tapi < stop(10)+1
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 11, out.soc_recovery_pct);
}

static void test_apply_clamp_tz() {
    SchedConfig cur = schedDefaultConfig();
    SchedSetInput in{};
    in.has_tz = true; in.tz_offset_min = 1000;
    SchedConfig out{};
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_EQUAL(840, out.tz_offset_min);

    in.tz_offset_min = -800;
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_EQUAL(-720, out.tz_offset_min);
}

static void test_apply_start_end_dilipat_modulo_defensif() {
    SchedConfig cur = schedDefaultConfig();
    SchedSetInput in{};
    in.has_start = true; in.start_min = 1500;   // di luar 0..1439 (defensif, seharusnya sudah difilter caller)
    SchedConfig out{};
    TEST_ASSERT_TRUE(SchedSetResult::CLAMPED == schedApplySetInput(cur, in, out));
    TEST_ASSERT_EQUAL(60, out.start_min);
}

// ---------------------------------------------------------------------------
// schedBuildAck
// ---------------------------------------------------------------------------
static void test_build_ack() {
    SchedConfig applied = schedDefaultConfig();
    applied.enabled = true; applied.start_min = 1020; applied.end_min = 1260;
    applied.soc_stop_pct = 10; applied.soc_recovery_pct = 20; applied.power_w = 1500;
    applied.tz_offset_min = 420;
    static char buf[512];
    size_t n = schedBuildAck("s1", "accepted", "", applied, 1785000001, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("s1", doc["id"]);
    TEST_ASSERT_EQUAL_STRING("set_schedule", doc["cmd"]);
    TEST_ASSERT_EQUAL_STRING("accepted", doc["result"]);
    JsonObject ap = doc["applied"];
    TEST_ASSERT_TRUE(ap["enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("17:00", ap["start_hhmm"]);
    TEST_ASSERT_EQUAL_STRING("21:00", ap["end_hhmm"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10, ap["soc_stop_pct"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 20, ap["soc_recovery_pct"]);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1500, ap["power_w"]);
    TEST_ASSERT_EQUAL(420, (int)ap["tz_offset_min"]);
    TEST_ASSERT_EQUAL_UINT32(1785000001u, doc["ts"].as<uint32_t>());
}

static void test_build_ack_ts_nol_saat_ntp_belum_sinkron() {
    SchedConfig applied = schedDefaultConfig();
    static char buf[512];
    size_t n = schedBuildAck("s2", "rejected", "bad_hhmm", applied, 8, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL(0, (int)doc["ts"]);
    TEST_ASSERT_EQUAL_STRING("bad_hhmm", doc["detail"]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_hhmm_valid);
    RUN_TEST(test_parse_hhmm_invalid);
    RUN_TEST(test_format_hhmm);
    RUN_TEST(test_default_config);
    RUN_TEST(test_in_window_normal);
    RUN_TEST(test_in_window_lintas_tengah_malam);
    RUN_TEST(test_in_window_tz_offset_positif);
    RUN_TEST(test_in_window_tz_offset_negatif);
    RUN_TEST(test_in_window_ts_nol);
    RUN_TEST(test_in_window_start_sama_end_kosong);
    RUN_TEST(test_battery_ready);
    RUN_TEST(test_decide_siklus_normal);
    RUN_TEST(test_decide_fault_saat_edge_lalu_retry_setelah_pulih);
    RUN_TEST(test_decide_comm_lost_saat_edge_lalu_retry_setelah_pulih);
    RUN_TEST(test_decide_retry_saat_soc_naik_ke_recovery_dalam_window);
    RUN_TEST(test_decide_tidak_retry_setelah_enable_sekali_di_window);
    RUN_TEST(test_decide_pending_hilang_saat_keluar_window);
    RUN_TEST(test_decide_pending_hilang_saat_jadwal_dinonaktifkan);
    RUN_TEST(test_decide_proteksi_soc_menghapus_pending);
    RUN_TEST(test_decide_jadwal_nonaktif_tidak_bertindak);
    RUN_TEST(test_decide_waktu_belum_sinkron_tidak_bertindak_dan_memo_utuh);
    RUN_TEST(test_decide_proteksi_soc_disable_only);
    RUN_TEST(test_decide_soc_rendah_saat_charging_tidak_disable);
    RUN_TEST(test_decide_proteksi_soc_butuh_running);
    RUN_TEST(test_apply_partial_update_retain);
    RUN_TEST(test_apply_semua_field_dalam_rentang_diterima);
    RUN_TEST(test_apply_clamp_soc_stop_atas_bawah);
    RUN_TEST(test_apply_recovery_dipaksa_di_atas_stop_walau_tak_dikirim);
    RUN_TEST(test_apply_recovery_clamp_rentang_dan_batas_bawah_stop);
    RUN_TEST(test_apply_clamp_tz);
    RUN_TEST(test_apply_start_end_dilipat_modulo_defensif);
    RUN_TEST(test_build_ack);
    RUN_TEST(test_build_ack_ts_nol_saat_ntp_belum_sinkron);
    return UNITY_END();
}
