#pragma once
#include <stddef.h>
#include <stdint.h>

uint16_t mbCrc16(const uint8_t* data, size_t len);
void mbAppendCrc(uint8_t* buf, size_t len);
bool mbCheckCrc(const uint8_t* frame, size_t len);
