# Desain: Sistem Gateway BESS (pengganti DCON) + Simulator BESS

Tanggal: 9 Agustus 2026 · Status: **disetujui user** (per bagian A/B/C)
Lokasi kerja: `D:\PT Bima Eco Power\embedded-system\gateway-bess\`

## 1. Latar belakang & tujuan

PT Bima Eco Power akan mengganti konverter DCON (STM32G474, protokol RS485 custom)
dengan perangkat **BESS**: konverter penyimpan energi **BSL AC series** dalam kabinet
**ESS-Grid C109** (50 kW / 108,86 kWh, LFP 806,4 V). Perangkat fisiknya **belum tersedia**,
sehingga fase ini membangun:

1. **`bess-sim/`** — simulator BESS berbasis Python yang berjalan di laptop dan tampil di
   kabel RS485 **persis seperti device asli** (register map penuh, timing, error frame).
2. **`firmware/`** — firmware baru ESP32-C6 untuk gateway yang berfungsi setara
   gateway DCON (ekspor daya / set power / enable / disable / telemetri cloud) tetapi
   berbicara **Modbus RTU** ke BESS.

Kriteria sukses utama: saat device BESS asli datang, USB-RS485 laptop dicabut, device
asli disambungkan ke jalur yang sama, dan **firmware gateway tidak berubah sama sekali**.

### Batasan referensi (permintaan user)

- **DILARANG** memakai `gateway-v2/` sebagai referensi kode.
- Boleh: `DCON Controller and Bootloader/DCON-Controller/` (perilaku konverter),
  `BEPESP32_WiFi_Extension/` (fakta hardware/pin), `bench-sim/` (pola simulator),
  `docs/` (kontrak cloud), dan kedua PDF BESS di folder ini.

## 2. Sumber kebenaran perangkat BESS

- `BSL  AC series Energy Storage Converter_Modbus RTU protocol V2.1.0.pdf` — protokol resmi:
  - Fisik: RS485, **9600 bps, 8N1**, slave Modbus RTU, node default **1** (range 1–15),
    jeda antar-frame **≥100 ms**, CRC16 Modbus urutan **Low-High**.
  - Function code: FC3/FC4 (block read), FC5 (coil), FC6 (word write), FC16 (block write).
  - Error frame: code **01** fungsi tak dikenal, **02** ID di luar range, **03** format/CRC,
    **06** device busy.
  - Register: `1000–1007` versi; `1050–1108` telemetri analog; `2050–2056` alarm (7 word
    bit-mapped); `2057` status word; `3050–3185` parameter kontrol; `3301–3326` parameter
    komunikasi; `1500–1505` tanggal/jam; `5050` On/Off (0xFF00=on, 0x0000=off); `5051` standby.
- `ESS-Grid C109 User Manual.pdf` — sistem: 50 kW / 108,86 kWh, pack LFP **806,4 V nominal**
  (operasi 705,6–907,2 V), 135 Ah, AC 400/230 V 3 fasa 50 Hz.

## 3. Arsitektur (Bagian A — disetujui)

```
┌─────────── LAPTOP ───────────┐            ┌────── GATEWAY ESP32-C6 ──────┐
│ bess-sim (Python, COM10)     │  RS485     │ firmware gateway-bess (baru) │
│ = BESS virtual, slave Modbus │◄──RJ45────►│ = Modbus RTU MASTER 9600 8N1 │
│   node 1, 9600 8N1           │ (jalur     │   di UART ex-DCON            │
│   register map BSL lengkap   │  ex-DCON)  │                              │
└──────────────────────────────┘            │ WiFi ──► MQTT dev broker     │
        COM3 (USB) ─── flash/monitor ──────►│ mqtt-dev.bepbatt.id:1883     │
                                            └──────────────────────────────┘
