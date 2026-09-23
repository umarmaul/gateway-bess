# Changelog

Format: versi firmware = `FW_VERSION` di `firmware/src/config.h` (ikut terkirim sebagai
`data.firmware_version` di telemetri). Bagian **Kontrak cloud** berisi hal yang wajib
diketahui tim backend/VPP; sisanya internal.

## bess-0.3.0 — 23 September 2026

Sub-proyek G (spec `docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md`):
OTA gateway via MQTT dengan tanda tangan Ed25519. **Belum diuji di hardware**
(tak ada bench terpasang saat implementasi) — verifikasi native test (37 test
`ota_logic` + 3 test blok `data.ota`) + build + 10 test pytest bench tool.

### Kontrak cloud (perlu tindakan di backend)

**Topic baru** (per `<gw>`, QoS1): `ota/manifest`/`ota/chunk` (cloud → gateway,
tidak retained), `ota/ack` (gateway → cloud, tidak retained), `ota/status`
(gateway → cloud, **retained**). Detail lengkap field/validasi/state di
`firmware/README.md` §OTA gateway.

**`hardware` WAJIB `"bep-gateway-bess-v1"`** di manifest (BUKAN
`"bep-gateway-v1"` milik gateway DCON tim) — papan fisik identik, jadi
backend harus mengirim manifest dengan hardware string yang benar per jenis
gateway atau image akan ditolak `hardware_mismatch`. `image_type` harus
`"gateway"` (BESS tidak punya jalur OTA proxy DCON seperti gateway lama).

**Tanda tangan**: Ed25519 detached atas 32 byte digest SHA-256 biner (bukan
manifest/image), kunci publik tim yang sama dengan gateway DCON
(`X76lzB83YKaD9wf/qBa5eV5/Rnm1PIzRckIdgNkIC98=`) — server cloud yang sudah
menandatangani image gateway DCON tidak perlu kunci baru untuk gateway BESS.

**`data.ota`** (telemetri, field baru): `{state,id,running_partition,
pending_verify}` — lihat firmware/README.md §`data.ota`.

**Command baru ditolak selama OTA**: semua command MQTT biasa (`enable`,
`disable`, `set_output`/`set_power`) dijawab `rejected`/`ota_in_progress`
selama job OTA aktif.

**Deviasi dari kontrak referensi tim** (tidak didaftarkan eksplisit di
dokumentasi mereka, lihat firmware/README.md §OTA untuk detail): amplop chunk
yang tidak bisa diurai sama sekali (JSON rusak/field hilang) dipetakan ke
`detail:"unexpected_chunk"`.

### Firmware (internal)

- `lib/bess_core/ota_logic.*`: parser+validator manifest, mesin status urutan
  chunk (new/duplicate/stale/unexpected/wrong_job), decode base64/hex tulisan
  sendiri (tak ada libsodium di native), builder JSON ack+status. Murni,
  37 test native.
- `src/task_ota.cpp`: verifikasi Ed25519 (libsodium, prebuilt di
  `framework-arduinoespressif32-libs` — tertaut otomatis tanpa perubahan
  `platformio.ini`) SEBELUM `esp_ota_begin`; SHA-256 inkremental (mbedtls)
  sambil menulis tiap chunk langsung ke partisi; finalize → NVS `mqtt_ota` →
  reboot 1 dtk; job timeout 120 dtk tanpa chunk baru. Task **tidak** diawasi
  task watchdog (`esp_ota_write` bisa lambat, menunggu `mqtt_tx` tak boleh
  memicu reboot).
- Rollback: `verifyRollbackLater()` di-override (`extern "C"`, symbol weak
  arduino-esp32) — image baru boot `PENDING_VERIFY`, ditandai valid saat MQTT
  tersambung pertama kali; 15 menit tanpa tersambung → mark-invalid + reboot
  paksa ke image lama. Status persisted dipublikasikan sekali per boot dari
  NVS, ditandai `reported` supaya tak terulang tiap reconnect.
- `mqtt_tx` digeneralisasi dari antrean khusus ack ke antrean generik
  `{topic_id,retain,t_ms,n,json}` (`mqttPublish`) — dipakai ack/ota_ack/
  ota_status, aturan tahan-sampai-connected + buang basi dipertahankan.
  `FW_VERSION` → `bess-0.3.0`.

