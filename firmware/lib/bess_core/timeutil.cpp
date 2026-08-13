#include "timeutil.h"

uint32_t tsOrZero(uint32_t epoch) {
    return epoch >= TS_VALID_MIN ? epoch : 0;
}

bool timeAfter(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
