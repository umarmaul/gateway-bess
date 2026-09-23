# bess-sim

Simulator BESS **BSL AC series** (protokol Modbus RTU V2.1.0) yang meniru kabinet
**ESS-Grid C109** (50 kW / 108,86 kWh, pack LFP 806,4 V nominal) di kabel RS485.
Berjalan sebagai proses Python terpisah di laptop, tersambung ke gateway lewat
dongle USB-RS485 — **bukan** kode yang jalan di dalam firmware.

Dipakai untuk mengembangkan & menguji `../firmware/` sebelum unit BESS fisik
tersedia. Lihat `../README.md` untuk peta bench lengkap, dan §"Keputusan
fidelity" di bawah untuk apa yang **sengaja** tidak 100% identik dengan device
asli (dan kenapa itu tetap aman untuk kompatibilitas firmware).

## Instalasi

Butuh **[uv](https://docs.astral.sh/uv/)** (mengelola venv + Python 3.12 sendiri,
tidak perlu instal Python manual) dan sebuah dongle USB-RS485.

```bash
cd bess-sim
uv sync            # sekali saja — bikin .venv + install dependency dari pyproject.toml
```

## Perintah

### `selftest` — uji cepat tanpa hardware

```bash
uv run bess-sim selftest
```

Menjalankan simulator lewat transport in-memory (tanpa port serial): kirim FC5
"on", tunggu sampai state `RUN`, kirim FC6 setpoint 20%, lalu cek daya aktif
(±10 kW) dan tegangan DC (700–910 V) masuk akal. Keluaran:

```
selftest: state=RUN p_ac=10.0 kW vdc=804.8 V soc=50.0%
selftest: LULUS
```

### `run` — layani port serial nyata (dipakai di bench)

```bash
uv run bess-sim run --port COM10 --soc 60
```

Opsi:

| Flag | Default | Arti |
|---|---|---|
| `--port` | *(wajib)* | Port serial dongle USB-RS485 (mis. `COM10`, `/dev/ttyUSB0`) |
| `--node` | `1` | Node ID Modbus (spec: 1–15) |
| `--soc` | `50.0` | SOC awal dalam persen |
| `--scenario` | — | Path file YAML skenario (lihat di bawah); `initial.soc` di file menimpa `--soc` |
| `--strict-timing` | mati | Buang query yang datang <100 ms setelah balasan sebelumnya (persis device asli) — lihat §"Keputusan fidelity" |

Baris log tiap ±2 dtk (`[  12.3s] RUN       p_ac= +5.00 kW soc= 59.9% vdc= 826.2 V`)
menunjukkan state machine, daya aktif, SOC, dan tegangan DC saat itu — murni untuk
observasi manusia di terminal, **bukan** bagian dari frame Modbus.

`Ctrl+C` untuk berhenti.

### `tools/master_probe.py` — probe manual dari laptop

Master Modbus kecil untuk latihan/debug tanpa perlu gateway ESP32 (baca register
apa saja, on/off, set persen daya langsung ke bus):

```bash
uv run python tools/master_probe.py --port COM10 status
uv run python tools/master_probe.py --port COM10 on
uv run python tools/master_probe.py --port COM10 setp --pct 20
uv run python tools/master_probe.py --port COM10 read --start 1050 --count 10
uv run python tools/master_probe.py --port COM10 off
```

### `tools/cloud_probe.py` — sisi "cloud" (kirim command MQTT, lihat ack)

Bukan bagian dari `bess-sim` itu sendiri (memakai `paho-mqtt`, dependency
opsional), tapi hidup di `bess-sim/tools/` karena dipakai bersamaan saat bench
e2e. Dijalankan dengan `uv run --with paho-mqtt python tools/cloud_probe.py`.
Kontrak command/ack lengkap ada di `../firmware/README.md`.

## Format skenario YAML

Skenario menjadwalkan injeksi alarm dan perubahan kondisi relatif terhadap detik
sejak simulator mulai (`t=0`). Field:

```yaml
initial: {soc: 55}              # SOC awal (%) — menimpa --soc
events:
  - {at: 45, action: alarm, name: bms_comm_failure}       # set bit alarm (value default 1)
  - {at: 60, action: alarm, name: grid_undervoltage, trip: true}  # + paksa ke state FAULT
  - {at: 90, action: clear_alarm, name: bms_comm_failure}  # bersihkan bit
  - {at: 120, action: set_soc, value: 40}                  # paksa SOC ke nilai tertentu
```

`name` merujuk ke nama bit di `bess_sim/alarms.py` (identik dengan nama yang
dipakai firmware di `alarms_decoded`/`status_decoded` — lihat
`../firmware/README.md`). Dua contoh siap pakai: `scenarios/bms_comm_fail.yaml`,
`scenarios/grid_undervoltage.yaml`.

```bash
uv run bess-sim run --port COM10 --scenario scenarios/grid_undervoltage.yaml
```

## Register yang disimulasikan

Register map **lengkap** sesuai PDF protokol V2.1.0 (`bess_sim/registers.py`,
`RegisterMap`) — bukan hanya subset yang dipakai firmware. Yang aktif dibaca/ditulis
gateway:

| Range | Isi | Akses | Catatan |
|---|---|---|---|
| `1000–1007` | Info produk/versi | RO | Nilai tetap |
| `1050–1108` | Telemetri analog (tegangan/arus grid, daya, DC, suhu, energi kumulatif) | RO | Sebagian signed sesuai kolom *Value Type* PDF; scaling ÷10 atau ÷100 tergantung field |
| `2050–2056` | 7 word alarm, bit-mapped | RO | Nama tiap bit di `alarms.py`, identik dengan firmware |
| `2057` | Status word, bit-mapped | RO | Tahapan precharge/soft-start/relay/run/fault/standby/shutdown/EPO |
| `3050` | Setpoint daya aktif, 0,1% dari rated | RW | Range `-1200..1200`; `+`=ekspor/discharge, `-`=charge (D6) |
| `3062` | Laju perubahan daya aktif (%/dtk) | RW | Membatasi ramp — bukan lompat instan |
| `3146` | Rated power, 0,1 kW | RW | Default 500 → 50 kW |
| `3184` | Battery SOC, 0,1% | RW | Lihat §"SOC di 3184" |
| `5050` | On/Off (coil, FC5) | RW | `0xFF00`=on, `0x0000`=off |
| `5051` | Standby (coil, FC5) | RW | |
| `1500–1505`, `3051–3326` lain | Tanggal/jam, parameter proteksi/komunikasi | RW/RO | Ada di register map untuk kelengkapan spec, tidak dibaca firmware gateway saat ini |

Function code diimplementasikan: **FC3/FC4** (baca block), **FC5** (coil),
**FC6** (tulis satu word), **FC16** (tulis block, atomik — validasi semua
alamat+range dulu sebelum menulis apa pun). Error frame: **01** fungsi tak
dikenal, **02** alamat/ID di luar range atau read-only, **03** format/CRC/range
nilai salah, **06** device busy. Node lain dari yang dikonfigurasi (`--node`)
**diam total** (sesuai spec bus RS485 multi-drop — bukan menjawab dengan error).

## Keputusan fidelity

Simulator dibangun untuk **tidak bisa dibedakan dari device asli** di level kabel
— device asli tidak punya register "aku simulator" untuk dibaca gateway, jadi
simulator pun tidak boleh menambahkannya.

1. **SOC di reg 3184, bukan di blok telemetri.** Spec protokol BSL tidak
   menempatkan SOC di blok telemetri `1050–1108` — itu murni parameter
   perangkat penyimpan (`3050…3185`, RW menurut tabel PDF). Simulator
   memantulkan SOC hasil integrasi fisika (`physics.soc`) ke register itu tiap
   tick, sehingga gateway (dan siapa pun) yang membaca `3184` mendapat nilai
   real-time, persis seolah itu telemetri. Ini konsisten dengan D7 di spec desain.