### Simulator/bench

- `bess-sim/tools/ota_publish.py`: alat bench yang memerankan server cloud —
  tanda tangan Ed25519 (`cryptography`), kirim manifest+chunk dengan
  flow-control ack, `--gen-key` untuk kunci dev. `cryptography` ditambah
  sebagai dev dependency `bess-sim` (`uv add --dev`); `paho-mqtt` TIDAK
  ditambah ke dependensi proyek (jalankan via `uv run --with paho-mqtt ...`,
  konsisten dengan `tools/cloud_probe.py`).

### Verifikasi bench yang masih wajib (checklist, belum dijalankan)

1. `--gen-key` → tempel public key ke `secrets.h` (`OTA_ED25519_PUBKEY_B64`)
   di gateway bench, reflash.
2. Kirim firmware.bin kecil (mis. build `bess-0.3.0` itu sendiri) via
   `ota_publish.py`: manifest `accepted`, tiap chunk `accepted`, status
   berjalan `downloading → verifying → restarting`, gateway reboot.
3. Pasca-reboot: `[boot] gateway-bess bess-0.3.0` muncul, status MQTT
   `installed` (retained), `data.ota.pending_verify:false`.
4. Kirim manifest dengan `hardware:"bep-gateway-v1"` (salah) →
   `rejected`/`hardware_mismatch`, TIDAK ada byte tertulis ke flash.
5. Kirim manifest dengan `signature` yang sengaja diubah satu karakter →
   `rejected`/`signature_invalid`.
6. Kirim `enable` di tengah job OTA aktif → `rejected`/`ota_in_progress`.
7. Matikan server di tengah chunk (jangan kirim >120 dtk) → status
   `failed`/`timeout`, job bisa dimulai ulang dari manifest baru.
8. Flash image yang SENGAJA crash sebelum MQTT connect (mis. panic di
   `setup()`) → bootloader rollback otomatis ke image lama dalam beberapa
   detik (uji dengan hati-hati, siapkan jalur recovery SWD/USB kalau gagal).

### Sub-proyek E (Provisioning)

Menyusul G di rilis yang sama (spec §E). **Belum diuji di hardware** (bench
tak terpasang) — verifikasi native test (10 test `prov_logic` + 1 test blok
`data.network`) + build.

**Kontrak cloud**: tidak ada topic/field MQTT baru selain `data.network`
dapat dua field (`ap_active` bool, `mdns` string) — lihat
`firmware/README.md` §Provisioning.

**Deviasi dari pola tim** (sengaja, alasan keselamatan — lihat
`lib/bess_core/prov_logic.h`): password AP fallback **wajib** 8-63 karakter
(tim membolehkan kosong ATAU 8-63); endpoint `/api/wifi/save|ap|forget`
wajib field `code` (gateway_code) — tim hanya mewajibkannya untuk OTA HTTP,
gateway ini mewajibkannya untuk SEMUA endpoint yang mengubah konfigurasi
karena mengendalikan konverter 50 kW.

- `lib/bess_core/prov_logic.*`: validasi SSID/pass STA/pass AP/hostname
  mDNS, parser IPv4 dotted-decimal, pembangkit `gateway_code` 6 char A-Z0-9
  dari sumber acak yang disuntikkan, pembanding `code` waktu-konstan,
  evaluasi status AP fallback (nyala saat STA putus, tetap 5 menit pasca
  connect). Murni, 10 test native.
- `src/prov.cpp`: NVS `device_id` (gateway_code, sekali dibangkitkan) +
  `wifi_cfg` (ssid/pass/mdns/sta_static/IP statis/ap_ssid/ap_pass), fallback
  ke `WIFI_SSID`/`WIFI_PASS` (secrets.h) kalau NVS kosong. SoftAP fallback
  `WiFi.mode(WIFI_AP_STA)` + `DNSServer` captive; mDNS `bep-bess-gateway`
  (retry 5 dtk). Tombol BOOT (GPIO9) 8 dtk → factory reset (`wifi_cfg` +
  `app_cfg`, identitas dipertahankan).
