#include <unity.h>
#include <string.h>
#include "crc16.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_vectors_pdf() {
    const uint8_t f1[] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03};
    TEST_ASSERT_EQUAL_HEX16(0x3C25, mbCrc16(f1, sizeof(f1)));  // kirim: 25 3C
    const uint8_t f2[] = {0x01, 0x06, 0x0B, 0xEA, 0x03, 0xE8};
    TEST_ASSERT_EQUAL_HEX16(0xA4AA, mbCrc16(f2, sizeof(f2)));  // kirim: AA A4
    const uint8_t f3[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_HEX16(0x5BA9, mbCrc16(f3, sizeof(f3)));  // kirim: A9 5B
}

static void test_append_check() {
    uint8_t buf[8] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03};
    mbAppendCrc(buf, 6);
    TEST_ASSERT_EQUAL_HEX8(0x25, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, buf[7]);
    TEST_ASSERT_TRUE(mbCheckCrc(buf, 8));
    buf[7] ^= 0xFF;
    TEST_ASSERT_FALSE(mbCheckCrc(buf, 8));
    TEST_ASSERT_FALSE(mbCheckCrc(buf, 3));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_vectors_pdf);
    RUN_TEST(test_append_check);
    return UNITY_END();
}
