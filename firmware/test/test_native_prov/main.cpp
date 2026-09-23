#include <unity.h>
#include <string.h>
#include "prov_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

// ---------------------------------------------------------------------------
// validasi SSID / password STA
// ---------------------------------------------------------------------------
static void test_ssid_bounds() {
    TEST_ASSERT_FALSE(provValidSsid(""));                 // 0 char
    TEST_ASSERT_TRUE(provValidSsid("A"));                  // 1 char
    TEST_ASSERT_TRUE(provValidSsid("Lantai 2"));
    TEST_ASSERT_TRUE(provValidSsid("12345678901234567890123456789012"));  // 32 char? cek di bawah
    char s32[33]; memset(s32, 'x', 32); s32[32] = 0;
    TEST_ASSERT_TRUE(provValidSsid(s32));                   // tepat 32
    char s33[34]; memset(s33, 'x', 33); s33[33] = 0;
    TEST_ASSERT_FALSE(provValidSsid(s33));                  // 33 -> tolak
}

static void test_sta_pass_bounds() {
    TEST_ASSERT_TRUE(provValidStaPass(""));                 // kosong = open network
    char p63[64]; memset(p63, 'x', 63); p63[63] = 0;
    TEST_ASSERT_TRUE(provValidStaPass(p63));
    char p64[65]; memset(p64, 'x', 64); p64[64] = 0;
    TEST_ASSERT_FALSE(provValidStaPass(p64));
}

// ---------------------------------------------------------------------------
// password AP -- deviasi sadar: kosong DILARANG
// ---------------------------------------------------------------------------
static void test_ap_pass_bounds() {
    TEST_ASSERT_FALSE(provValidApPass(""));                 // kosong dilarang (deviasi)
    TEST_ASSERT_FALSE(provValidApPass("1234567"));           // 7 char -- masih kurang
    TEST_ASSERT_TRUE(provValidApPass("12345678"));           // tepat 8
    char p63[64]; memset(p63, 'x', 63); p63[63] = 0;
    TEST_ASSERT_TRUE(provValidApPass(p63));
    char p64[65]; memset(p64, 'x', 64); p64[64] = 0;
    TEST_ASSERT_FALSE(provValidApPass(p64));
}

// ---------------------------------------------------------------------------
// hostname mDNS
// ---------------------------------------------------------------------------
static void test_mdns_hostname() {
    TEST_ASSERT_TRUE(provValidMdnsHostname("bep-bess-gateway"));
    TEST_ASSERT_TRUE(provValidMdnsHostname("a"));
    TEST_ASSERT_TRUE(provValidMdnsHostname("gw01"));
    TEST_ASSERT_FALSE(provValidMdnsHostname(""));                // kosong
    TEST_ASSERT_FALSE(provValidMdnsHostname("Bep-Gateway"));      // huruf besar
    TEST_ASSERT_FALSE(provValidMdnsHostname("-bep"));             // awali strip
    TEST_ASSERT_FALSE(provValidMdnsHostname("bep-"));             // akhiri strip
    TEST_ASSERT_FALSE(provValidMdnsHostname("bep_gw"));           // underscore tak diizinkan
    TEST_ASSERT_FALSE(provValidMdnsHostname("bep gw"));           // spasi tak diizinkan
    char h63[64]; memset(h63, 'a', 63); h63[63] = 0;
    TEST_ASSERT_TRUE(provValidMdnsHostname(h63));
    char h64[65]; memset(h64, 'a', 64); h64[64] = 0;
    TEST_ASSERT_FALSE(provValidMdnsHostname(h64));
}

// ---------------------------------------------------------------------------
// parse IPv4
// ---------------------------------------------------------------------------
static void test_parse_ipv4_valid() {
    uint8_t o[4];
    TEST_ASSERT_TRUE(provParseIPv4("192.168.1.50", o));
    TEST_ASSERT_EQUAL_UINT8(192, o[0]); TEST_ASSERT_EQUAL_UINT8(168, o[1]);
    TEST_ASSERT_EQUAL_UINT8(1, o[2]);   TEST_ASSERT_EQUAL_UINT8(50, o[3]);

    TEST_ASSERT_TRUE(provParseIPv4("0.0.0.0", o));
    TEST_ASSERT_EQUAL_UINT8(0, o[0]);

    TEST_ASSERT_TRUE(provParseIPv4("255.255.255.255", o));
    TEST_ASSERT_EQUAL_UINT8(255, o[3]);
}

