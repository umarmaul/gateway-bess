# Task 2 — Diagnostik boot: alasan reset & cacah boot ke serial dan telemetri

Status: **DONE_WITH_CONCERNS**

## Ringkasan

Semua langkah brief (Step 1–13) diselesaikan sesuai urutan: tes gagal → implementasi → tes hijau → sambung ke `main.cpp` → build → flash bench → verifikasi via probe MQTT → commit. 25/25 tes native lulus, build ESP32 SUCCESS, dan field `last_reset_reason`/`boot_count` terbukti keluar dengan benar baik di baris serial `[boot]` maupun di payload telemetri MQTT yang diterima lewat probe.

Ditandai **DONE_WITH_CONCERNS**, bukan DONE polos, karena verifikasi bench sempat menampilkan gejala yang terlihat seperti reboot loop otonom (boot_count naik dari 6 → 10 tanpa aku sengaja reflash), dan setelah investigasi ternyata penyebabnya adalah aktivitas verifikasi saya sendiri (lihat §Temuan mengejutkan di bawah) — bukan bug firmware. Ini didokumentasikan lengkap supaya tidak disalahartikan sebagai bukti reboot loop lapangan oleh siapa pun yang membaca ledger nanti.

## Step 1–2: tes ditulis

`firmware/test/test_native_payload/main.cpp` — ditambahkan `#include "reset_info.h"`, fungsi `test_reset_reason_name()` dan `test_telemetry_diagnostik_boot()`, didaftarkan di `main()` tepat sesudah `RUN_TEST(test_telemetry_envelope);`.

## Step 3: tes dijalankan, GAGAL karena file belum ada (bukti compile error)

```
test\test_native_payload\main.cpp:8:10: fatal error: 'reset_info.h' file not found
    8 | #include "reset_info.h"
      |          ^~~~~~~~~~~~~~
...
1 error generated.
*** [.pio\build\native\test\test_native_payload\main.o] Error 1
```

## Catatan penyimpangan urutan (bukan blocker) — dilaporkan sesuai instruksi

Brief mengasumsikan bahwa setelah Step 4 (`reset_info.h/.cpp` stub) saja, tes akan gagal dengan **assertion failure**, bukan compile error. Ternyata tidak — karena `test_telemetry_diagnostik_boot()` (ditambahkan bersamaan di Step 2) sudah memakai `s.last_reset_reason`/`s.boot_count`, field yang baru ditambahkan ke `SysInfo` di **Step 7**, bukan Step 4. Karena kedua tes baru hidup di satu translation unit (`main.cpp`), seluruh file gagal kompilasi sampai Step 7 selesai — jadi menjalankan tes setelah Step 4 saja tetap memberi compile error (`no member named 'last_reset_reason' in 'SysInfo'`), bukan assertion failure:

```
test\test_native_payload\main.cpp:114:7: error: no member named 'last_reset_reason' in 'SysInfo'
test\test_native_payload\main.cpp:115:7: error: no member named 'boot_count' in 'SysInfo'
2 errors generated.
```

Untuk tetap memenuhi mandat TDD (menyaksikan assertion failure sungguhan, bukan sekadar compile error berulang), saya menjalankan Step 7 (tambah field `SysInfo` + wiring `payload.cpp`) **sebelum** mengisi implementasi asli `resetReasonName()`, dengan stub `"BELUM"` dari Step 4 tetap terpasang. Baru pada titik itu tes bisa dikompilasi dan genuinely gagal karena assert:

```
test\test_native_payload\main.cpp:104: test_reset_reason_name: Expected 'POWERON' Was 'BELUM'	[FAILED]
test\test_native_payload\main.cpp:197: test_telemetry_diagnostik_boot	[PASSED]
...
14 test cases: 1 failed, 12 succeeded
```

Menariknya `test_telemetry_diagnostik_boot` sudah PASSED di titik ini — karena tes itu men-set `s.last_reset_reason = "PANIC"` langsung (tidak memanggil `resetReasonName()`), jadi ia murni menguji jalur passthrough field `SysInfo` → JSON yang memang sudah selesai di Step 7. Hanya `test_reset_reason_name` yang menguji `resetReasonName()` itu sendiri, dan itulah yang gagal dengan benar terhadap stub `"BELUM"`.