- `src/web.cpp`: `WebServer` sinkron port 80, `GET /wifi` + `POST
  /api/wifi/{save,ap,forget}`, 404 saat AP aktif → redirect captive portal
  ke `/wifi`. `webServer()` diekspos untuk sub-proyek F/H mendaftarkan rute
  tambahan.
- `src/wifi_mgr.cpp`: `wifiInit()` sekarang menerima kredensial dari
  `prov.cpp` (dulu macro `WIFI_SSID`/`WIFI_PASS` langsung) + IP statis
  opsional; backoff reconnect (`wifiTick`, satu-satunya driver) TIDAK
  berubah. `data.network.ssid` kini SSID yang benar-benar dipakai
  (`wifiSsid()`), bukan lagi macro tetap.
- `config.h`: `PIN_BOOT_BUTTON` (GPIO9), timing AP/mDNS/factory-reset/reboot,
  `AP_PASS` default `"bepgateway"` (`#ifndef`, timpa di `secrets.h`).

**Verifikasi bench yang masih wajib** (checklist, belum dijalankan):
1. Boot pertama tanpa `wifi_cfg` di NVS: SoftAP `BEP-CONNECT-<code>` menyala,
   captive portal membuka `/wifi` otomatis (atau manual ke `192.168.4.1/wifi`).
2. Isi form simpan WiFi dengan `code` benar → `200 restarting:true` → reboot →
   gateway konek ke router; AP tetap menyala sampai 5 menit pasca connect
   lalu mati sendiri.
3. `code` salah/kosong di salah satu dari 3 endpoint → `403 forbidden`, NVS
   tidak berubah.
4. SSID/hostname/IP invalid → `400` dengan `error` yang sesuai, NVS tidak
   berubah.
5. Tahan tombol BOOT 8 detik saat berjalan → reboot ke SoftAP-only,
   `gateway_code` di halaman `/wifi` TIDAK berubah dari sebelum reset.
6. `http://bep-bess-gateway.local/wifi` bisa diakses dari perangkat lain di
   LAN yang sama (mDNS).
7. Field `ap_active`/`mdns` muncul benar di `data.network` telemetri saat AP
   menyala vs mati.

### Sub-proyek F (Jadwal + auto-SOC)

Menyusul E di rilis yang sama (spec §F). **Belum diuji di hardware** (bench
tak terpasang) — verifikasi native test (28 test `sched_logic` + 6 test
`commands` untuk parsing `set_schedule` + 2 test blok `data.auto`) + build
ESP32 SUCCESS (flash 68,7%) + 84 test pytest `bess-sim` (tak tersentuh,
tetap lulus).

**Kontrak cloud (perlu tindakan di backend)**: command baru **`set_schedule`**
(topic `device/<gw>/command` biasa) + blok telemetri baru **`data.auto`** —
lihat `firmware/README.md` §Jadwal + auto-control SOC untuk contoh JSON
lengkap (args, ack, `GET`/`POST /api/auto/config`, field `data.auto`).
Ringkas: satu window harian per gateway (`enabled`, `start_hhmm`, `end_hhmm`
format `"HH:MM"`, `soc_stop_pct`, `soc_recovery_pct`, `power_w`,
`tz_offset_min`), ack `{id,cmd,result,detail,applied,ts}` seperti command
lain. `data.auto` `{schedule_enabled,start_hhmm,end_hhmm,tz_offset_min,
power_w,soc_stop_pct,soc_recovery_pct,in_window,battery_ready,last_action,
last_action_ts}`.

**Deviasi dari pola tim** (sengaja, menyelaraskan keputusan owner 23 Juli
gateway-v2 "gateway tak pernah auto-enable sendiri" — lihat
`lib/bess_core/sched_logic.h`): tim (`AutoControl.cpp`) punya **auto-restart**
opt-in yang mengizinkan gateway auto-ENABLE sendiri saat SOC pulih ke
`recovery`; gateway-bess **disable-only** murni untuk proteksi SOC otonom
(hanya `disable`, tak pernah `enable` tanpa instruksi eksplisit) — jadwal
(`set_schedule`) adalah **satu-satunya** jalur gateway boleh `enable` dirinya
sendiri, dan itu pun hanya sebagai eksekusi instruksi cloud/operator yang
sudah disetel, bukan keputusan otonom. Deviasi kedua: `power_w` pada
`set_schedule` **tidak dipangkas ±120% rated saat disimpan** (beda dari
`soc_stop_pct`/`soc_recovery_pct`/`tz_offset_min` yang dipangkas langsung) —
pemangkasan baru terjadi saat eksekusi lewat jalur `set_output` biasa,
karena rated power device bisa saja belum pernah terbaca saat
`set_schedule` diterima.

