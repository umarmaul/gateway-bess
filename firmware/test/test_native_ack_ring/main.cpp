#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include "ack_ring.h"

void setUp(void) {}
void tearDown(void) {}

static void push(AckRing& r, const char* json) {
    ackRingPush(r, json, strlen(json));
}

// ---------------------------------------------------------------------------
// ackRingInit / ackRingBuildJson -- ring kosong
// ---------------------------------------------------------------------------
static void test_init_kosong() {
    AckRing r;
    ackRingInit(r);
    TEST_ASSERT_EQUAL(0, r.count);
    char buf[64];
    size_t n = ackRingBuildJson(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("[]", buf);
}

// ---------------------------------------------------------------------------
// Urutan: terbaru dulu
// ---------------------------------------------------------------------------
static void test_urutan_terbaru_dulu() {
    AckRing r;
    ackRingInit(r);
    push(r, "{\"id\":\"a\"}");
    push(r, "{\"id\":\"b\"}");
    push(r, "{\"id\":\"c\"}");
    TEST_ASSERT_EQUAL(3, r.count);

    char buf[256];
    size_t n = ackRingBuildJson(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonArray arr = doc.as<JsonArray>();
    TEST_ASSERT_EQUAL(3, (int)arr.size());
    TEST_ASSERT_EQUAL_STRING("c", arr[0]["id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("b", arr[1]["id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("a", arr[2]["id"].as<const char*>());
}

// ---------------------------------------------------------------------------
// Wrap-around: dorong > ACK_RING_CAP -- entri tertua ditimpa, ukuran tetap CAP
// ---------------------------------------------------------------------------
static void test_wrap_around_menimpa_tertua() {
    AckRing r;
    ackRingInit(r);
    char id[24];
    for (int i = 0; i < ACK_RING_CAP + 3; i++) {
        snprintf(id, sizeof(id), "{\"id\":\"%d\"}", i);
        push(r, id);
    }
    TEST_ASSERT_EQUAL(ACK_RING_CAP, r.count);

    char buf[ACK_RING_JSON_CAP];
    size_t n = ackRingBuildJson(r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    JsonArray arr = doc.as<JsonArray>();
    TEST_ASSERT_EQUAL(ACK_RING_CAP, (int)arr.size());
    // Terbaru = push terakhir (index ACK_RING_CAP+2), tertua yang tersisa =
    // push ke-3 (index 3) -- push 0,1,2 sudah tertimpa.
    TEST_ASSERT_EQUAL_STRING("10", arr[0]["id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("3", arr[ACK_RING_CAP - 1]["id"].as<const char*>());
}

// ---------------------------------------------------------------------------
// Entri lebih besar dari kapasitas -- dipotong dengan aman, tetap NUL-terminated
// ---------------------------------------------------------------------------
static void test_entri_kepanjangan_dipotong_aman() {
    AckRing r;
    ackRingInit(r);
    static char big[ACK_RING_ENTRY_MAX + 100];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    ackRingPush(r, big, strlen(big));
    TEST_ASSERT_EQUAL(1, r.count);
    TEST_ASSERT_TRUE(r.lens[0] == ACK_RING_ENTRY_MAX - 1);
    TEST_ASSERT_EQUAL(0, r.entries[0][ACK_RING_ENTRY_MAX - 1]);
}

// ---------------------------------------------------------------------------
// Buffer keluaran terlalu kecil -- gagal jujur (0), tidak menulis sampah
// ---------------------------------------------------------------------------
static void test_buffer_keluaran_kurang() {
    AckRing r;
    ackRingInit(r);
    push(r, "{\"id\":\"abcdefgh\"}");
    char buf[4];
    size_t n = ackRingBuildJson(r, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_init_kosong);
    RUN_TEST(test_urutan_terbaru_dulu);
    RUN_TEST(test_wrap_around_menimpa_tertua);
    RUN_TEST(test_entri_kepanjangan_dipotong_aman);
    RUN_TEST(test_buffer_keluaran_kurang);
    return UNITY_END();
}
