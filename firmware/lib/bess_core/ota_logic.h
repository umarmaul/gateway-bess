#ifndef OTA_LOGIC_H
#define OTA_LOGIC_H

#include <stdint.h>
#include <stddef.h>

// ota_logic — logika MURNI sub-proyek G (OTA gateway via MQTT, Ed25519).
// Paritas kontrak (topic/field/ukuran chunk/bentuk ack+status) dengan
// BEPESP32_WiFi_Extension branch gateway-mqtt, KECUALI image_type/hardware
// yang sengaja dibedakan (lihat OTA_HARDWARE_ID) supaya image gateway DCON
// tim tidak bisa ter-flash ke gateway BESS lewat jalur bertanda tangan sah
// (papan fisiknya identik, jadi tanda tangan valid saja tidak cukup).
//
// Verifikasi Ed25519 (libsodium) dan penulisan flash (esp_ota_*) TIDAK ada
// di sini -- keduanya tidak tersedia di build native. File ini hanya parsing,
// validasi, mesin status urutan chunk, decode base64/hex, dan builder JSON
// ack/status; dipanggil dari src/task_ota.cpp yang menyambungkannya ke
// libsodium + esp_ota_ops.

#define OTA_HARDWARE_ID       "bep-gateway-bess-v1"
#define OTA_IMAGE_TYPE        "gateway"
#define OTA_MANIFEST_ID_MAX   128          // char, di luar NUL
#define OTA_CHUNK_MAX_BASE64  1536         // char base64 maksimum per chunk
#define OTA_CHUNK_MAX_BYTES   1152         // byte biner maksimum per chunk
#define OTA_MAX_IMAGE_SIZE    0x1E0000u    // = ukuran satu slot app OTA (partitions.csv)

struct OtaManifest {
    char id[OTA_MANIFEST_ID_MAX + 1];
    char version[32];
    uint32_t image_size;
    char sha256_hex[65];
    uint8_t sha256_raw[32];
    uint8_t signature_raw[64];
    uint32_t chunk_count;
};

enum class OtaManifestStatus { OK, INVALID_MANIFEST, HARDWARE_MISMATCH };

// Parse + validasi manifest OTA (lihat aturan lengkap di ota_logic.cpp).
// sha256/signature di-decode ke bentuk biner di sini (hex/base64 decode =
// logika murni); verifikasi Ed25519 aktualnya (butuh libsodium) dilakukan
// pemanggil di src/ menggunakan sha256_raw + signature_raw.
OtaManifestStatus otaParseManifest(const char* json, size_t len, OtaManifest& out);

enum class OtaChunkOutcome { NEW, DUPLICATE, UNEXPECTED, STALE, WRONG_JOB };

// Mesin status urutan chunk murni -- tidak menyentuh flash atau hash sama
// sekali, hanya memutuskan APA yang terjadi pada index yang baru masuk
// relatif terhadap job yang sedang berjalan (kalau ada).
// active_job_id == "" berarti tidak ada job aktif -> semua chunk WRONG_JOB.
OtaChunkOutcome otaEvaluateChunk(const char* active_job_id, uint32_t chunk_count,
                                  uint32_t next_index, const char* chunk_id,
                                  uint32_t chunk_index);

// Parse amplop chunk {id,index,data} TANPA decode data base64-nya.
// Panjang data yang SEBENARNYA (bukan yang tersalin ke b64_out) selalu
// dilaporkan lewat b64_len_out, walau melebihi kapasitas buffer -- supaya
// otaDecodeChunkData bisa mendeteksi TOO_LONG tanpa perlu buffer raksasa.
// false = JSON tak valid, atau field id/index/data tidak ada.
bool otaParseChunkEnvelope(const char* json, size_t len, char id_out[OTA_MANIFEST_ID_MAX + 1],
                            uint32_t& index_out, char b64_out[OTA_CHUNK_MAX_BASE64 + 1],
                            size_t& b64_len_out);

enum class OtaDataStatus { OK, EMPTY, TOO_LONG, INVALID_BASE64, OVERSIZED };

// Decode base64 data chunk ke biner. out_cap harus >= OTA_CHUNK_MAX_BYTES.
// b64_len boleh melebihi panjang string yang tersimpan aktual di b64 (lihat
// otaParseChunkEnvelope) -- kasus itu selalu berakhir TOO_LONG sebelum decode.
OtaDataStatus otaDecodeChunkData(const char* b64, size_t b64_len,
                                  uint8_t* out, size_t out_cap, size_t& out_len);

// Builder ack {id,kind,index?,result,detail,received_bytes?,next_index?,ts}.
// is_chunk=false (manifest): index/received_bytes/next_index TIDAK disertakan
// (field itu hanya relevan untuk kemajuan chunk-per-chunk).
size_t otaBuildAck(const char* id, bool is_chunk, uint32_t index,
                    const char* result, const char* detail,
                    uint32_t received_bytes, uint32_t next_index,
                    uint32_t ts, char* out, size_t cap);

struct OtaStatusFields {
    const char* id;
    const char* state;    // idle|downloading|verifying|restarting|installed|failed
    const char* image_type;
    const char* hardware;
    const char* gateway_id;
    const char* gateway_firmware_version;          // versi target manifest
    const char* running_gateway_firmware_version;   // FW_VERSION yang sedang jalan
    const char* running_partition;
    const char* sha256;
    uint32_t received_bytes;
    const char* detail;
    uint32_t ts;
};

// Builder status (topic RETAINED) -- lihat OtaStatusFields untuk daftar field.
size_t otaBuildStatus(const OtaStatusFields& f, char* out, size_t cap);

#endif