2. **Busy (exception 06) saat transisi state.** Selama state machine berada di
   `PRECHARGE`/`SOFTSTART`/`RELAY`/`STOPPING` (~1 dtk per tahap, lihat
   `state_machine.py`), tulisan ke register kontrol bisa ditolak dengan
   exception `06` sesekali — meniru device fisik yang tidak menerima perintah
   baru di tengah sekuens precharge/relay. **Pengecualian: FC5 OFF
   (`5050=0x0000`) selalu diterima** — perintah stop di tengah sekuens start
   langsung membawa state ke `STOPPING`; menolak stop dengan "busy" tidak
   pernah jadi perilaku yang aman untuk ditiru. Firmware gateway sudah menghormati
   ini (retry dengan jeda saat exception 6, lihat `../firmware/README.md`
   §arsitektur task, fungsi `doOnOff`/`doSetPower`).
3. **`--strict-timing`.** Spec protokol mewajibkan jeda antar-frame ≥100 ms
   (`MB_FRAME_GAP_MS` di firmware). Secara default simulator hanya mencatat
   warning bila master melanggar jeda ini (supaya test/dev cepat tidak
   terganggu); dengan `--strict-timing` simulator **membuang** (tidak menjawab)
   query yang datang terlalu cepat — persis perilaku device asli yang
   digambarkan spec §3.10 (arbitrase bus). Pakai flag ini saat menguji
   ketahanan firmware terhadap bus yang benar-benar disiplin timing-nya.
4. **Total charge/discharge (`1105–1108`) reset tiap start proses**, bukan
   persisten — sesuai catatan protokol bahwa akumulator ini adalah nilai
   sesi/lifetime device, dan simulator tidak punya penyimpanan persisten
   antar-restart (state selalu in-memory).
5. **Fault latch + reset lewat OFF (asumsi — PDF tidak mengatur reset fault).**
   Alarm dengan `trip` membawa state ke `FAULT` (bit 7 status, daya langsung 0
   karena relay AC terbuka — tidak meluruh mengikuti laju `3062`). `FAULT`
   bertahan sampai master mengirim FC5 OFF **dan** tidak ada penyebab trip yang
   masih aktif; alarm skenario harus di-`clear_alarm` dulu. Alarm proteksi
   otomatis (over-discharge/over-charge) bersifat latch dan ikut dibersihkan
   oleh reset itu. Sesudahnya state `STOP` dan `enable` bisa dipakai lagi.
6. **Arus fase tak pernah negatif.** Register `1053–1055`/`1093–1095` UINT16
   menurut PDF; noise di sekitar 0 A di-clamp ke 0 (dulu bisa terbungkus jadi
   `0xFFFF` = 6553,5 A di telemetri gateway).
7. **Pemotong frame 30 ms.** Frame dianggap selesai setelah bus sunyi 30 ms (bukan
   3,5 karakter ≈ 4 ms): dongle USB-serial dan timer Windows menyerahkan byte
   berkelompok sehingga satu query bisa tiba dalam dua potongan. Spec menjamin
   jeda ≥100 ms antar frame, jadi ambang ini tetap aman. Setelah membalas,
   simulator membuang input sisa (dongle yang meng-echo TX-nya sendiri).
8. **Identitas jujur, kabel setia**: simulator menandai dirinya sendiri di log
   terminal (`bess-sim AKTIF di COM10 node 1 (SIMULATOR — bukan device asli)`)
   dan CLI — tapi tidak ada apa pun di frame Modbus yang membocorkan hal itu ke
   gateway atau ke cloud.

## Test

```bash
uv run pytest -v       # 64 test: CRC (vektor persis PDF), register map,
                        # fisika, state machine, alarm/skenario, transport, CLI
uv run bess-sim selftest
```
