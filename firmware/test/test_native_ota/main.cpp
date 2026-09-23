#include <unity.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "ota_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

// sha256_hex 64 hex char valid: byte i = i (0..31)
static const char* SHA_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
// signature base64: 64 byte, byte i = i (0..63)
static const char* SIG_B64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+Pw==";

static void buildManifestJson(char* out, size_t cap, const char* id, const char* image_type,
                              const char* hardware, const char* encoding, uint32_t image_size,
                              const char* sha256, const char* signature, uint32_t chunk_count) {
    JsonDocument doc;
    doc["id"] = id;
    doc["image_type"] = image_type;
    doc["hardware"] = hardware;
    doc["version"] = "bess-0.3.0-dev";
    doc["encoding"] = encoding;
    doc["image_size"] = image_size;
    doc["sha256"] = sha256;
    doc["signature"] = signature;
    doc["chunk_count"] = chunk_count;
    serializeJson(doc, out, cap);
}

static void test_manifest_ok() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::OK == otaParseManifest(j, strlen(j), m));
    TEST_ASSERT_EQUAL_STRING("ota-1", m.id);
    TEST_ASSERT_EQUAL_UINT32(2000u, m.image_size);
    TEST_ASSERT_EQUAL_UINT32(2u, m.chunk_count);
    TEST_ASSERT_EQUAL_HEX8(0x00, m.sha256_raw[0]);
    TEST_ASSERT_EQUAL_HEX8(0x1f, m.sha256_raw[31]);
    TEST_ASSERT_EQUAL_HEX8(0x00, m.signature_raw[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3f, m.signature_raw[63]);
}

static void test_manifest_missing_id() {
    JsonDocument doc;
    doc["image_type"] = OTA_IMAGE_TYPE;
    doc["hardware"] = OTA_HARDWARE_ID;
    doc["encoding"] = "base64";
    doc["image_size"] = 2000;
    doc["sha256"] = SHA_HEX;
    doc["signature"] = SIG_B64;
    doc["chunk_count"] = 2;
    char j[512];
    serializeJson(doc, j, sizeof(j));
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_id_too_long() {
    char id[140];
    memset(id, 'a', sizeof(id) - 1);
    id[sizeof(id) - 1] = 0;   // 139 char, > 128
    char j[1024];
    buildManifestJson(j, sizeof(j), id, OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_id_batas_128_ok() {
    char id[129];
    memset(id, 'b', sizeof(id) - 1);
    id[sizeof(id) - 1] = 0;   // tepat 128 char
    char j[1024];
    buildManifestJson(j, sizeof(j), id, OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::OK == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_wrong_image_type() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", "dcon", OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::HARDWARE_MISMATCH == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_wrong_hardware() {
    // papan fisik sama dengan gateway DCON tim -- string hardware harus
    // berbeda supaya image mereka tidak bisa ter-flash ke gateway BESS.
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, "bep-gateway-v1", "base64",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::HARDWARE_MISMATCH == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_wrong_encoding() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "raw",
                       2000, SHA_HEX, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_image_size_nol() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       0, SHA_HEX, SIG_B64, 0);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_image_size_terlalu_besar() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       OTA_MAX_IMAGE_SIZE + 1, SHA_HEX, SIG_B64, 1708);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_image_size_tepat_batas_atas_ok() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       OTA_MAX_IMAGE_SIZE, SHA_HEX, SIG_B64, 1707);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::OK == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_sha256_pendek() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, "abcd", SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_sha256_bukan_hex() {
    char bad[65];
    memset(bad, 'z', 64);
    bad[64] = 0;
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, bad, SIG_B64, 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_signature_kosong() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, "", 2);
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_signature_panjang_salah() {
    // base64 valid tapi mendekode ke selain 64 byte
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, "Zm9vYmFy", 2);   // "foobar" -> 6 byte
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_chunk_count_tidak_pas() {
    char j[512];
    buildManifestJson(j, sizeof(j), "ota-1", OTA_IMAGE_TYPE, OTA_HARDWARE_ID, "base64",
                       2000, SHA_HEX, SIG_B64, 3);   // seharusnya 2
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest(j, strlen(j), m));
}

static void test_manifest_json_rusak() {
    OtaManifest m{};
    TEST_ASSERT_TRUE(OtaManifestStatus::INVALID_MANIFEST == otaParseManifest("{oops", 5, m));
}

// --- otaEvaluateChunk ---

static void test_chunk_baru_berurutan() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::NEW == otaEvaluateChunk("ota-1", 10, 3, "ota-1", 3));
}

