#ifndef BESS_DECODE_H
#define BESS_DECODE_H

#include "bess_data.h"

void bessDecodeTelemetry(const uint16_t regs[59], BessData& d);  // 1050..1108
void bessDecodeAlarmStatus(const uint16_t regs[8], BessData& d); // 2050..2057
const char* bessAlarmName(int word, int bit);   // word 0..6 = 2050..2056; NULL bila tak terdefinisi
const char* bessStatusName(int bit);            // NULL bila tak terdefinisi

inline bool bessRunning(const BessData& d)  { return d.status_raw & (1u << 6); }
inline bool bessFault(const BessData& d)    { return d.status_raw & (1u << 7); }
inline bool bessCharging(const BessData& d) { return d.status_raw & (1u << 5); }
inline bool bessStandby(const BessData& d)  { return d.status_raw & (1u << 10); }
inline bool bessOffGrid(const BessData& d)  { return d.status_raw & (1u << 4); }
inline bool bessEpo(const BessData& d)      { return d.status_raw & (1u << 12); }

#endif
