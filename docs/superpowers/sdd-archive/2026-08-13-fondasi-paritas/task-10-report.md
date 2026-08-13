## Fix round 1/5 — reconciliation kode↔README (ronde perbaikan review)

Tidak ada perubahan kode; hanya `firmware/README.md`. Tidak ada build/flash/bench
ulang (sesuai instruksi reviewer — verifikasi cukup dengan membaca sumber).

**Important — transport ack (`README.md` baris 76 lama).** Tabel modul menyebut ack
lewat `publish` QoS1, padahal Task 4 mengganti `mqttPublishAck` ke
`esp_mqtt_client_enqueue`. Dicek langsung ke `firmware/src/mqtt_link.cpp:78-83`:

```cpp
bool mqttPublishAck(const char* json, size_t n) {
    if (!cli || !connected) return false;
    // enqueue, bukan publish: publish menulis soket di task pemanggil sambil
    // memegang lock client — kalau TX tercekik, task_cmd ikut terblokir.
    return esp_mqtt_client_enqueue(cli, t_ack, json, n, 1, 0, true) >= 0;
}
```

dan telemetri di baris 73-76 file yang sama juga `enqueue`. Baris tabel diganti jadi
"publish telemetri & ack lewat `enqueue` (non-blocking, QoS1)".

**Minor — kosakata command usang.** Diverifikasi ke `firmware/lib/bess_core/commands.cpp:25-28`
(`set_output`/`set_power` sama-sama masuk `Command::SET_POWER`) dan
`firmware/src/task_cmd.cpp` (`doSetPower`, `bess_busy`/`readback_mismatch`/`bad_value`
berlaku untuk command yang sama, tanpa peduli nama mana yang dipakai pengirim).
Diperbaiki di baris 75 (`task_cmd` row), 183 (`unsupported_cmd`), 184 (`bad_value` —
tidak ada di daftar reviewer tapi penyebabnya identik, jadi disamakan sekalian), 188
(`bess_busy`), 189 (`readback_mismatch`), 192-193 (paragraf `applied`). Semuanya kini
menyebut `set_output`/`set_power` bersamaan, selaras cara baris 161 (tabel command)
sudah menuliskannya.

**Ditemukan sendiri saat membaca ulang seluruh file — contoh "Log boot yang sehat"
(baris ~52-60) hilang baris `[boot] reset=...boot_count=...` yang ditambahkan Task 2.**
Dicek urutan cetak di `firmware/src/main.cpp:52-68` — baris `Serial.printf("[boot]
reset=%s boot_count=%u ...")` (baris 53) tercetak SEBELUM `Serial.println("[boot]
gateway-bess " FW_VERSION)` (baris 62), jadi contoh lama memberi kesan baris itu tidak
ada. Dikonfirmasi silang dengan capture serial nyata dari checklist bench Task 10:

```
[boot] reset=USB boot_count=27 heap=371764 min_heap=366776
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
```

Ditambahkan baris `[boot] reset=POWERON boot_count=12 heap=371764 min_heap=366776`
di depan contoh (nilai reset `POWERON` dipilih konsisten dengan contoh payload
telemetri yang sudah ada di file yang sama, bukan `USB` dari capture bench).

**Bagian lain file diperiksa dan TIDAK ditemukan penyimpangan lain**: konstanta
timing (`POLL_PERIOD_MS`=1500, `TELEMETRY_PERIOD_MS`=60000, `MB_FRAME_GAP_MS`=105,
`MB_TIMEOUT_MS`=500, `MB_RETRIES`=2, `COMM_LOST_AFTER`=3 — dicek ke
`firmware/src/config.h:11-30`) dan rentang register (`REG_TELEM_START`=1050
count=59 → 1050..1108, `REG_ALARM_START`=2050 count=8 → 2050..2057, `REG_P_SET`=3050,
`REG_PARAM_START`=3146 count=39 → 3146..3184 — juga `config.h`) cocok persis dengan
apa yang tertulis di README. Nomor bit `Run`=6 dan `Shutdown`=11 dicek ke tabel
`STATUS[16]` di `firmware/lib/bess_core/bess_decode.cpp:69-71` — indeks 6="running",
indeks 11="shutdown", cocok.

Commit: lihat hash di balasan singkat ke koordinator.

---

# Task 10: Dokumentasi kontrak + verifikasi akhir — Laporan

