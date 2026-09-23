#include "ota_logic.h"
#include <ArduinoJson.h>
#include <string.h>
#include "timeutil.h"

// ---------------------------------------------------------------------------
// base64 (RFC 4648, alfabet standar +/ dengan padding =) -- ditulis sendiri
// karena ArduinoJson tidak menyediakannya dan libsodium (yang punya
// sodium_base642bin) tidak tersedia di build native.
// ---------------------------------------------------------------------------

static const int8_t* b64Table() {
    static int8_t t[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) t[i] = -1;
        for (int i = 'A'; i <= 'Z'; i++) t[i] = (int8_t)(i - 'A');
        for (int i = 'a'; i <= 'z'; i++) t[i] = (int8_t)(26 + i - 'a');
        for (int i = '0'; i <= '9'; i++) t[i] = (int8_t)(52 + i - '0');
        t[(unsigned char)'+'] = 62;
        t[(unsigned char)'/'] = 63;
        t[(unsigned char)'='] = -2;   // padding, ditangani terpisah dari karakter tak dikenal (-1)
        init = true;
    }
    return t;
}

// Decode mentah: pemanggil SUDAH memvalidasi len%4==0 dan decoded_len pas
// muat di out_cap. Mengembalikan false hanya untuk encoding yang rusak
// (karakter di luar alfabet, atau '=' di posisi yang tidak valid).
static bool base64DecodeRaw(const char* b64, size_t len, uint8_t* out, size_t out_cap, size_t& out_len) {
    const int8_t* T = b64Table();
    size_t oi = 0;
    for (size_t i = 0; i < len; i += 4) {
        int8_t c0 = T[(unsigned char)b64[i]];
        int8_t c1 = T[(unsigned char)b64[i + 1]];
        int8_t c2 = T[(unsigned char)b64[i + 2]];
        int8_t c3 = T[(unsigned char)b64[i + 3]];
        bool lastGroup = (i + 4 == len);
        if (c0 < 0 || c1 < 0) return false;          // dua char pertama tak pernah padding
        if (!lastGroup && (c2 < 0 || c3 < 0)) return false;
        if (lastGroup) {
            if (c2 == -1 || c3 == -1) return false;   // karakter tak dikenal, bukan padding
            if (c2 == -2 && c3 != -2) return false;   // padding "AB=C" tidak valid
        }
        uint32_t n = ((uint32_t)c0 << 18) | ((uint32_t)c1 << 12) |
                     ((uint32_t)(c2 == -2 ? 0 : c2) << 6) | (uint32_t)(c3 == -2 ? 0 : c3);
        if (oi >= out_cap) return false;
        out[oi++] = (uint8_t)((n >> 16) & 0xFF);
        if (c2 != -2) {
            if (oi >= out_cap) return false;
            out[oi++] = (uint8_t)((n >> 8) & 0xFF);
        }
        if (c3 != -2) {
            if (oi >= out_cap) return false;
            out[oi++] = (uint8_t)(n & 0xFF);
        }
    }
    out_len = oi;
    return true;
}

// Hitung panjang hasil decode dari panjang base64 (termasuk padding) TANPA
// men-decode -- dipakai untuk menolak lebih awal (OVERSIZED) sebelum
// menyentuh isi buffer.
static bool base64DecodedLen(const char* b64, size_t len, size_t& decoded_len) {
    if (len == 0 || len % 4 != 0) return false;
    size_t pad = 0;
    if (b64[len - 1] == '=') pad++;
    if (pad == 1 && len >= 2 && b64[len - 2] == '=') pad++;
    decoded_len = (len / 4) * 3 - pad;
    return true;
}

static bool base64DecodeExact(const char* b64, size_t len, uint8_t* out, size_t exact_len) {
    size_t decoded_len = 0;
    if (!base64DecodedLen(b64, len, decoded_len) || decoded_len != exact_len) return false;
    size_t n = 0;
    if (!base64DecodeRaw(b64, len, out, exact_len, n)) return false;
    return n == exact_len;
}