- `lib/bess_core/sched_logic.*`: parser/formatter `"HH:MM"`, konfigurasi
  jadwal + clamp partial-update (`soc_stop_pct` 0-99, `soc_recovery_pct`
  dipaksa `>soc_stop_pct`, `tz_offset_min` -720..840), evaluasi window harian
  (lintas tengah malam + tz), dan `schedDecide` murni: proteksi SOC
  disable-only (selalu aktif, walau jadwal nonaktif) + jadwal sebagai
  instruksi eksplisit (aksi edge-triggered per transisi window, TIDAK
  di-retry sampai transisi berikutnya kalau syarat gagal tepat saat edge
  masuk). 28 test native (tabel kasus: lintas tengah malam, tz positif/
  negatif, waktu belum sinkron, fault/comm_lost saat edge, SOC rendah saat
  charging tak memicu disable, dll).
- `lib/bess_core/commands.*`: `Command` diperluas dengan tipe `SET_SCHEDULE`
  + `SchedSetInput sched` + `sched_bad_input` (HH:MM tak valid / `power_w`
  NaN — ditolak SEBELUM `schedApplySetInput` dipanggil sama sekali, config
  tersimpan tidak tersentuh). 6 test native baru (`test_native_commands`,
  folder yang sebelumnya belum ada untuk `parseCommand`).
- `lib/bess_core/payload.*`: `AutoInfo` + blok `data.auto` di
  `buildTelemetryJson` (builder defensif — `last_action` kosong → `"none"`,
  sama pola dengan `data.ota.state`).
- `src/schedule.cpp` (integrasi ESP32): NVS `app_cfg` (namespace yang sama
  dihapus tombol factory reset E) + cache RAM mutex-protected, satu jalur
  persist (`schedApplyAndSave`) dibagi command MQTT `set_schedule` dan HTTP
  `POST /api/auto/config`. `schedNotifyManualOverride()` mematikan jadwal
  saat command manual (bukan dari jadwal sendiri) masuk. ⚠️ File **sengaja
  diberi nama `schedule.h`/`schedule.cpp`, BUKAN `sched.h`/`sched.cpp`** —
  nama itu bentrok dengan header POSIX `<sched.h>` milik toolchain (ditarik
  transitif lewat `<pthread.h>` yang meng-include `<sched.h>` pakai angle
  bracket); karena `-Isrc` ada di search path compiler, deklarasi kita
  tertelan ke dalam blok `extern "C"` milik `pthread.h` dan gagal link
  (linkage C, bukan C++) — dikonfirmasi lewat pembacaan output preprocessor
  (`-E`) yang menunjukkan isi `sched.h` lama muncul PERSIS di tengah isi
  `pthread.h`.
- `src/task_auto.cpp`: task baru (prioritas 1, diawasi task watchdog, tiap
  5 dtk) — snapshot `g_state.bess` + NTP → `schedDecide` (murni) → eksekusi
  lewat `taskCmdSubmitInternal` (jalur Modbus/safety/ack SAMA dengan command
  cloud, TANPA menonaktifkan jadwal yang memicunya). Ack aksi otonom
  `id:"auto-<epoch>"`. Snapshot `data.auto` diekspos via `autoGetInfo`
  (mutex, pola sama `task_ota`).
- `src/task_cmd.cpp`: command internal (`RawCmd.internal`) dibedakan dari
  command eksternal — hanya command manual (cloud/HTTP) yang memanggil
  `schedNotifyManualOverride()`; `set_schedule` dieksekusi via
  `schedApplyAndSave` + ack format sendiri (`schedBuildAck`, `applied`
  berisi config jadwal, bukan `power_pct`/`power_w`).
