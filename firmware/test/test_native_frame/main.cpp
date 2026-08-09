#include <unity.h>
#include <string.h>
#include "mb_frame.h"

static void test_build_read_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildRead(1, 1050, 3, buf);
    const uint8_t exp[] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03, 0x25, 0x3C};
    TEST_ASSERT_EQUAL(8, n);
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, 8);
}

static void test_build_write6_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildWrite6(1, 3050, 1000, buf);
    const uint8_t exp[] = {0x01, 0x06, 0x0B, 0xEA, 0x03, 0xE8, 0xAA, 0xA4};
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, n);
}

static void test_build_write5_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildWrite5(1, 5050, true, buf);
    const uint8_t exp[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00, 0xA9, 0x5B};
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, n);
}

static void test_parse_read_resp() {
    const uint8_t resp[] = {0x01, 0x03, 0x06, 0x08, 0x98, 0x08, 0x98,
                            0x08, 0x98, 0x84, 0x04};
    uint16_t vals[3]; uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_OK, mbParseReadResp(resp, 11, 1, 3, vals, &exc));
    TEST_ASSERT_EQUAL_HEX16(0x0898, vals[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0898, vals[2]);
}

static void test_parse_exception() {
    uint8_t resp[5] = {0x01, 0x83, 0x02};
    mbAppendCrc(resp, 3);
    uint16_t vals[1]; uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_EXCEPTION, mbParseReadResp(resp, 5, 1, 1, vals, &exc));
    TEST_ASSERT_EQUAL(2, exc);
}

static void test_parse_crc_salah() {
    uint8_t resp[] = {0x01, 0x03, 0x02, 0x04, 0x00, 0xBA, 0x85};
    uint16_t vals[1]; uint8_t exc;
    TEST_ASSERT_EQUAL(MB_CRC, mbParseReadResp(resp, 7, 1, 1, vals, &exc));
}

static void test_parse_echo_write() {
    uint8_t resp[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00, 0xA9, 0x5B};
    uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_OK, mbParseEcho(resp, 8, 1, 5, &exc));
    uint8_t ex6[5] = {0x01, 0x86, 0x06};
    mbAppendCrc(ex6, 3);
    TEST_ASSERT_EQUAL(MB_EXCEPTION, mbParseEcho(ex6, 5, 1, 6, &exc));
    TEST_ASSERT_EQUAL(6, exc);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_build_read_contoh_pdf);
    RUN_TEST(test_build_write6_contoh_pdf);
    RUN_TEST(test_build_write5_contoh_pdf);
    RUN_TEST(test_parse_read_resp);
    RUN_TEST(test_parse_exception);
    RUN_TEST(test_parse_crc_salah);
    RUN_TEST(test_parse_echo_write);
    return UNITY_END();
}
