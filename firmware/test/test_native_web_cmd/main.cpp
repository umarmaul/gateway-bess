#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include "web_cmd.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Tanpa "id" -- dibangkitkan "web-<millis>"
// ---------------------------------------------------------------------------
static void test_tanpa_id_dibangkitkan() {
    const char* body = "{\"cmd\":\"enable\"}";
    char out[256]; size_t n = 0;
    char id[40];
    bool ok = webCmdEnsureId(body, strlen(body), 12345, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("web-12345", id);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, out, n) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("web-12345", doc["id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("enable", doc["cmd"].as<const char*>());
}

// ---------------------------------------------------------------------------
// id kosong "" -- diperlakukan sama seperti tidak ada
// ---------------------------------------------------------------------------
static void test_id_kosong_dibangkitkan() {
    const char* body = "{\"id\":\"\",\"cmd\":\"disable\"}";
    char out[256]; size_t n = 0;
    char id[40];
    bool ok = webCmdEnsureId(body, strlen(body), 999, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("web-999", id);
}

// ---------------------------------------------------------------------------
// id sudah ada -- dipertahankan apa adanya, args lain tetap utuh
// ---------------------------------------------------------------------------
static void test_id_sudah_ada_dipertahankan() {
    const char* body = "{\"id\":\"op-1\",\"cmd\":\"set_output\",\"args\":{\"power_w\":1500}}";
    char out[256]; size_t n = 0;
    char id[40];
    bool ok = webCmdEnsureId(body, strlen(body), 5, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("op-1", id);

    JsonDocument doc;
    deserializeJson(doc, out, n);
    TEST_ASSERT_EQUAL_STRING("op-1", doc["id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("set_output", doc["cmd"].as<const char*>());
    TEST_ASSERT_EQUAL_FLOAT(1500, doc["args"]["power_w"].as<float>());
}

// ---------------------------------------------------------------------------
// JSON rusak -- gagal, out/id TIDAK disentuh (caller pakai body asli)
// ---------------------------------------------------------------------------
static void test_json_rusak_gagal() {
    const char* body = "{\"cmd\":\"enable\"";   // kurung tak ditutup
    char out[256] = "SENTINEL"; size_t n = 999;
    char id[40] = "SENTINEL";
    bool ok = webCmdEnsureId(body, strlen(body), 1, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_STRING("SENTINEL", out);
    TEST_ASSERT_EQUAL_STRING("SENTINEL", id);
}

// ---------------------------------------------------------------------------
// Bukan objek JSON (array/scalar) -- gagal juga, walau valid secara JSON
// ---------------------------------------------------------------------------
static void test_bukan_objek_gagal() {
    const char* body = "[1,2,3]";
    char out[256]; size_t n = 0;
    char id[40];
    bool ok = webCmdEnsureId(body, strlen(body), 1, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Buffer keluaran terlalu kecil -- gagal jujur, bukan korup diam-diam
// ---------------------------------------------------------------------------
static void test_buffer_keluaran_kurang_gagal() {
    const char* body = "{\"cmd\":\"enable\",\"args\":{\"target\":1}}";
    char out[8]; size_t n = 0;
    char id[40];
    bool ok = webCmdEnsureId(body, strlen(body), 1, out, sizeof(out), n, id, sizeof(id));
    TEST_ASSERT_FALSE(ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tanpa_id_dibangkitkan);
    RUN_TEST(test_id_kosong_dibangkitkan);
    RUN_TEST(test_id_sudah_ada_dipertahankan);
    RUN_TEST(test_json_rusak_gagal);
    RUN_TEST(test_bukan_objek_gagal);
    RUN_TEST(test_buffer_keluaran_kurang_gagal);
    return UNITY_END();
}
