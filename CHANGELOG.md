# Changelog

Format: versi firmware = `FW_VERSION` di `firmware/src/config.h` (ikut terkirim sebagai
`data.firmware_version` di telemetri). Bagian **Kontrak cloud** berisi hal yang wajib
diketahui tim backend/VPP; sisanya internal.

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
