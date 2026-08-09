# Task 16: Checklist verifikasi akhir + README — laporan verifikasi

## Ringkasan

Seluruh 4 step dijalankan sesuai brief, tanpa deviasi kecuali yang eksplisit
diizinkan di Step 2 (reset gateway via toggle RTS/DTR, bukan cabut USB fisik).
Semua verifikasi otomatis hijau, bench e2e cold-start LULUS, tiga README ditulis,
`docs/superpowers/plans/2026-08-09-gateway-bess.md` dicentang untuk keempat step
Task 16.

## Step 1 — Verifikasi otomatis

```
$ cd bess-sim && uv run pytest -v
...
============================= 57 passed in 0.19s ==============================

$ uv run bess-sim selftest
selftest: state=RUN p_ac=10.0 kW vdc=804.8 V soc=50.0%
selftest: LULUS

$ cd ../firmware && pio test -e native
...
native         test_native_crc      PASSED    00:00:02.096
native         test_native_decode   PASSED    00:00:01.420
native         test_native_frame    PASSED    00:00:01.388
native         test_native_payload  PASSED    00:00:02.262
================= 19 test cases: 19 succeeded in 00:00:07.167 =================

$ pio run -e esp32c6
...
RAM:   [==        ]  16.1% (used 52824 bytes from 327680 bytes)
Flash: [========  ]  84.3% (used 1104533 bytes from 1310720 bytes)
========================= [SUCCESS] Took 17.92 seconds =========================
```

**Semua hijau/SUCCESS**: 57 pytest + selftest LULUS + 19 native test + build ESP32-C6.

## Step 2 — Checklist e2e dari kondisi dingin

Deviasi (sesuai izin di brief): tidak bisa mencabut USB gateway secara fisik.
Sebagai gantinya dipakai reset via pulsa RTS/DTR lewat pyserial (sekuens
auto-reset standar CP2102/USB-CDC — `EN` via RTS, `GPIO0` via DTR), plus
start/stop simulator sungguhan di COM10. Bench: gateway COM3 (`gw=58E6C5218C78`,
firmware hasil build Step 1, belum di-flash ulang — sudah firmware final dari
Task 15/produk Step 1 build ini), simulator `bess-sim` di COM10.

### 2a. Cold boot TANPA simulator (BESS belum menyala)

Simulator dipastikan mati (`taskkill` semua proses `bess-sim`), lalu gateway
di-reset dan log ditangkap dari nol:

```
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
E (409) esp-tls: couldn't get hostname for :mqtt-dev.bepbatt.id: getaddrinfo() returns 202, addrinfo=0x0
E (410) transport_base: Failed to open a new connection: 32769
E (413) mqtt_client: Error transport connect
[mqtt] disconnected
E (418) wifi:sta is connecting, return error
[   143][E][STA.cpp:417] connect(): STA connect failed! 0x3007: ESP_ERR_WIFI_CONN
[wifi] OK rssi=-52 ip=192.168.18.52
[bess] OK p=0.0kW soc=0.0% vdc=0.0V status=0x0000
[wifi] OK rssi=-41 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=0.0% vdc=0.0V status=0x0000
[wifi] OK rssi=-43 ip=192.168.18.52
[mqtt] connected
[bess] COMM_LOST p=0.0kW soc=0.0% vdc=0.0V status=0x0000
[wifi] OK rssi=-59 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=0.0% vdc=0.0V status=0x0000
...
```

**LULUS.** Gateway boot sampai tuntas tanpa menggantung meski BESS tidak ada di
bus sama sekali: WiFi asosiasi normal (bukti `[wifi] OK`), MQTT gagal konek
pertama kali (DNS belum siap) lalu **pulih sendiri** (`[mqtt] connected`, retry
internal esp-mqtt — bukan campur tangan manual), dan `task_bess` melaporkan
`COMM_LOST` dengan jujur (nilai 0 karena memang belum pernah ada data — bukan
macet, siklus 5 detik terus berjalan).

### 2b. Simulator dinyalakan → recovery

```
$ uv run bess-sim run --port COM10 --soc 55
bess-sim AKTIF di COM10 node 1 (SIMULATOR — bukan device asli)
[    2.0s] STOP      p_ac= +0.00 kW soc= 55.0% vdc= 816.5 V
```

Log gateway (COM3) tak lama setelahnya:

```
[wifi] OK rssi=-58 ip=192.168.18.52
[bess] OK p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
[wifi] OK rssi=-56 ip=192.168.18.52
[bess] OK p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
```

**LULUS.** `[bess] OK` menggantikan `COMM_LOST` begitu simulator hidup, `soc=55.0%`
dan `vdc=816.5V` cocok persis dengan `--soc 55` yang dipakai — bukti gateway benar
membaca data real dari bus, bukan kebetulan.

### 2c. Simulator dimatikan lagi → comm_lost jujur (nilai terakhir dipertahankan)

Kedua proses `bess-sim` (`python.exe` PID 40036 & 35468 — verified via
`Get-CimInstance Win32_Process`) dihentikan paksa:

```
[wifi] OK rssi=-57 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
[wifi] OK rssi=-56 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
```

