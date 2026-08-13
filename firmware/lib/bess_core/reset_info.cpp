#include "reset_info.h"
#include <stdio.h>
#include <string.h>

const char* resetReasonName(int reason, char* out, size_t cap) {
    static const char* NAMA[] = {
        "UNKNOWN", "POWERON", "EXT", "SW", "PANIC", "INT_WDT",
        "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO",
    };
    const int n = (int)(sizeof(NAMA) / sizeof(NAMA[0]));
    if (reason > 0 && reason < n) {
        snprintf(out, cap, "%s", NAMA[reason]);
    } else if (reason == RESET_UNKNOWN) {
        snprintf(out, cap, "UNKNOWN");
    } else {
        snprintf(out, cap, "UNKNOWN_%d", reason);
    }
    return out;
}