- `src/web.cpp`: `GET`/`POST /api/auto/config` (field tim:
  `enabled,threshold,recovery,start,end,tz_offset` + `power_w`, `code`
  wajib untuk `POST` — paritas penyimpangan E, LAN "trusted" saja tak cukup
  untuk gateway yang mengendalikan konverter 50 kW).
- `src/main.cpp`: `schedInit()` + `taskAutoStart()` (setelah `taskCmdStart()`
  — butuh antreannya sudah ada), `autoGetInfo()` mengisi `data.auto` tiap
  telemetri.

**Verifikasi bench yang masih wajib** (checklist, belum dijalankan):
1. `set_schedule` dengan window pendek (mis. 5 menit dari sekarang) + SOC
   di atas `soc_recovery_pct` → tepat di edge masuk window, gateway
   `set_output` lalu `enable` sendiri (ack `id:"auto-<epoch>"` di
   `device/<gw>/command/ack`); tepat di edge keluar → `disable` sendiri.
2. Set `fault` aktif (atau putus BMS/comm_lost) tepat sebelum edge masuk
   window → gateway TIDAK enable, dan TIDAK retry walau fault hilang
   sebelum window berakhir (baru dicoba lagi window berikutnya).
3. Kirim `enable`/`disable`/`set_output` manual (MQTT atau `POST
   /api/command`) saat jadwal aktif → `data.auto.schedule_enabled` jadi
   `false` di telemetri berikutnya, jadwal tidak menyalakan/mematikan BESS
   lagi sampai `set_schedule`/`POST /api/auto/config` diset ulang.
4. BESS mengekspor + SOC turun sampai `<= soc_stop_pct` → gateway `disable`
   sendiri walau jadwal nonaktif/di luar window (proteksi disable-only).
   SOC rendah saat CHARGING tidak memicu ini.
5. Matikan NTP (putus internet sebelum sinkron, atau reboot lalu cek log
   sebelum `time_valid:true`) → jadwal tidak bertindak sama sekali
   (`in_window` tetap `false`) walau jam lokal seharusnya di dalam window.
6. `POST /api/auto/config` tanpa `code`/`code` salah → `403 forbidden`,
   config tidak berubah; `start`/`end` bukan `"HH:MM"` → `400 bad_hhmm`.
7. Window lintas tengah malam (mis. `22:00`..`06:00`) + `tz_offset_min`
   WIB (`420`) → verifikasi edge masuk/keluar terjadi di jam LOKAL yang
   benar, bukan UTC.

### Sub-proyek H (Dashboard + API lokal)

Menyusul F di rilis yang sama (spec §H). **Belum diuji di hardware**
(bench tak terpasang) — verifikasi native test (5 test `ack_ring` + 6 test
`web_cmd`) + build ESP32 SUCCESS (flash 69,9%, naik dari 68,7% di F) + 84
test pytest `bess-sim` (tak tersentuh, tetap lulus).

**Kontrak cloud**: **tidak ada perubahan MQTT** — `GET /api/data` memakai
`buildTelemetryJson` yang SAMA persis dengan telemetri MQTT (tidak ada
field baru ditambahkan ke payload untuk kebutuhan dashboard), dan
`POST /api/command` menerima body command JSON yang identik dengan
`device/<gw>/command`. H murni menambah permukaan **HTTP lokal** (dashboard
operator + endpoint bench `curl`) di atas kontrak yang sudah ada — tim
cloud tidak perlu tindakan apa pun. Detail endpoint lengkap + contoh
`curl` di `firmware/README.md` §Dashboard & API lokal.

**Deviasi dari spec** (alasan di `firmware/README.md` §Dashboard `GET /`):
tidak ada upload OTA HTTP (`/update` milik tim) — satu-satunya jalur OTA
gateway tetap MQTT bertanda tangan Ed25519 (sub-proyek G); jalur HTTP
polos di LAN tidak memverifikasi tanda tangan sama sekali, jadi
menambahkannya berarti membuka jalur bypass di sebelah jalur bertanda
tangan yang sudah dibangun G. Dashboard hanya menampilkan status OTA.

