#pragma once
#include <stddef.h>

// task_cmd.h — eksekusi command MQTT (enable/disable/set_power) + ack (Task 15)
void taskCmdStart();
void taskCmdSubmit(const char* json, size_t n);   // dipanggil dari event MQTT (copy ke queue)
