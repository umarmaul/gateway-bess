#include <unity.h>
#include <string.h>
#include "bess_data.h"
#include "bess_decode.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_scaling_telemetri() {
    uint16_t regs[59]; memset(regs, 0, sizeof(regs));
    regs[0] = 4000;                    // 1050: 400.0 V
    regs[6] = 5001;                    // 1056: 50.01 Hz
    regs[10] = (uint16_t)(int16_t)-125; // 1060: -12.5 kW (charge)
    regs[13] = 8064;                   // 1063: 806.4 V
    regs[14] = (uint16_t)(int16_t)-155; // 1064: -15.5 A
    regs[24] = 45;                     // 1074: 45 C
    regs[27] = 970;                    // 1077: 97.0 %
    regs[57] = 0; regs[58] = 1234;     // 1107/1108: 123.4 kWh discharge
    BessData d{};
    bessDecodeTelemetry(regs, d);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 400.0, d.grid_v_ab);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 50.01, d.grid_f_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01, -12.5, d.active_power_kw);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 806.4, d.dc_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.01, -15.5, d.dc_current_a);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 45, d.tube_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 97.0, d.efficiency_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 123.4, d.total_discharge_kwh);
}

static void test_status_flags() {
    uint16_t regs[8]; memset(regs, 0, sizeof(regs));
    regs[7] = (1u << 6) | (1u << 5) | 0b1111 | (1u << 8) | (1u << 15);
    BessData d{};
    bessDecodeAlarmStatus(regs, d);
    TEST_ASSERT_TRUE(bessRunning(d));
    TEST_ASSERT_TRUE(bessCharging(d));
    TEST_ASSERT_FALSE(bessFault(d));
}

static void test_alarm_names() {
    TEST_ASSERT_EQUAL_STRING("dc_bus_overvoltage", bessAlarmName(0, 2));
    TEST_ASSERT_EQUAL_STRING("grid_undervoltage", bessAlarmName(2, 1));
    TEST_ASSERT_EQUAL_STRING("battery_over_discharge", bessAlarmName(5, 14));
    TEST_ASSERT_EQUAL_STRING("bms_comm_failure", bessAlarmName(6, 2));
    TEST_ASSERT_NULL(bessAlarmName(0, 15));
    TEST_ASSERT_EQUAL_STRING("running", bessStatusName(6));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scaling_telemetri);
    RUN_TEST(test_status_flags);
    RUN_TEST(test_alarm_names);
    return UNITY_END();
}