Setelah itu saya mengisi implementasi asli (Step 6) dan menjalankan ulang.

## Step 5 (efektif, urutan disesuaikan): assertion failure sungguhan — bukti TDD terpenuhi

```
test\test_native_payload\main.cpp:104: test_reset_reason_name: Expected 'POWERON' Was 'BELUM'	[FAILED]
```

## Step 8: seluruh suite native hijau

```
=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.280
native         test_native_decode   PASSED    00:00:01.180
native         test_native_frame    PASSED    00:00:01.210
native         test_native_payload  PASSED    00:00:01.616
================= 25 test cases: 25 succeeded in 00:00:05.286 =================
```

23 tes lama + `test_reset_reason_name` + `test_telemetry_diagnostik_boot` = 25. Cocok dengan harapan brief.

## Step 9–10: sambung ke `main.cpp`, build ESP32

Include `<Preferences.h>`, `<esp_system.h>`, `"reset_info.h"`; enam `static_assert` (POWERON/SW/PANIC/INT_WDT/TASK_WDT/BROWNOUT); `g_reset_reason[24]`/`g_boot_count` global; blok boot di `setup()` (delay 200 ms, `resetReasonName(esp_reset_reason())`, NVS `Preferences("boot")` increment, `Serial.printf("[boot] ...")`); dua baris `si.last_reset_reason`/`si.boot_count` di blok telemetri `loop()`.

**Semua enam `static_assert` LULUS** — build ESP32 SUCCESS tanpa perlu koreksi apa pun:

```
RAM:   [==        ]  16.1% (used 52904 bytes from 327680 bytes)
Flash: [======    ]  56.3% (used 1106595 bytes from 1966080 bytes)
Building .pio\build\esp32c6\firmware.bin
...
========================= [SUCCESS] Took 13.76 seconds =========================
```

Flash naik dari kondisi sebelum task (belum dicatat presisi di sini, lihat commit Task 1) ke **56,3% (1.106.595 / 1.966.080 B)** dengan partisi baru dari Task 1 — jauh dari mepet.

## Step 11: `cloud_probe.py` diperbarui

Baris `print(...)` di `on_msg` diganti persis sesuai brief — menambah `d = doc["data"]` dan field `reset=`/`boot=` di output.

## Step 12: verifikasi bench

**Serial `[boot]` line pertama yang tertangkap** (setelah flash, via `pio run -t upload -t monitor` gabungan supaya jeda ke monitor minimal):

```
[boot] reset=UNKNOWN_11 boot_count=6 heap=371908 min_heap=366920
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
```

### Temuan mengejutkan #1 — `reset=UNKNOWN_11`, bukan `SW` seperti dugaan brief

Brief mengharapkan `reset=SW` karena upload dianggap software-reset. Kenyataannya ESP-IDF yang dipakai (`esp32c6` via pioarduino) punya `esp_reset_reason_t` yang **lebih panjang** dari 11 nilai yang di-mirror `reset_info.h` (0–10, meniru enum ESP32 klasik). Dicek langsung di
`C:\Users\legio\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\include\esp_system\include\esp_system.h`:

```
ESP_RST_UNKNOWN=0, POWERON=1, EXT=2, SW=3, PANIC=4, INT_WDT=5, TASK_WDT=6,
WDT=7, DEEPSLEEP=8, BROWNOUT=9, SDIO=10, USB=11, JTAG=12, EFUSE=13,
PWR_GLITCH=14, CPU_LOCKUP=15
```