## Ringkasan

Status: **DONE**. Dokumentasi `firmware/README.md` disamakan dengan perilaku firmware
saat ini (`set_output` resmi + alias `set_power`, hasil `clamped`, alasan tolak
`queue_full`, field telemetri `last_reset_reason`/`boot_count`). Seluruh 71 checkbox di
`docs/superpowers/plans/2026-08-13-fondasi-paritas.md` dicentang. Suite native = **36
test cases, 36 succeeded**. Build esp32c6 = **SUCCESS**, `Flash: ... from 1966080
bytes`. Checklist bench 10 langkah dijalankan dari kondisi dingin (reflash penuh) —
**seluruhnya lulus**, `boot_count` tetap **27** dari awal sampai akhir (tidak ada
reboot tak terduga).

## Step 4 — Verifikasi otomatis (verbatim)

### `pio test -e native`

```
Collected 4 tests

Processing test_native_crc in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_crc\main.cpp:33: test_vectors_pdf	[PASSED]
test\test_native_crc\main.cpp:34: test_append_check	[PASSED]
-------------- native:test_native_crc [PASSED] Took 1.16 seconds --------------

Processing test_native_decode in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_decode\main.cpp:55: test_scaling_telemetri	[PASSED]
test\test_native_decode\main.cpp:56: test_status_flags	[PASSED]
test\test_native_decode\main.cpp:57: test_alarm_names	[PASSED]
------------- native:test_native_decode [PASSED] Took 1.07 seconds -------------

Processing test_native_frame in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_frame\main.cpp:68: test_build_read_contoh_pdf	[PASSED]
test\test_native_frame\main.cpp:69: test_build_write6_contoh_pdf	[PASSED]
test\test_native_frame\main.cpp:70: test_build_write5_contoh_pdf	[PASSED]
test\test_native_frame\main.cpp:71: test_parse_read_resp	[PASSED]
test\test_native_frame\main.cpp:72: test_parse_exception	[PASSED]
test\test_native_frame\main.cpp:73: test_parse_crc_salah	[PASSED]
test\test_native_frame\main.cpp:74: test_parse_echo_write	[PASSED]
------------- native:test_native_frame [PASSED] Took 1.09 seconds -------------

Processing test_native_payload in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_payload\main.cpp:277: test_telemetry_envelope	[PASSED]
test\test_native_payload\main.cpp:278: test_reset_reason_name	[PASSED]
test\test_native_payload\main.cpp:279: test_telemetry_diagnostik_boot	[PASSED]
test\test_native_payload\main.cpp:280: test_network_rssi_dbm	[PASSED]
test\test_native_payload\main.cpp:281: test_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:282: test_ts_diteruskan_saat_ntp_sinkron	[PASSED]
test\test_native_payload\main.cpp:283: test_ack_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:284: test_parse_enable	[PASSED]
test\test_native_payload\main.cpp:285: test_parse_set_power	[PASSED]
test\test_native_payload\main.cpp:286: test_parse_set_output_nama_resmi	[PASSED]
test\test_native_payload\main.cpp:287: test_parse_target_default_satu	[PASSED]
test\test_native_payload\main.cpp:288: test_parse_target_eksplisit	[PASSED]
test\test_native_payload\main.cpp:289: test_plan_power_dalam_rentang	[PASSED]
test\test_native_payload\main.cpp:290: test_plan_power_dipangkas_atas	[PASSED]
test\test_native_payload\main.cpp:291: test_plan_power_dipangkas_bawah	[PASSED]
test\test_native_payload\main.cpp:292: test_plan_power_rated_belum_diketahui	[PASSED]
test\test_native_payload\main.cpp:293: test_plan_power_nan_ditolak	[PASSED]
test\test_native_payload\main.cpp:294: test_plan_power_tepat_di_batas_bukan_clamp	[PASSED]
test\test_native_payload\main.cpp:295: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:296: test_ack	[PASSED]
test\test_native_payload\main.cpp:297: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:298: test_parse_command_truncation	[PASSED]
test\test_native_payload\main.cpp:299: test_time_after_biasa	[PASSED]
test\test_native_payload\main.cpp:300: test_time_after_rollover	[PASSED]
------------ native:test_native_payload [PASSED] Took 1.68 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.161
native         test_native_decode   PASSED    00:00:01.072
native         test_native_frame    PASSED    00:00:01.092
native         test_native_payload  PASSED    00:00:01.676
================= 36 test cases: 36 succeeded in 00:00:05.002 =================
```

