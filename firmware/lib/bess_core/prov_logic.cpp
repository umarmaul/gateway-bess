#include "prov_logic.h"
#include <string.h>

static size_t slen(const char* s) { return s ? strlen(s) : 0; }

bool provValidSsid(const char* ssid) {
    size_t n = slen(ssid);
    return n >= 1 && n <= 32;
}

bool provValidStaPass(const char* pass) {
    size_t n = slen(pass);
    return n <= 63;
}

bool provValidApPass(const char* pass) {
    // Kosong DILARANG di sini -- lihat komentar deviasi di prov_logic.h.
    size_t n = slen(pass);
    return n >= 8 && n <= 63;
}

bool provValidMdnsHostname(const char* host) {
    size_t n = slen(host);
    if (n < 1 || n > 63) return false;
    for (size_t i = 0; i < n; i++) {
        char c = host[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    if (host[0] == '-' || host[n - 1] == '-') return false;
    return true;
}

bool provParseIPv4(const char* s, uint8_t out[4]) {
    if (!s) return false;
    int octs[4];
    int idx = 0;
    long val = 0;
    int digits = 0;
    for (const char* p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            digits++;
            if (digits > 3 || val > 255) return false;
        } else if (c == '.' || c == '\0') {
            if (digits == 0) return false;      // ".." / leading '.' / string kosong
            if (idx >= 4) return false;          // sudah 4 oktet, tak boleh ada lagi
            octs[idx++] = (int)val;
            val = 0;
            digits = 0;
            if (c == '\0') break;
            if (idx == 4) return false;          // 4 oktet tercapai tapi masih ada '.' menyusul
        } else {
            return false;                        // karakter di luar digit/'.'
        }
    }
    if (idx != 4) return false;
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)octs[i];
    return true;
}

void provGenGatewayCode(ProvRandFn rand_fn, char out[PROV_GATEWAY_CODE_LEN + 1]) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";  // 36 char
    for (int i = 0; i < PROV_GATEWAY_CODE_LEN; i++) {
        uint32_t r = rand_fn ? rand_fn() : 0;
        out[i] = alphabet[r % 36];
    }
    out[PROV_GATEWAY_CODE_LEN] = 0;
}

bool provCodeEquals(const char* input, const char* stored) {
    // Panjang bukan rahasia -- boleh dibandingkan lebih awal (strlen bukan
    // operasi rahasia di sini). Isinya yang dibandingkan waktu-konstan.
    if (!input || !stored) return false;
    if (strlen(input) != PROV_GATEWAY_CODE_LEN) return false;
    uint8_t diff = 0;
    for (int i = 0; i < PROV_GATEWAY_CODE_LEN; i++)
        diff |= (uint8_t)input[i] ^ (uint8_t)stored[i];
    return diff == 0;
}

bool provApShouldBeOn(bool sta_connected, uint32_t ms_since_connect, uint32_t after_connect_ms) {
    if (!sta_connected) return true;
    return ms_since_connect < after_connect_ms;
}
