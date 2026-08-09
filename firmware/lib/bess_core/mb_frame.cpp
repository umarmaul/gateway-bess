#include "mb_frame.h"
#include "crc16.h"

static size_t build4(uint8_t node, uint8_t fc, uint16_t a, uint16_t b, uint8_t out[8]) {
    out[0] = node; out[1] = fc;
    out[2] = a >> 8; out[3] = a & 0xFF;
    out[4] = b >> 8; out[5] = b & 0xFF;
    mbAppendCrc(out, 6);
    return 8;
}

size_t mbBuildRead(uint8_t node, uint16_t start, uint16_t count, uint8_t out[8]) {
    return build4(node, 3, start, count, out);
}

size_t mbBuildWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t out[8]) {
    return build4(node, 6, id, val, out);
}

size_t mbBuildWrite5(uint8_t node, uint16_t id, bool on, uint8_t out[8]) {
    return build4(node, 5, id, on ? 0xFF00 : 0x0000, out);
}

size_t mbExpectedReadLen(uint16_t count) { return 5 + 2 * (size_t)count; }

static MbStatus preCheck(const uint8_t* r, size_t n, uint8_t node,
                         uint8_t fc, uint8_t* exc) {
    if (n < 5) return MB_MALFORMED;
    if (!mbCheckCrc(r, n)) return MB_CRC;
    if (r[0] != node) return MB_MALFORMED;
    if (r[1] == (fc | 0x80)) { *exc = r[2]; return MB_EXCEPTION; }
    if (r[1] != fc) return MB_MALFORMED;
    return MB_OK;
}

MbStatus mbParseReadResp(const uint8_t* r, size_t n, uint8_t node,
                         uint16_t count, uint16_t* vals, uint8_t* exc) {
    MbStatus st = preCheck(r, n, node, 3, exc);
    if (st != MB_OK) return st;
    if (n != mbExpectedReadLen(count) || r[2] != 2 * count) return MB_MALFORMED;
    for (uint16_t k = 0; k < count; k++)
        vals[k] = ((uint16_t)r[3 + 2 * k] << 8) | r[4 + 2 * k];
    return MB_OK;
}

MbStatus mbParseEcho(const uint8_t* r, size_t n, uint8_t node,
                     uint8_t fc, uint8_t* exc) {
    MbStatus st = preCheck(r, n, node, fc, exc);
    if (st != MB_OK) return st;
    return (n == 8) ? MB_OK : MB_MALFORMED;
}
