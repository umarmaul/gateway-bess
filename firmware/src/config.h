#pragma once
// Pin — fakta hardware dari BEPESP32_WiFi_Extension/src/Config.h
#define PIN_BESS_RX        21
#define PIN_BESS_TX        20
#define PIN_BESS_REDE      22
#define PIN_LED_BESS       18
#define PIN_LED_WIFI       14
// Modbus BESS (BSL AC series V2.1.0)
#define BESS_BAUD          9600
#define BESS_NODE          1
#define MB_FRAME_GAP_MS    105     // spec: >= 100 ms
#define MB_TIMEOUT_MS      500
#define MB_RETRIES         2       // total 3 percobaan
#define POLL_PERIOD_MS     1500
#define COMM_LOST_AFTER    3       // siklus gagal beruntun
// Register kunci
#define REG_TELEM_START    1050
#define REG_TELEM_COUNT    59
#define REG_ALARM_START    2050
#define REG_ALARM_COUNT    8
#define REG_STATUS         2057    // word status (bit 6 Run, bit 11 Shutdown)
#define REG_P_SET          3050
#define REG_PARAM_START    3146
#define REG_PARAM_COUNT    39      // 3146..3184 (rated .. soc)
#define REG_ONOFF          5050
// MQTT
#define MQTT_KEEPALIVE_S       300
#define MQTT_NETWORK_TIMEOUT_MS 60000
#define MQTT_WRITE_BUFFER      24576   // sama dengan BEPESP32_WiFi_Extension
#define MQTT_READ_BUFFER       2048
#define TELEMETRY_PERIOD_MS    60000
#define TELEMETRY_JSON_MAX     8192    // buffer JSON telemetri (terukur ~3,5 KB)
#define ACK_JSON_MAX           512     // buffer builder buildAckJson (command)
// Antrean generik mqtt_tx: satu FIFO untuk topic ack/ota_ack/ota_status
// (menggantikan antrean khusus ack Task 15). item.retain menentukan flag
// retain per pesan (status OTA retained, ack/ota_ack tidak).
#define MQTT_TX_JSON_MAX        1024    // >= ACK_JSON_MAX lama, cukup utk status OTA
#define TX_QUEUE_LEN            8       // pesan menunggu koneksi MQTT
#define TX_MAX_AGE_MS           600000  // tertahan >10 menit dibuang (basi)
// Batas command masuk = buffer baca esp-mqtt. Pesan yang lebih besar sudah
// dipotong esp-mqtt dan dibuang di mqtt_link, jadi RawCmd seukuran ini tidak
// pernah memotong JSON lagi (dulu 512 B -> ack bad_json menyesatkan).
#define CMD_JSON_MAX           MQTT_READ_BUFFER
// Task watchdog untuk loop/task_bess/task_cmd. Tak satu pun menunggu lock
// esp-mqtt (semua kiriman lewat task mqtt_tx yang tidak diawasi), jadi batas
// ini hanya perlu melampaui jalur terpanjang Modbus (~30 dtk) dengan margin.
#define WDT_TIMEOUT_S          120
#define FW_VERSION         "bess-0.3.0"

// OTA gateway via MQTT (sub-proyek G). Kontrak field/ukuran chunk ada di
// lib/bess_core/ota_logic.h (dipakai native test); konstanta di sini
// khusus ESP (timeout, buffer, kunci publik default).
#define OTA_QUEUE_LEN            2       // manifest+chunk in-flight (server 1 in-flight)
#define OTA_JSON_MAX             MQTT_READ_BUFFER  // amplop chunk (~1,7 KB) muat di sini
#define OTA_STATUS_JSON_MAX      768
#define OTA_JOB_TIMEOUT_MS       120000UL            // tanpa chunk baru -> job gagal
#define OTA_ROLLBACK_PENDING_MS  (15UL * 60UL * 1000UL)  // batas mark-valid pasca boot
// Kunci publik Ed25519 tim (base64, 32 byte) -- image yang ditandatangani
// server cloud yang sama diterima tanpa konfigurasi tambahan. Bench boleh
// menimpanya di secrets.h dengan kunci dev sendiri (lihat secrets.example.h).
#ifndef OTA_ED25519_PUBKEY_B64
#define OTA_ED25519_PUBKEY_B64 "X76lzB83YKaD9wf/qBa5eV5/Rnm1PIzRckIdgNkIC98="
#endif