static void test_parse_ipv4_invalid() {
    uint8_t o[4];
    TEST_ASSERT_FALSE(provParseIPv4("", o));
    TEST_ASSERT_FALSE(provParseIPv4("256.1.1.1", o));
    TEST_ASSERT_FALSE(provParseIPv4("1.2.3", o));
    TEST_ASSERT_FALSE(provParseIPv4("1.2.3.4.5", o));
    TEST_ASSERT_FALSE(provParseIPv4("a.b.c.d", o));
    TEST_ASSERT_FALSE(provParseIPv4(" 1.2.3.4", o));
    TEST_ASSERT_FALSE(provParseIPv4("1.2.3.4x", o));
    TEST_ASSERT_FALSE(provParseIPv4("1.2.3.4.", o));
    TEST_ASSERT_FALSE(provParseIPv4("1234.1.1.1", o));
    TEST_ASSERT_FALSE(provParseIPv4("300.1.1.1", o));
}

// ---------------------------------------------------------------------------
// gateway_code
// ---------------------------------------------------------------------------
static uint32_t s_seq = 0;
static uint32_t seqRand() { return s_seq++; }

static void test_gen_gateway_code_charset_and_len() {
    s_seq = 0;
    char code[PROV_GATEWAY_CODE_LEN + 1];
    provGenGatewayCode(seqRand, code);
    TEST_ASSERT_EQUAL(PROV_GATEWAY_CODE_LEN, (int)strlen(code));
    for (int i = 0; i < PROV_GATEWAY_CODE_LEN; i++) {
        char c = code[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        TEST_ASSERT_TRUE(ok);
    }
}

static uint32_t s_fixed = 0;
static uint32_t fixedRand() { return s_fixed; }

static void test_gen_gateway_code_deterministic() {
    s_fixed = 0;   // alfabet[0] = 'A'
    char code[PROV_GATEWAY_CODE_LEN + 1];
    provGenGatewayCode(fixedRand, code);
    for (int i = 0; i < PROV_GATEWAY_CODE_LEN; i++) TEST_ASSERT_EQUAL(('A'), code[i]);

    s_fixed = 35;  // alfabet[35] = '9' (36 karakter: A-Z0-9)
    provGenGatewayCode(fixedRand, code);
    for (int i = 0; i < PROV_GATEWAY_CODE_LEN; i++) TEST_ASSERT_EQUAL(('9'), code[i]);
}

// ---------------------------------------------------------------------------
// pembanding code (waktu-konstan)
// ---------------------------------------------------------------------------
static void test_code_equals() {
    TEST_ASSERT_TRUE(provCodeEquals("AB12CD", "AB12CD"));
    TEST_ASSERT_FALSE(provCodeEquals("AB12CE", "AB12CD"));   // beda 1 char terakhir
    TEST_ASSERT_FALSE(provCodeEquals("XB12CD", "AB12CD"));   // beda char pertama
    TEST_ASSERT_FALSE(provCodeEquals("AB12C", "AB12CD"));    // kurang panjang
    TEST_ASSERT_FALSE(provCodeEquals("AB12CDE", "AB12CD"));  // kelebihan panjang
    TEST_ASSERT_FALSE(provCodeEquals("", "AB12CD"));
}

// ---------------------------------------------------------------------------
// status AP fallback
// ---------------------------------------------------------------------------
static void test_ap_should_be_on() {
    const uint32_t AFTER = 5 * 60 * 1000UL;   // 5 menit
    TEST_ASSERT_TRUE(provApShouldBeOn(false, 0, AFTER));           // STA putus -> AP nyala
    TEST_ASSERT_TRUE(provApShouldBeOn(false, 999999, AFTER));      // putus, ms diabaikan
    TEST_ASSERT_TRUE(provApShouldBeOn(true, 0, AFTER));            // baru connect -> AP masih nyala
    TEST_ASSERT_TRUE(provApShouldBeOn(true, AFTER - 1, AFTER));    // tepat sebelum batas
    TEST_ASSERT_FALSE(provApShouldBeOn(true, AFTER, AFTER));       // tepat di batas -> mati
    TEST_ASSERT_FALSE(provApShouldBeOn(true, AFTER + 1000, AFTER));// lewat batas -> mati
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ssid_bounds);
    RUN_TEST(test_sta_pass_bounds);
    RUN_TEST(test_ap_pass_bounds);
    RUN_TEST(test_mdns_hostname);
    RUN_TEST(test_parse_ipv4_valid);
    RUN_TEST(test_parse_ipv4_invalid);
    RUN_TEST(test_gen_gateway_code_charset_and_len);
    RUN_TEST(test_gen_gateway_code_deterministic);
    RUN_TEST(test_code_equals);
    RUN_TEST(test_ap_should_be_on);
    return UNITY_END();
}
