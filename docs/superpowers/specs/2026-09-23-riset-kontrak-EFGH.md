# Riset kontrak firmware tim (BEPESP32_WiFi_Extension, branch `origin/gateway-mqtt`)

Sumber: `git -C "D:\PT Bima Eco Power\embedded-system\BEPESP32_WiFi_Extension" show origin/gateway-mqtt:<path>`.
Repo TIDAK diubah (branch lokal `main` tetap aktif, tidak checkout). File dibaca (read-only,
dump ke scratchpad lokal): `src/MqttManager.cpp`, `src/MqttManager.h`, `src/NetworkManager.cpp`,
`src/NetworkManager.h`, `src/WebServerSetup.cpp`, `src/WebServerSetup.h`, `src/WebApi.cpp`,
`src/WebApi.h`, `src/AutoControl.cpp`, `src/AutoControl.h`, `src/Config.h`, `src/Globals.h`,
`src/Globals.cpp`, `src/Secrets.example.h`, `docs/MQTT_OTA_CONCEPT.md`, `docs/API_V1.md`,
`partitions.csv`. HEAD branch saat baca: `d994bb1` (13 Agu 2026, "Fix LED indicator, scheduling
reliability, and OTA focus when from MQTT").

Firmware ini: `BEPESP32_FIRMWARE_VERSION "1.1.6"`, `BEP_MDNS_HOSTNAME "bep-dev-gateway"`, broker
MQTT `mqtt-dev.bepbatt.id:1883` (plain TCP, belum TLS). Device ID = MAC WiFi STA (12 hex uppercase,
`esp_read_mac(ESP_MAC_WIFI_STA)`), fungsi `initializeDeviceId()` di `MqttManager.cpp`.

⚠️ File sensitif: `src/Secrets.h` **TIDAK ada** di branch ini (tidak tracked) — hanya
`src/Secrets.example.h` berisi placeholder kosong/`"change-me"` (WiFi SSID/pass default, AP
default, OTA password, MQTT user/pass). `Globals.cpp` meng-include `Secrets.h` kalau ada
(`__has_include`), fallback ke `Secrets.example.h`. Tidak ada kredensial asli ter-commit.

---

## G. OTA ESP via MQTT (Ed25519)

**Topics** (per `device_id`, mis. `58E6C5218B58`):
```
device/<id>/ota/manifest  server -> gateway   (QoS1, not retained)
device/<id>/ota/chunk     server -> gateway   (QoS1, not retained)
device/<id>/ota/ack       gateway -> server   (QoS1, not retained)
device/<id>/ota/status    gateway -> server   (QoS1, RETAINED)
```
Topic `device/<id>/command` TIDAK dipakai untuk data firmware — command biasa tetap di topic itu.
Satu firmware ini menangani DUA target: `image_type: "gateway"` (OTA diri sendiri, via
`esp_ota_ops`/`Update.h`) dan `image_type: "dcon"` (proxy flash STM32 via RS485 bootloader —
**bagian ini DCON-spesifik, tidak relevan untuk paritas gateway-bess**).

**Manifest JSON** (field gateway-relevan saja):
```json
{
  "id": "ota-20260731-001",
  "image_type": "gateway",
  "hardware": "bep-gateway-v1",
  "version": "1.0.1",
  "encoding": "base64",
  "image_size": 1245184,
  "sha256": "64-char lowercase hex SHA-256",
  "signature": "base64 signature (64 byte Ed25519, detached)",
  "chunk_count": 1081
}
```
Validasi manifest (semua wajib, lihat `processMqttOtaManifest` di `MqttManager.cpp:402`):
`id` non-kosong ≤128 char; `image_type` "gateway" (hardware harus persis string
`BEP_MQTT_OTA_GATEWAY_HARDWARE = "bep-gateway-v1"`, dikompilasi ke firmware — mismatch = reject);
`encoding == "base64"`; `image_size` 1..`BEP_MQTT_OTA_MAX_IMAGE_SIZE` (`0x1E0000` = 1.960.960 B,
sama dengan ukuran partisi app0/app1); `sha256` 64 hex char valid; `signature` ada;
`chunk_count == ceil(image_size / 1152)` (harus PAS, bukan sekadar cukup). Kalau
`ota_update_in_progress` atau job lain sedang aktif → reject `"busy"` (tanpa publish status).
Kalau field invalid → ack `rejected/invalid_manifest` + status `failed`.

**Verifikasi Ed25519** (fungsi `mqttOtaManifestSignatureValid`, pakai libsodium `sodium.h`
`crypto_sign_verify_detached`):
- Yang ditandatangani = **32 byte biner SHA-256 digest** (BUKAN manifest JSON, BUKAN image
  mentah) — `sha256` field di-decode dari hex ke 32 byte, itulah pesan yang diverifikasi.
- `signature` = base64 dari 64-byte detached Ed25519 signature atas 32 byte digest itu.
- Public key **32 byte, dikompilasi ke firmware** sebagai konstanta base64 di `Config.h`:
  `BEP_MQTT_OTA_ED25519_PUBLIC_KEY_B64 = "X76lzB83YKaD9wf/qBa5eV5/Rnm1PIzRckIdgNkIC98="`
  (bukan disimpan di NVS/flash terpisah — hardcoded compile-time).
- Verifikasi terjadi SEBELUM `Update.begin()` dipanggil dan sebelum satu byte firmware ditulis.
- Library: **libsodium** (`#include <sodium.h>`, `sodium_init()` dipanggil tiap verifikasi).

**Chunk flow** (`processMqttOtaChunk`, `MqttManager.cpp:493`):
```json
{"id": "ota-20260731-001", "index": 0, "data": "<base64, maks 1536 char>"}
```
- Base64 chunk maks **1536 karakter** → decode maks **1152 byte** (konstanta
  `BEP_MQTT_OTA_MAX_CHUNK_BASE64=1536`, `BEP_MQTT_OTA_MAX_CHUNK_BYTES=1152`). Alasan didokumentasi
  di `docs/MQTT_OTA_CONCEPT.md`: batas pesan MQTT gateway 2048 B (`BEP_MQTT_READ_BUFFER`), jadi
  base64+JSON envelope harus muat di situ; gateway TIDAK PERNAH menyimpan seluruh image di RAM.
- Chunk **wajib sekuensial mulai dari index 0** — server kirim chunk berikutnya HANYA setelah ack
  `accepted` untuk chunk sebelumnya (flow control ketat, satu chunk in-flight).
- Duplikat chunk (index == next_index - 1) → ack `accepted/"duplicate"` (idempotent, aman untuk
  retry QoS1). Chunk index < (next_index - 1) atau job salah → reject `stale_chunk`/`wrong_job`.
  Chunk index tidak cocok / job penuh / data kosong / data > 1536 char → reject `unexpected_chunk`.
- Base64 invalid, decode gagal, oversized, atau gagal tulis ke partisi OTA → job **langsung GAGAL**
  (`mqttOtaFail`, bukan sekadar reject chunk itu) — job harus dimulai ulang dari manifest.
- Tiap chunk yang diterima langsung ditulis ke partisi (`Update.write`) dan di-hash inkremental
  (`mbedtls_sha256_update`) — tidak ada buffer penuh image di RAM.
- Setelah chunk terakhir (`next_index == chunk_count`): cek `received_bytes == image_size`, lalu
  hitung SHA-256 final dan bandingkan ke `sha256` manifest (`sha256_mismatch` jika beda) →
  publish status `"verifying"` → `Update.end()` → jika sukses, simpan `id/version/sha256/partition`
  ke Preferences namespace `mqtt_ota` (`mqttOtaPersistInstalled`) → publish status `"restarting"`
  → **delay 1 detik lalu `ESP.restart()`** (bukan reboot instan; ini di loop `updateMqtt()`,
  dicek tiap tick `mqtt_ota_restart_pending`).
- Setelah reboot & MQTT reconnect: `publishPersistedMqttOtaStatus()` dipanggil di event
  `MQTT_EVENT_CONNECTED`. Ia baca Preferences `mqtt_ota`, cek partition label yang RUNNING sekarang
  sama dengan yang disimpan sebelum reboot → publish status `"installed"` (retained) kalau cocok,
  atau `"failed"` dengan detail `boot_partition_mismatch`/`boot_partition_unverified` kalau tidak
  (bukti boot sukses harus POST-REBOOT, bukan sekadar `Update.end()` berhasil).

**Ack chunk** (topic `.../ota/ack`):
```json
{"id":"ota-20260731-001","kind":"chunk","index":0,"result":"accepted",
 "received_bytes":1152,"next_index":1,"ts":1785480000}
```
`kind` = `"manifest"` atau `"chunk"`. `result` = `"accepted"` atau `"rejected"`. `detail` field
berisi alasan singkat (mis. `signature_invalid`, `invalid_manifest`, `busy`, `duplicate`,
`unexpected_chunk`, `stale_chunk`, `wrong_job`, `""` kalau sukses biasa).

**Status** (topic `.../ota/status`, RETAINED):
```json
{"id":"...", "state":"downloading|verifying|restarting|installed|failed",
 "image_type":"gateway", "hardware":"bep-gateway-v1", "gateway_id":"<mac>",
 "gateway_firmware_version":"1.0.1", "running_gateway_firmware_version":"1.1.6",
 "running_partition":"app0", "sha256":"...", "received_bytes":1245184,
 "detail":"...", "ts":1785480000}
```
State enum lengkap: `idle, downloading, programming, verifying, restarting, installed, failed`
(`programming` khusus jalur DCON). `running_partition` dari `esp_ota_get_running_partition()`.

**Rollback / mark-valid**: TIDAK ditemukan panggilan eksplisit `esp_ota_mark_app_valid_cancel_rollback()`
di kode yang dibaca — proyek memakai `Update.h` (Arduino OTA wrapper di atas `esp_ota_ops`), dan
verifikasi keberhasilan boot dilakukan secara APLIKASI (bandingkan running partition tersimpan di
Preferences vs partition aktif saat ini setelah reboot), BUKAN via app-rollback ESP-IDF bawaan
(tidak ada bukti `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` dipakai di kode ini). Kalau firmware baru
gagal boot / watchdog reset sebelum publish status, tidak terlihat mekanisme auto-rollback firmware
di file yang dibaca — perlu cek `sdkconfig`/partitions lebih lanjut kalau presisi ini penting.

**Timeout & retry**: gateway sendiri **tidak menjalankan timer timeout job** eksplisit di kode yang
dibaca (server yang bertanggung jawab retry). `docs/MQTT_OTA_CONCEPT.md` menyatakan kontrak: server
retry chunk yang timeout pakai `id`+`index` sama; untuk chunk DCON harus tunggu 10-30 detik (proxy
RS485 bootloader) — untuk gateway murni tidak disebutkan angka tunggu spesifik.

**Ukuran maksimum image gateway**: `BEP_MQTT_OTA_MAX_IMAGE_SIZE = 0x1E0000` (1.960.960 byte) —
sama persis dengan ukuran satu slot OTA di `partitions.csv` (app0/app1 masing-masing `0x1E0000`).

**Selama OTA berlangsung** (`ota_update_in_progress = true`): semua command MQTT biasa direject
dengan alasan `"ota_in_progress"` (lihat `processMqttCommand`); WiFi power-save dipaksa OFF
(`prepareWiFiForMqttOta()`); LED WiFi berkedip cepat (`WIFI_LED_OTA_BLINK_MS = 1000 ms`), dan
setelah sukses berkedip cepat 100 ms selama 10 detik (persisted lewat reboot via Preferences
`ota_led`).

---

## E. Provisioning

**Bukan SoftAP kondisional dengan captive portal scan+simpan** seperti gateway-v2 — modelnya lebih
sederhana:

- **SoftAP SELALU aktif sebagai fallback** kalau STA (router WiFi) belum/putus konek
  (`updateWiFiRecoveryAp()` di `NetworkManager.cpp`). Saat STA **baru** connect sukses, AP tetap
  dinyalakan SEMENTARA (`startRecoveryAp(true)`) selama `RECOVERY_AP_AFTER_CONNECT_MS = 5 menit`
  lalu otomatis dimatikan (`stopRecoveryAp()`). Saat STA putus, AP permanen menyala lagi
  (`startRecoveryAp(false)`).
- **SSID AP**: dibuat stabil per-device — `"BEP-CONNECT-" + gateway_code` (6 karakter A-Z0-9 acak,
  disimpan di Preferences namespace `device_id` key `gateway_code`; sekali dibuat, dipertahankan).
  Default SSID/password DIAMBIL dari `Secrets.h`/`Secrets.example.h`
  (`SECRET_DEFAULT_AP_SSID`, `SECRET_DEFAULT_AP_PASS`), lalu di-override oleh Preferences kalau
  user pernah save via `/api/wifi/ap`. AP IP tetap default ESP `192.168.4.1` (tidak ada static IP
  custom untuk AP).
- **Captive portal**: `DNSServer` (`<DNSServer.h>`) start di `startRecoveryAp()`
  (`captive_dns.start(53, "*", WiFi.softAPIP())`), diproses tiap loop lewat
  `updateCaptivePortal()` → `captive_dns.processNextRequest()`. TIDAK ADA endpoint scan WiFi
  (`WiFi.scanNetworks()` tidak ditemukan di kode yang dibaca) — user harus tahu SSID router sendiri
  dan mengetiknya manual di form `/wifi`.
- **Endpoint HTTP provisioning**:
  - `GET /wifi` — halaman HTML (embedded PROGMEM string, bukan LittleFS) menampilkan status
    (SSID router, STA IP, fallback AP SSID, mDNS `.local`, gateway code, fallback IP) + 3 form.
  - `POST /api/wifi/save` (`handleWiFiSave`) — set SSID/pass router, mDNS hostname, DAN static IP
    STA (field `sta_static`, `sta_ip`, `sta_gw`, `sta_mask`, `sta_dns1`, `sta_dns2` — jadi static
    IP DIGABUNG dengan endpoint save, bukan endpoint terpisah). Validasi SSID 1-32 char, pass
    0-63 char, hostname lowercase+angka+strip. Simpan ke Preferences `wifi_cfg`, lalu **selalu
    reboot** (`restartSoon()`) — TIDAK ada opsi apply tanpa reboot / "live tanpa reboot" seperti
    gateway-v2.
  - `POST /api/wifi/ap` (`handleWiFiApSave`) — ganti SSID/password fallback AP. Validasi SSID
    1-32 char, password kosong ATAU 8-63 char. Simpan lalu reboot.
  - `POST /api/wifi/forget` (`handleWiFiForget`) — hapus hanya `ssid`+`pass` router dari Preferences
    `wifi_cfg` (AP settings tetap), lalu reboot ke mode AP-only.
  - Tidak ada endpoint scan/list AP terdekat maupun endpoint "IP statis" terpisah dari save.
- **mDNS hostname**: default `bep-dev-gateway` (`BEP_MDNS_HOSTNAME`), bisa diganti user (disimpan
  Preferences `wifi_cfg` key `mdns`), divalidasi `validMdnsHostname()` (lowercase, angka, strip,
  1-63 char, tidak diawali/diakhiri strip). Service diiklankan: `MDNS.addService("http","tcp",80)`.
  Start/retry tiap `MDNS_RETRY_MS = 5000 ms` selama STA connected.
- **Factory reset**: TIDAK ada endpoint HTTP. Hanya **tombol BOOT fisik** (GPIO 9, `PIN_BOOT_BUTTON`)
  ditahan **8 detik** (`FACTORY_RESET_HOLD_MS`) saat gateway running → `updateFactoryResetButton()`
  di `NetworkManager.cpp` meng-clear Preferences namespace `wifi_cfg` DAN `app_cfg` lalu
  `restartSoon()`. Tidak menyentuh namespace `mqtt_ota` atau `device_id` (gateway_code/AP SSID
  stabil tetap bertahan setelah factory reset — hanya kredensial WiFi + config app yang hilang).
- **Penyimpanan kredensial**: `Preferences` (NVS), beberapa namespace terpisah:
  - `wifi_cfg`: `ssid`, `pass`, `ap_ssid`, `ap_pass`, `mdns`, `sta_static`, `sta_ip`, `sta_gw`,
    `sta_mask`, `sta_dns1`, `sta_dns2`.
  - `device_id`: `ap_ssid` (SSID AP stabil, salinan), `gateway_code` (6 char, sekali generate).
  - `app_cfg`: auto-SOC + jadwal + slot terpilih + BMS↔DCON assignment mask (lihat bagian F).
  - `mqtt_ota`: `id`, `version`, `sha256`, `partition` — dipakai untuk verifikasi boot pasca-OTA.
  - `ota_led`: flag `success` untuk melanjutkan blink sukses OTA lewat reboot.
- **Web dashboard TIDAK PAKAI PASSWORD/autentikasi HTTP sama sekali** untuk endpoint
  monitoring/kontrol biasa (dikonfirmasi: tidak ada `Authorization` header check, tidak ada basic
  auth di `WebApi.cpp`/`WebServerSetup.cpp`). `docs/API_V1.md` eksplisit menyatakan: "The gateway
  API is intended for a trusted LAN or VPN. Do not expose it directly to the public Internet."
  Satu-satunya endpoint dengan "password" adalah **OTA HTTP** (`/update`, `/dcon_update`) via
  `otaCredentialsOk()` — cek query/form `password == OTA_UPDATE_PASSWORD` (dari Secrets) DAN
  `gateway_code == gateway_code_tersimpan ATAU == device_id` (bukan Bearer/Basic Auth HTTP standar,
  hanya field form biasa).

---

## F. Scheduling + auto-control SOC

**Bukan command MQTT terpisah untuk jadwal** — semuanya dikirim via command MQTT `set_schedule`
(lewat topic `device/<id>/command` biasa) ATAU via HTTP `POST /api/auto/config`. Tidak ada
penyimpanan "banyak jadwal" (array of schedules) — hanya **SATU window harian** aktif per gateway.

### Command MQTT `set_schedule` (di `MqttManager.cpp:769`, handler `processMqttCommand`)
Args JSON:
```json
{"id":"...", "cmd":"set_schedule",
 "args": {"enabled": true, "start_hhmm":"17:00", "end_hhmm":"21:00",
          "soc_stop_pct":20, "soc_recovery_pct":30,
          "vout_v":220, "power_limit_w":2000,
          "tz_offset_min":420,
          "target": 1, "dcon_code":"ABC123"}}
```
- `start_hhmm`/`end_hhmm`: format string `"HH:MM"` (parser `parseHHMM`), disimpan sebagai menit
  0-1439. Window BOLEH melewati tengah malam (`start > end` → dianggap wrap).
- `soc_stop_pct` (clamp 0-99), `soc_recovery_pct` (clamp `stop+1`..100) — INI values yang jadi
  `auto_soc_threshold_percent`/`auto_soc_recovery_percent` (field sama dipakai baik oleh jadwal
  MAUPUN auto-SOC biasa — set_schedule ikut mengubah ambang SOC global).
  - Auto-control **bukan disable-only murni** di firmware ini: kalau `auto_restart` (di sini otomatis
    di-set `= enabled`) aktif, gateway JUGA meng-auto-ENABLE saat SOC pulih ke `recovery_percent`
    (lihat `autoDconSocControlUpdate()` — cabang `can_auto_enable` mengirim `sendDconEnablePWM`
    sendiri). Ini BEDA dari kebijakan gateway-bess yang mungkin ingin disable-only.
- `vout_v` (clamp 100-310V) + `power_limit_w` (clamp 0-3500W) — dikonversi ke command CP
  (`cp_vmax_v`, `cp_power_w`) dan dikirim via `processMqttCpOutput` dengan `control_mode = CP`
  ke DCON target. **Field ini DCON-spesifik (voltase/daya converter) — tidak relevan langsung ke
  BESS**, tapi POLA "jadwal juga menyetel setpoint daya keluaran, bukan cuma on/off" relevan.
- `tz_offset_min`: menit offset dari UTC, clamp -720..840. Disimpan per-device (bukan per-jadwal).
- Prasyarat `enabled=true`: DCON code valid (6 char, dari args ATAU dari slot tersimpan),
  `dconHasBmsAssignment(target)` harus true (target DCON wajib punya minimal satu BMS ter-assign)
  — kalau tidak, ditolak `bms_assignment_required`.
- Setelah sukses: field tersimpan ke `app_cfg` (Preferences) via `saveAppConfig()`. `auto_dcon_has_commanded`
  di-reset supaya re-evaluasi ambang terjadi ulang.
- Ack: `{"id":..,"cmd":"set_schedule","result":"accepted"|"clamped"|"rejected",
  "detail":"...","applied":{...semua field yg diterapkan...},"ts":...}`.

### HTTP `POST /api/auto/config` (`handleAutoConfig`, `WebApi.cpp:1267`)
Endpoint form-encoded, semua field OPSIONAL (partial update), argumen: `threshold`, `recovery`,
`enabled`, `auto_restart`, `time_enabled`, `start` (HH:MM), `end` (HH:MM), `tz_offset` (menit),
`tz_name` (string bebas 1-32 char, label saja). Validasi sama seperti versi MQTT (clamp/reject).
Response JSON: `{"ok":bool,"enabled":bool,"auto_restart":bool,"threshold_percent":N.N,
"recovery_percent":N.N, ...}` (dipotong di titik baca, field lanjutan tak sempat diverifikasi
lengkap tapi pola sama).

### Perilaku auto-control SOC (`autoDconSocControlUpdate()` di `AutoControl.cpp:196`)
- **Per-DCON-node** (loop node 1..32), masing-masing dievaluasi independen berdasar BMS yang
  di-assign ke node itu (bitmask 16-bit, 1 bit = 1 alamat BMS 1..16, via `dcon_bms_assignment_mask`).
  Node tanpa assignment (`mask==0`) dilewati.
- **Ambang**: `auto_soc_threshold_percent` (default 20%) = titik **stop/disable**.
  `auto_soc_recovery_percent` (default 30%) = titik **boleh auto-enable lagi** (histeresis 10 poin
  default, field independen — bukan hardcoded gap, user bisa set berapa saja asal
  `recovery > threshold`).
- SOC yang dipakai = **SOC TERENDAH** di antara semua BMS yang di-assign ke node itu
  (`lowest_soc`), bukan rata-rata.
- **Disable-trigger** (`should_disable`): jadwal `discharge_time_window_enabled && !time_allows`,
  ATAU BMS assigned stale >30 detik (`BMS_STALE_MS`, dengan grace period = tidak langsung disable
  kalau assignment baru saja diubah, `assignment_grace_active`), ATAU SOC invalid (NaN/Inf), ATAU
  `lowest_soc <= threshold`.
- **Auto-enable** hanya terjadi kalau: `auto_soc_control_enabled && auto_soc_auto_restart_enabled`
  (dua flag terpisah — auto-restart adalah opt-in tambahan di atas auto-SOC-control dasar) **DAN**
  `!should_disable && time_allows && assigned_bms_ready && lowest_soc >= recovery_percent` **DAN**
  DCON slot punya `dcon_code` tersimpan (6 char) — kalau kode tidak diketahui, TIDAK BISA
  auto-enable (harus manual sekali untuk gateway "belajar" kodenya lewat telemetry ACK sebelumnya).
- **Cooldown** antar-command RS485: `AUTO_DCON_COMMAND_COOLDOWN_MS = 3000 ms`, dan **hanya SATU**
  command RS485 dikirim per siklus kontrol (`break` setelah command pertama) — bukan broadcast ke
  semua node sekaligus.
- **Manual override**: manual enable via `/api/dcon/on` atau command MQTT `enable` MEMATIKAN kontrol
  jadwal (`disableMqttScheduleControl()` di-panggil pada `enable`/`disable` manual) — jadi begitu
  operator intervensi manual, auto-schedule berhenti sampai di-set ulang eksplisit. Ini KONSISTEN
  dengan pola "manual override sampai operator ubah lagi" yang juga dipakai gateway-v2.
- Command MQTT terkait: `enable`/`disable` (wajib `target`+`dcon_code`, lihat `mqttTargetAndCode`/
  `mqttAuthorizeOnOff` — kode dicocokkan ke `dcon_slots[target].dcon_code` yang tersimpan dari
  telemetry DCON terakhir, BUKAN secret statis), `set_control_mode`, `set_cp_output`/`set_output`/
  `set_mppt_feed` (set setpoint daya, DCON-spesifik). Semua pakai bentuk ack sama:
  `{"id","cmd","result":"accepted"|"clamped"|"rejected","detail","applied","ts"}`.

---

## H. Web dashboard + API

**Library**: `WebServer` SINKRON bawaan ESP32 Arduino core (`#include <WebServer.h>`,
`WebServer server(80);` di `Globals.cpp`) — **BUKAN** `AsyncWebServer`/ESPAsyncWebServer.
**Semua halaman HTML di-embed sebagai string `PROGMEM`** langsung di source `.cpp`
(`WebServerSetup.cpp` untuk index/monitor, handler HTML lain seperti `/wifi`, `/update` dibangun
string di `WebApi.cpp` juga inline) — **BUKAN disajikan dari LittleFS/SPIFFS** (partisi `spiffs`
di `partitions.csv` ada tapi tidak dipakai untuk dashboard; kemungkinan sisa slot kosong/tidak
dipakai, tidak diverifikasi lebih jauh).

**Autentikasi**: **TIDAK ADA** untuk endpoint monitor/kontrol/config biasa (lihat bagian E untuk
detail — `docs/API_V1.md` eksplisit: LAN/VPN trusted only, jangan expose ke internet publik).
OTA saja pakai password+gateway_code sebagai form field (bukan header HTTP standar).

**Daftar endpoint HTTP lengkap** (dari `server.on(...)` di `WebServerSetup.cpp:540-596`):

| Method | Path | Fungsi |
|---|---|---|
| GET | `/` | `handleRoot` — halaman monitor utama (index HTML PROGMEM) |
| GET | `/settings` | `handleDconSettingsPage` — halaman setting DCON (fault/control param) |
| GET | `/api/data` | `handleData` — **telemetri pasif**, JSON lengkap (`makeDataJson()`), header `Cache-Control: no-store`. Tidak ada endpoint `/api/live` ringan di branch ini (beda dari gateway-v2) |
| GET | `/api/dcon/fault_history` | `handleDconFaultHistory` — riwayat fault RAM-only |
| GET/POST | `/api/dcon/on`, `/api/dcon/enable` | `handleDconOn` — enable, wajib `target`+`code` (6 char), cek jadwal blokir + BMS assignment |
| GET/POST | `/api/dcon/off`, `/api/dcon/disable` | `handleDconOff` — disable, wajib `target`+`code` |
| GET/POST | `/api/dcon/set` | `handleDconSetpoints` |
| GET/POST | `/api/dcon/read_setpoints` | `handleDconReadSetpoints` |
| GET/POST | `/api/dcon/fault_param`, `/read_fault_param`, `/fault_params` | baca/tulis param fault individual/semua |
| GET/POST | `/api/dcon/control_param`, `/read_control_param`, `/control_params` | baca/tulis param kontrol (CP/CC/CV) |
| GET/POST | `/api/dcon/config_schema` | `handleDconConfigSchema` |
| GET/POST | `/api/dcon/read_all_settings` | `handleDconReadAllSettings` |
| GET | `/api/firmware_versions` | `handleFirmwareVersions` |
| POST | `/api/monitor/select` | `handleMonitorSelect` — pilih slot BMS/DCON aktif dimonitor (`bms=1..16`, `dcon=1..32`), persist |
| GET/POST | `/api/dcon/bms_assignment` | `handleDconBmsAssignment` — assign BMS↔DCON (comma-separated addr list → bitmask) |
| GET/POST | `/api/dcon/node_id` | `handleDconNodeId` |
| GET/POST | `/api/auto/config` | `handleAutoConfig` — lihat bagian F |
| GET | `/wifi` | `handleWiFiPage` |
| POST | `/api/wifi/save`, `/api/wifi/ap`, `/api/wifi/forget` | lihat bagian E |
| GET | `/update` | `handleOtaPage` — form OTA gateway via HTTP (upload multipart, alternatif dari MQTT OTA) |
| POST | `/update` | `handleOtaFinished`/`handleOtaUpload` — upload firmware gateway via HTTP multipart (perlu `password`+`gateway_code`) |
| GET | `/dcon_update` | `handleDconFwPage` (DCON-spesifik) |
| POST | `/dcon_update` | `handleDconFwFinished`/`handleDconFwUpload` (DCON-spesifik) |
| (404) | — | `handleNotFound` |

**Request/response ringkas untuk yang paling relevan ke gateway-bess:**
- `GET /api/data` → 200, `application/json`, isi = `makeDataJson()` (device_id, firmware_version,
  uptime, slot BMS/DCON terpilih + array multi-slot — TIDAK sempat dibaca definisi `makeDataJson()`
  detail per-field karena ada di bagian lain `WebApi.cpp` yang tidak digali; per `docs/API_V1.md`:
  field `api_schema_version:1`, `firmware_version`, `uptime_ms`, `device_id`, objek `multi` berisi
  array semua slot, objek top-level `bms`/`dcon` = slot yang sedang dipilih).
- `POST /api/monitor/select?bms=1&dcon=1` → 200/400, `{"ok","selected_bms_addr","selected_dcon_node","changed","error"}`.
- `POST /api/dcon/on?target=1&code=ABC123` → 200/400/403 (403 kalau diblokir jadwal),
  `{"ok","cmd":"ENABLE_CONVERTER","ack","error"}`.
- `POST /api/auto/config?enabled=true&threshold=20&recovery=30&start=17:00&end=21:00` → 200/400,
  `{"ok","enabled","auto_restart","threshold_percent","recovery_percent",...}`.

---

## Daftar LENGKAP command MQTT (`MqttManager.cpp`, `processMqttCommand`, topic `device/<id>/command`)

Semua command: JSON `{"id":"...", "cmd":"...", "args":{...}}` → balasan di
`device/<id>/command/ack`: `{"id","cmd","result":"accepted"|"clamped"|"rejected","detail","applied","ts"}`.
Kalau `ota_update_in_progress`, SEMUA command ditolak `"ota_in_progress"`.

1. **`enable`** — args: `target` (1-32, default 1), `dcon_code` (wajib, 6 char). Cek jadwal-blokir
   tidak eksplisit di sini (beda dari endpoint HTTP `/api/dcon/on` yang cek 403) tapi cek
   `dconEnableSafetyAllows` (BMS assignment + SOC di atas ambang). Mematikan kontrol jadwal setelah
   sukses. DCON-spesifik (auth via `dcon_code`) — **tidak berlaku langsung ke BESS** (BESS tidak
   pakai `dcon_code`), tapi pola nama command `enable`/args `target` relevan untuk paritas nama.
2. **`disable`** — args sama. Mematikan kontrol jadwal SEBELUM disable (supaya jadwal tak
   menyalakan ulang).
3. **`set_cp_output`** — args: `target`, `dcon_code`, `cp_power_w`/`power_w`, `cp_vmax_v`/`vmax_v`,
   `cp_vmin_v`/`vmin_v`, `cp_ramp_w_per_s`, `cp_tolerance_w`. Set mode CP + parameter constant-power.
   DCON-spesifik (parameter converter fisik) — relevan sebagai POLA "set_output" untuk BESS
   (`set_power` versi gateway-bess kemungkinan analognya).
4. **`set_output`** — alias sederhana `set_cp_output` tapi target selalu node 1, `dcon_code` diambil
   otomatis dari slot tersimpan (tidak perlu dikirim).
5. **`set_mppt_feed`** — sama seperti `set_cp_output` tapi mode `MPPT_FEED` bukan `CP`.
6. **`set_control_mode`** — args: `target`, `dcon_code`, `control_mode` (integer 0-3: CV/CC/CP/
   MPPT_FEED, harus integer utuh).
7. **`set_schedule`** — lihat bagian F di atas untuk detail penuh.

Command tidak dikenal → `rejected/"unsupported_cmd"`. Total **7 command**, semuanya DCON-terkait
kecuali `set_schedule` yang campuran jadwal+setpoint.

## Partisi flash (`partitions.csv`)

```
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x1E0000   (1.960.960 B)
app1,     app,  ota_1,   0x1F0000,0x1E0000   (1.960.960 B)
spiffs,   data, spiffs,  0x3D0000,0x20000    (131.072 B)
coredump, data, coredump,0x3F0000,0x10000    (65.536 B)
```
Total = 0x400000 (4 MB flash). Komentar di file: "MQTT requires a larger application slot than the
Arduino ESP32-C6 default. This keeps two OTA slots while retaining a small SPIFFS partition."
Dua slot OTA penuh (app0/app1) dipertahankan meski ukurannya dikurangi dari default Arduino untuk
menyisakan spiffs+coredump — relevan sebagai referensi ukuran kalau gateway-bess butuh menyamakan
skema partisi 4 MB dengan dua slot OTA.

---

## Ringkasan relevansi ke gateway-bess

**Yang BISA disamakan langsung (kontrak, bukan implementasi DCON):**
- Topic OTA: `device/<gw>/ota/{manifest,chunk,ack,status}`, QoS1, manifest+chunk+ack TIDAK
  retained, status RETAINED.
- Ukuran chunk: base64 ≤1536 char / ≤1152 byte biner.
- Field manifest gateway: `id, image_type, hardware, version, encoding, image_size, sha256,
  signature, chunk_count`; `chunk_count` harus PAS `ceil(image_size/1152)`.
- Signature = Ed25519 detached atas **32-byte SHA-256 digest biner** (bukan atas manifest/image),
  public key 32-byte dikompilasi ke firmware, library libsodium.
- Ack chunk format `{id,kind,index,result,received_bytes,next_index,ts}`; status format
  `{id,state,image_type,hardware,gateway_id,gateway_firmware_version,
  running_gateway_firmware_version,running_partition,sha256,received_bytes,detail,ts}`.
- State enum: `idle,downloading,programming,verifying,restarting,installed,failed`.
- Bukti instalasi sukses = verifikasi PASCA-REBOOT (partition label tersimpan vs running partition
  saat ini), bukan sekadar `Update.end()==true`.
- Format ack command umum: `{id,cmd,result:"accepted"|"clamped"|"rejected",detail,applied,ts}` —
  ini pola yang **sudah dipakai gateway-bess** juga (disebut di CLAUDE.md), jadi sudah paritas.
- Web dashboard: **tanpa auth**, asumsi LAN/VPN trusted — gateway-bess sudah konsisten dengan ini.

**Yang TIDAK bisa/tidak perlu disamakan (DCON/BMS-spesifik, di luar scope BESS):**
- `dcon_code` sebagai mekanisme auth enable/disable, semua param CP/CC/CV, OTA proxy STM32 via
  RS485 bootloader, BMS assignment bitmask per-node, `/dcon_update`, `/api/dcon/*`.

**Perbedaan desain provisioning yang perlu KEPUTUSAN SADAR** (bukan sekadar "ikut tim"):
- Tim TIDAK punya scan WiFi / static-IP-terpisah / captive-portal-otomatis-redirect-ke-config —
  provisioning mereka lebih sederhana daripada gateway-v2 yang sudah dibangun. Static IP malah
  digabung ke endpoint `/api/wifi/save` yang sama (bukan endpoint terpisah).
- Tim TIDAK auto-enable dari auto-SOC-control murni disable-only — mereka PUNYA auto-restart
  (auto-enable) opt-in. Gateway-v2 (dan mungkin gateway-bess) sengaja disable-only per keputusan
  owner 23 Juli (auto-enable = wewenang server/VPP). Ini KONTRADIKSI eksplisit dengan pola tim —
  perlu diputuskan sadar kalau mau ikut pola tim atau tetap disable-only.
