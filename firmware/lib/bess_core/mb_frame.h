#pragma once
#include <stddef.h>
#include <stdint.h>
#include "crc16.h"

enum MbStatus : uint8_t { MB_OK, MB_TIMEOUT, MB_CRC, MB_EXCEPTION, MB_MALFORMED };

size_t mbBuildRead(uint8_t node, uint16_t start, uint16_t count, uint8_t out[8]);
size_t mbBuildWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t out[8]);
size_t mbBuildWrite5(uint8_t node, uint16_t id, bool on, uint8_t out[8]);

// resp = frame lengkap; count = jumlah register yang diminta; exc diisi bila MB_EXCEPTION
MbStatus mbParseReadResp(const uint8_t* resp, size_t n, uint8_t node,
                         uint16_t count, uint16_t* vals, uint8_t* exc);
MbStatus mbParseEcho(const uint8_t* resp, size_t n, uint8_t node,
                     uint8_t fc, uint8_t* exc);

size_t mbExpectedReadLen(uint16_t count);   // 5 + 2*count