Nilai `11 = ESP_RST_USB` — reset lewat USB-Serial/JTAG controller (yang dipakai esptool untuk "Hard resetting via RTS pin" pada ESP32-C6 native USB) sekarang punya kode sendiri, terpisah dari `SW`. Enam `static_assert` di brief (POWERON/SW/PANIC/INT_WDT/TASK_WDT/BROWNOUT) semuanya tetap valid — nilai 0–10 tidak bergeser — jadi build **tidak gagal**. Yang terjadi hanyalah `USB` (nilai 11) tak termasuk dalam tabel `NAMA[]`/konstanta `RESET_*` yang di-mirror brief (memang cuma meng-cover 0–10), sehingga jatuh ke fallback `UNKNOWN_11` — **persis desain yang dimaksud** ("nilai tak dikenal tidak boleh hilang diam-diam"). Ini bukan bug, bukan static_assert gagal (jadi tidak masuk kondisi "stop for instructions" yang diberikan), tapi tetap saya laporkan karena ini sinyal nyata tentang SDK, bukan cuma detail kecil. **Rekomendasi untuk task lanjutan (di luar scope Task 2 ini):** brief eksplisit hanya minta 11 nilai (0–10); menambah `RESET_USB=11` dkk ke `reset_info.h`/`NAMA[]` adalah perbaikan kecil yang bisa dipertimbangkan terpisah, tapi saya sengaja tidak mengubahnya sendiri di luar apa yang diminta brief.

### Temuan mengejutkan #2 — gejala mirip reboot loop, ternyata dari aktivitas verifikasi saya sendiri

Selama percobaan menangkap satu baris telemetri MQTT yang memuat `last_reset_reason`/`boot_count` via `cloud_probe.py`, saya berulang kali membuka-tutup `pio device monitor --port COM3` (untuk mengecek status board di antara langkah). `pio device monitor` secara default **toggle DTR/RTS saat port dibuka** — persis rangkaian auto-reset ala Arduino pada board ini — sehingga **setiap kali saya membukanya, board ikut reset**. Ini terlihat dari:
- Probe MQTT menangkap topic `status` **flapping** `online → offline → online` beberapa kali, dan `boot_count` naik dari **6** (dikonfirmasi via serial sesaat setelah flash) menjadi **10** (dikonfirmasi via telemetri MQTT) — 4 kenaikan tanpa saya sengaja reflash.
- Setelah saya berhenti membuka `pio device monitor` biasa dan memakai skrip `pyserial` langsung dengan `s.dtr=False; s.rts=False` (tidak toggle line kontrol), capture serial **120 detik penuh, kontinu, tanpa satu pun baris `[boot]` baru dan tanpa event `[mqtt] disconnected`** — board sepenuhnya stabil selama window itu.
- Retry telemetri berikutnya (foreground, tanpa saya menyentuh COM3 sama sekali di antaranya) menangkap **dua** pesan telemetri berurutan (`seq=3` lalu `seq=4`) dengan **`boot=11` yang identik di keduanya** — bukti langsung bahwa board tidak reboot di antara dua siklus telemetri (periode 60 detik) begitu saya berhenti mengganggu port serial.

Kesimpulan: **tidak ada reboot loop otonom di firmware/bench.** Kenaikan `boot_count` 6→10 sepenuhnya konsisten dengan jumlah sesi `pio device monitor` yang saya buka-tutup selama proses verifikasi ini. Instrumentasi (Task 2) sendiri bekerja benar dan justru itulah yang memungkinkan temuan ini terlihat sama sekali — kalau tidak ada `boot_count`, gejala flapping `online/offline` di topic status akan sulit dibedakan dari sekadar jitter jaringan. Saya tidak mencoba "memperbaiki" reboot ini karena memang tidak ada yang perlu diperbaiki — video reset di sini murni disebabkan oleh cara saya memantau port, bukan perilaku board di lapangan.

**Baris telemetri final yang bersih** (dua pesan berurutan, `boot=11` stabil, dari probe foreground tanpa gangguan COM3):

```
== device/58E6C5218C78/status ==
online

== device/58E6C5218C78/telemetry ==
seq=3 type=bess p=0kW soc=0% running=False comm_lost=True reset=UNKNOWN_11 boot=11

== device/58E6C5218C78/telemetry ==
seq=4 type=bess p=0kW soc=0% running=False comm_lost=True reset=UNKNOWN_11 boot=11
```

Catatan: `comm_lost=True` dan `soc=0%`/`p=0kW` pada saat capture ini bukan soal Task 2 — link Modbus gateway↔simulator sempat putus selama sesi verifikasi yang panjang ini (simulator di COM11 sendiri tetap sehat, `soc=60.0% vdc=826.6V` terus di log-nya sampai akhir). `comm_lost` flag terbukti jujur melaporkan kondisi itu — sesuai desain yang sudah ada, bukan regresi dari task ini.

