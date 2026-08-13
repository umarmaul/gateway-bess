#include "payload.h"
#include <ArduinoJson.h>
#include "bess_decode.h"
#include "timeutil.h"

size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap) {
    JsonDocument doc;
    uint32_t ts = tsOrZero(s.ts);
    doc["gw"] = s.gw;
    doc["ts"] = ts;
    doc["seq"] = s.seq;
    doc["api_schema_version"] = 1;
    JsonObject data = doc["data"].to<JsonObject>();
    data["device_type"] = "bess";
    data["api_schema_version"] = 1;
    data["firmware_version"] = s.fw_version;
    data["device_id"] = s.gw;
    data["uptime_ms"] = s.uptime_ms;
    data["time_valid"] = ts != 0;
    JsonObject net = data["network"].to<JsonObject>();
    net["ssid"] = s.ssid; net["ip"] = s.ip; net["rssi_dbm"] = s.rssi;
    JsonObject b = data["bess"].to<JsonObject>();
    b["grid_voltage_ab_v"] = d.grid_v_ab;
    b["grid_voltage_bc_v"] = d.grid_v_bc;
    b["grid_voltage_ca_v"] = d.grid_v_ca;
    b["grid_current_a_a"] = d.grid_i_a;
    b["grid_current_b_a"] = d.grid_i_b;
    b["grid_current_c_a"] = d.grid_i_c;
    b["grid_frequency_hz"] = d.grid_f_hz;
    b["power_factor"] = d.power_factor;
    b["active_power_kw"] = d.active_power_kw;
    b["reactive_power_kvar"] = d.reactive_power_kvar;
    b["apparent_power_kva"] = d.apparent_power_kva;
    b["dc_voltage_v"] = d.dc_voltage_v;
    b["dc_current_a"] = d.dc_current_a;
    b["dc_power_kw"] = d.dc_power_kw;
    b["power_tube_temp_c"] = d.tube_temp_c;
    b["ambient_temp_c"] = d.ambient_temp_c;
    b["efficiency_percent"] = d.efficiency_pct;
    b["soc_percent"] = d.soc_pct;
    b["rated_power_kw"] = d.rated_kw;
    b["power_setpoint_percent"] = d.setpoint_pct;
    b["total_charge_kwh"] = d.total_charge_kwh;
    b["total_discharge_kwh"] = d.total_discharge_kwh;
    b["running"] = bessRunning(d);
    b["charging"] = bessCharging(d);
    b["standby"] = bessStandby(d);
    b["fault"] = bessFault(d);
    b["grid_connected"] = !bessOffGrid(d);
    b["epo"] = bessEpo(d);
    b["comm_lost"] = d.comm_lost;
    JsonObject al = b["alarms_decoded"].to<JsonObject>();
    for (int w = 0; w < 7; w++)
        for (int bit = 0; bit < 16; bit++) {
            const char* nm = bessAlarmName(w, bit);
            if (nm) al[nm] = (d.alarm_raw[w] >> bit) & 1;
        }
    JsonObject stt = b["status_decoded"].to<JsonObject>();
    for (int bit = 0; bit < 16; bit++) {
        const char* nm = bessStatusName(bit);
        if (nm) stt[nm] = (d.status_raw >> bit) & 1;
    }
    size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;  // buffer kurang (perlu ruang untuk NUL terminator)
    return serializeJson(doc, out, cap);
}
