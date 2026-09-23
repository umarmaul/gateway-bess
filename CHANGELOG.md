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
