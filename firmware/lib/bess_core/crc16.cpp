#include "crc16.h"

uint16_t mbCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

void mbAppendCrc(uint8_t* buf, size_t len) {
    uint16_t c = mbCrc16(buf, len);
    buf[len] = c & 0xFF;
    buf[len + 1] = c >> 8;
}

bool mbCheckCrc(const uint8_t* frame, size_t len) {
    if (len < 4) return false;
    uint16_t c = mbCrc16(frame, len - 2);
    return frame[len - 2] == (c & 0xFF) && frame[len - 1] == (c >> 8);
}