- `lib/bess_core/ack_ring.*`: ring buffer 8 ack terakhir, murni
  menggabungkan entri JSON yang sudah dibangun `buildAckJson`/
  `schedBuildAck` (tanpa re-parse) jadi satu array `[terbaru,...,terlama]`.
  5 test native (urutan, wrap-around, entri kepanjangan, buffer keluaran
  kurang).
- `lib/bess_core/web_cmd.*`: `webCmdEnsureId` membangkitkan id
  `"web-<millis>"` untuk command `/api/command` yang tidak mengirim `id`
  (atau mengirim string kosong) — supaya ack-nya tetap bisa dikorelasikan
  lewat `GET /api/acks`, sama seperti command MQTT yang selalu punya `id`.
  Body bukan objek JSON valid → gagal jujur, diteruskan APA ADANYA ke
  `task_cmd` (satu kontrak kegagalan `bad_json`/`unsupported_cmd` dengan
  command MQTT yang korup, bukan dua jalur berbeda). 6 test native.
- `src/sysinfo.*` (baru): `fillSysInfo()` — SysInfo yang dulu diisi inline
  di `main.cpp::loop()` sekarang satu fungsi dipakai telemetri MQTT DAN
  `GET /api/data`. `seq` SENGAJA tidak diisi di sini (tetap milik
  telemetri MQTT, di-increment hanya di `loop()`); `/api/data` memakai
  `g_state.seq` saat ini tanpa increment.
- `src/task_cmd.*`: jalur submit KETIGA `taskCmdSubmitWeb` (buffer statis
  `rc_web`, terpisah dari `rc_ext`/`rc_int`) — satu-satunya jalur submit
  command yang aman dipanggil dari `loop()` (dua jalur lain masing-masing
  hanya aman dari task esp-mqtt / task_auto). Beda dari dua jalur lain:
  mengembalikan hasil seketika (`bool`) dan TIDAK memakai slot luapan
  1-slot — HTTP `503` sudah jadi jawaban sinkron sendiri, jadi command web
  tidak ikut memperebutkan slot luapan yang dipakai command MQTT/jadwal.
  Ring buffer ack terpasang di `sendAck`/`sendScheduleAck` (push), dibaca
  lewat `taskCmdGetAcksJson` (mutex pendek, snapshot disalin lalu dibangun
  JSON di luar lock).
- `src/web_dashboard.*` (baru): `GET /` (dashboard PROGMEM ~17 KB),
  `GET /api/data` (`Cache-Control: no-store`), `GET /api/acks`,
  `POST /api/command` (auth `code` query/form arg — pola sama
  `/api/wifi/*`/`/api/auto/config`, BUKAN field di body JSON), `GET
  /api/firmware_versions`. Balasan `/api/command` `202 {"queued":true,"id":...}`
  membalas ack SEBENARNYA lewat jalur ack yang sama dengan MQTT
  (`GET /api/acks`) — eksekusi command (tulis Modbus, tunggu bukti status
  sampai 10 dtk) tidak boleh memblokir handler HTTP.
- `src/main.cpp`: `sysInfoInit()` setelah `crashLogInit()`,
  `webDashboardInit()` setelah `webInit()`, blok telemetri `loop()`
  disederhanakan lewat `fillSysInfo()` (perilaku IDENTIK, hanya
  dipindah — seq masih di-increment di titik yang sama).

**Verifikasi bench yang masih wajib** (checklist, belum dijalankan):
1. Buka `http://<ip-gateway>/` dari HP (lebar ~360 px) dan desktop — layout
   tidak pecah, mode gelap otomatis mengikuti OS.
2. Matikan simulator/BESS (comm_lost) → banner "DATA BASI" muncul dalam
   beberapa detik; nyalakan lagi → banner hilang otomatis.
3. Isi `gateway_code` di panel kontrol, klik Enable → konfirmasi muncul →
   setelah OK, `GET /api/acks` menampilkan ack `enable` dalam ~10 dtk;
   ulangi untuk Disable dan Set Output.
4. `code` salah/kosong di `/api/command` → `403`; body > 2048 B → `413`;
   body bukan JSON valid → tetap `202` tapi ack berikutnya `bad_json`.
