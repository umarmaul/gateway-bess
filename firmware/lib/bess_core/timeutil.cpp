#include "timeutil.h"

uint32_t tsOrZero(uint32_t epoch) {
    return epoch >= TS_VALID_MIN ? epoch : 0;
}
