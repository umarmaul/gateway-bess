# Desain: sub-proyek E (provisioning), F (jadwal + auto-SOC), G (OTA), H (dashboard)

Tanggal: 23 September 2026. Status: **diputuskan** (owner mendelegasikan semua keputusan —
"lanjutkan sesuai rekomendasi"). Acuan paritas kontrak: `BEPESP32_WiFi_Extension` branch
`gateway-mqtt` (`d994bb1`), dibaca read-only. Ringkasan riset kontraknya:
`docs/superpowers/specs/2026-09-23-riset-kontrak-EFGH.md`.

Prinsip: **samakan KONTRAK** (topic, field, endpoint, bentuk ack) supaya cloud dan operator
memakai satu cara untuk kedua jenis gateway; **jangan samakan** hal yang spesifik DCON
(`dcon_code`, OTA proxy STM32, assignment BMS, param CP/CV) atau yang melemahkan keselamatan.
Urutan kerja: **G → E → F → H** (semua menyentuh `main.cpp`/`mqtt_link`/`task_cmd`, jadi
berurutan, bukan paralel).

## Infrastruktur bersama (dikerjakan di G, dipakai E/F/H)

- **Jalur kirim generik di `mqtt_tx`**: antrean pesan `{topic_id, retain, t_ms, n, json[≤1024]}`
  menggantikan antrean khusus ack. Topic: `ack`, `ota_ack`, `ota_status`. Semua publish
  selain telemetri lewat antrean ini (aturan watchdog tetap: task yang diawasi WDT tak pernah
  memanggil esp-mqtt langsung).
- **Router topic di `onEvent`**: `device/<gw>/command` → `task_cmd` (seperti sekarang);
  `device/<gw>/ota/manifest|chunk` → antrean `task_ota` baru.

## G. OTA gateway via MQTT (Ed25519) — paritas penuh kontrak

- Topic `device/<gw>/ota/{manifest,chunk,ack,status}`, QoS1; `status` **retained**, lainnya tidak.
- Manifest `{id,image_type,hardware,version,encoding,image_size,sha256,signature,chunk_count}`.
  Validasi identik tim: `encoding=="base64"`, `image_size` 1..ukuran slot OTA (`0x1E0000`),
  `sha256` 64 hex, `chunk_count == ceil(image_size/1152)` persis, `id` ≤128.
- **`image_type` harus `"gateway"` dan `hardware` harus `"bep-gateway-bess-v1"`** — sengaja
  BEDA dari `"bep-gateway-v1"` milik tim. Papan fisiknya identik, jadi tanpa pembeda ini image
  firmware DCON-gateway bisa ter-flash ke gateway BESS (dan sebaliknya) hanya karena
  tanda tangannya sah. Mismatch → `rejected`/`hardware_mismatch`.
- Tanda tangan: Ed25519 detached atas **32 byte SHA-256 biner** (libsodium
  `crypto_sign_verify_detached`), diverifikasi **sebelum** satu byte pun ditulis.
- Public key: default = kunci tim (`X76lzB83YKaD9wf/qBa5eV5/Rnm1PIzRckIdgNkIC98=`, sehingga
  image yang ditandatangani server cloud yang sama diterima), bisa ditimpa
  `OTA_ED25519_PUBKEY_B64` di `secrets.h` untuk bench (kunci dev sendiri).
- Chunk `{id,index,data}` base64 ≤1536 char → ≤1152 byte; wajib berurutan dari 0, satu
  in-flight; duplikat index terakhir → `accepted`/`duplicate`; salah urutan → `unexpected_chunk`,
  `stale_chunk`, `wrong_job`; base64 rusak / gagal tulis → job gagal. Hash SHA-256 inkremental,
  tulis langsung ke partisi (`esp_ota_*`), tak ada image penuh di RAM.
- Ack `{id,kind:"manifest"|"chunk",index,result,detail,received_bytes,next_index,ts}`.
  Status `{id,state,image_type,hardware,gateway_id,gateway_firmware_version,
  running_gateway_firmware_version,running_partition,sha256,received_bytes,detail,ts}`,
  state `idle|downloading|verifying|restarting|installed|failed`.
