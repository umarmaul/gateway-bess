"""Nama semua bit alarm (2050-2056) & status (2057) — sumber PDF §4.3-4.4."""

ALARM_BITS: dict[tuple[int, int], str] = {}


def _w(reg, names):
    for bit, name in enumerate(names):
        if name:
            ALARM_BITS[(reg, bit)] = name


_w(2050, ["positive_bus_overvoltage", "negative_bus_overvoltage",
          "dc_bus_overvoltage", "bus_half_voltage_unbalance",
          "dc_bus_short_circuit", "dc_overcurrent", "balance_bridge_overcurrent",
          "dc_voltage_reverse", "dc_voltage_low", "dc_voltage_high",
          "insulation_impedance_abnormal", "pv_power_low_shutdown"])
_w(2051, ["inverter_voltage_a_abnormal", "inverter_voltage_b_abnormal",
          "inverter_voltage_c_abnormal", "inverter_voltage_dc_a_abnormal",
          "inverter_voltage_dc_b_abnormal", "inverter_voltage_dc_c_abnormal",
          "output_overload_shutdown_a", "output_overload_shutdown_b",
          "output_overload_shutdown_c", "output_overcurrent_a",
          "output_overcurrent_b", "output_overcurrent_c",
          "output_short_circuit_a", "output_short_circuit_b",
          "output_short_circuit_c", "inverter_phase_desync"])
_w(2052, ["grid_overvoltage", "grid_undervoltage", "grid_overfrequency",
          "grid_underfrequency", "islanding_protection", "grid_wrong_phase",
          "ac_power_failure", "ac_current_limit_shutdown", "parallel_cable_fault",
          "carrier_sync_fault", "inverter_sync_fault", "parallel_comm_fault",
          "ac_fuse_failure", "power_tube_over_temperature", "power_supply_fault",
          "leakage_current_fault"])
_w(2053, ["dc_precharge_fault", "ac_precharge_fault", "dc_relay_short_circuit",
          "dc_relay_open_circuit", "ac_relay_short_a", "ac_relay_short_b",
          "ac_relay_short_c", "ac_relay_open_a", "ac_relay_open_b",
          "ac_relay_open_c", "bridge_arm_shoot_through_a",
          "bridge_arm_shoot_through_b", "bridge_arm_shoot_through_c"])
_w(2054, ["grid_current_zero_bias", "inverter_current_zero_bias",
          "inverter_current_dc_zero_bias", "dc_current_zero_bias",
          "balance_bridge_current_zero_bias", "leakage_current_zero_bias",
          "reference_2v5_abnormal"])
_w(2055, ["output_overload_alarm_a", "output_overload_alarm_b",
          "output_overload_alarm_c", "low_voltage_ride_through",
          "high_voltage_ride_through", "balance_bridge_current_limit_alarm",
          "balance_bridge_over_temperature", "ambient_over_temperature",
          "over_temperature_derating", "dc_spd_fault", "ac_spd_fault",
          "fan_fault_1", "fan_fault_2", "battery_over_charge",
          "battery_over_discharge"])
_w(2056, ["internal_comm_failure", "ems_comm_failure", "bms_comm_failure",
          "bms_fault", "dry_contact_1_fault", "dry_contact_2_fault"])

STATUS_BITS = {0: "dc_precharge", 1: "ac_soft_start", 2: "dc_relay",
               3: "ac_relay", 4: "off_grid", 5: "charging", 6: "running",
               7: "fault", 8: "master_machine", 9: "master_cabinet",
               10: "standby", 11: "shutdown", 12: "epo",
               15: "param_init_done"}

_BY_NAME = {v: k for k, v in ALARM_BITS.items()}


def by_name(name: str) -> tuple[int, int]:
    return _BY_NAME[name]