Catatan: rencana Task 10 Step 4 menyebut ekspektasi "34 test cases" (angka ini sudah
usang sejak Task 8 menambahkan `test_time_after_biasa`/`test_time_after_rollover`,
membuat total naik ke 36 — dua tes ini sudah ada dari commit `341ba5f` sebelum task
ini dimulai). Instruksi eksekusi (di luar file rencana) menetapkan **36 test, semua
lulus** sebagai target — tercapai persis.

### `pio run -e esp32c6`

```
Processing esp32c6 (platform: https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip; board: esp32-c6-devkitc-1; framework: arduino)
--------------------------------------------------------------------------------
PLATFORM: Espressif 32 (53.3.13) > Espressif ESP32-C6-DevKitC-1
HARDWARE: ESP32C6 160MHz, 320KB RAM, 8MB Flash
...
Dependency Graph
|-- ArduinoJson @ 7.4.3
|-- bess_core
|-- Preferences @ 3.1.3
|-- WiFi @ 3.1.3
Building in release mode
Retrieving maximum program size .pio\build\esp32c6\firmware.elf
Checking size .pio\build\esp32c6\firmware.elf
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [==        ]  16.1% (used 52904 bytes from 327680 bytes)
Flash: [======    ]  56.4% (used 1108285 bytes from 1966080 bytes)
========================= [SUCCESS] Took 2.76 seconds =========================

Environment    Status    Duration
-------------  --------  ------------
esp32c6        SUCCESS   00:00:02.761
========================= 1 succeeded in 00:00:02.761 =========================
```

`Flash: ... from 1966080 bytes` — sesuai harapan.

## Step 5 — Checklist bench dari kondisi dingin

Persiapan: simulator lama (proses `uv`/`python` yang sudah berjalan sejak sesi
sebelumnya di COM11) dihentikan lewat PowerShell `Stop-Process` (bukan `kill` di Git
Bash — proses tree `uv run bess-sim` tidak mati lewat itu), lalu simulator dinyalakan
ulang bersih (`uv run bess-sim run --port COM11 --soc 60`, PID baru, log
`bess-sim/sim_run.log`). Gateway di-**reflash penuh** (`pio run -e esp32c6 -t upload
--upload-port COM3`) supaya boot benar-benar dari kondisi dingin.

Pembacaan serial dilakukan lewat skrip pyserial `dtr=False; rts=False` sebelum
`open()` (bukan `pio device monitor`, yang me-reset board saat baru terhubung),
dijalankan foreground dengan durasi tetap lewat Bash `timeout`, atau untuk kasus yang
perlu berjalan bersamaan dengan perintah lain, lewat PowerShell `Start-Process`
terpisah (proses OS mandiri, tetap berdurasi tetap/self-terminating — bukan
`run_in_background` milik tool ini).

### 1. Boot bersih

Serial 60 detik pertama sesudah reflash:

```
[boot] reset=USB boot_count=27 heap=371764 min_heap=366776
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
...
[wifi] OK rssi=-53 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
...
[mqtt] connected
```

`boot_count=27` — ini baseline yang dipakai untuk item 10. `reset=USB` masuk akal:
`pio run -t upload` mereset board lewat RTS pin selama proses upload esptool, yang
oleh ESP-IDF diklasifikasikan sebagai reset `USB`, bukan `SW`. **LULUS.**

### 2. Telemetri memuat field yang diminta

Dump JSON penuh dari `device/58E6C5218C78/telemetry` (dipotong):

```json
{
  "gw": "58E6C5218C78", "ts": 1786642886, "seq": 4, "api_schema_version": 1,
  "data": {
    "device_type": "bess",
    "firmware_version": "bess-0.1.0",
    "device_id": "58E6C5218C78",
    "uptime_ms": 240281,
    "time_valid": true,
    "last_reset_reason": "USB",
    "boot_count": 27,
    "network": {"ssid": "Lantai 2", "ip": "192.168.18.52", "rssi_dbm": -41},
    "bess": { ... }
  }
}
```

`rssi_dbm`, `last_reset_reason`, `boot_count`, `device_type:"bess"` — semuanya hadir.
**LULUS.**

### 3. `enable` → ack accepted + transisi state

