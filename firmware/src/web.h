#pragma once
#include <WebServer.h>

// web.cpp -- kerangka web server sinkron (WebServer bawaan core, sama pola
// dengan tim).
//
// TEMUAN REVIEW 23 Sep 2026 (TINGGI): WebServer::_parseRequest() (library
// core, framework-arduinoespressif32/libraries/WebServer/src/Parsing.cpp)
// membaca body POST lewat readBytesWithTimeout() SEBELUM handler dipanggil
// (dan sebelum cek Content-Length berlebihan) -- fungsi itu me-reset jatah
// tunggunya (HTTP_MAX_POST_WAIT, default 5000 ms, TIDAK dibungkus #ifndef di
// WebServer.h jadi tidak bisa ditimpa lewat build_flags tanpa menimpa file
// library) setiap kali SATU byte baru tiba. Klien yang sengaja mengirim body
// 1 byte tiap <5 dtk ("slowloris") bisa menahan handleClient() menggantung
// nyaris tanpa batas. Kalau ini dipanggil dari loop() (diawasi task watchdog
// WDT_TIMEOUT_S=120 dtk), klien seperti itu memicu panic TASK_WDT -> reboot
// gateway -- BESS/Modbus, MQTT, dan command ikut mati walau tidak ada yang
// salah di jalur itu.
//
// Fix: handleClient() dipanggil dari task terpisah, task_web (webTaskStart(),
// prioritas 1), yang SENGAJA TIDAK didaftarkan ke task watchdog -- pola sama
// dengan task_ota/mqtt_tx (lihat task_ota.cpp/mqtt_link.cpp: keduanya juga
// dikecualikan karena bisa menunggu lama di luar kendali kode kita). loop()
// TIDAK LAGI memanggil webTick() sama sekali.
//
// Residual risk (didokumentasikan juga di README §Kerangka web server):
// (1) WebServer bawaan hanya memproses SATU koneksi per handleClient() --
//     satu klien lambat masih bisa membuat server HTTP itu sendiri tak
//     responsif untuk klien LAIN sementara, tapi TIDAK lagi mereboot gateway
//     (BESS/Modbus/MQTT/command jalan di task lain, tidak terpengaruh).
// (2) readBytesWithTimeout() mem-malloc/realloc seluruh body sebelum handler
//     jalan -- body besar yang dikirim CEPAT (bukan slowloris) tetap memakan
//     heap sementara selama request itu diproses; tidak ada guard tambahan
//     untuk itu di luar Content-Length yang dikirim klien.
//
// Sub-proyek E mendaftarkan /wifi + /api/wifi/* sendiri lewat webInit();
// sub-proyek F menambahkan GET/POST /api/auto/config di file yang sama.
// Modul lain (H: "/" dashboard, /api/data, /api/command, /api/acks,
// /api/firmware_versions) memanggil webServer() untuk mendaftarkan rute
// tambahan tanpa perlu WebServer instance sendiri.
void webInit();       // panggil di setup(): daftarkan rute + s_server.begin()
void webTaskStart();  // panggil di setup() SETELAH semua *Init() pendaftar rute
                       // (webInit(), webDashboardInit(), ...) -- start task_web

WebServer& webServer();
