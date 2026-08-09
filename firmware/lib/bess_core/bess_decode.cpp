#include "bess_decode.h"
#include <stddef.h>

static inline float u10(uint16_t r)  { return r / 10.0f; }
static inline float s10(uint16_t r)  { return (int16_t)r / 10.0f; }
static inline float u100(uint16_t r) { return r / 100.0f; }

void bessDecodeTelemetry(const uint16_t r[59], BessData& d) {
    d.grid_v_ab = u10(r[0]);  d.grid_v_bc = u10(r[1]);  d.grid_v_ca = u10(r[2]);
    d.grid_i_a = u10(r[3]);   d.grid_i_b = u10(r[4]);   d.grid_i_c = u10(r[5]);
    d.grid_f_hz = u100(r[6]);
    d.power_factor = (int16_t)r[9] / 100.0f;
    d.active_power_kw = s10(r[10]);
    d.reactive_power_kvar = s10(r[11]);
    d.apparent_power_kva = u10(r[12]);
    d.dc_voltage_v = u10(r[13]);
    d.dc_current_a = s10(r[14]);
    d.dc_power_kw = s10(r[15]);
    d.tube_temp_c = (float)(int16_t)r[24];
    d.ambient_temp_c = (float)(int16_t)r[26];
    d.efficiency_pct = u10(r[27]);
    d.total_charge_kwh = (((uint32_t)r[55] << 16) | r[56]) / 10.0f;   // 1105/1106
    d.total_discharge_kwh = (((uint32_t)r[57] << 16) | r[58]) / 10.0f; // 1107/1108
}

void bessDecodeAlarmStatus(const uint16_t r[8], BessData& d) {
    for (int i = 0; i < 7; i++) d.alarm_raw[i] = r[i];
    d.status_raw = r[7];
}

// Nama identik dengan bess_sim/alarms.py — jaga paritas!
static const char* W2050[16] = {"positive_bus_overvoltage", "negative_bus_overvoltage",
    "dc_bus_overvoltage", "bus_half_voltage_unbalance", "dc_bus_short_circuit",
    "dc_overcurrent", "balance_bridge_overcurrent", "dc_voltage_reverse",
    "dc_voltage_low", "dc_voltage_high", "insulation_impedance_abnormal",
    "pv_power_low_shutdown", NULL, NULL, NULL, NULL};
static const char* W2051[16] = {"inverter_voltage_a_abnormal", "inverter_voltage_b_abnormal",
    "inverter_voltage_c_abnormal", "inverter_voltage_dc_a_abnormal",
    "inverter_voltage_dc_b_abnormal", "inverter_voltage_dc_c_abnormal",
    "output_overload_shutdown_a", "output_overload_shutdown_b",
    "output_overload_shutdown_c", "output_overcurrent_a", "output_overcurrent_b",
    "output_overcurrent_c", "output_short_circuit_a", "output_short_circuit_b",
    "output_short_circuit_c", "inverter_phase_desync"};
static const char* W2052[16] = {"grid_overvoltage", "grid_undervoltage",
    "grid_overfrequency", "grid_underfrequency", "islanding_protection",
    "grid_wrong_phase", "ac_power_failure", "ac_current_limit_shutdown",
    "parallel_cable_fault", "carrier_sync_fault", "inverter_sync_fault",
    "parallel_comm_fault", "ac_fuse_failure", "power_tube_over_temperature",
    "power_supply_fault", "leakage_current_fault"};
static const char* W2053[16] = {"dc_precharge_fault", "ac_precharge_fault",
    "dc_relay_short_circuit", "dc_relay_open_circuit", "ac_relay_short_a",
    "ac_relay_short_b", "ac_relay_short_c", "ac_relay_open_a", "ac_relay_open_b",
    "ac_relay_open_c", "bridge_arm_shoot_through_a", "bridge_arm_shoot_through_b",
    "bridge_arm_shoot_through_c", NULL, NULL, NULL};
static const char* W2054[16] = {"grid_current_zero_bias", "inverter_current_zero_bias",
    "inverter_current_dc_zero_bias", "dc_current_zero_bias",
    "balance_bridge_current_zero_bias", "leakage_current_zero_bias",
    "reference_2v5_abnormal", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const char* W2055[16] = {"output_overload_alarm_a", "output_overload_alarm_b",
    "output_overload_alarm_c", "low_voltage_ride_through", "high_voltage_ride_through",
    "balance_bridge_current_limit_alarm", "balance_bridge_over_temperature",
    "ambient_over_temperature", "over_temperature_derating", "dc_spd_fault",
    "ac_spd_fault", "fan_fault_1", "fan_fault_2", "battery_over_charge",
    "battery_over_discharge", NULL};
static const char* W2056[16] = {"internal_comm_failure", "ems_comm_failure",
    "bms_comm_failure", "bms_fault", "dry_contact_1_fault", "dry_contact_2_fault",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const char** ALARM_WORDS[7] = {W2050, W2051, W2052, W2053, W2054, W2055, W2056};
static const char* STATUS[16] = {"dc_precharge", "ac_soft_start", "dc_relay",
    "ac_relay", "off_grid", "charging", "running", "fault", "master_machine",
    "master_cabinet", "standby", "shutdown", "epo", NULL, NULL, "param_init_done"};

const char* bessAlarmName(int word, int bit) {
    if (word < 0 || word > 6 || bit < 0 || bit > 15) return NULL;
    return ALARM_WORDS[word][bit];
}
const char* bessStatusName(int bit) {
    if (bit < 0 || bit > 15) return NULL;
    return STATUS[bit];
}
