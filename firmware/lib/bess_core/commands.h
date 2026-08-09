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
};

// Parses JSON command into Command struct
// Handles: enable, disable, set_power (with args.power_w)
void parseCommand(const char* json, size_t len, Command& out);

// Builds ACK JSON response
// applied_pct and applied_w should be NAN if not relevant
// Returns number of bytes written (excluding NUL terminator)
size_t buildAckJson(const Command& c, const char* result, const char* detail,
                    float applied_pct, float applied_w, uint32_t ts,
                    char* out, size_t cap);

#endif
