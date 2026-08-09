# Task 15: MQTT uplink + command handler + ack (end-to-end bench) — laporan verifikasi

## Ringkasan

Implementasi (`mqtt_link.h/.cpp`, `task_cmd.h/.cpp`, `bess-sim/tools/cloud_probe.py`,
modifikasi `main.cpp`) ditulis persis sesuai kode VERBATIM di brief, dengan dua deviasi
minor yang murni diperlukan untuk kompilasi (dicatat di bawah). Build ESP32-C6 SUCCESS,
native test 19/19 PASSED. Seluruh 6 butir checklist Step 4 diuji **langsung di bench
nyata** (gateway COM3, gw `58E6C5218C78`, lawan simulator `bess-sim` di COM10, broker
dev `mqtt-dev.bepbatt.id:1883`) dan **LULUS SEMUA** — bukti verbatim di bawah.

## Deviasi dari brief (murni kebutuhan kompilasi, bukan perubahan logika)

1. `main.cpp` menambah `#include "secrets.h"` — dipakai untuk `WIFI_SSID` di blok
   telemetri (brief mencantumkan pemakaian `si.ssid = WIFI_SSID;` tapi tidak
   mencantumkan include-nya; tanpa ini simbol tidak terdefinisi di scope `main.cpp`).
2. `main.cpp` menambah `Serial.printf("[boot] gw=%s\n", gw);` sesudah `wifiGw(gw)` di
   `setup()` — sesuai saran brief ("boleh permanen, berguna"), dipakai untuk membaca
   MAC/gw id dari log boot (`58E6C5218C78`) yang dipakai di seluruh pengujian di bawah.

Tidak ada perubahan lain dari kode verbatim brief.

## Build

```
$ pio run -e esp32c6
...
RAM:   [==        ]  16.1% (used 52824 bytes from 327680 bytes)
Flash: [========  ]  84.3% (used 1104533 bytes from 1310720 bytes)
========================= [SUCCESS] Took 15.76 seconds =========================
```

```
$ pio test -e native
...
=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.761
native         test_native_decode   PASSED    00:00:01.121
native         test_native_frame    PASSED    00:00:01.141
native         test_native_payload  PASSED    00:00:01.930
================= 19 test cases: 19 succeeded in 00:00:05.952 =================
```

## Flash ke bench (COM3)

```
Wrote 1128848 bytes (689055 compressed) at 0x00010000 in 4.0 seconds (effective 2263.2 kbit/s)...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
========================= [SUCCESS] Took 8.60 seconds =========================
```

Log boot lengkap (reset manual via toggle DTR/RTS setelah flash, untuk menangkap baris
`gw=`):

```
ESP-ROM:esp32c6-20220919
...
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
E (411) esp-tls: couldn't get hostname for :mqtt-dev.bepbatt.id: getaddrinfo() returns 202, addrinfo=0x0
E (412) transport_base: Failed to open a new connection: 32769
E (415) mqtt_client: Error transport connect
[mqtt] disconnected
E (419) wifi:sta is connecting, return error
[   147][E][STA.cpp:417] connect(): STA connect failed! 0x3007: ESP_ERR_WIFI_CONN
[wifi] OK rssi=-54 ip=192.168.18.52
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

**Catatan (bukan bug):** percobaan konek MQTT pertama gagal karena `mqttInit()` dipanggil
di `setup()` sebelum asosiasi WiFi selesai (DNS belum bisa di-resolve). esp-mqtt melakukan
retry internal secara otomatis — pada monitor sesi berikutnya `[mqtt] connected` muncul
dan seluruh pengujian di bawah berjalan normal tanpa intervensi manual.

Setup bench:
- Simulator: `uv run bess-sim run --port COM10 --soc 60` (proses background, log ke file).
- Probe: `uv run --with paho-mqtt python -u tools/cloud_probe.py --gw 58E6C5218C78 --user guest --passwd <mqtt-pass> <cmd>`
  dijalankan dari `bess-sim/`, output di-redirect ke file, di-poll dengan loop
  `until grep -q ... ; do sleep 1; done`, lalu proses (`uv`+turunannya) di-`taskkill /T /F`
  setelah ack tertangkap (karena `loop_forever()` tidak pernah keluar sendiri).
  Ditambahkan `PYTHONUNBUFFERED=1` + `python -u` — tanpa ini, saat stdout diarahkan ke
  file (bukan TTY), Python melakukan full-buffering sehingga baris `on_msg` tidak muncul
  di file sampai proses dibunuh (ditemukan & diperbaiki saat butir 1).
- Monitor serial gateway: `pio device monitor --port COM3 --baud 115200`, log ke file
  terpisah, dipakai sebagai bukti pendukung frekuensi tinggi (setiap 5 dtk) di samping
  telemetri MQTT (setiap 60 dtk).

## Butir 1 — telemetri `device_type=bess` + `status=online` masuk via watch

```
== device/58E6C5218C78/status ==
online

