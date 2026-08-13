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
    RUN_TEST(test_network_rssi_dbm);
    RUN_TEST(test_ts_nol_saat_ntp_belum_sinkron);
    RUN_TEST(test_ts_diteruskan_saat_ntp_sinkron);
    RUN_TEST(test_ack_ts_nol_saat_ntp_belum_sinkron);
    RUN_TEST(test_parse_enable);
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
