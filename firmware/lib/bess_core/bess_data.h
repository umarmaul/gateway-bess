#ifndef BESS_DATA_H
#define BESS_DATA_H

#include <stdint.h>

struct BessData {
    float grid_v_ab, grid_v_bc, grid_v_ca;
    float grid_i_a, grid_i_b, grid_i_c;
    float grid_f_hz, power_factor;
    float active_power_kw, reactive_power_kvar, apparent_power_kva;
    float dc_voltage_v, dc_current_a, dc_power_kw;
    float tube_temp_c, ambient_temp_c, efficiency_pct;
    float total_charge_kwh, total_discharge_kwh;
    uint16_t alarm_raw[7];   // 2050..2056
    uint16_t status_raw;     // 2057
    float soc_pct;           // reg 3184 / 10
    float rated_kw;          // reg 3146 / 10
    float setpoint_pct;      // reg 3050 (signed) / 10
    bool comm_lost;
    uint32_t last_ok_ms;
};

#endif
