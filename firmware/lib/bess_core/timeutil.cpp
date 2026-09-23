#include "timeutil.h"

uint32_t tsOrZero(uint32_t epoch) {
    return epoch >= TS_VALID_MIN ? epoch : 0;
}

bool timeAfter(uint32_t now, uint32_t deadline) {
    // Konversi uint32->int32 untuk nilai >INT32_MAX implementation-defined
    // sebelum C++20 (well-defined sejak C++20). GCC/Clang/MSVC semuanya
    // two's-complement, jadi aman di toolchain ini; tinjau ulang bila berganti.
    return (int32_t)(now - deadline) >= 0;
}
