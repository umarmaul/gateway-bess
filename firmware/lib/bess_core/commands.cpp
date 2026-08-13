#include "commands.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "timeutil.h"

static void scopy(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

void parseCommand(const char* json, size_t len, Command& out) {
    out = Command{};
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
        out.type = Command::BAD_JSON;
        return;
    }
    scopy(out.id, sizeof(out.id), doc["id"] | "");
    scopy(out.name, sizeof(out.name), doc["cmd"] | "");
    out.target = doc["args"]["target"] | 1u;
    if (!strcmp(out.name, "enable")) out.type = Command::ENABLE;
    else if (!strcmp(out.name, "disable")) out.type = Command::DISABLE;
    else if (!strcmp(out.name, "set_output") || !strcmp(out.name, "set_power")) {
        // set_output = nama resmi (selaras BEPESP32_WiFi_Extension);
        // set_power = alias lama fase 1, dipertahankan supaya cloud tidak rusak.
        out.type = Command::SET_POWER;
        JsonVariant p = doc["args"]["power_w"];
        out.has_power = !p.isNull();
        out.power_w = out.has_power ? p.as<float>() : 0.0f;
    } else out.type = Command::UNSUPPORTED;
}

size_t buildAckJson(const Command& c, const char* result, const char* detail,
                    float applied_pct, float applied_w, uint32_t ts,
                    char* out, size_t cap) {
    JsonDocument doc;
    doc["id"] = c.id;
    doc["cmd"] = c.name[0] ? c.name : "unknown";
    doc["result"] = result;
    doc["detail"] = detail;
    JsonObject ap = doc["applied"].to<JsonObject>();
    if (!isnan(applied_pct)) ap["power_pct"] = applied_pct;
    if (!isnan(applied_w)) ap["power_w"] = applied_w;
    doc["ts"] = tsOrZero(ts);
    return serializeJson(doc, out, cap);
}

bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped) {
    if (!(rated_w > 0.0f)) return false;     // juga menangkap NAN
    if (isnan(power_w)) return false;        // NaN power_w tidak boleh lolos jadi pct NaN
    // Hitung di presisi double: pembagian float saja membulatkan 60000/50000*100
    // jadi ~120,00000763 (bukti float32), sehingga batas TEPAT 120% salah
    // terpangkas oleh perbandingan ketat di bawah. Presisi double menghindarinya.
    double raw_d = (double)power_w / (double)rated_w * 100.0;
    clamped = false;
    if (raw_d > (double)POWER_PCT_LIMIT) { raw_d = POWER_PCT_LIMIT; clamped = true; }
    if (raw_d < -(double)POWER_PCT_LIMIT) { raw_d = -POWER_PCT_LIMIT; clamped = true; }
    pct = (float)raw_d;
    return true;
}