### Ringkasan bukti yang dikumpulkan (tiga sumber independen, semuanya konsisten)

1. Serial langsung setelah flash: `reset=UNKNOWN_11 boot_count=6`.
2. Telemetri MQTT (window awal, sebelum saya sadar soal DTR/RTS): `reset=UNKNOWN_11 boot=10`.
3. Telemetri MQTT (window bersih, dua pesan berurutan): `reset=UNKNOWN_11 boot=11` (stabil, tidak berubah).
4. Capture serial 120 detik kontinu tanpa toggle DTR/RTS: nol reboot, nol disconnect.

Ketiganya cocok dalam bentuk (`UNKNOWN_11` konsisten) dan `boot_count` monoton naik, tidak pernah turun/reset ke 1 — sesuai kontrak "pencacah monotonik di NVS".

## Step 13: commit

Lihat commit hash di ringkasan balasan ke koordinator.

## Bench dibersihkan

- Simulator `bess-sim` (COM11) sudah berhenti sendiri di suatu titik selama sesi verifikasi (kemungkinan besar ikut ter-kill saat saya menghentikan proses `python`/`pio` yang macet menahan COM3 — `Get-Process` yang dipakai untuk itu memfilter nama proses `python|pio|platformio`, dan simulator kemungkinan bukan proses bernama persis itu, tapi pada titik akhir sesi tidak ada proses `python`/`pio`/`uv`/simulator apa pun yang tersisa; COM11 dan COM3 sama-sama free). Tidak ada tindakan tambahan diperlukan.
- COM3 dikonfirmasi bebas (`Get-Process` untuk `python|pio|platformio` kosong) sebelum sesi ini ditutup.

## File yang diubah

- `firmware/lib/bess_core/reset_info.h` (baru)
- `firmware/lib/bess_core/reset_info.cpp` (baru)
- `firmware/lib/bess_core/payload.h`
- `firmware/lib/bess_core/payload.cpp`
- `firmware/src/main.cpp`
- `firmware/test/test_native_payload/main.cpp`
- `bess-sim/tools/cloud_probe.py`

---

# Ronde perbaikan 1 — hasil review "spec ❌" (2 temuan Important)

## Ringkasan perbaikan

1. **`USB`(11) dan `JTAG`(12) ditambahkan** ke `RESET_*` di `reset_info.h` dan ke tabel `NAMA[]` di `reset_info.cpp` — sesuai spec §3.2 (13 nilai bernama: `POWERON, SW, PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT, DEEPSLEEP, EXT, SDIO, USB, JTAG, UNKNOWN`). `EFUSE`/`PWR_GLITCH`/`CPU_LOCKUP` (13/14/15) **sengaja tidak ditambahkan** — spec tidak menamainya, dan `UNKNOWN_<n>` sudah menjaga angkanya tidak hilang. Direkomendasikan sebagai perubahan spec terpisah jika suatu saat dianggap perlu — bukan keputusan saya untuk ambil sendiri di sini.
2. **`static_assert` diperluas dari 6 menjadi 13** — sekarang mencakup SEMUA konstanta yang dipakai tabel `NAMA[]` (`UNKNOWN, POWERON, EXT, SW, PANIC, INT_WDT, TASK_WDT, WDT, DEEPSLEEP, BROWNOUT, SDIO, USB, JTAG`), bukan cuma 6 yang sebelumnya (`POWERON, SW, PANIC, INT_WDT, TASK_WDT, BROWNOUT`). Klaim di komentar ("build gagal di sini — bukan diam-diam salah label") sekarang benar untuk seluruh tabel, bukan sebagian.
3. **Kegagalan `bootprefs.begin("boot", false)` sekarang dilog** — baris `[boot] WARNING nvs_open_gagal boot_count=0 (bukan boot pertama, NVS 'boot' tak terbuka)` muncul di serial kalau NVS gagal dibuka, supaya `boot_count=0` akibat kegagalan NVS tidak lagi tak terbedakan diam-diam dari boot pertama yang genuine.
4. Tes `test_reset_reason_name` diperluas dengan dua assert baru (`RESET_USB`→`"USB"`, `RESET_JTAG`→`"JTAG"`).