- Selesai: cek ukuran + SHA-256 → `verifying` → `esp_ota_end` + set boot partition → simpan
  `{id,version,sha256,partition}` ke NVS `mqtt_ota` → `restarting` → reboot 1 dtk.
- **Rollback (lebih kuat dari tim)**: bootloader sudah `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.
  Firmware meng-override `verifyRollbackLater()` → image baru boot dalam status
  `PENDING_VERIFY` dan **baru ditandai valid saat MQTT tersambung pertama kali**. Crash/reboot
  sebelum itu = bootloader kembali ke image lama otomatis. Kalau 15 menit tak kunjung
  tersambung, firmware menandai dirinya invalid dan reboot ke image lama. Setelah reconnect,
  status `installed` (partisi berjalan = partisi tersimpan) atau `failed`
  (`boot_partition_mismatch` = rollback terjadi).
- Selama job aktif: command biasa ditolak `ota_in_progress`; manifest kedua ditolak `busy`;
  job tanpa chunk baru selama 120 dtk dibatalkan (`failed`/`timeout`) supaya slot tak terkunci.
- Alat bench: `bess-sim/tools/ota_publish.py` — tanda tangani `firmware.bin` dengan kunci dev
  (PEM/raw 32 byte) lalu kirim manifest + chunk dengan flow-control ack; plus `--gen-key`.

## E. Provisioning — paritas pola tim

- SoftAP fallback: menyala saat STA belum/putus tersambung; setelah STA tersambung tetap
  menyala 5 menit lalu mati. SSID `BEP-CONNECT-<gateway_code>` (6 char A-Z0-9 acak, NVS
  `device_id/gateway_code`, dibuat sekali). Password AP default `AP_PASS` dari `secrets.h`
  (fallback compile `"bepgateway"`), bisa diganti via `/api/wifi/ap`. Captive DNS
  (`DNSServer`) selama AP menyala.
- Kredensial STA: NVS `wifi_cfg` (`ssid,pass,mdns,sta_static,sta_ip,sta_gw,sta_mask,sta_dns1,
  sta_dns2`, `ap_ssid,ap_pass`); **kalau NVS kosong jatuh ke `WIFI_SSID/WIFI_PASS` secrets.h**
  (bench tetap jalan tanpa provisioning). Backoff reconnect yang ada dipertahankan.
- Endpoint: `GET /wifi` (halaman), `POST /api/wifi/save` (SSID/pass/mDNS/IP statis → simpan →
  reboot), `POST /api/wifi/ap`, `POST /api/wifi/forget` — nama & field sama dengan tim.
  **Penyimpangan keselamatan**: endpoint yang mengubah konfigurasi wajib field
  `code=<gateway_code>` (tim hanya mewajibkannya untuk OTA HTTP). Gateway ini mengendalikan
  konverter 50 kW; LAN "trusted" tidak cukup sebagai satu-satunya pagar.
- mDNS default `bep-bess-gateway` (BUKAN `bep-dev-gateway` milik tim — dua gateway di satu LAN
  akan bentrok nama), `_http._tcp:80`.
- Factory reset: tombol BOOT (GPIO9) ditahan 8 dtk → hapus NVS `wifi_cfg` + `app_cfg`, reboot
  (identitas `device_id`, `mqtt_ota`, `boot`, `crash` dipertahankan).
- Web server: `WebServer` sinkron bawaan core (seperti tim). **Update review 23 Sep 2026**:
  `handleClient()` dipindah dari `loop()` ke task sendiri (`task_web`, TIDAK diawasi task
  watchdog) -- library core menunggu body POST tanpa batas waktu total (slowloris bisa
  memicu panic TASK_WDT kalau dijalankan di `loop()`). Lihat `firmware/README.md`
  §Kerangka web server.

## F. Jadwal + auto-control SOC

- Command MQTT `set_schedule` (paritas nama) + `GET/POST /api/auto/config`. Satu window harian:
  `{enabled,start_hhmm,end_hhmm,soc_stop_pct,soc_recovery_pct,power_w,tz_offset_min}`
  (`power_w` menggantikan `vout_v`/`power_limit_w` DCON; ±, +=ekspor, dipangkas ±120% rated).
  Window boleh melewati tengah malam. Disimpan NVS `app_cfg`. Ack bentuk biasa dengan `applied`.
- **Kebijakan (menyelaraskan keputusan owner 23 Juli "gateway tak pernah auto-enable
  sendiri"):**
  - **Proteksi SOC otonom = disable-only**: bila BESS mengekspor (`active_power_kw > 0`) dan
    `soc ≤ soc_stop_pct` → `disable`. Berlaku selalu (dengan atau tanpa jadwal), default
    `soc_stop_pct=10`. Gateway melaporkan `battery_ready` (SOC ≥ recovery, tak fault, tak
    comm_lost) di telemetri.
  - **Jadwal = instruksi eksplisit dari cloud/operator**, jadi mengeksekusinya bukan keputusan
    otonom: saat masuk window (dan waktu NTP valid, tak fault, tak comm_lost, SOC ≥ recovery)
    gateway menulis `power_w` lalu `enable`; saat keluar window → `disable`. **ENABLE paling
    banyak sekali per window** (update review 23 Sep 2026): kalau syarat belum terpenuhi
    tepat saat masuk window, gateway menandai *pending* dan mencoba lagi tiap siklus (~5 dtk)
    selama masih di window yang sama — begitu syarat terpenuhi, `enable` langsung jalan;
    setelah `enable` berhasil sekali, tidak dicoba lagi sampai window berikutnya, jadi
    operator tetap bisa mematikan BESS manual di tengah window tanpa dinyalakan ulang.
  - Command manual `enable`/`disable`/`set_output` **menonaktifkan jadwal** (paritas tim:
    campur tangan manual menang sampai jadwal diset ulang).
  - Waktu belum sinkron (`ts==0`) → jadwal tidak bertindak sama sekali.
- Eksekusi lewat `task_cmd` (antrean internal, jalur Modbus & safety yang sama dengan command
  cloud); ack aksi otonom dipublikasikan dengan `id:"auto-<ts>"` supaya cloud melihatnya.
- Telemetri: blok `data.auto` `{schedule_enabled,start_hhmm,end_hhmm,tz_offset_min,power_w,
  soc_stop_pct,soc_recovery_pct,in_window,battery_ready,last_action,last_action_ts}`.

## H. Dashboard + API lokal

- `GET /` dashboard satu halaman (HTML/CSS/JS di PROGMEM, tanpa CDN — gateway sering tanpa
  internet), poll `GET /api/data` tiap 2 dtk **berantai** (pelajaran gateway-v2: jangan
  `setInterval` ke server 1-koneksi). `/api/data` = JSON yang sama dengan telemetri (satu
  builder, satu kontrak) + blok `auto` + `ota`.
- Kontrol: `POST /api/command` body JSON command (sama persis dengan MQTT) + `code` →
  diantrekan ke `task_cmd`, balas 202 `{queued,id}`; `GET /api/acks` = 8 ack terakhir (ring).
  Tanpa `code` benar → 403.
- `GET/POST /api/auto/config` (F), halaman `/wifi` (E), `GET /api/firmware_versions`.
- Tidak ada upload OTA HTTP (`/update` tim): satu-satunya jalur OTA adalah MQTT bertanda
  tangan — jalur LAN tanpa tanda tangan akan melewati Ed25519.

## Pengujian

- Native (TDD): parser/validator manifest + urutan chunk + base64 + verifikasi Ed25519 dengan
  vektor uji (libsodium tak tersedia di native → verifikasi dipisah di balik antarmuka; logika
  urutan/validasi murni), parser/validator jadwal + evaluasi window (termasuk lewat tengah
  malam, tz), kebijakan auto-SOC (tabel kasus), validasi input wifi/hostname/IP.
- Simulator: `ota_publish.py` diuji offline (pembagian chunk, tanda tangan terverifikasi
  dengan PyNaCl/cryptography).
- Build ESP32 wajib SUCCESS dan flash < 85% slot. Bench hardware: checklist di `CHANGELOG.md`.
