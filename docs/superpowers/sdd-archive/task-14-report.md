# Task 14: ModbusMaster runtime + task poll BESS — laporan verifikasi bench

## Ringkasan

Implementasi (`modbus_port.h/.cpp`, `task_bess.h/.cpp`, modifikasi `main.cpp`) ditulis
persis sesuai kode VERBATIM di brief. Build ESP32-C6 SUCCESS, native test 19/19 PASSED,
dan diuji langsung di bench nyata (gateway COM3 lawan simulator `bess-sim` di COM10)
dengan hasil sesuai ekspektasi untuk kondisi normal, comm_lost, dan recovery.

## Build

```
$ pio run -e esp32c6
...
RAM:   [=         ]  13.1% (used 42872 bytes from 327680 bytes)
Flash: [=======   ]  73.4% (used 961561 bytes from 1310720 bytes)
========================= [SUCCESS] Took 15.13 seconds =========================
```

```
$ pio test -e native
...
=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.854
native         test_native_decode   PASSED    00:00:01.208
native         test_native_frame    PASSED    00:00:01.255
native         test_native_payload  PASSED    00:00:01.841
================= 19 test cases: 19 succeeded in 00:00:06.159 =================
```

## Flash ke bench (COM3)

Flash berhasil di percobaan pertama, tidak ada port-wedge:

```
Wrote 981808 bytes (599540 compressed) at 0x00010000 in 3.4 seconds (effective 2277.9 kbit/s)...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
========================= [SUCCESS] Took 8.50 seconds =========================
```

## Uji bench — simulator berjalan normal (soc=60)

Simulator dijalankan sebagai proses background:
`uv run bess-sim run --port COM10 --soc 60` (dari `bess-sim/`).

Log simulator (verbatim, awal run):
```
bess-sim AKTIF di COM10 node 1 (SIMULATOR — bukan device asli)
[    2.0s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[    4.0s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[    6.0s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
... (berulang stabil selama run)
```

Log monitor gateway COM3 (verbatim, ~30 detik):
```
--- Terminal on COM3 | 115200 8-N-1
[wifi] OK rssi=-50 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-40 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-48 ip=192.168.18.52
[wifi] OK rssi=-48 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-39 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-44 ip=192.168.18.52
```

**Verifikasi:** `[bess] OK ...` muncul konsisten (bukan `COMM_LOST`), `soc=60.0%` dan
`vdc=826.6V` di log gateway **cocok persis** dengan angka yang dilaporkan simulator —
bukti langsung bahwa `mbReadRegs` berhasil menarik telemetri, alarm, setpoint, dan param
(termasuk SOC di reg 3184) lewat RS485 nyata (COM3 gateway ↔ COM10 dongle ↔ simulator).
`bess-sim` versi terpasang tidak punya flag verbose per-query; kecocokan nilai dipakai
sebagai bukti aktivitas query berhasil (tidak ada indikasi command lain yang bisa
menghasilkan angka itu).

## Uji comm_lost (deviasi diizinkan: hentikan proses simulator, bukan cabut kabel)

Proses simulator (`uv`, `bess-sim.exe` via python) dihentikan paksa (`Stop-Process`)
untuk mensimulasikan efek yang sama seperti kabel tercabut (tidak ada balasan RS485).

Log monitor gateway COM3 setelah simulator dihentikan:
```
[wifi] OK rssi=-45 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-40 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-52 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-52 ip=192.168.18.52
[wifi] OK rssi=-49 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

**Verifikasi:** `COMM_LOST` muncul dalam ≤3 siklus poll (`COMM_LOST_AFTER=3`,
`POLL_PERIOD_MS=1500`) setelah simulator berhenti membalas, sesuai spec di brief.
Nilai lama (`soc=60.0%`, `vdc=826.6V`) **dipertahankan** (tidak di-reset ke 0/NaN) —
sesuai komentar di brief `// nilai lama dipertahankan`.

## Uji recovery

Simulator dijalankan ulang: `uv run bess-sim run --port COM10 --soc 60`.

Log monitor gateway COM3 setelah simulator restart:
```
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-41 ip=192.168.18.52
[wifi] OK rssi=-51 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-41 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
[wifi] OK rssi=-40 ip=192.168.18.52
[wifi] OK rssi=-51 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

**Verifikasi:** status kembali ke `OK` segera setelah simulator aktif kembali (poll
berikutnya sudah pulih), sesuai ekspektasi.

## File yang dibuat/dimodifikasi

- `firmware/src/modbus_port.h` (baru)
- `firmware/src/modbus_port.cpp` (baru)
- `firmware/src/task_bess.h` (baru)
- `firmware/src/task_bess.cpp` (baru)
- `firmware/src/main.cpp` (dimodifikasi: `#include "modbus_port.h"` + `#include "task_bess.h"`,
  panggil `mbPortInit(); taskBessStart();` di akhir `setup()`)

## Concerns

- Tidak ada flag verbose di `bess-sim` CLI untuk menampilkan aktivitas query per-frame;
  verifikasi "aktivitas query" mengandalkan kecocokan nilai telemetri antara log gateway
  dan log simulator, bukan hitungan frame eksplisit. Cukup meyakinkan tapi dicatat sebagai
  keterbatasan observasi (bukan bug firmware).
- `status_raw=0x8B00` — belum di-cross-check terhadap `bessStatusName()` per bit di laporan
  ini (di luar scope Task 14); tidak berpengaruh terhadap acceptance test yang diminta.