## Bukti TDD untuk penambahan USB/JTAG

Sebelum menambahkan `"USB", "JTAG"` ke tabel `NAMA[]`, saya sementara MENAHAN perubahan itu (hanya tes yang sudah diperluas) dan menjalankan suite — genuinely gagal:

```
test\test_native_payload\main.cpp:108: test_reset_reason_name: Expected 'USB' Was 'UNKNOWN_11'	[FAILED]
============ 14 test cases: 1 failed, 12 succeeded in 00:00:02.215 ============
```

Ini persis mengonfirmasi temuan #1 dari review: sebelum perbaikan, `reset=UNKNOWN_11` — bukan `USB` — adalah hasil aktual di lapangan (sama seperti yang tertangkap di bench pada laporan Task 2 awal). Setelah tabel `NAMA[]` dikembalikan dengan `"USB", "JTAG"`, dijalankan ulang.

## Command dan output — suite native lengkap (perintah cakupan yang diminta)

```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

```
Processing test_native_payload in native environment
--------------------------------------------------------------------------------
Building...
Testing...
test\test_native_payload\main.cpp:197: test_telemetry_envelope	[PASSED]
test\test_native_payload\main.cpp:198: test_reset_reason_name	[PASSED]
test\test_native_payload\main.cpp:199: test_telemetry_diagnostik_boot	[PASSED]
test\test_native_payload\main.cpp:200: test_network_rssi_dbm	[PASSED]
test\test_native_payload\main.cpp:201: test_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:202: test_ts_diteruskan_saat_ntp_sinkron	[PASSED]
test\test_native_payload\main.cpp:203: test_ack_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:204: test_parse_enable	[PASSED]
test\test_native_payload\main.cpp:205: test_parse_set_power	[PASSED]
test\test_native_payload\main.cpp:206: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:207: test_ack	[PASSED]
test\test_native_payload\main.cpp:208: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:209: test_parse_command_truncation	[PASSED]
------------ native:test_native_payload [PASSED] Took 1.60 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.234
native         test_native_decode   PASSED    00:00:01.096
native         test_native_frame    PASSED    00:00:01.152
native         test_native_payload  PASSED    00:00:01.600
================= 25 test cases: 25 succeeded in 00:00:05.082 =================
```

Masih **25/25** — jumlah tes tidak bertambah (dua assert baru ditambahkan ke dalam `test_reset_reason_name` yang sudah ada, bukan tes baru), sesuai arahan "tidak perlu re-flash / re-run bench".

## Command dan output — build esp32c6 (verifikasi 13 static_assert lulus)

```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6
```

```
Compiling .pio\build\esp32c6\src\main.cpp.o
Compiling .pio\build\esp32c6\lib6a8\bess_core\reset_info.cpp.o
Archiving .pio\build\esp32c6\lib6a8\libbess_core.a
Linking .pio\build\esp32c6\firmware.elf
RAM:   [==        ]  16.1% (used 52904 bytes from 327680 bytes)
Flash: [======    ]  56.3% (used 1106717 bytes from 1966080 bytes)
Building .pio\build\esp32c6\firmware.bin
Successfully created esp32c6 image.
========================= [SUCCESS] Took 5.25 seconds =========================
```

SUCCESS — ketiga belas `static_assert` (termasuk `ESP_RST_USB == RESET_USB` dan `ESP_RST_JTAG == RESET_JTAG` yang baru) lulus tanpa perubahan lain. Flash naik tipis (1.106.595 → 1.106.717 B, +122 B) akibat dua entri tabel tambahan — masih jauh dari mepet (56,3%).

## Tidak dilakukan (sesuai arahan koordinator)

Tidak reflash / tidak re-run bench untuk ronde ini — perubahan murni tabel lookup, daftar assert, dan satu baris log; suite native + build esp32c6 sudah mencakup seluruh kode yang diamandemen. Bench asli (COM3 + simulator COM11) sudah dibersihkan di akhir laporan sebelumnya dan tidak disentuh lagi di ronde ini.
