#pragma once
#include <stddef.h>
#include "payload.h"   // OtaInfo

// task_ota.h — OTA gateway via MQTT dengan tanda tangan Ed25519 (sub-proyek G).
// Task ini DIAWASI task watchdog (WDT_TIMEOUT_S=120 dtk, sejak audit 23 Sep
// 2026): selama job aktif semua command ditolak ota_in_progress, jadi task
// yang macet tanpa jaring pengaman = kendali BESS hilang sampai power-cycle.
// Operasi terlamanya (erase partisi di esp_ota_begin) puluhan detik, dan
// publish lewat antrean mqtt_tx -- tak pernah menunggu lock esp-mqtt.
void taskOtaStart(const char* gw);   // gw = MAC, dipakai sebagai gateway_id di status

// Dipanggil dari event MQTT (task esp-mqtt) begitu pesan lengkap tiba di
// topic ota/manifest atau ota/chunk. Menyalin payload ke antrean task_ota
// (pola sama dengan taskCmdSubmit) lalu kembali seketika.
void taskOtaSubmitManifest(const char* json, size_t n);
void taskOtaSubmitChunk(const char* json, size_t n);

// Dipanggil dari mqtt_link saat MQTT_EVENT_CONNECTED (bisa dari task esp-mqtt,
// jadi hanya menaikkan flag atomic -- kerja sebenarnya terjadi di task_ota):
// (1) kalau image berjalan masih PENDING_VERIFY, tandai valid (batalkan
//     rollback); (2) sekali per boot, publish status persisted dari NVS
//     (installed/failed) supaya cloud tahu hasil OTA sebelumnya.
void otaOnMqttConnected();

// true selama job OTA aktif (manifest diterima sampai selesai/gagal) --
// task_cmd memakainya untuk menolak command biasa dengan "ota_in_progress".
bool otaInProgress();

// Snapshot untuk blok data.ota di telemetri. Aman dipanggil dari loop().
void otaGetInfo(OtaInfo& out);