OtaDataStatus otaDecodeChunkData(const char* b64, size_t b64_len,
                                  uint8_t* out, size_t out_cap, size_t& out_len) {
    out_len = 0;
    if (b64_len == 0) return OtaDataStatus::EMPTY;
    if (b64_len > OTA_CHUNK_MAX_BASE64) return OtaDataStatus::TOO_LONG;
    size_t decoded_len = 0;
    if (!base64DecodedLen(b64, b64_len, decoded_len)) return OtaDataStatus::INVALID_BASE64;
    if (decoded_len > OTA_CHUNK_MAX_BYTES || decoded_len > out_cap) return OtaDataStatus::OVERSIZED;
    size_t n = 0;
    if (!base64DecodeRaw(b64, b64_len, out, out_cap, n)) return OtaDataStatus::INVALID_BASE64;
    out_len = n;
    return OtaDataStatus::OK;
}

// ---------------------------------------------------------------------------
// hex (sha256)
// ---------------------------------------------------------------------------

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hexDecode32(const char* hex, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) {
        int hi = hexVal(hex[i * 2]);
        int lo = hexVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

// ---------------------------------------------------------------------------
// manifest
// ---------------------------------------------------------------------------

static void copyTruncate(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

OtaManifestStatus otaParseManifest(const char* json, size_t len, OtaManifest& out) {
    out = OtaManifest{};
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok)
        return OtaManifestStatus::INVALID_MANIFEST;

    const char* id = doc["id"] | (const char*)nullptr;
    if (!id || !id[0] || strlen(id) > OTA_MANIFEST_ID_MAX)
        return OtaManifestStatus::INVALID_MANIFEST;
    copyTruncate(out.id, sizeof(out.id), id);

    // Identitas hardware/image sengaja dicek SEBELUM field lain -- ini
    // pembeda paling penting dari kontrak tim (lihat komentar di ota_logic.h):
    // papan fisiknya identik, jadi image gateway DCON tim harus ditolak di
    // sini walau tanda tangan Ed25519-nya sah.
    const char* image_type = doc["image_type"] | (const char*)nullptr;
    const char* hardware = doc["hardware"] | (const char*)nullptr;
    if (!image_type || strcmp(image_type, OTA_IMAGE_TYPE) != 0 ||
        !hardware || strcmp(hardware, OTA_HARDWARE_ID) != 0)
        return OtaManifestStatus::HARDWARE_MISMATCH;

    const char* encoding = doc["encoding"] | (const char*)nullptr;
    if (!encoding || strcmp(encoding, "base64") != 0)
        return OtaManifestStatus::INVALID_MANIFEST;

    copyTruncate(out.version, sizeof(out.version), doc["version"] | "");

    JsonVariant iv = doc["image_size"];
    if (iv.isNull()) return OtaManifestStatus::INVALID_MANIFEST;
    uint32_t image_size = iv.as<uint32_t>();
    if (image_size < 1 || image_size > OTA_MAX_IMAGE_SIZE)
        return OtaManifestStatus::INVALID_MANIFEST;
    out.image_size = image_size;

    const char* sha = doc["sha256"] | (const char*)nullptr;
    if (!sha || strlen(sha) != 64 || !hexDecode32(sha, out.sha256_raw))
        return OtaManifestStatus::INVALID_MANIFEST;
    copyTruncate(out.sha256_hex, sizeof(out.sha256_hex), sha);

    const char* sig = doc["signature"] | (const char*)nullptr;
    if (!sig || !sig[0] || !base64DecodeExact(sig, strlen(sig), out.signature_raw, 64))
        return OtaManifestStatus::INVALID_MANIFEST;

    JsonVariant cv = doc["chunk_count"];
    if (cv.isNull()) return OtaManifestStatus::INVALID_MANIFEST;
    uint32_t chunk_count = cv.as<uint32_t>();
    uint32_t expected = (image_size + OTA_CHUNK_MAX_BYTES - 1) / OTA_CHUNK_MAX_BYTES;   // ceil
    if (chunk_count != expected) return OtaManifestStatus::INVALID_MANIFEST;
    out.chunk_count = chunk_count;

    return OtaManifestStatus::OK;
}

// ---------------------------------------------------------------------------
// urutan chunk
// ---------------------------------------------------------------------------

OtaChunkOutcome otaEvaluateChunk(const char* active_job_id, uint32_t chunk_count,
                                  uint32_t next_index, const char* chunk_id,
                                  uint32_t chunk_index) {
    if (!active_job_id || !active_job_id[0] || !chunk_id || strcmp(active_job_id, chunk_id) != 0)
        return OtaChunkOutcome::WRONG_JOB;
    if (next_index >= chunk_count)
        return OtaChunkOutcome::UNEXPECTED;    // job sudah lengkap, menunggu finalisasi
    if (chunk_index == next_index)
        return OtaChunkOutcome::NEW;
    if (next_index > 0 && chunk_index == next_index - 1)
        return OtaChunkOutcome::DUPLICATE;     // retry aman (QoS1), idempotent
    if (chunk_index < next_index)
        return OtaChunkOutcome::STALE;
    return OtaChunkOutcome::UNEXPECTED;        // chunk_index > next_index: melompat maju
}

bool otaParseChunkEnvelope(const char* json, size_t len, char id_out[OTA_MANIFEST_ID_MAX + 1],
                            uint32_t& index_out, char b64_out[OTA_CHUNK_MAX_BASE64 + 1],
                            size_t& b64_len_out) {
    id_out[0] = 0;
    index_out = 0;
    b64_out[0] = 0;
    b64_len_out = 0;
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;

    const char* id = doc["id"] | (const char*)nullptr;
    if (!id || !id[0] || strlen(id) > OTA_MANIFEST_ID_MAX) return false;

    JsonVariant iv = doc["index"];
    if (iv.isNull()) return false;

    const char* data = doc["data"] | (const char*)nullptr;
    if (!data) return false;

    copyTruncate(id_out, OTA_MANIFEST_ID_MAX + 1, id);
    index_out = iv.as<uint32_t>();
    // Panjang SEBENARNYA dilaporkan meski melebihi buffer lokal -- data mentah
    // masih utuh di dalam JsonDocument, jadi strlen() di sini tetap akurat;
    // otaDecodeChunkData yang menolak TOO_LONG berdasarkan angka ini.
    size_t true_len = strlen(data);
    size_t cap = OTA_CHUNK_MAX_BASE64 + 1;
    size_t copy_n = true_len < cap - 1 ? true_len : cap - 1;
    memcpy(b64_out, data, copy_n);
    b64_out[copy_n] = 0;
    b64_len_out = true_len;
    return true;
}

// ---------------------------------------------------------------------------
// builder ack / status
// ---------------------------------------------------------------------------

size_t otaBuildAck(const char* id, bool is_chunk, uint32_t index,
                    const char* result, const char* detail,
                    uint32_t received_bytes, uint32_t next_index,
                    uint32_t ts, char* out, size_t cap) {
    JsonDocument doc;
    doc["id"] = id ? id : "";
    doc["kind"] = is_chunk ? "chunk" : "manifest";
    if (is_chunk) {
        doc["index"] = index;
        doc["received_bytes"] = received_bytes;
        doc["next_index"] = next_index;
    }
    doc["result"] = result;
    doc["detail"] = detail ? detail : "";
    doc["ts"] = tsOrZero(ts);
    size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;
    return serializeJson(doc, out, cap);
}

size_t otaBuildStatus(const OtaStatusFields& f, char* out, size_t cap) {
    JsonDocument doc;
    doc["id"] = f.id ? f.id : "";
    doc["state"] = f.state ? f.state : "idle";
    doc["image_type"] = f.image_type ? f.image_type : "";
    doc["hardware"] = f.hardware ? f.hardware : "";
    doc["gateway_id"] = f.gateway_id ? f.gateway_id : "";
    doc["gateway_firmware_version"] = f.gateway_firmware_version ? f.gateway_firmware_version : "";
    doc["running_gateway_firmware_version"] =
        f.running_gateway_firmware_version ? f.running_gateway_firmware_version : "";
    doc["running_partition"] = f.running_partition ? f.running_partition : "";
    doc["sha256"] = f.sha256 ? f.sha256 : "";
    doc["received_bytes"] = f.received_bytes;
    doc["detail"] = f.detail ? f.detail : "";
    doc["ts"] = tsOrZero(f.ts);
    size_t need = measureJson(doc);
    if (need + 1 > cap) return 0;
    return serializeJson(doc, out, cap);
}
