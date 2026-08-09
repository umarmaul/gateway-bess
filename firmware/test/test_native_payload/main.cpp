#include <unity.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "bess_data.h"
#include "payload.h"
#include "commands.h"

void setUp(void) {
}

void tearDown(void) {
}

static SysInfo sys_() {
    SysInfo s{};
    strcpy(s.gw, "AABBCCDDEEFF");
    s.fw_version = "bess-0.1.0";
    s.uptime_ms = 123456; s.seq = 7; s.ts = 1785000000; s.time_valid = true;
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_envelope);
    RUN_TEST(test_parse_enable);
    RUN_TEST(test_parse_set_power);
    RUN_TEST(test_parse_unsupported_dan_bad_json);
    RUN_TEST(test_ack);
    return UNITY_END();
}
