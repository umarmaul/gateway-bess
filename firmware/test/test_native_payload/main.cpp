#include <unity.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "bess_data.h"
#include "payload.h"
#include "commands.h"
#include "reset_info.h"
#include "timeutil.h"

void setUp(void) {
}

void tearDown(void) {
}

static SysInfo sys_() {
    SysInfo s{};
    strcpy(s.gw, "AABBCCDDEEFF");
    s.fw_version = "bess-0.1.0";
    s.uptime_ms = 123456; s.seq = 7; s.ts = 1785000000;
    s.rssi = -55; s.ssid = "Lantai 2"; strcpy(s.ip, "192.168.1.50");
    s.ap_active = false; s.mdns = "bep-bess-gateway";
    return s;
}

static void test_telemetry_envelope() {
    SysInfo s = sys_();
    BessData d{};
    d.active_power_kw = 5.0f; d.soc_pct = 47.5f; d.rated_kw = 50.0f;
    d.status_raw = (1u << 6) | (1u << 15);
    d.alarm_raw[2] = 0b10;    // grid_undervoltage
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < sizeof(buf));
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF", doc["gw"]);
    TEST_ASSERT_EQUAL(7, (int)doc["seq"]);
    TEST_ASSERT_EQUAL(1, (int)doc["api_schema_version"]);
    TEST_ASSERT_EQUAL_STRING("bess", doc["data"]["device_type"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, doc["data"]["bess"]["active_power_kw"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 47.5, doc["data"]["bess"]["soc_percent"]);
    TEST_ASSERT_TRUE(doc["data"]["bess"]["running"].as<bool>());
    TEST_ASSERT_TRUE(doc["data"]["bess"]["alarms_decoded"]["grid_undervoltage"].as<bool>());
    TEST_ASSERT_FALSE(doc["data"]["bess"]["alarms_decoded"]["dc_bus_overvoltage"].as<bool>());
    TEST_ASSERT_TRUE(doc["data"]["bess"]["status_decoded"]["running"].as<bool>());
}

static void test_network_rssi_dbm() {
    // Nama field harus "rssi_dbm" — sama dengan BEPESP32_WiFi_Extension
    // (branch gateway-mqtt, makeDataJson) supaya parser cloud tidak perlu cabang.
    SysInfo s = sys_();
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonObject net = doc["data"]["network"];
    TEST_ASSERT_EQUAL(-55, (int)net["rssi_dbm"]);
    TEST_ASSERT_TRUE(net["rssi"].isNull());   // nama lama tidak boleh tersisa
}

static void test_network_ap_active_dan_mdns() {
    // data.network.ap_active (sub-proyek E, provisioning): true selama SoftAP
    // fallback menyala (STA putus, atau STA baru connect < 5 menit). mdns =
    // hostname yang sedang diiklankan (default "bep-bess-gateway", bisa diganti
    // operator lewat /api/wifi/save).
    SysInfo s = sys_();
    s.ap_active = true;
    s.mdns = "bep-bess-gateway";
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonObject net = doc["data"]["network"];
    TEST_ASSERT_TRUE(net["ap_active"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("bep-bess-gateway", net["mdns"]);

    s.ap_active = false;
    n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_FALSE(doc["data"]["network"]["ap_active"].as<bool>());
}

static void test_ts_nol_saat_ntp_belum_sinkron() {
    // Sebelum NTP sinkron, time(nullptr) mengembalikan detik sejak boot (angka
    // kecil). Kirim 0 — bukan angka kecil yang terbaca cloud sebagai tahun 1970.
    SysInfo s = sys_();
    s.ts = 8;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL(0, (int)doc["ts"]);
    TEST_ASSERT_FALSE(doc["data"]["time_valid"].as<bool>());
}

static void test_ts_diteruskan_saat_ntp_sinkron() {
    SysInfo s = sys_();
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_UINT32(1785000000u, doc["ts"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["data"]["time_valid"].as<bool>());
}

static void test_ack_ts_nol_saat_ntp_belum_sinkron() {
    Command c{};
    c.type = Command::ENABLE;
    strcpy(c.id, "z9"); strcpy(c.name, "enable");
    static char buf[512];
    size_t n = buildAckJson(c, "accepted", "", NAN, NAN, 8, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL(0, (int)doc["ts"]);
}

static void test_reset_reason_name() {
    char buf[24];
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(RESET_POWERON, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(RESET_PANIC, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(RESET_BROWNOUT, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(RESET_TASK_WDT, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("USB", resetReasonName(RESET_USB, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("JTAG", resetReasonName(RESET_JTAG, buf, sizeof(buf)));
    // Nilai tak dikenal tidak boleh hilang diam-diam
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_99", resetReasonName(99, buf, sizeof(buf)));
}

static void test_telemetry_diagnostik_boot() {
    SysInfo s = sys_();
    s.last_reset_reason = "PANIC";
    s.boot_count = 42;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("PANIC", doc["data"]["last_reset_reason"]);
    TEST_ASSERT_EQUAL(42, (int)doc["data"]["boot_count"]);
}

static void test_telemetry_last_crash_null_bila_tak_ada() {
    SysInfo s = sys_();
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    // Kunci selalu ada (skema stabil), nilainya null saat tak ada crash tercatat
    TEST_ASSERT_TRUE(doc["data"]["last_crash"].is<JsonVariantConst>());
    TEST_ASSERT_TRUE(doc["data"]["last_crash"].isNull());
}

static void test_telemetry_last_crash_terisi() {
    // Ringkasan coredump (task, PC, mcause) + boot_count saat crash ditangkap:
    // reboot misterius bisa didiagnosis dari cloud tanpa kabel serial.
    SysInfo s = sys_();
    s.crash.present = true;
    strcpy(s.crash.task, "task_cmd");
    s.crash.pc = 0x42001234u;
    s.crash.mcause = 7;
    s.crash.boot_count = 41;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonObject c = doc["data"]["last_crash"];
    TEST_ASSERT_EQUAL_STRING("task_cmd", c["task"]);
    TEST_ASSERT_EQUAL_STRING("0x42001234", c["pc"]);
    TEST_ASSERT_EQUAL(7, (int)c["mcause"]);
    TEST_ASSERT_EQUAL(41, (int)c["boot_count"]);
    TEST_ASSERT_TRUE(c["wdt_tasks"].isNull());   // bukan crash watchdog
}

static void test_telemetry_last_crash_watchdog_menyebut_task_macet() {
    // Coredump TASK_WDT merekam task yang sedang jalan (biasanya IDLE), bukan
    // yang macet. Nama task yang tak memberi makan watchdog dilaporkan terpisah.
    SysInfo s = sys_();
    s.crash.present = true;
    strcpy(s.crash.task, "IDLE");
    strcpy(s.crash.wdt_tasks, "task_cmd,loopTask");
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("task_cmd,loopTask", doc["data"]["last_crash"]["wdt_tasks"]);
}

static void test_wdt_task_list_append() {
    char buf[24] = "";
    wdtTaskListAppend(buf, sizeof(buf), "task_cmd");
    wdtTaskListAppend(buf, sizeof(buf), "loopTask");
    TEST_ASSERT_EQUAL_STRING("task_cmd,loopTask", buf);
    wdtTaskListAppend(buf, sizeof(buf), "task_bess");   // tak muat: dibuang utuh
    TEST_ASSERT_EQUAL_STRING("task_cmd,loopTask", buf);
}

static void test_telemetry_heap() {
    // Heap bebas + low-water mark ikut telemetri: kebocoran heap perlahan
    // (penyebab klasik reboot tanpa jejak) terlihat dari cloud sebelum crash.
    SysInfo s = sys_();
    s.free_heap = 301234;
    s.min_free_heap = 287000;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL(301234, (int)doc["data"]["free_heap_bytes"]);
    TEST_ASSERT_EQUAL(287000, (int)doc["data"]["min_free_heap_bytes"]);
}

static void test_telemetry_ota_default_idle() {
    // src/ selalu mengisi s.ota (task_ota mengembalikan snapshot setiap saat),
    // tapi builder tetap jujur soal fallback kalau state kosong (defensif).
    SysInfo s = sys_();
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("idle", doc["data"]["ota"]["state"]);
    TEST_ASSERT_EQUAL_STRING("", doc["data"]["ota"]["id"]);
    TEST_ASSERT_FALSE(doc["data"]["ota"]["pending_verify"].as<bool>());
}

static void test_telemetry_ota_downloading() {
    SysInfo s = sys_();
    strcpy(s.ota.state, "downloading");
    strcpy(s.ota.id, "ota-20260923-001");
    strcpy(s.ota.running_partition, "app0");
    s.ota.pending_verify = false;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("downloading", doc["data"]["ota"]["state"]);
    TEST_ASSERT_EQUAL_STRING("ota-20260923-001", doc["data"]["ota"]["id"]);
    TEST_ASSERT_EQUAL_STRING("app0", doc["data"]["ota"]["running_partition"]);
}

static void test_telemetry_ota_pending_verify() {
    SysInfo s = sys_();
    strcpy(s.ota.state, "idle");
    strcpy(s.ota.running_partition, "app1");
    s.ota.pending_verify = true;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc["data"]["ota"]["pending_verify"].as<bool>());
}

static void test_telemetry_auto_default_kosong() {
    // src/ selalu mengisi s.auto_info (task_auto mengembalikan snapshot
    // setiap saat), tapi builder tetap jujur soal fallback last_action kalau
    // string-nya kosong (defensif, sama pola dengan data.ota.state).
    SysInfo s = sys_();
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonObject au = doc["data"]["auto"];
    TEST_ASSERT_FALSE(au["schedule_enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("none", au["last_action"]);
    TEST_ASSERT_EQUAL(0, (int)au["last_action_ts"]);
}

static void test_telemetry_auto_terisi() {
    SysInfo s = sys_();
    s.auto_info.schedule_enabled = true;
    strcpy(s.auto_info.start_hhmm, "17:00");
    strcpy(s.auto_info.end_hhmm, "21:00");
    s.auto_info.tz_offset_min = 420;
    s.auto_info.power_w = 1500.0f;
    s.auto_info.soc_stop_pct = 10.0f;
    s.auto_info.soc_recovery_pct = 20.0f;
    s.auto_info.in_window = true;
    s.auto_info.battery_ready = true;
    strcpy(s.auto_info.last_action, "enable_with_power");
    s.auto_info.last_action_ts = 1785000005;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonObject au = doc["data"]["auto"];
    TEST_ASSERT_TRUE(au["schedule_enabled"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("17:00", au["start_hhmm"]);
    TEST_ASSERT_EQUAL_STRING("21:00", au["end_hhmm"]);
    TEST_ASSERT_EQUAL(420, (int)au["tz_offset_min"]);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1500, au["power_w"]);
    TEST_ASSERT_TRUE(au["in_window"].as<bool>());
    TEST_ASSERT_TRUE(au["battery_ready"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("enable_with_power", au["last_action"]);
    TEST_ASSERT_EQUAL_UINT32(1785000005u, au["last_action_ts"].as<uint32_t>());
}

static void test_parse_enable_dengan_power_w() {
    // Jadwal mengirim enable + power_w sebagai SATU command internal supaya
    // tak ada command lain yang menyelip di antara tulis daya dan enable.
    Command c;
    const char* j = "{\"id\":\"auto-1\",\"cmd\":\"enable\",\"args\":{\"power_w\":20000}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::ENABLE, c.type);
    TEST_ASSERT_TRUE(c.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 20000.0f, c.power_w);
    const char* j2 = "{\"cmd\":\"enable\"}";
    parseCommand(j2, strlen(j2), c);
    TEST_ASSERT_FALSE(c.has_power);
}

static void test_parse_enable() {
    Command c;
    const char* j = "{\"id\":\"a1\",\"cmd\":\"enable\",\"args\":{}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::ENABLE, c.type);
    TEST_ASSERT_EQUAL_STRING("a1", c.id);
}

static void test_parse_set_power() {
    Command c;
    const char* j = "{\"id\":\"b2\",\"cmd\":\"set_power\",\"args\":{\"power_w\":5000}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_POWER, c.type);
    TEST_ASSERT_TRUE(c.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, c.power_w);
}

static void test_parse_set_output_nama_resmi() {
    Command c;
    const char* j = "{\"id\":\"c3\",\"cmd\":\"set_output\",\"args\":{\"power_w\":5000}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_POWER, c.type);
    TEST_ASSERT_TRUE(c.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, c.power_w);
    // ack harus menggemakan nama yang dikirim cloud, bukan nama internal
    TEST_ASSERT_EQUAL_STRING("set_output", c.name);
}

static void test_parse_target_default_satu() {
    Command c;
    const char* j = "{\"id\":\"d4\",\"cmd\":\"enable\",\"args\":{}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL_UINT32(1u, c.target);
}

static void test_parse_target_eksplisit() {
    Command c;
    const char* j = "{\"id\":\"e5\",\"cmd\":\"enable\",\"args\":{\"target\":2}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL_UINT32(2u, c.target);
}

static void test_parse_unsupported_dan_bad_json() {
    Command c;
    const char* j = "{\"id\":\"x\",\"cmd\":\"fly\"}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::UNSUPPORTED, c.type);
    parseCommand("{oops", 5, c);
    TEST_ASSERT_EQUAL(Command::BAD_JSON, c.type);
}

static void test_ack() {
    Command c{};
    c.type = Command::SET_POWER;
    strcpy(c.id, "b2"); strcpy(c.name, "set_power");
    static char buf[512];
    size_t n = buildAckJson(c, "accepted", "", 10.0f, 5000.0f, 1785000001, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL_STRING("b2", doc["id"]);
    TEST_ASSERT_EQUAL_STRING("accepted", doc["result"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, doc["applied"]["power_pct"]);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, doc["applied"]["power_w"]);
}

static void test_telemetry_buffer_too_small() {
    SysInfo s = sys_();
    BessData d{};
    d.active_power_kw = 5.0f; d.soc_pct = 47.5f; d.rated_kw = 50.0f;
    d.status_raw = (1u << 6) | (1u << 15);
    d.alarm_raw[2] = 0b10;
    static char small[256];  // payload terukur ~3319 byte, buffer ini pasti kurang
    size_t n = buildTelemetryJson(s, d, small, sizeof(small));
    TEST_ASSERT_EQUAL(0, n);  // harus return 0, bukan cap
}

static void test_plan_power_dalam_rentang() {
    float pct = 0; bool clamped = true;
    TEST_ASSERT_TRUE(planPowerPct(5000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, pct);
    TEST_ASSERT_FALSE(clamped);
}

static void test_plan_power_dipangkas_atas() {
    float pct = 0; bool clamped = false;
    TEST_ASSERT_TRUE(planPowerPct(70000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 120.0, pct);
    TEST_ASSERT_TRUE(clamped);
}

static void test_plan_power_dipangkas_bawah() {
    float pct = 0; bool clamped = false;
    TEST_ASSERT_TRUE(planPowerPct(-70000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, -120.0, pct);
    TEST_ASSERT_TRUE(clamped);
}

static void test_plan_power_rated_belum_diketahui() {
    float pct = 0; bool clamped = false;
    // rated 0 = comm_lost sejak boot; tidak ada acuan untuk memangkas
    TEST_ASSERT_FALSE(planPowerPct(5000.0f, 0.0f, pct, clamped));
}

static void test_plan_power_nan_ditolak() {
    float pct = 0; bool clamped = false;
    // power_w NaN tidak boleh lolos jadi pct NaN yang berujung UB di lroundf
    TEST_ASSERT_FALSE(planPowerPct(NAN, 50000.0f, pct, clamped));
}

static void test_plan_power_tepat_di_batas_bukan_clamp() {
    float pct = 0; bool clamped = true;
    // 60000/50000*100 = 120 persis; perbandingan ketat > / < jadi ini BUKAN clamp
    TEST_ASSERT_TRUE(planPowerPct(60000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 120.0, pct);
    TEST_ASSERT_FALSE(clamped);
}

static void test_parse_command_truncation() {
    Command c;
    // id lebih panjang dari 39 char (sizeof c.id = 40, jadi max 39 + NUL)
    const char* j_long_id = "{\"id\":\"aaaaaaaaaa_bbbbbbbbbb_cccccccccc_dddddddd\",\"cmd\":\"enable\",\"args\":{}}";
    parseCommand(j_long_id, strlen(j_long_id), c);
    TEST_ASSERT_EQUAL(Command::ENABLE, c.type);
    TEST_ASSERT_EQUAL(39, (int)strlen(c.id));  // ter-truncate di 39 char

    // cmd lebih panjang dari 23 char (sizeof c.name = 24, jadi max 23 + NUL)
    const char* j_long_cmd = "{\"id\":\"x\",\"cmd\":\"very_long_command_name_xyzz\",\"args\":{}}";
    parseCommand(j_long_cmd, strlen(j_long_cmd), c);
    TEST_ASSERT_EQUAL(Command::UNSUPPORTED, c.type);  // unknown command → UNSUPPORTED
    TEST_ASSERT_EQUAL(23, (int)strlen(c.name));  // ter-truncate di 23 char
}

static void test_time_after_biasa() {
    TEST_ASSERT_TRUE(timeAfter(1000u, 500u));
    TEST_ASSERT_TRUE(timeAfter(500u, 500u));     // tepat di batas = sudah waktunya
    TEST_ASSERT_FALSE(timeAfter(499u, 500u));
}

static void test_time_after_rollover() {
    // millis() berputar di hari ke-49. deadline dekat 0xFFFFFFFF, now sudah
    // berputar ke angka kecil: perbandingan biasa (now >= deadline) salah.
    TEST_ASSERT_TRUE(timeAfter(10u, 0xFFFFFF00u));
    TEST_ASSERT_FALSE(timeAfter(0xFFFFFF00u, 10u));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_envelope);
    RUN_TEST(test_reset_reason_name);
    RUN_TEST(test_telemetry_diagnostik_boot);
    RUN_TEST(test_telemetry_heap);
    RUN_TEST(test_telemetry_last_crash_null_bila_tak_ada);
    RUN_TEST(test_telemetry_last_crash_terisi);
    RUN_TEST(test_telemetry_last_crash_watchdog_menyebut_task_macet);
    RUN_TEST(test_wdt_task_list_append);
    RUN_TEST(test_network_rssi_dbm);
    RUN_TEST(test_network_ap_active_dan_mdns);
    RUN_TEST(test_telemetry_ota_default_idle);
    RUN_TEST(test_telemetry_ota_downloading);
    RUN_TEST(test_telemetry_ota_pending_verify);
    RUN_TEST(test_ts_nol_saat_ntp_belum_sinkron);
    RUN_TEST(test_ts_diteruskan_saat_ntp_sinkron);
    RUN_TEST(test_ack_ts_nol_saat_ntp_belum_sinkron);
    RUN_TEST(test_telemetry_auto_default_kosong);
    RUN_TEST(test_telemetry_auto_terisi);
    RUN_TEST(test_parse_enable);
    RUN_TEST(test_parse_enable_dengan_power_w);
    RUN_TEST(test_parse_set_power);
    RUN_TEST(test_parse_set_output_nama_resmi);
    RUN_TEST(test_parse_target_default_satu);
    RUN_TEST(test_parse_target_eksplisit);
    RUN_TEST(test_plan_power_dalam_rentang);
    RUN_TEST(test_plan_power_dipangkas_atas);
    RUN_TEST(test_plan_power_dipangkas_bawah);
    RUN_TEST(test_plan_power_rated_belum_diketahui);
    RUN_TEST(test_plan_power_nan_ditolak);
    RUN_TEST(test_plan_power_tepat_di_batas_bukan_clamp);
    RUN_TEST(test_parse_unsupported_dan_bad_json);
    RUN_TEST(test_ack);
    RUN_TEST(test_telemetry_buffer_too_small);
    RUN_TEST(test_parse_command_truncation);
    RUN_TEST(test_time_after_biasa);
    RUN_TEST(test_time_after_rollover);
    return UNITY_END();
}
