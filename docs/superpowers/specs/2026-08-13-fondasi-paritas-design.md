# Desain: Fase Fondasi Paritas gateway-bess ↔ BEPESP32_WiFi_Extension

Tanggal: 13 Agustus 2026 · Status: **disetujui user**
Lokasi kerja: `D:\PT Bima Eco Power\embedded-system\gateway-bess\firmware\`

## 1. Latar belakang

Audit 13 Agustus 2026 membandingkan `gateway-bess/firmware` dengan firmware milik rekan
kerja, **`BEPESP32_WiFi_Extension` branch `gateway-mqtt`** (HEAD `d994bb1`) — bukan
`main` (`50351ca`), yang sama sekali belum punya MQTT. Kedua perangkat **identik secara
desain hardware** (ESP32-C6, pin RS485 dan LED sama persis), hanya berbeda unit fisik:
rekan kerja memakai `58E6C5218B58`, gateway-bess memakai `58E6C5218C78`.

Audit menemukan gateway-bess sudah cocok pada envelope telemetri, topic, identitas MAC,
LWT, dan bentuk ack — tetapi menyimpang di sejumlah titik, dan kehilangan beberapa
subsistem utuh. Lima penyimpangan terkecil sudah diperbaiki dan diverifikasi di bench
(lihat §6). Dokumen ini merancang **fase fondasi**: prasyarat yang mengunci semua
pekerjaan paritas berikutnya.

### Keputusan dekomposisi

Scope penuh ("implementasikan yang belum ada, ikuti program teman") terlalu besar untuk
satu spec — sumber firmware rekan kerja ±330 KB C++ (`WebApi.cpp` saja 128 KB), sedangkan
gateway-bess sudah memakai 84,3% dari slot aplikasi 1,25 MB. Scope dipecah jadi:

| Kode | Sub-proyek | Ukuran | Fase |
|---|---|---|---|
| A | Akar penyebab reboot | Kecil | **Fondasi** |
| B | Tabel partisi 1,875 MB × 2 + coredump | Kecil | **Fondasi** |
| C | Hardening MQTT (buffer, pesan terpotong, ack enqueue) | Kecil | **Fondasi** |
| D | Penyelarasan kosakata command | Kecil | **Fondasi** |
| E | Provisioning SoftAP + captive portal + mDNS + factory reset | Sedang | Berikutnya |
| F | Scheduling + auto-control SOC | Sedang | Berikutnya |
| G | OTA gateway via MQTT (Ed25519) | Besar | Berikutnya |
| H | Web dashboard + API BESS | Besar | Berikutnya |

Spec ini **hanya** mencakup **A, B, C, D** ditambah empat item ekstra di §5.

### Yang sengaja TIDAK dibawa dari firmware rekan kerja

Bukan karena kehabisan waktu, tapi karena tidak ada padanannya di perangkat ini:

1. **OTA proxy DCON** (flash STM32 lewat RS485 + bootloader `0xB0`) — BESS adalah produk
   pihak ketiga; tidak ada jalur flash firmware-nya dari gateway.
2. **Blok `dcon` dan `bms` di payload serta UI** — BESS punya register map sendiri yang
   sudah diterbitkan sebagai blok `bess`.
3. **UART BMS terpisah + LED BMS** (pin 16/17/19 dan LED 15) — BESS punya BMS internal,
   dibaca lewat bus Modbus yang sama.

## 2. Urutan eksekusi: dua langkah flash

Fase ini dikerjakan sebagai **dua perubahan berurutan**, bukan satu.

**Langkah 1** — B (partisi + coredump) + diagnostik boot → flash penuh dengan erase →
reproduksi tes rebutan `client_id` → baca coredump → perbaiki A.

**Langkah 2** — C + D + tiga ekstra sisanya.

Alasan pemisahan: kalau buffer MQTT (C) berubah bersamaan dengan diagnosis reboot dan
reboot-nya hilang, **"diperbaiki" tidak bisa dibedakan dari "tertutupi"**. Ini penerapan
langsung pelajaran yang sudah tercatat di `CLAUDE.md` §4 — tetapkan baseline terukur,
ubah satu variabel, balikkan kalau tak terbukti.

Alternatif yang dipertimbangkan lalu ditolak:

- *Semua sekaligus lalu uji sekali* — hemat satu siklus flash, tapi kehilangan kaitan
  sebab-akibat pada satu-satunya bug yang belum dipahami.
- *Diagnosis dulu tanpa mengganti partisi* — tanpa partisi coredump hasilnya cuma
  `reset_reason` tanpa backtrace, hampir pasti menuntut siklus reproduksi kedua.

## 3. Langkah 1

### 3.1 B — Tabel partisi

Salin `partitions.csv` milik rekan kerja **apa adanya**, karena hardware-nya identik dan
tujuannya memang membuat kedua perangkat berperilaku sama:

```
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,   data, spiffs,  0x3D0000,0x20000,
coredump, data, coredump,0x3F0000,0x10000,
```

Didaftarkan lewat `board_build.partitions = partitions.csv` di `platformio.ini`.

**Flash pertama wajib `pio run -t erase` lalu upload.** ⚠️ **Koreksi (lihat catatan di
§7):** alasan yang tertulis semula di sini — "offset `nvs` dan `otadata` berpindah" —
**salah**. Dibandingkan langsung dengan `partitions/default.csv` bawaan toolchain
(`framework-arduinoespressif32`), `nvs` (`0x9000,0x5000`), `otadata` (`0xe000,0x2000`),
dan `coredump` (`0x3F0000,0x10000`) **identik offset dan ukurannya** di kedua tabel. Yang
benar-benar berubah hanya `app0` (ukuran naik), `app1` (offset **dan** ukuran naik), dan
`spiffs` (offset naik). Erase penuh dilakukan sebagai **kehati-hatian**, bukan karena ada
offset `nvs`/`otadata` yang bergeser — keduanya tidak bergeser.

Efek terukur yang diharapkan: **84,3% dari 1.310.720 B → ±59% dari 1.966.080 B**, dan dua
slot OTA tersedia untuk sub-proyek G.

**Terverifikasi sebagai prasyarat:** `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` (format ELF,
checksum CRC32) sudah aktif di `framework-arduinoespressif32-libs/esp32c6/sdkconfig`
bawaan pioarduino. ⚠️ **Koreksi:** partisi `coredump` **bukan** hal baru yang "diaktifkan"
tabel ini — partisi itu sudah ada di offset dan ukuran yang sama persis di
`default.csv` bawaan toolchain. Tabel baru tidak menambah kemampuan coredump-ke-flash;
kemampuan itu sudah ada sebelum spec ini. Konsekuensinya dicatat di §7.

### 3.2 A — Diagnosis reboot

**Gejala terukur (bench 13 Agustus).** Gateway restart satu kali: `seq` melompat 5 → 1 dan
`uptime_ms` mengonfirmasi boot baru, sekitar **2 detik setelah publish telemetri `seq=5`**,
yaitu ~50 detik sesudah tes rebutan `client_id` selesai. Jejak panic hilang karena USB-CDC
re-enumerate saat reset sehingga `pio device monitor` mati diam-diam — log berhenti tanpa
boot banner dan tanpa backtrace. Sesudahnya 8 menit operasi normal tanpa reset (`seq` 1→8
rapi tiap 60 detik).

**Instrumentasi.** Di `setup()`, cetak `esp_reset_reason()`, `esp_get_free_heap_size()`,
dan `esp_get_minimum_free_heap_size()`. Simpan alasan reset dan penghitung boot ke NVS,
lalu terbitkan di telemetri sebagai `data.last_reset_reason` dan `data.boot_count` — supaya
reboot di lapangan terlihat dari cloud, bukan hanya dari kabel serial.

`last_reset_reason` adalah **string nama enum ESP-IDF**, bukan angka: `POWERON`, `SW`,
`PANIC`, `INT_WDT`, `TASK_WDT`, `WDT`, `BROWNOUT`, `DEEPSLEEP`, `EXT`, `SDIO`, `USB`,
`JTAG`, `UNKNOWN`. Nilai enum yang tak dikenal diterbitkan sebagai `UNKNOWN_<angka>`
supaya tidak ada informasi yang hilang diam-diam. `boot_count` adalah pencacah monotonik
di NVS, naik satu tiap boot, tidak pernah di-reset kecuali NVS dihapus.

**Reproduksi.** Ulangi tes rebutan `client_id` (klien lain menyambung dengan
`client_id` = `58E6C5218C78`, ditahan 90 detik, lalu dilepas), sambil mengamati telemetri
dan serial.

**Kriteria selesai.** Kalau reboot muncul: baca coredump (`espcoredump.py info_corefile`),
tetapkan akar penyebabnya, perbaiki, lalu tunjukkan tes yang sama tidak lagi mereboot.
**Kalau tidak muncul dalam 3 percobaan: dilaporkan sebagai belum tereproduksi — bukan
sebagai sudah diperbaiki.** Instrumentasi tetap dipertahankan supaya kejadian berikutnya
tertangkap sendiri.

## 4. Langkah 2

### 4.1 C — Hardening MQTT

Tiga penyesuaian, seluruhnya mengikuti `MqttManager.cpp` rekan kerja:

| Aspek | Sekarang | Menjadi |
|---|---|---|
| `buffer.out_size` | tidak di-set (default 1024) | `24576` |
| `buffer.size` | tidak di-set (default 1024) | `2048` |
| Pesan masuk | langsung diproses | hanya diproses kalau `current_data_offset == 0 && data_len == total_data_len` |
| Ack | `esp_mqtt_client_publish` (memblokir) | `esp_mqtt_client_enqueue` |

Telemetri ~3,3 KB terbukti tetap terkirim dengan buffer default, jadi ini bukan perbaikan
bug melainkan menghapus ketiadaan margin. Penjaga pesan terpotong adalah yang paling
substantif: tanpa itu, potongan pertama command >1 KB diproses sebagai JSON utuh dan
dijawab `bad_json`.

### 4.2 D — Kosakata command

**Nama.** `set_output` menjadi nama resmi; `set_power` dipertahankan sebagai alias dengan
perilaku identik. Argumen `power_w` dipakai keduanya. Dokumentasi menyebut `set_output`
lebih dulu dan menandai `set_power` sebagai alias lama. Biaya flash mendekati nol dan tidak
ada perintah tim cloud yang rusak kapan pun mereka berpindah.

**Pemangkasan.** Nilai daya di luar rentang **dipangkas**, bukan ditolak. Yang dipangkas
adalah **persentase hasil hitung** (`power_w / rated_w * 100`) ke rentang `-120..+120`;
`applied.power_pct` dan `applied.power_w` keduanya melaporkan nilai **sesudah** pemangkasan,
sehingga `applied.power_w` bisa berbeda dari yang diminta. Ack menjadi `result:"clamped"`
hanya kalau pemangkasan benar-benar terjadi. Batas ±120% adalah batas device itu sendiri:
register `3050` berjangkauan `-1200..1200` dalam satuan 0,1% dari rated. Ini filosofi yang
sama dengan rekan kerja memangkas ke 3500 W milik DCON.

`rated_w` yang belum diketahui (nol, mis. saat `comm_lost` sejak boot) tetap ditolak
`bess_no_ack` seperti sekarang — tidak ada yang bisa dipangkas tanpa acuan rated.

**`target`.** Diterima sebagai argumen dan divalidasi; ditolak `bad_value` kalau ≠ 1,
karena BESS adalah node tunggal. Sebelumnya argumen ini diabaikan diam-diam.

Hasil ack yang mungkin menjadi: `accepted`, `clamped`, `rejected`.

### 4.3 Ekstra

| Item | Perubahan |
|---|---|
| Antrean penuh | Balas `rejected` + `detail:"queue_full"` (mekanisme di bawah). Sebelumnya dibuang diam-diam dan cloud menunggu ack yang tak pernah datang. Ini **lebih baik** dari firmware rekan kerja, yang juga membuang diam-diam — dipertahankan sebagai penyimpangan yang disengaja. |
| Rollover `wifiTick` | `now >= next_try_ms` → selisih bertanda `(int32_t)(now - next_try_ms) >= 0`. Bug laten: pada hari ke-49 `millis()` berputar dan gateway berhenti mencoba reconnect sampai di-reboot. |
| Exception Modbus | Kode exception (mis. `6` = busy) ikut dicatat di log, bukan cuma "gagal". |
| Diagnostik boot | Sudah tercakup di §3.2 (dikerjakan di langkah 1, bukan langkah 2). |

**Mekanisme `queue_full`.** Ack butuh `id` perintah, yang berarti JSON harus di-parse —
dan itu tidak boleh dilakukan di dalam event handler esp-mqtt (task jaringan, tidak boleh
dibebani). Jadi: di samping antrean perintah 4-slot yang ada, ditambah **antrean luapan
1-slot**. Saat antrean utama penuh, payload mentah dimasukkan ke slot luapan; `task_cmd`
menguras slot itu setiap selesai satu perintah, mem-parse `id`-nya seperti biasa, lalu
membalas `rejected`/`queue_full`. Kalau slot luapan **juga** penuh, pesan dibuang dan
dicatat di log — batas atas yang jujur, bukan antrean tak terbatas.

## 5. Pengujian

**Native (TDD, wajib merah dulu).** Kosakata `set_output` + alias `set_power`; pemangkasan
ke ±120% dengan `result:"clamped"` dan isi `applied`; penolakan `target` ≠ 1; bentuk ack
`queue_full`. Seluruh 23 tes native yang ada harus tetap hijau.

Perbaikan rollover `wifiTick` dan hardening MQTT tidak punya jalur uji native (menyentuh
`WiFi`/esp-mqtt) — keduanya diverifikasi lewat bench dan pembacaan kode.

**Bench.** Simulator di dongle CH340 (nomor port berpindah-pindah — identifikasi lewat
`Get-CimInstance Win32_PnPEntity`, cari `USB-SERIAL CH340`), gateway di COM3. Yang
dijalankan: tes rebutan `client_id`, round-trip `enable`/`set_output`/`set_power`/`disable`,
dan verifikasi telemetri termasuk `last_reset_reason` serta `boot_count`.

**Catatan alat:** `pio test -e native` hanya jalan lewat Bash dengan
`export PATH="/c/Users/legio/bin:$PATH"` — `gcc` tidak ada di PATH PowerShell.

## 6. Pekerjaan yang sudah selesai sebelum spec ini

Lima perbaikan sudah diterapkan dan sebagian diverifikasi di bench 13 Agustus. Dicatat di
sini supaya rencana implementasi tidak mengulanginya:

| Perbaikan | Status |
|---|---|
| `network.rssi` → `rssi_dbm` | Terbukti di bench |
| `ts` = 0 saat NTP belum sinkron (envelope + ack) | Jalur normal terbukti di bench; jalur nol terbukti di tes native |
| `client_id` MQTT = MAC | Terbukti di bench (klien dengan id sama berhasil menendang gateway) |
| `WiFi.setSleep(false)` | Berjalan, efeknya tidak terukur dari luar |
| Telemetri digerbang `mqttConnected()` | **Belum teruji** — tidak ada tik telemetri yang jatuh di dalam jendela putus |

## 7. Risiko

**Penggantian partisi menuntut flash penuh lewat kabel.** Aman sekarang karena gateway ada
di meja; tidak aman kalau perangkat sudah terpasang di lokasi. Ini justru alasan terkuat
mengerjakannya di fase fondasi, sebelum pemasangan.

**RAM naik ±26 KB** karena buffer MQTT, dari 16,1% (52.824 B dari 327.680 B). Masih lapang.

**Reboot bisa saja tidak tereproduksi.** Kalau begitu, fase ini selesai dengan instrumentasi
terpasang dan satu pertanyaan terbuka yang jujur dicatat, bukan dengan klaim perbaikan.

---

**⚠️ KOREKSI (ditambahkan pasca-review akhir seluruh branch, setelah Task 3 dilaporkan
selesai).** §3.1 di atas semula mengklaim erase penuh wajib karena `nvs` dan `otadata`
berpindah offset. Klaim itu salah — dibandingkan byte-per-byte dengan `default.csv` bawaan
toolchain, `nvs`, `otadata`, **dan** `coredump` identik di kedua tabel; hanya `app0`,
`app1`, dan `spiffs` yang berubah. §3.1 sudah diperbaiki di tempat, ditandai jelas.

Konsekuensinya nyata untuk Task 3: partisi `coredump` di `0x3F0000` **sudah ada** sebelum
spec ini (bukan baru "lahir" bersama tabel baru), jadi kalau reboot tak dikenal 13 Agustus
itu benar sebuah panic, coredump-nya kemungkinan **sudah tersimpan di flash** pada saat
kejadian — lalu **dihapus oleh `pio run -t erase` di Task 1**, sebelum Task 3 sempat
membacanya. Ledger Task 3 (`.superpowers/sdd/2026-08-13-fondasi-paritas/progress.md`)
menjelaskan coredump kosong dengan alasan "partisinya lahir di `be760d3`, sesudah kejadian
kemarin" — alasan itu keliru (partisi itu bukan baru); koreksi terpisah ditambahkan di
ledger tersebut, entri asli tidak dihapus.

**Kesimpulan yang tersisa jujur:** akar penyebab reboot 13 Agustus **tetap tidak
diketahui** — bukan karena coredump-nya kosong secara alami, tapi karena buktinya
kemungkinan pernah ada dan lalu terhapus oleh langkah kita sendiri di Task 1.
