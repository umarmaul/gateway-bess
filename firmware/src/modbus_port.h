#pragma once
#include <stdint.h>
#include "mb_frame.h"

// modbus_port.h — dipakai task_bess DAN task_cmd (Task 15); thread-safe via mutex internal
void mbPortInit();     // Serial1 9600 8N1 RX21/TX20 + REDE22
MbStatus mbReadRegs(uint8_t node, uint16_t start, uint16_t count,
                    uint16_t* out, uint8_t* exc);   // retry internal MB_RETRIES
MbStatus mbWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t* exc);
MbStatus mbWrite5(uint8_t node, uint16_t id, bool on, uint8_t* exc);