static void test_chunk_pertama_index_nol() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::NEW == otaEvaluateChunk("ota-1", 10, 0, "ota-1", 0));
}

static void test_chunk_duplikat_terakhir() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::DUPLICATE == otaEvaluateChunk("ota-1", 10, 3, "ota-1", 2));
}

static void test_chunk_basi_lebih_lama_dari_duplikat() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::STALE == otaEvaluateChunk("ota-1", 10, 3, "ota-1", 1));
    TEST_ASSERT_TRUE(OtaChunkOutcome::STALE == otaEvaluateChunk("ota-1", 10, 3, "ota-1", 0));
}

static void test_chunk_tak_terduga_melompat_maju() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::UNEXPECTED == otaEvaluateChunk("ota-1", 10, 3, "ota-1", 5));
}

static void test_chunk_job_salah_id_beda() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::WRONG_JOB == otaEvaluateChunk("ota-1", 10, 3, "ota-lain", 3));
}

static void test_chunk_job_salah_tak_ada_job_aktif() {
    TEST_ASSERT_TRUE(OtaChunkOutcome::WRONG_JOB == otaEvaluateChunk("", 10, 0, "ota-1", 0));
}

static void test_chunk_tak_terduga_job_sudah_penuh() {
    // next_index sudah == chunk_count: job menunggu finalisasi, chunk baru apa pun ditolak
    TEST_ASSERT_TRUE(OtaChunkOutcome::UNEXPECTED == otaEvaluateChunk("ota-1", 5, 5, "ota-1", 5));
}

// --- otaParseChunkEnvelope ---

static void test_envelope_ok() {
    const char* j = "{\"id\":\"ota-1\",\"index\":2,\"data\":\"Zm9vYmFy\"}";
    char id[OTA_MANIFEST_ID_MAX + 1]; uint32_t idx = 999;
    char b64[OTA_CHUNK_MAX_BASE64 + 1]; size_t b64len = 0;
    TEST_ASSERT_TRUE(otaParseChunkEnvelope(j, strlen(j), id, idx, b64, b64len));
    TEST_ASSERT_EQUAL_STRING("ota-1", id);
    TEST_ASSERT_EQUAL_UINT32(2u, idx);
    TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", b64);
    TEST_ASSERT_EQUAL(8, (int)b64len);
}

static void test_envelope_field_hilang() {
    char id[OTA_MANIFEST_ID_MAX + 1]; uint32_t idx = 0;
    char b64[OTA_CHUNK_MAX_BASE64 + 1]; size_t b64len = 0;
    const char* j1 = "{\"index\":2,\"data\":\"Zm9v\"}";
    TEST_ASSERT_FALSE(otaParseChunkEnvelope(j1, strlen(j1), id, idx, b64, b64len));
    const char* j2 = "{\"id\":\"x\",\"data\":\"Zm9v\"}";
    TEST_ASSERT_FALSE(otaParseChunkEnvelope(j2, strlen(j2), id, idx, b64, b64len));
    const char* j3 = "{\"id\":\"x\",\"index\":1}";
    TEST_ASSERT_FALSE(otaParseChunkEnvelope(j3, strlen(j3), id, idx, b64, b64len));
}

static void test_envelope_json_rusak() {
    char id[OTA_MANIFEST_ID_MAX + 1]; uint32_t idx = 0;
    char b64[OTA_CHUNK_MAX_BASE64 + 1]; size_t b64len = 0;
    TEST_ASSERT_FALSE(otaParseChunkEnvelope("{oops", 5, id, idx, b64, b64len));
}