5. Ubah jadwal lewat panel dashboard, reload halaman → nilai yang
   ditampilkan sama dengan yang baru disimpan (`GET /api/auto/config`
   dibaca ulang saat load).
6. Buka DevTools Network saat dashboard aktif ~1 menit: tidak ada dua
   request `/api/data` yang tumpang tindih (selalu sekuensial), gap
   antar-request stabil di sekitar 2 dtk (bukan menumpuk saat lambat).
7. Kirim command lewat MQTT (bukan dashboard) → muncul juga di
   `GET /api/acks` (ring buffer bukan cuma untuk command dari web).

### Perbaikan review (23 Sep 2026, pasca-implementasi E–H)

Tetap `bess-0.3.0` (bukan versi baru). 144 test native lulus (33
`sched_logic`, sisanya tak berubah); `pio run -e esp32c6` SUCCESS,
flash 70,1%; 84 test pytest `bess-sim` tetap lulus (tak tersentuh).

- **(TINGGI) Slowloris → reboot watchdog.** `handleClient()` dipindah dari
  `loop()` ke task baru `task_web` (TIDAK diawasi task watchdog, pola sama
  `task_ota`/`mqtt_tx`) — klien HTTP yang mengirim body sangat lambat dulu
  bisa menahan `loop()` sampai panic `TASK_WDT`/reboot gateway. Diikuti
  audit thread-safety: `prov.cpp` dapat mutex baru untuk state yang kini
  disentuh `loop()` DAN `task_web` sekaligus (AP aktif/SSID/pass, hostname
  mDNS, flag reboot terjadwal). Detail + residual risk: `firmware/README.md`
  §Kerangka web server.
- **(SEDANG) Command manual yang ditolak tetap mematikan jadwal.**
  `schedNotifyManualOverride()` di `task_cmd.cpp` dipindah dari awal
  `doOnOff`/`doSetPower` ke tepat sebelum tulisan Modbus pertama — command
  manual yang ditolak (`comm_lost`/`bess_fault`/`bad_value`/`rated_unknown`)
  sekarang tidak lagi ikut menonaktifkan jadwal.
- **(RENDAH) Enable jadwal tak di-retry dalam window.** `sched_logic.*`
  (`SchedMemo.pending_enable`): dulu kegagalan syarat (fault/comm_lost/SOC
  di bawah recovery) tepat saat window mulai membuat jadwal menyerah
  sampai window berikutnya. Sekarang dicoba lagi tiap siklus (~5 dtk)
  selama masih di window yang sama; setelah enable berhasil sekali, tidak
  di-retry lagi (operator tetap bisa mematikan manual di tengah window).
- **(UX)** Tombol Enable/Disable/Set Output/Simpan jadwal di dashboard
  sekarang menolak mengirim (tanpa `confirm()`) kalau `gateway_code`
  kosong, dengan pesan "Isi gateway_code dulu".
- **(UX)** `GET /api/acks`, `GET /api/firmware_versions`, dan
  `GET /api/auto/config` sekarang dimuat di siklus polling PERTAMA (dulu
  baru di siklus ke-5/ke-15, tabel kosong 10–30 dtk pertama), tetap
  sekuensial (satu rantai, tak ada dua request terbang bersamaan).

## bess-0.2.0 — 23 September 2026

### Kontrak cloud (perlu tindakan di backend)

**Ack command**

| Perubahan | Sebelum | Sesudah | Yang harus dilakukan backend |
|---|---|---|---|
| Nilai `result` baru `"timeout"` | `enable`/`disable` yang tertulis tapi bit status tak muncul dalam 10 dtk dijawab `rejected` + `bess_no_ack` | `result:"timeout"`, `detail:"status_timeout"` | Perlakukan sebagai **hasil tidak diketahui**, bukan gagal: baca `bess.running`/`bess.standby` di telemetri berikutnya sebelum mengirim ulang. Parser jangan menolak nilai `result` yang tak dikenal. |
| `detail` baru `rated_unknown` | `set_output` sebelum rated power terbaca dijawab `bess_no_ack` | `rejected` + `rated_unknown` | Kirim ulang setelah telemetri menunjukkan `rated_power_kw` > 0. |
| `detail` baru `payload_too_large` | command > 511 B dipotong diam-diam lalu dijawab `bad_json` | batas naik ke **2048 B**; di atas itu `rejected` + `payload_too_large` dengan **`id` kosong** | Jaga payload command ≤ 2048 B. |
| Ack saat broker putus | ack hilang | ack ditahan gateway (antrean 8) dan terkirim begitu MQTT tersambung lagi; yang tertahan >10 menit dibuang | Ack bisa datang terlambat; cocokkan lewat `id`, bukan waktu tiba. |

