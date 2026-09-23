#pragma once
#include <stddef.h>

// task_cmd.h — eksekusi command MQTT (enable/disable/set_power/set_schedule) + ack
void taskCmdStart();
void taskCmdSubmit(const char* json, size_t n);   // dipanggil dari event MQTT (copy ke queue)

// sub-proyek F: dipanggil task_auto untuk mengeksekusi aksi jadwal/auto-SOC
// lewat jalur command yang SAMA (Modbus + safety + ack), TANPA menonaktifkan
// jadwal (beda dari taskCmdSubmit, yang dianggap datang dari cloud/operator
// dan MEMATIKAN jadwal untuk enable/disable/set_output -- lihat schedule.h).
void taskCmdSubmitInternal(const char* json, size_t n);