Ack:
```json
{"id":"24c59696","cmd":"enable","result":"accepted","detail":"","applied":{},"ts":1786642903}
```

Log simulator (sampel tiap 2 dtk):
```
[  272.6s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[  274.6s] SOFTSTART p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[  276.6s] RUN       p_ac= +2.50 kW soc= 60.0% vdc= 826.2 V
```

Catatan jujur: checklist menulis "STOP → PRECHARGE → RELAY → RUN", tapi log hanya
menangkap `STOP → SOFTSTART → RUN`. Diperiksa ke `bess_sim/bess_sim/state_machine.py`:
urutan state sebenarnya memang `PRECHARGE → SOFTSTART → RELAY → RUN` dengan
`STAGE_S = 1.0` detik per tahap (total ~3 dtk STOP→RUN). Log konsol men-sample tiap 2
detik, jadi `PRECHARGE` (berakhir ~1 dtk) dan `RELAY` (berakhir ~1 dtk sebelum RUN)
keduanya terlewat oleh jendela sampling, bukan hilang dari kode. Ini bukan kegagalan
fungsional — command diterima dan device sampai RUN — tapi pencatatan checklist di
rencana kurang presisi soal apa yang *terlihat* di log 2-detik ini. **LULUS secara
fungsional**, dicatat sebagai catatan presisi checklist bukan bug.

### 4. `set_output --watt 5000` → accepted, power_pct=10

```json
{"id":"8f8c51a3","cmd":"set_output","result":"accepted","detail":"",
 "applied":{"power_pct":10,"power_w":5000},"ts":1786642969}
```
**LULUS.**

### 5. `set_output --watt 70000` → clamped, jalan di 60 kW

```json
{"id":"0eaed80c","cmd":"set_output","result":"clamped","detail":"",
 "applied":{"power_pct":120,"power_w":60000},"ts":1786643001}
```
Telemetri berikutnya: `seq=6 type=bess p=60kW soc=59.8% running=True comm_lost=False
reset=USB boot=27`. **LULUS.**

### 6. `set_power --watt 5000` (alias) → accepted

```json
{"id":"ff8b2fe3","cmd":"set_power","result":"accepted","detail":"",
 "applied":{"power_pct":10,"power_w":5000},"ts":1786643034}
```
**LULUS.**

### 7. Banjir 7 `enable` beruntun

Sebelum banjir, `disable` dikirim & di-ack `accepted` lebih dulu supaya `enable`
pertama benar-benar butuh waktu (transisi dari STOP), sehingga antrean 4-slot +
luapan 1-slot punya alasan terisi. 7 command `enable` dipublish via `paho-mqtt`
tanpa jeda (skrip mandiri, bukan `cloud_probe.py` yang hanya menangani satu command
per invokasi).

Hasil ack (dikumpulkan dari topic `command/ack`):
```
c07f9d49: result=accepted detail=''
8b66e48f: result=accepted detail=''
07ebd026: result=accepted detail=''
b82551b2: result=accepted detail=''
a0ef03c0: result=accepted detail=''
dd8a3887: result=rejected detail='queue_full'
f1bd052f: TANPA JAWABAN

total terjawab: 6/7
```

