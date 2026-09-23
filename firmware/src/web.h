#pragma once
#include <WebServer.h>

// web.cpp -- kerangka web server sinkron (WebServer bawaan core, sama pola
// dengan tim). webTick() = handleClient() dipanggil dari loop() -- loop()
// diawasi task watchdog 120 dtk, jadi SEMUA handler wajib cepat: jangan
// menunggu lock esp-mqtt atau bus Modbus.
//
// Sub-proyek E mendaftarkan /wifi + /api/wifi/* sendiri lewat webInit();
// sub-proyek F menambahkan GET/POST /api/auto/config di file yang sama.
// Modul lain (H: "/" dashboard, /api/data, /api/command, /api/acks,
// /api/firmware_versions) memanggil webServer() untuk mendaftarkan rute
// tambahan tanpa perlu WebServer instance sendiri.
void webInit();
void webTick();

WebServer& webServer();