== device/58E6C5218C78/telemetry ==
seq=7 type=bess p=0kW soc=60% running=False comm_lost=False
```

**LULUS.** `status` retained `online` diterima (dipublish saat `MQTT_EVENT_CONNECTED`
dengan `retain=1`), dan telemetri pertama (`seq=7`, `device_type=bess` — terlihat di
field `type` yang diambil dari `doc["data"]["device_type"]`) masuk sesuai siklus
`TELEMETRY_PERIOD_MS=60000`.

## Butir 2 — `enable` → ack `accepted` + simulator PRECHARGE→RUN + `running=true`

Ack (`cloud_probe.py enable`):
```
perintah terkirim: {'id': '878f4bb6', 'ts': 1786299826, 'cmd': 'enable', 'args': {}, 'api_schema_version': 1}

== device/58E6C5218C78/command/ack ==
{
 "id": "878f4bb6",
 "cmd": "enable",
 "result": "accepted",
 "detail": "",
 "applied": {},
 "ts": 1786299831
}
```

Transisi simulator (verbatim, `sim.log`):
```
[  539.7s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[  541.7s] PRECHARGE p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[  543.7s] RELAY     p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
[  545.7s] RUN       p_ac= +2.50 kW soc= 60.0% vdc= 826.2 V
[  547.7s] RUN       p_ac= +2.50 kW soc= 60.0% vdc= 826.2 V
```

Serial gateway (verbatim) — `status_raw` berubah `0x8B00`→`0x834F` (bit Run set) tepat
sesudah `[cmd] enable -> accepted`:
```
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x834F
[cmd] enable -> accepted 
[wifi] OK rssi=-80 ip=192.168.18.52
[bess] OK p=2.5kW soc=60.0% vdc=826.2V status=0x834F
```

Telemetri berikutnya (`seq=8`):
```
== device/58E6C5218C78/telemetry ==
seq=8 type=bess p=2.5kW soc=60% running=True comm_lost=False
```

**LULUS.** Ack `accepted`, simulator berpindah STOP→PRECHARGE→RELAY→RUN, gateway
membaca `status_raw` bit Run (bukti `waitStatusBit(6, true, ...)` sukses sebelum ack
dikirim), dan telemetri berikutnya melaporkan `running=True`.

## Butir 3 — `set_power --watt 5000` → ack `applied.power_pct=10` + `active_power_kw≈5.0` + SOC turun

Ack:
```
perintah terkirim: {'id': '86bc9cfe', 'ts': 1786299863, 'cmd': 'set_power', 'args': {'power_w': 5000.0}, 'api_schema_version': 1}

== device/58E6C5218C78/command/ack ==
{
 "id": "86bc9cfe",
 "cmd": "set_power",
 "result": "accepted",
 "detail": "",
 "applied": {
  "power_pct": 10,
  "power_w": 5000
 },
 "ts": 1786299864
}
```

Serial gateway:
```
[cmd] set_power -> accepted 
[wifi] OK rssi=-85 ip=192.168.18.52
[bess] OK p=5.0kW soc=60.0% vdc=825.8V status=0x834F
[wifi] OK rssi=-42 ip=192.168.18.52
[bess] OK p=5.0kW soc=59.9% vdc=825.7V status=0x834F
```

Telemetri berikutnya (`seq=9`):
```
== device/58E6C5218C78/telemetry ==
seq=9 type=bess p=5kW soc=59.9% running=True comm_lost=False
```

**LULUS.** `applied.power_pct=10` (5000 W / rated 50 kW × 100 = 10%, dikonfirmasi
readback register `REG_P_SET`), `active_power_kw` naik dari 2.5→5.0 sesuai perubahan
setpoint, dan `soc_percent` turun dari `60%`→`59.9%` antar-telemetri (bukti fisika
simulator benar-benar menguras SOC berdasar daya yang di-*apply*, bukan sekadar
mem-verifikasi command diterima).

## Butir 4 — `set_power --watt 99999` → ack `rejected bad_value`

```
perintah terkirim: {'id': '937ccef2', 'ts': 1786299903, 'cmd': 'set_power', 'args': {'power_w': 99999.0}, 'api_schema_version': 1}

== device/58E6C5218C78/command/ack ==
{
 "id": "937ccef2",
 "cmd": "set_power",
 "result": "rejected",
 "detail": "bad_value",
 "applied": {},
 "ts": 1786299904
}
```

Serial gateway: `[cmd] set_power -> rejected bad_value`.

**LULUS.** 99999 W / rated 50 kW × 100 = ~200% > batas `120%` di `doSetPower()` → ditolak
sebelum menyentuh Modbus sama sekali (tidak ada perubahan `active_power_kw` di telemetri
berikutnya).

## Butir 5 — `disable` → ack `accepted`, `running=false`, daya kembali 0

Ack:
```
perintah terkirim: {'id': '1eccd827', 'ts': 1786299923, 'cmd': 'disable', 'args': {}, 'api_schema_version': 1}

== device/58E6C5218C78/command/ack ==
{
 "id": "1eccd827",
 "cmd": "disable",
 "result": "accepted",
 "detail": "",
 "applied": {},
 "ts": 1786299925
}
```

Serial gateway — `status_raw` kembali `0x834F`→`0x8B00` (bit Shutdown, sama persis
dengan baseline sebelum `enable` di butir 2):
```
[cmd] disable -> accepted 
[wifi] OK rssi=-53 ip=192.168.18.52
[bess] OK p=0.0kW soc=59.9% vdc=826.4V status=0x8B00
```

Simulator kembali ke `STOP`:
```
[  639.9s] STOP      p_ac= +0.00 kW soc= 59.9% vdc= 826.4 V
```

Telemetri berikutnya (`seq=10`):
```
== device/58E6C5218C78/telemetry ==
seq=10 type=bess p=0kW soc=59.9% running=False comm_lost=False
```

**LULUS.**

## Butir 6 — stop simulator → `comm_lost=true`; `enable` → `rejected comm_lost`; restart → pulih

Simulator dihentikan paksa (`taskkill /T /F` pada proses `uv run bess-sim`, sesuai
deviasi yang diizinkan brief — bukan cabut kabel fisik).

Serial gateway — `COMM_LOST` muncul setelah 3 siklus poll gagal (`COMM_LOST_AFTER=3`,
`POLL_PERIOD_MS=1500`):
```
[bess] OK p=0.0kW soc=59.9% vdc=826.4V status=0x8B00
[bess] COMM_LOST p=0.0kW soc=59.9% vdc=826.4V status=0x8B00
```

Telemetri berikutnya (`seq=11`):
```
== device/58E6C5218C78/telemetry ==
seq=11 type=bess p=0kW soc=59.9% running=False comm_lost=True
```

`enable` dikirim saat `comm_lost=true`:
```
perintah terkirim: {'id': '0a3d8049', 'ts': 1786300025, 'cmd': 'enable', 'args': {}, 'api_schema_version': 1}

== device/58E6C5218C78/command/ack ==
{
 "id": "0a3d8049",
 "cmd": "enable",
 "result": "rejected",
 "detail": "comm_lost",
 "applied": {},
 "ts": 1786300026
}
```

Serial gateway sesuai: `[cmd] enable -> rejected comm_lost`.

Simulator dijalankan ulang: `uv run bess-sim run --port COM10 --soc 60`:
```
bess-sim AKTIF di COM10 node 1 (SIMULATOR — bukan device asli)
[    2.0s] STOP      p_ac= +0.00 kW soc= 60.0% vdc= 826.6 V
```

Serial gateway pulih:
```
[bess] COMM_LOST p=0.0kW soc=59.9% vdc=826.4V status=0x8B00
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

Telemetri berikutnya (`seq=12`) mengonfirmasi `comm_lost` kembali `false`:
```
== device/58E6C5218C78/telemetry ==
seq=12 type=bess p=0kW soc=60% running=False comm_lost=False
```

**LULUS SEMUA sub-butir.**

## Log lengkap watch (`cloud_probe.py watch`, verbatim, seluruh sesi butir 1–6)

```
== device/58E6C5218C78/status ==
online

== device/58E6C5218C78/telemetry ==
seq=7 type=bess p=0kW soc=60% running=False comm_lost=False

== device/58E6C5218C78/command/ack ==
{
 "id": "878f4bb6",
 "cmd": "enable",
 "result": "accepted",
 "detail": "",
 "applied": {},
 "ts": 1786299831
}

== device/58E6C5218C78/telemetry ==
seq=8 type=bess p=2.5kW soc=60% running=True comm_lost=False

== device/58E6C5218C78/command/ack ==
{
 "id": "86bc9cfe",
 "cmd": "set_power",
 "result": "accepted",
 "detail": "",
 "applied": {
  "power_pct": 10,
  "power_w": 5000
 },
 "ts": 1786299864
}

== device/58E6C5218C78/telemetry ==
seq=9 type=bess p=5kW soc=59.9% running=True comm_lost=False

== device/58E6C5218C78/command/ack ==
{
 "id": "937ccef2",
 "cmd": "set_power",
 "result": "rejected",
 "detail": "bad_value",
 "applied": {},
 "ts": 1786299904
}

== device/58E6C5218C78/command/ack ==
{
 "id": "1eccd827",
 "cmd": "disable",
 "result": "accepted",
 "detail": "",
 "applied": {},
 "ts": 1786299925
}

== device/58E6C5218C78/telemetry ==
seq=10 type=bess p=0kW soc=59.9% running=False comm_lost=False

== device/58E6C5218C78/telemetry ==
seq=11 type=bess p=0kW soc=59.9% running=False comm_lost=True

== device/58E6C5218C78/command/ack ==
{
 "id": "0a3d8049",
 "cmd": "enable",
 "result": "rejected",
 "detail": "comm_lost",
 "applied": {},
 "ts": 1786300026
}

== device/58E6C5218C78/telemetry ==
seq=12 type=bess p=0kW soc=60% running=False comm_lost=False
```

Catatan: baris `command/ack` "disable → accepted" (`id=1eccd827`) muncul tanpa didahului
"perintah terkirim" di log ini karena baris itu ditangkap oleh proses `watch` yang terus
berjalan (subscriber terpisah dari proses `disable` yang mengirim command) — bukti bahwa
ack di-broadcast ke semua subscriber topic `command/ack`, bukan hanya balik ke pengirim.

## File yang dibuat/dimodifikasi

- `firmware/src/mqtt_link.h` (baru)
- `firmware/src/mqtt_link.cpp` (baru)
- `firmware/src/task_cmd.h` (baru)
- `firmware/src/task_cmd.cpp` (baru)
- `bess-sim/tools/cloud_probe.py` (baru)
- `firmware/src/main.cpp` (dimodifikasi: include `payload.h`/`mqtt_link.h`/`task_cmd.h`/
  `secrets.h`, panggil `taskCmdStart(); mqttInit(gw);` di akhir `setup()` + print
  `[boot] gw=...`, blok kirim telemetri 60 dtk di `loop()`)

## Concerns

- **Flash 84.3% terpakai** (1.104.533/1.310.720 byte) — naik dari 73.4% di Task 14
  akibat esp-mqtt + TLS stack bawaan ESP-IDF. Masih ada ruang, tapi makin sempit untuk
  fitur berikutnya (mis. TLS 8883 produksi yang disebut di roadmap `docs/00`).
- **Percobaan konek MQTT pertama di boot selalu gagal** (DNS belum siap saat
  `mqttInit()` dipanggil, karena WiFi belum tentu sudah dapat IP) — esp-mqtt retry
  otomatis dan seluruh pengujian berjalan normal setelahnya, tapi ini menunda koneksi
  ~beberapa detik tiap boot dan menghasilkan log error yang terlihat mengkhawatirkan
  padahal tidak fatal. Di luar scope Task 15 untuk diperbaiki (tidak ada instruksi di
  brief untuk menunda `mqttInit()` sampai WiFi connect), dicatat sebagai temuan untuk
  task berikutnya.
- **Buffering stdout Python** ketika di-redirect ke file adalah jebakan pengujian (bukan
  bug firmware) — butuh `PYTHONUNBUFFERED=1` + `python -u` supaya `cloud_probe.py`
  menulis output secara real-time saat dijalankan sebagai proses background. Dicatat di
  sini supaya sesi berikutnya tidak mengulang kebingungan yang sama.
- `bess-sim` di-restart dengan `--soc 60` (sama seperti awal) untuk butir 6 "pulih" —
  ini me-reset SOC ke 60% (bukan melanjutkan dari 59.9%), sesuai dengan sifat simulator
  (state in-memory, hilang saat proses baru). Bukan masalah untuk tujuan pengujian
  (recovery comm_lost), tapi dicatat supaya tidak disalahartikan sebagai SOC "naik
  sendiri".
- MQTT publish `status=online`/LWT `offline`, `keepalive=300s`, dan
  `network.timeout_ms=60000` **belum diuji** di Task 15 ini (baterai/converter beban
  penuh yang memicu kondisi TX tercekik seperti dicatat di `CLAUDE.md` bagian FIX 26 Juli
  untuk gateway-v2 tidak direproduksi di bench BESS ini — bench hanya RS485 simulator,
  bukan converter fisik). Di luar scope Task 15.