static void test_envelope_data_lebih_panjang_dari_batas_tetap_terukur() {
    // panjang asli tetap terbaca walau lebih panjang dari buffer lokal --
    // otaDecodeChunkData yang menolaknya sebagai TOO_LONG, bukan parser amplop.
    char big[2000];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    JsonDocument doc;
    doc["id"] = "ota-1"; doc["index"] = 0; doc["data"] = big;
    char j[3000];
    serializeJson(doc, j, sizeof(j));
    char id[OTA_MANIFEST_ID_MAX + 1]; uint32_t idx = 0;
    char b64[OTA_CHUNK_MAX_BASE64 + 1]; size_t b64len = 0;
    TEST_ASSERT_TRUE(otaParseChunkEnvelope(j, strlen(j), id, idx, b64, b64len));
    TEST_ASSERT_EQUAL(1999, (int)b64len);
}

// --- otaDecodeChunkData ---

static void test_decode_vektor_rfc4648() {
    uint8_t out[OTA_CHUNK_MAX_BYTES]; size_t n = 0;
    TEST_ASSERT_TRUE(OtaDataStatus::OK == otaDecodeChunkData("Zg==", 4, out, sizeof(out), n));
    TEST_ASSERT_EQUAL(1, (int)n); TEST_ASSERT_EQUAL_HEX8('f', out[0]);
    TEST_ASSERT_TRUE(OtaDataStatus::OK == otaDecodeChunkData("Zm8=", 4, out, sizeof(out), n));
    TEST_ASSERT_EQUAL(2, (int)n);
    TEST_ASSERT_TRUE(OtaDataStatus::OK == otaDecodeChunkData("Zm9v", 4, out, sizeof(out), n));
    TEST_ASSERT_EQUAL(3, (int)n);
    TEST_ASSERT_EQUAL_UINT8('f', out[0]); TEST_ASSERT_EQUAL_UINT8('o', out[1]); TEST_ASSERT_EQUAL_UINT8('o', out[2]);
    TEST_ASSERT_TRUE(OtaDataStatus::OK == otaDecodeChunkData("Zm9vYmFy", 8, out, sizeof(out), n));
    TEST_ASSERT_EQUAL(6, (int)n);
    TEST_ASSERT_EQUAL_UINT8('f', out[0]); TEST_ASSERT_EQUAL_UINT8('r', out[5]);
}

static void test_decode_kosong() {
    uint8_t out[OTA_CHUNK_MAX_BYTES]; size_t n = 999;
    TEST_ASSERT_TRUE(OtaDataStatus::EMPTY == otaDecodeChunkData("", 0, out, sizeof(out), n));
}

static void test_decode_terlalu_panjang() {
    char big[OTA_CHUNK_MAX_BASE64 + 5];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    uint8_t out[OTA_CHUNK_MAX_BYTES]; size_t n = 0;
    TEST_ASSERT_TRUE(OtaDataStatus::TOO_LONG == otaDecodeChunkData(big, strlen(big), out, sizeof(out), n));
}

static void test_decode_base64_rusak() {
    uint8_t out[OTA_CHUNK_MAX_BYTES]; size_t n = 0;
    TEST_ASSERT_TRUE(OtaDataStatus::INVALID_BASE64 == otaDecodeChunkData("!!!!not-b64", 11, out, sizeof(out), n));
}

static void test_decode_pas_1152_byte_ok() {
    // 1536 char base64 (tanpa padding, 1536 % 4 == 0) -> 1152 byte biner persis
    char full[OTA_CHUNK_MAX_BASE64 + 1];
    for (int i = 0; i < OTA_CHUNK_MAX_BASE64; i++) full[i] = "ABCD"[i % 4];
    full[OTA_CHUNK_MAX_BASE64] = 0;
    uint8_t out[OTA_CHUNK_MAX_BYTES]; size_t n = 0;
    TEST_ASSERT_TRUE(OtaDataStatus::OK == otaDecodeChunkData(full, strlen(full), out, sizeof(out), n));
    TEST_ASSERT_EQUAL(OTA_CHUNK_MAX_BYTES, (int)n);
}

// --- builder ack / status ---

