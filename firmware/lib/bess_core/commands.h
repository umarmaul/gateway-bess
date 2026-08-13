#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>

struct Command {
    enum Type { NONE, ENABLE, DISABLE, SET_POWER, UNSUPPORTED, BAD_JSON } type;
    char id[40];        // "" if not present
    char name[24];      // raw command name
    float power_w;      // only for SET_POWER
    bool has_power;     // whether power_w is valid
    uint32_t target;    // node tujuan; default 1 (BESS node tunggal)
};

// Parses JSON command into Command struct
// Handles: enable, disable, set_output/set_power (with args.power_w)
void parseCommand(const char* json, size_t len, Command& out);

// Builds ACK JSON response
// applied_pct and applied_w should be NAN if not relevant
// Returns number of bytes written (excluding NUL terminator)
size_t buildAckJson(const Command& c, const char* result, const char* detail,
                    float applied_pct, float applied_w, uint32_t ts,
                    char* out, size_t cap);

// Batas device: register 3050 berjangkauan -1200..1200 dalam satuan 0,1% rated.
#define POWER_PCT_LIMIT 120.0f

// Menghitung setpoint persen dari watt dan memangkasnya ke +-POWER_PCT_LIMIT.
// Mengembalikan false kalau rated_w tidak valid (<= 0) — tidak ada acuan untuk
// memangkas, jadi pemanggil harus menolak perintahnya.
bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped);

#endif