```

- Bench terverifikasi: **COM3** = gateway ESP32-C6 (USB-CDC), **COM10** = dongle
  USB-RS485 CH340 → jalur RJ45 bekas DCON.
- Peran dibalik dari sistem DCON: BESS adalah **slave murni** — hanya menjawab jika
  ditanya. Gateway menjadi **master yang mem-poll** → tidak ada masalah arbitrase bus.
- Perintah tulis diselipkan lewat antrian di sela siklus poll, tetap menghormati jeda 100 ms.
- Aliran data: fisika sim → register → poll gateway → state → envelope V11 (blok `bess`)
  → MQTT → cloud; balik: cloud command → validasi → tulis register (5050/3050) →
  simulator bereaksi → terlihat di telemetri + ack.

## 4. Simulator `bess-sim/` (Bagian B — disetujui)

Runtime: **uv + Python 3.12 + pyserial**. Monorepo `gateway-bess/` (satu git repo untuk
sim + firmware + docs).

| Modul | Tugas |
|---|---|
| `modbus/slave.py` | Mesin slave Modbus RTU: parse frame, CRC16 L-H, FC3/4/5/6/16, error frame 01/02/03/06 persis spec, **diam total** untuk node lain |
| `registers.py` | Register map **LENGKAP** dengan default & range persis PDF; tulisan di luar range ditolak |
| `physics.py` | Pack LFP 806,4 V / 135 Ah / 108,86 kWh; SOC terintegrasi dari daya aktif; V_dc = f(SOC) di 705,6–907,2 V; ramp daya mengikuti reg 3062 (active rate of change); efisiensi ~97%; suhu power tube naik dengan beban; grid 3 fasa 400/230 V 50 Hz + noise kecil |
| `state_machine.py` | Stop → (5050=0xFF00) → DC precharge → AC soft start → relay close → **Run** (~3 dtk, bit 2057 berubah bertahap) → ramp ke setpoint; off → ramp down → relay open → Stop; Standby (5051), EPO, Fault latch |
| `scenario.py` + YAML | Injeksi alarm 2050–2056 terjadwal/manual, SOC awal, perubahan beban |
| `cli.py` | `uv run bess-sim run --port COM10 --node 1 --soc 50`; `selftest` (transport in-memory); tampilan status live |

Keputusan fidelity:

1. **Konvensi tanda daya** (dari manual C109 §commissioning): **positif = discharge/ekspor,
   negatif = charge**. Reg `3050` satuan 0,1% dari rated power (`3146`, default 500 = 50 kW).
   `3050 = 100` → ekspor 5 kW.
2. **SOC**: tidak ada di blok telemetri BSL → simulator memantulkan SOC aktual ke reg
   **`3184`** (Battery SOC, 0–1000, readable FC3); plus bit 5 status 2057
   (charging/discharging) dan V_dc yang berkorelasi SOC. Gateway membaca SOC dari 3184.
3. **Timing**: delay jawab 10–40 ms; `--strict-timing` = ABAIKAN query <100 ms setelah
   frame sebelumnya (persis device); default hanya warning.
4. **Identitas jujur, kabel setia**: simulator menandai dirinya di log/CLI, tetapi di kabel
   tidak bisa dibedakan dari device asli (tidak ada register `sim_mode` — device asli tak punya).
5. Total charge/discharge (`1105–1108`) reset tiap start (sesuai catatan protokol);
   error 06 (busy) sesekali saat transisi state.

## 5. Firmware gateway (Bagian C — disetujui)

PlatformIO + pioarduino, ESP32-C6, FreeRTOS. Pin (fakta hardware dari repo tim
`BEPESP32_WiFi_Extension/src/Config.h`): jalur DCON = **RX GPIO21, TX GPIO20, RE/DE
GPIO22**; LED DCON GPIO18, LED WiFi GPIO14, tombol BOOT GPIO9. Debug via USB-CDC (COM3).

| Modul | Tugas |
|---|---|
| `modbus_master.*` | Master Modbus RTU mandiri (~200 baris, tanpa library eksternal): FC3/FC5/FC6/FC16, CRC16, arah RE/DE, timeout 500 ms + 2 retry, jeda ≥100 ms ditegakkan di satu tempat |
| `bess.*` | Register → struct `BessData` (scaling /10, /100 persis PDF); decode 7 word alarm + status word ke nama bit; `comm_lost` setelah 3 gagal beruntun |
| `task_bess` | Poll: 1050–1108 → 2050–2057 → 3050/3146/3184 tiap ~1,5 dtk; antrian perintah tulis diprioritaskan |
| `task_mqtt` | esp-mqtt: keepalive 300 s, network timeout 60 s, telemetri via `enqueue`, command/ack via `publish` (pelajaran terdokumentasi diterapkan sejak awal) |
| `wifi_mgr` | Station sederhana: kredensial `secrets.h` (gitignored + `secrets.example.h`), country code `"ID"`, reconnect exponential backoff |
| `state.*` | `AppState` tunggal + mutex |

**Non-scope fase ini** (eksplisit): captive portal/provisioning, OTA (ESP maupun BESS),
dashboard web lokal, auto-control SOC, fault history ring buffer. Menyusul setelah inti
terbukti end-to-end.

## 6. Kontrak data cloud

### 6.1 Telemetri

Broker/topic/irama sama dengan sistem DCON: `mqtt-dev.bepbatt.id:1883`,
`device/<gw>/telemetry` tiap 60 dtk, `device/<gw>/status` online/offline (retained, LWT),
`device/<gw>/command` + `/command/ack`. Envelope V11 sama (`gw`, `ts`, `seq`,
`api_schema_version`, `data`).

Pembeda: `data.device_type = "bess"`, dan blok **`bess`** menggantikan `dcon`+`bms`:

- Grid: `grid_voltage_ab/bc/ca_v`, `grid_current_a/b/c_a`, `grid_frequency_hz`
- Daya: `active_power_kw`, `reactive_power_kvar`, `apparent_power_kva`, `power_factor`
- DC: `dc_voltage_v`, `dc_current_a`, `dc_power_kw`
- Termal/efisiensi: `power_tube_temp_c`, `ambient_temp_c`, `efficiency_percent`
- Baterai: `soc_percent` (dari reg 3184), `total_charge_kwh`, `total_discharge_kwh`
- Kontrol: `rated_power_kw`, `power_setpoint_percent`
- Status: `running`, `charging`, `standby`, `fault`, `grid_connected`, `epo`,
  `status_decoded` (semua bit 2057 bernama), `alarms_decoded` (semua bit 2050–2056 bernama)
- Kesehatan link: `comm_lost` (nilai terakhir dipertahankan + flag; **bukan angka nol palsu**)

Perkiraan ukuran ±2–3 KB (vs 14 KB DCON).

### 6.2 Command (bentuk ack mengikuti docs/17)

| Command | Aksi | Ack |
|---|---|---|
| `{"cmd":"enable"}` | FC6 `5050=0xFF00` → tunggu bit Run di 2057 ≤10 dtk | `ok` / `error: bess_no_ack`, `bess_fault`, `comm_lost` |
| `{"cmd":"disable"}` | `5050=0x0000` → tunggu Stop | idem |
| `{"cmd":"set_power","args":{"power_w":5000}}` | validasi: tolak bila \|power_w\| > 120% × rated (3146), sesuai range register −1200~1200 → tulis 3050 (0,1%; +ekspor/−charge) → baca balik | ack berisi persen yang benar-benar tertulis; `error: out_of_range` |

**Keputusan eksplisit**: tanpa `dcon_code` — konsep itu milik firmware DCON; BESS asli
tidak memilikinya dan simulator harus persis device asli. Pengaman pengganti: `enable`
ditolak saat `fault` aktif atau `comm_lost`.

## 7. Error handling

- Modbus timeout/exception → retry (2×) → `comm_lost` di telemetri; perintah saat
  `comm_lost` → ack error, tidak digantung.
- Exception frame BESS diteruskan apa adanya ke log + alasan ack.
- WiFi putus → poll jalan terus; MQTT outbox menyusul saat tersambung.
- Tulisan register selalu **dibaca balik** sebelum ack `ok`.

## 8. Testing

1. **pytest simulator** (tanpa hardware): CRC dengan test vector langsung dari contoh PDF
   (`01 03 04 1A 00 03 25 3C` → `01 03 06 08 98 08 98 08 98 84 04`; contoh FC6/FC5 juga),
   penolakan out-of-range, fisika (SOC turun benar pada ekspor konstan; V_dc mengikuti SOC),
   state machine on/off/standby/fault, error frame 01/02/03/06.
2. **`tools/master_probe.py`** — master Modbus Python kecil untuk latihan ke simulator via
   transport in-memory (juga bisa dipakai ke COM nyata untuk debug kabel).
3. **End-to-end di bench**: flash firmware ke COM3 → poll jalan (log USB-CDC) → telemetri
   `device_type:"bess"` muncul di broker → `enable` via MQTT → simulator precharge→Run →
   `set_power` 5 kW → SOC turun perlahan di telemetri → `disable` → Stop. Checklist
   verifikasi ditulis di implementation plan.

Pelajaran ukur dari proyek DCON tetap berlaku: satu run tidak pernah cukup untuk
klaim keandalan; gunakan beberapa run bila menilai stabilitas link.

## 9. Keputusan terkunci (ringkasan)

| # | Keputusan |
|---|---|
| D1 | Monorepo `gateway-bess/` = 1 git repo: `bess-sim/` + `firmware/` + `docs/` |
| D2 | Simulator = program Python terpisah di laptop (preseden owner), register map **penuh** |
| D3 | Gateway = Modbus RTU master 9600 8N1 di UART ex-DCON (GPIO21/20/22) |
| D4 | Telemetri: broker/topic/envelope sama, `device_type:"bess"`, blok `bess` |
| D5 | Command: `enable` / `disable` / `set_power` (watt); tanpa `dcon_code` |
| D6 | Tanda daya: positif = ekspor/discharge, negatif = charge |
| D7 | SOC dibaca dari reg 3184; simulator memantulkan SOC aktual ke sana |
| D8 | Fase ini tanpa provisioning/OTA/dashboard/auto-control |
