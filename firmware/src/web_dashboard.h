#pragma once

// web_dashboard.h -- sub-proyek H: dashboard "/" + API lokal (/api/data,
// /api/acks, /api/command, /api/firmware_versions). Rute didaftarkan lewat
// webServer() (lihat web.h) di atas WebServer instance yang SAMA dengan
// /wifi (E) dan /api/auto/config (F) -- tidak ada WebServer kedua.
void webDashboardInit();   // panggil sekali di setup(), SETELAH webInit()