**Telemetri** (`data.*`, semua tambahan — tidak ada field yang dihapus/diganti nama)

| Field | Tipe | Arti |
|---|---|---|
| `free_heap_bytes` | int | Heap bebas saat telemetri dibangun |
| `min_free_heap_bytes` | int | Titik terendah heap sejak boot — turun terus pada `boot_count` yang sama = kebocoran |
| `last_crash` | `null` atau objek | Crash terakhir dari coredump: `{"task","pc","mcause","boot_count"}` + `wdt_tasks` (hanya crash watchdog: task yang macet). Bertahan lintas reboot sampai crash berikutnya |

**Waktu deteksi `comm_lost`**: kembali **~8–9 dtk** (di `bess-0.1.x` ~25 dtk).

`last_reset_reason` kini benar-benar bisa bernilai `"TASK_WDT"` — task watchdog 120 dtk
akhirnya mengawasi task gateway (sebelumnya aktif tapi tidak mengawasi apa pun).

### Firmware (internal)

- Task watchdog `loop`/`task_bess`/`task_cmd`, 120 dtk. Semua kiriman MQTT lewat task `mqtt_tx` (tak diawasi watchdog), jadi lock esp-mqtt yang tertahan saat link tercekik tidak pernah memicu reboot.
- Coredump crash sebelumnya diringkas ke NVS saat boot lalu image dihapus (`crash_log.cpp`).
- Poll berhenti di blok pertama yang timeout, tetap lanjut saat exception (log exception per blok tetap ada).
- MQTT di-start saat WiFi pertama naik (diulang kalau start gagal); WiFi tak lagi `begin()` dobel saat boot.
- Penjaga `n == 0` di enqueue (esp-mqtt membaca len 0 sebagai `strlen`), `connected` atomic, log `task_bess` di luar mutex.

### Simulator

- Arus fase idle tak lagi terbungkus `0xFFFF` (6553,5 A).
- FAULT bisa direset lewat FC5 OFF setelah penyebabnya hilang (asumsi — PDF tidak mengatur).
- FC5 OFF selalu diterima, termasuk di tengah sekuens start; daya langsung 0 saat FAULT/STOP.
- Pemotong frame 30 ms, `--strict-timing` mengukur ke awal query, penjaga echo dongle.
- `--ip65`: node 160 + ganti alamat via `3182` (PDF §2.6).

### Verifikasi bench yang masih wajib

Rilis ini **baru terverifikasi lewat unit test (73 simulator + 41 native), build, dan review kode independen** —
belum di hardware. Sebelum dipakai di lapangan, di bench (simulator + gateway):

1. Flash, biarkan ≥10 menit: `boot_count` tidak naik (watchdog tidak false-positive), `last_crash` = `null`.
2. `free_heap_bytes`/`min_free_heap_bytes` muncul dan stabil antar-telemetri.
3. `enable` → `accepted`; `set_output` 5000 → `accepted` + `applied`; `disable` → `accepted`.
4. Cabut dongle simulator: `comm_lost` naik dalam ~10 dtk; colok lagi: turun sendiri.
5. Skenario `grid_undervoltage.yaml`: `enable` ditolak `bess_fault` selama fault; sesudah alarm bersih, `disable` lalu `enable` berhasil.
6. Kirim command > 2048 B → `payload_too_large`; command 1–2 KB valid → dieksekusi normal.
7. Kirim `enable`, lalu segera putuskan WiFi/broker ~1 menit selama gateway menunggu bit status → setelah tersambung lagi, ack-nya menyusul (bukan hilang) dan `boot_count` tidak naik.

## bess-0.1.0 — 10–14 Agustus 2026

Fase 1 (e2e bench) + fase 2 (fondasi paritas). Lihat `docs/superpowers/`.