**LULUS.** `comm_lost` naik lagi begitu poll mulai gagal, dan `soc`/`vdc`/`status`
**mempertahankan nilai terakhir yang diketahui** (55,0% / 816,5 V / 0x8B00) —
persis kontrak yang didokumentasikan di README ("bukan angka nol palsu").

### 2d. Simulator direstart → recovery kedua kalinya (fokus utama brief)

```
$ uv run bess-sim run --port COM10 --soc 55
```

Log gateway:

```
[wifi] OK rssi=-47 ip=192.168.18.52
[mqtt] connected
[bess] OK p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
[wifi] OK rssi=-83 ip=192.168.18.52
[wifi] OK rssi=-55 ip=192.168.18.52
[bess] OK p=0.0kW soc=55.0% vdc=816.5V status=0x8B00
```

**LULUS — inilah bukti langsung yang diminta brief:** setelah simulator
di-restart, baris `[bess] OK` muncul lagi tanpa intervensi apa pun di sisi
gateway (tidak perlu reboot, tidak perlu reconnect manual). WiFi/MQTT tidak
pernah putus sepanjang seluruh siklus 2a–2d (log `[wifi] OK` konsisten,
`[mqtt] connected` bertahan) — hanya link Modbus RS485 yang naik-turun mengikuti
hidup-matinya simulator.

Kedua proses simulator dihentikan lagi setelah verifikasi (bench dibiarkan bersih).

## Step 3 — README

Tiga file dibuat:
- `README.md` (root) — peta repo, diagram bench, cara menjalankan (3 perintah),
  tautan spec/plan, catatan "BESS asli menggantikan simulator tanpa perubahan
  firmware", non-scope, batasan `gateway-v2/`.
- `bess-sim/README.md` — instalasi uv, `selftest`/`run`/`master_probe.py`/
  `cloud_probe.py`, format skenario YAML, tabel register yang disimulasikan,
  5 keputusan fidelity (SOC di 3184, busy saat transisi, `--strict-timing`,
  reset akumulator per-start, identitas jujur-tapi-kabel-setia).
- `firmware/README.md` — prasyarat PlatformIO, salin `secrets.h`, build/upload/
  monitor, tabel arsitektur task (`loop`/`task_bess`/`task_cmd`/`mqtt_link`/
  `wifi_mgr`/`state`), kontrak MQTT lengkap (topic, contoh payload telemetri,
  tabel 3 command, bentuk ack + tabel 8 alasan tolak, semantik `comm_lost`).

Tidak ada kredensial nyata (SSID/password/broker-user) disalin ke README manapun —
hanya placeholder dan nama variabel dari `secrets.example.h`.

## Step 4 — Commit

`docs/superpowers/plans/2026-08-09-gateway-bess.md` dicentang untuk 4 step Task 16
(dengan catatan hasil ringkas di Step 1 & 2). Commit dibuat mencakup: `README.md`,
`bess-sim/README.md`, `firmware/README.md`, dan file plan yang dimodifikasi — lihat
hash di ringkasan balasan.

## File yang dibuat/dimodifikasi

- `README.md` (baru)
- `bess-sim/README.md` (baru)
- `firmware/README.md` (baru)
- `docs/superpowers/plans/2026-08-09-gateway-bess.md` (dimodifikasi: centang Task 16)
- `.superpowers/sdd/2026-08-09-gateway-bess/task-16-report.md` (baru, file ini)

## Concerns

- **Flash 84,3% terpakai** — tidak berubah dari Task 15 (README tidak menyentuh
  kode), tapi tetap dicatat sebagai batas ruang untuk fitur berikutnya (mis. TLS
  8883 produksi, disebut eksplisit sebagai non-scope di README firmware).
- **Step 2 dijalankan dengan reset software (RTS/DTR), bukan cabut-colok USB
  fisik** — sesuai deviasi yang diizinkan brief. Ini mencakup skenario "BESS
  belum menyala saat gateway boot" dan "simulator di-restart" dengan baik, tapi
  **tidak** menguji jalur cabut-USB-gateway-lalu-colok-lagi (power-cycle
  sesungguhnya) — kalau itu penting untuk fase berikutnya (mis. menguji
  brown-out/watchdog), perlu sesi terpisah dengan akses fisik ke board.
- Selama bench Step 2 ditemukan **dua proses Python berjalan bersamaan** untuk
  satu `uv run bess-sim run` (PID 40036 dari `.venv` dan PID 35468 dari Python
  sistem, keduanya membuka `--port COM10 --soc 55`) — kemungkinan artefak cara
  `uv run` dijalankan sebagai proses background di sesi ini (bukan bug di
  `bess-sim` itu sendiri; `SerialServer` hanya membuka satu handle port, jadi
  proses kedua kemungkinan idle/gagal diam-diam berebut port). Tidak
  mempengaruhi hasil verifikasi (log gateway tetap benar), tapi dicatat sebagai
  kejanggalan operasional yang belum ditelusuri akar penyebabnya — bukan
  masalah untuk task ini karena di luar scope (bukan kode simulator/firmware).