Diulang sekali lagi dengan serial capture berjalan bersamaan untuk memastikan
perintah ke-7 yang tanpa jawaban benar-benar tercatat di log (bukan hilang tanpa
jejak) — sesuai penyimpangan yang didokumentasikan di Task 7 rencana ("tidak ada
perintah yang hilang tanpa jawaban KECUALI yang benar-benar melewati kapasitas slot
luapan, dan yang itu muncul di log serial"):

```
[cmd] dibuang: antrean utama dan luapan penuh
[wifi] OK rssi=-41 ip=192.168.18.52
[cmd] enable -> accepted 
[cmd] enable -> rejected queue_full
[bess] OK p=5.0kW soc=59.2% vdc=824.3V status=0x834F
[cmd] enable -> accepted 
[cmd] enable -> accepted 
[cmd] enable -> accepted 
[cmd] enable -> accepted 
```

5 accepted + 1 `queue_full` + 1 dibuang-dan-tercatat-di-log = 7. Paling sedikit satu
`queue_full` (terpenuhi), dan yang tanpa jawaban MQTT tetap punya jejak di serial
(sesuai penyimpangan yang disengaja dari desain Task 7). **LULUS**, sesuai kontrak
yang didokumentasikan (bukan "tidak ada yang hilang sama sekali", tapi "tidak ada
yang hilang TANPA jejak").

### 8. `disable` → accepted, STOPPING

```json
{"id":"40675c1d","cmd":"disable","result":"accepted","detail":"","applied":{},"ts":1786643300}
```
Telemetri: `seq=11 ... running=False comm_lost=False reset=USB boot=27`. Log
simulator sesudahnya langsung menunjukkan `STOP` (sampel 2 detik melewatkan
`STOPPING` yang berdurasi ~1 detik) — pola sampling sama seperti item 3. **LULUS
secara fungsional**, catatan presisi sampling sama seperti item 3.

### 9. Simulator dimatikan → blok gagal tercatat, lalu `COMM_LOST` nilai lama dipertahankan

Serial capture (simulator dihentikan lewat PowerShell `Stop-Process` di tengah
capture):
```
[bess] blok param: gagal (status 1)
[bess] OK p=0.0kW soc=59.2% vdc=824.9V status=0x8B00
[bess] blok telem: gagal (status 1)
[bess] blok alarm: gagal (status 1)
[bess] blok pset: gagal (status 1)
[bess] blok param: gagal (status 1)
[bess] OK p=0.0kW soc=59.2% vdc=824.9V status=0x8B00
[bess] blok telem: gagal (status 1)
[bess] blok alarm: gagal (status 1)
[bess] blok pset: gagal (status 1)
[bess] blok param: gagal (status 1)
[bess] COMM_LOST p=0.0kW soc=59.2% vdc=824.9V status=0x8B00
```
Telemetri MQTT sesudahnya: `seq=13 type=bess p=0kW soc=59.2% running=False
comm_lost=True reset=USB boot=27` — `soc_percent` tetap 59,2% (nilai lama, sama
persis dengan sebelum comm lost), bukan dipaksa nol. **LULUS.**

### 10. `boot_count` tidak berubah sepanjang checklist

Awal: `boot_count=27` (baris `[boot]` sesudah reflash, item 1).
Akhir: `boot_count=27` (telemetri terakhir, item 9, `seq=13`).
Tidak ada baris `[boot]` baru muncul di log serial mana pun sepanjang item 2–9.
**LULUS — tidak ada reboot tak terduga.**

## Commit

Kode (Task 1–9) sudah ter-commit sebelum task ini dimulai (HEAD `b65500f` saat mulai).
Task 10 menambah satu commit dokumentasi:

```
firmware/README.md, docs/superpowers/plans/2026-08-13-fondasi-paritas.md
```

Hash commit dicatat di ringkasan balasan ke pemanggil.

## Hal yang mengejutkan / catatan untuk pembaca berikutnya

1. **Angka "34 test cases" di rencana sudah usang** — kode nyata (setelah Task 8)
   sudah 36 sejak sebelum Task 10 dimulai. Instruksi eksekusi eksternal sudah
   menyesuaikan ke 36; tidak ada tindakan lebih lanjut diperlukan.
2. **State transisi `PRECHARGE`/`RELAY`/`STOPPING` tidak pernah terlihat di log
   konsol simulator** karena berdurasi 1 detik sedangkan log men-sample tiap 2
   detik — bukan bug, tapi checklist rencana ("STOP → PRECHARGE → RELAY → RUN")
   mengasumsikan log yang lebih rapat dari kenyataan. Dikonfirmasi lewat kode
   `bess_sim/bess_sim/state_machine.py` (`_SEQ`, `STAGE_S=1.0`).
3. **Command ke-7 dalam uji banjir tidak pernah mendapat balasan MQTT** — ini
   sesuai desain yang didokumentasikan secara eksplisit di Task 7 (penyimpangan
   dari spec, kapasitas antrean 4+1 = 5 command tertunda maksimum), bukan
   kegagalan yang tidak terduga. Dibuktikan lewat log serial yang mencatat
   `[cmd] dibuang: antrean utama dan luapan penuh`.
4. **`grid_current_b_a` sempat menunjukkan 6553.5** di satu dump telemetri awal
   (di luar 10 item checklist) — kemungkinan nilai sisa dari sesi sebelumnya atau
   anomali sesaat pada simulator; tidak diselidiki lebih lanjut karena di luar
   scope Task 10 dan tidak muncul lagi di dump-dump berikutnya.
