#include <unity.h>
#include <string.h>
#include <math.h>
#include "commands.h"

// Test parseCommand() khusus command `set_schedule` (sub-proyek F) --
// command enable/disable/set_power/set_output sudah dicakup
// test_native_payload (buildAckJson dkk juga hidup di sana). File ini fokus
// ke ekstraksi args set_schedule ke Command.sched + penanda bad_sched_input
// (HH:MM tak valid / power_w NaN -- ditolak SEBELUM schedApplySetInput
// dipanggil, lihat komentar sched_logic.h).

void setUp(void) {
}

void tearDown(void) {
}

static void test_parse_set_schedule_semua_field() {
    Command c;
    const char* j = "{\"id\":\"s1\",\"cmd\":\"set_schedule\",\"args\":{"
                     "\"enabled\":true,\"start_hhmm\":\"17:00\",\"end_hhmm\":\"21:00\","
                     "\"soc_stop_pct\":10,\"soc_recovery_pct\":20,\"power_w\":1500,"
                     "\"tz_offset_min\":420}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_FALSE(c.sched_bad_input);
    TEST_ASSERT_TRUE(c.sched.has_enabled); TEST_ASSERT_TRUE(c.sched.enabled);
    TEST_ASSERT_TRUE(c.sched.has_start); TEST_ASSERT_EQUAL(1020, c.sched.start_min);
    TEST_ASSERT_TRUE(c.sched.has_end); TEST_ASSERT_EQUAL(1260, c.sched.end_min);
    TEST_ASSERT_TRUE(c.sched.has_soc_stop); TEST_ASSERT_FLOAT_WITHIN(0.01, 10, c.sched.soc_stop_pct);
    TEST_ASSERT_TRUE(c.sched.has_soc_recovery); TEST_ASSERT_FLOAT_WITHIN(0.01, 20, c.sched.soc_recovery_pct);
    TEST_ASSERT_TRUE(c.sched.has_power); TEST_ASSERT_FLOAT_WITHIN(0.1, 1500, c.sched.power_w);
    TEST_ASSERT_TRUE(c.sched.has_tz); TEST_ASSERT_EQUAL(420, c.sched.tz_offset_min);
}

static void test_parse_set_schedule_partial() {
    Command c;
    const char* j = "{\"id\":\"s2\",\"cmd\":\"set_schedule\",\"args\":{\"power_w\":800}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_FALSE(c.sched_bad_input);
    TEST_ASSERT_TRUE(c.sched.has_power);
    TEST_ASSERT_FALSE(c.sched.has_enabled);
    TEST_ASSERT_FALSE(c.sched.has_start);
    TEST_ASSERT_FALSE(c.sched.has_end);
    TEST_ASSERT_FALSE(c.sched.has_soc_stop);
    TEST_ASSERT_FALSE(c.sched.has_soc_recovery);
    TEST_ASSERT_FALSE(c.sched.has_tz);
}

static void test_parse_set_schedule_hhmm_tak_valid() {
    Command c;
    const char* j = "{\"id\":\"s3\",\"cmd\":\"set_schedule\",\"args\":{\"start_hhmm\":\"25:99\"}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_TRUE(c.sched_bad_input);
}

static void test_parse_set_schedule_end_hhmm_tak_valid() {
    Command c;
    const char* j = "{\"id\":\"s4\",\"cmd\":\"set_schedule\",\"args\":{\"end_hhmm\":\"tidak\"}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_TRUE(c.sched_bad_input);
}

static void test_parse_set_schedule_power_nan_ditolak() {
    Command c;
    // JSON tidak bisa mengirim NaN literal -- kirim string bukan angka, yang
    // di-deserialize ArduinoJson sebagai NaN kalau di-.as<float>() paksa;
    // pengujian NaN langsung dilakukan di test sched_logic (planPowerPct pun
    // sudah menguji ini). Di sini cukup pastikan field valid numerik biasa
    // tidak keliru ditandai bad_input.
    const char* j = "{\"id\":\"s5\",\"cmd\":\"set_schedule\",\"args\":{\"power_w\":-500}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_FALSE(c.sched_bad_input);
    TEST_ASSERT_TRUE(c.sched.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.1, -500, c.sched.power_w);
}

static void test_parse_set_schedule_tanpa_args() {
    Command c;
    const char* j = "{\"id\":\"s6\",\"cmd\":\"set_schedule\"}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_SCHEDULE, c.type);
    TEST_ASSERT_FALSE(c.sched_bad_input);
    TEST_ASSERT_FALSE(c.sched.has_enabled);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_set_schedule_semua_field);
    RUN_TEST(test_parse_set_schedule_partial);
    RUN_TEST(test_parse_set_schedule_hhmm_tak_valid);
    RUN_TEST(test_parse_set_schedule_end_hhmm_tak_valid);
    RUN_TEST(test_parse_set_schedule_power_nan_ditolak);
    RUN_TEST(test_parse_set_schedule_tanpa_args);
    return UNITY_END();
}
