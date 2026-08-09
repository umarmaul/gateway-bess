#include "modbus_port.h"
#include <Arduino.h>
#include "config.h"
#include "crc16.h"
#include "mb_frame.h"

static SemaphoreHandle_t mb_mtx;
static uint32_t last_frame_ms = 0;

void mbPortInit() {
    mb_mtx = xSemaphoreCreateMutex();
    pinMode(PIN_BESS_REDE, OUTPUT);
    digitalWrite(PIN_BESS_REDE, LOW);           // RX
    Serial1.begin(BESS_BAUD, SERIAL_8N1, PIN_BESS_RX, PIN_BESS_TX);
}

static size_t xfer(const uint8_t* req, size_t reqlen, uint8_t* resp, size_t cap) {
    while (millis() - last_frame_ms < MB_FRAME_GAP_MS) vTaskDelay(pdMS_TO_TICKS(5));
    while (Serial1.available()) Serial1.read();  // buang sisa
    digitalWrite(PIN_BESS_REDE, HIGH);
    Serial1.write(req, reqlen);
    Serial1.flush();
    digitalWrite(PIN_BESS_REDE, LOW);
    size_t got = 0;
    uint32_t t0 = millis(), last_rx = millis();
    while (millis() - t0 < MB_TIMEOUT_MS) {
        while (Serial1.available() && got < cap) {
            resp[got++] = Serial1.read();
            last_rx = millis();
        }
        // frame dianggap selesai: ada data & sunyi 10 ms
        if (got >= 5 && millis() - last_rx > 10) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    last_frame_ms = millis();
    return got;
}

template <typename ParseFn>
static MbStatus doReq(const uint8_t* req, size_t reqlen, ParseFn parse) {
    xSemaphoreTake(mb_mtx, portMAX_DELAY);
    MbStatus st = MB_TIMEOUT;
    for (int attempt = 0; attempt <= MB_RETRIES; attempt++) {
        uint8_t resp[300];
        size_t n = xfer(req, reqlen, resp, sizeof(resp));
        st = (n == 0) ? MB_TIMEOUT : parse(resp, n);
        if (st == MB_OK || st == MB_EXCEPTION) break;   // exception = jawaban sah
    }
    xSemaphoreGive(mb_mtx);
    return st;
}

MbStatus mbReadRegs(uint8_t node, uint16_t start, uint16_t count,
                    uint16_t* out, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildRead(node, start, count, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseReadResp(r, len, node, count, out, exc);
    });
}

MbStatus mbWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildWrite6(node, id, val, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseEcho(r, len, node, 6, exc);
    });
}

MbStatus mbWrite5(uint8_t node, uint16_t id, bool on, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildWrite5(node, id, on, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseEcho(r, len, node, 5, exc);
    });
}