static void test_build_ack_manifest() {
    char buf[512];
    size_t n = otaBuildAck("ota-1", false, 0, "accepted", "", 0, 0, 1785000000, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("ota-1", doc["id"]);
    TEST_ASSERT_EQUAL_STRING("manifest", doc["kind"]);
    TEST_ASSERT_EQUAL_STRING("accepted", doc["result"]);
    TEST_ASSERT_TRUE(doc["index"].isNull());
    TEST_ASSERT_TRUE(doc["received_bytes"].isNull());
    TEST_ASSERT_TRUE(doc["next_index"].isNull());
    TEST_ASSERT_EQUAL_UINT32(1785000000u, doc["ts"].as<uint32_t>());
}

static void test_build_ack_chunk() {
    char buf[512];
    size_t n = otaBuildAck("ota-1", true, 0, "accepted", "", 1152, 1, 1785000001, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("chunk", doc["kind"]);
    TEST_ASSERT_EQUAL(0, (int)doc["index"]);
    TEST_ASSERT_EQUAL(1152, (int)doc["received_bytes"]);
    TEST_ASSERT_EQUAL(1, (int)doc["next_index"]);
}

static void test_build_ack_ts_nol_saat_ntp_belum_sinkron() {
    char buf[512];
    size_t n = otaBuildAck("ota-1", false, 0, "rejected", "invalid_manifest", 0, 0, 8, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL(0, (int)doc["ts"]);
    TEST_ASSERT_EQUAL_STRING("invalid_manifest", doc["detail"]);
}

static void test_build_status() {
    OtaStatusFields f{};
    f.id = "ota-1"; f.state = "downloading"; f.image_type = "gateway";
    f.hardware = OTA_HARDWARE_ID; f.gateway_id = "58E6C5218C78";
    f.gateway_firmware_version = "bess-0.3.0"; f.running_gateway_firmware_version = "bess-0.2.0";
    f.running_partition = "app0"; f.sha256 = SHA_HEX; f.received_bytes = 1152;
    f.detail = ""; f.ts = 1785000002;
    char buf[768];
    size_t n = otaBuildStatus(f, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("downloading", doc["state"]);
    TEST_ASSERT_EQUAL_STRING("app0", doc["running_partition"]);
    TEST_ASSERT_EQUAL_STRING("bess-0.2.0", doc["running_gateway_firmware_version"]);
    TEST_ASSERT_EQUAL(1152, (int)doc["received_bytes"]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_manifest_ok);
    RUN_TEST(test_manifest_missing_id);
    RUN_TEST(test_manifest_id_too_long);
    RUN_TEST(test_manifest_id_batas_128_ok);
    RUN_TEST(test_manifest_wrong_image_type);
    RUN_TEST(test_manifest_wrong_hardware);
    RUN_TEST(test_manifest_wrong_encoding);
    RUN_TEST(test_manifest_image_size_nol);
    RUN_TEST(test_manifest_image_size_terlalu_besar);
    RUN_TEST(test_manifest_image_size_tepat_batas_atas_ok);
    RUN_TEST(test_manifest_sha256_pendek);
    RUN_TEST(test_manifest_sha256_bukan_hex);
    RUN_TEST(test_manifest_signature_kosong);
    RUN_TEST(test_manifest_signature_panjang_salah);
    RUN_TEST(test_manifest_chunk_count_tidak_pas);
    RUN_TEST(test_manifest_json_rusak);
    RUN_TEST(test_chunk_baru_berurutan);
    RUN_TEST(test_chunk_pertama_index_nol);
    RUN_TEST(test_chunk_duplikat_terakhir);
    RUN_TEST(test_chunk_basi_lebih_lama_dari_duplikat);
    RUN_TEST(test_chunk_tak_terduga_melompat_maju);
    RUN_TEST(test_chunk_job_salah_id_beda);
    RUN_TEST(test_chunk_job_salah_tak_ada_job_aktif);
    RUN_TEST(test_chunk_tak_terduga_job_sudah_penuh);
    RUN_TEST(test_envelope_ok);
    RUN_TEST(test_envelope_field_hilang);
    RUN_TEST(test_envelope_json_rusak);
    RUN_TEST(test_envelope_data_lebih_panjang_dari_batas_tetap_terukur);
    RUN_TEST(test_decode_vektor_rfc4648);
    RUN_TEST(test_decode_kosong);
    RUN_TEST(test_decode_terlalu_panjang);
    RUN_TEST(test_decode_base64_rusak);
    RUN_TEST(test_decode_pas_1152_byte_ok);
    RUN_TEST(test_build_ack_manifest);
    RUN_TEST(test_build_ack_chunk);
    RUN_TEST(test_build_ack_ts_nol_saat_ntp_belum_sinkron);
    RUN_TEST(test_build_status);
    return UNITY_END();
}
