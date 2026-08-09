# gateway-bess

Firmware gateway ESP32-C6 + simulator BESS (BSL AC series / ESS-Grid C109) untuk
PT Bima Eco Power. Menggantikan pasangan **DCON + gateway lama** dengan pasangan
**BESS (konverter penyimpan energi) + gateway baru**, memakai protokol
**Modbus RTU** (bukan RS485 custom DCON).

> Perangkat BESS fisik **belum tersedia** saat repo ini dibangun. `bess-sim/` meniru
> BESS di kabel RS485 (register map, timing, error frame) supaya firmware gateway
> bisa ditulis dan diuji end-to-end tanpa menunggu hardware datang.

## Peta repo

```
gateway-bess/
├── bess-sim/     Simulator BESS (Python, uv) — slave Modbus RTU di port serial
│                 laptop. Lihat bess-sim/README.md.
├── firmware/     Firmware gateway (PlatformIO, ESP32-C6, FreeRTOS) — master
│                 Modbus RTU + uplink MQTT ke cloud. Lihat firmware/README.md.
├── docs/superpowers/
│   ├── specs/2026-08-09-gateway-bess-design.md   Spec desain (disetujui)
│   └── plans/2026-08-09-gateway-bess.md          Rencana implementasi per-task
├── .superpowers/sdd/2026-08-09-gateway-bess/     Brief + laporan tiap task (log kerja)
├── BSL  AC series Energy Storage Converter_Modbus RTU protocol V2.1.0.pdf
└── ESS-Grid C109 User Manual.pdf                 Sumber kebenaran protokol/hardware BESS
```

## Bench

```
┌─────────────── LAPTOP ───────────────┐              ┌────── GATEWAY ESP32-C6 ──────┐
│ bess-sim (Python, dongle USB-RS485)   │   RS485      │ firmware gateway-bess         │
│ = BESS virtual, slave Modbus node 1   │◄──RJ45──────►│ = Modbus RTU MASTER 9600 8N1  │
│ COM10 (bench acuan)                   │ (jalur       │   di UART ex-DCON             │
│                                        │  ex-DCON)    │   RX=GPIO21 TX=GPIO20         │
└────────────────────────────────────────┘              │   RE/DE=GPIO22                │
        COM3 (USB-CDC, bench acuan) ── flash/monitor ──►│                               │
                                                          │ WiFi ──► MQTT dev broker      │
                                                          │ mqtt-dev.bepbatt.id:1883      │
                                                          └───────────────────────────────┘
```

Nomor port (`COM3`/`COM10`) adalah port yang terpasang di bench pengujian; di
laptop lain bisa berbeda — cek Device Manager / `[System.IO.Ports.SerialPort]::GetPortNames()`.

Peran dibalik dari sistem DCON lama: BESS adalah **slave murni** (hanya menjawab
saat ditanya), gateway adalah **master yang mem-poll** — tidak ada masalah
arbitrase bus seperti di DCON.

## Cara menjalankan (bench)

Tiga perintah, tiga terminal:

```bash
# 1. Simulator BESS (laptop, dongle USB-RS485 di jalur ex-DCON)
cd bess-sim && uv run bess-sim run --port COM10 --soc 60

# 2. Flash + monitor gateway (perlu firmware/src/secrets.h — lihat firmware/README.md)
cd firmware && pio run -e esp32c6 -t upload --upload-port COM3 && pio device monitor -p COM3 -b 115200

# 3. (opsional) Probe manual dari laptop — baca register / on-off / set daya langsung ke bus
cd bess-sim && uv run python tools/master_probe.py --port COM10 status
```

Log gateway yang sehat: `[wifi] OK ...` lalu `[bess] OK p=...kW soc=...% vdc=...V
status=0x....` tiap ±5 dtk, dan telemetri MQTT `device/<gw>/telemetry` tiap 60 dtk
(lihat `firmware/README.md` untuk kontrak lengkap dan `bess-sim/tools/cloud_probe.py`
untuk mengirim command/melihat ack dari sisi "cloud").

## Spec & rencana

- Desain (disetujui, per bagian A/B/C): [`docs/superpowers/specs/2026-08-09-gateway-bess-design.md`](docs/superpowers/specs/2026-08-09-gateway-bess-design.md)
- Rencana implementasi 16 task + self-review: [`docs/superpowers/plans/2026-08-09-gateway-bess.md`](docs/superpowers/plans/2026-08-09-gateway-bess.md)
- Brief & laporan verifikasi tiap task (termasuk bukti bench hardware): [`.superpowers/sdd/2026-08-09-gateway-bess/`](.superpowers/sdd/2026-08-09-gateway-bess/)

## BESS asli menggantikan simulator TANPA perubahan firmware

Ini kriteria sukses utama proyek. `bess-sim` menjawab persis seperti device BESS
asli di level kabel RS485: register map, scaling, kode error Modbus (01/02/03/06),
dan jeda antar-frame ≥100 ms — **tidak ada register atau frame penanda "ini
simulator"** yang bisa dibaca gateway. Saat unit BESS asli datang:

1. Cabut dongle USB-RS485 laptop dari jalur RJ45 ex-DCON.
2. Sambungkan BESS asli (RS485, node Modbus default 1, 9600 8N1) ke jalur yang sama.
3. Nyalakan. **Firmware gateway tidak diubah/di-flash ulang sama sekali.**

Konsekuensinya: fidelitas simulator (lihat `bess-sim/README.md` §"Keputusan
fidelity") harus benar-benar mengikuti PDF protokol resmi, bukan sekadar cukup
untuk lolos test — setiap penyimpangan dari device asli adalah bug simulator,
bukan sesuatu yang "nanti disesuaikan di firmware".

## Non-scope fase ini

Provisioning/captive portal, OTA (ESP maupun BESS), dashboard web lokal,
auto-control berbasis SOC, fault-history ring buffer, TLS produksi (bench pakai
broker dev `1883` tanpa TLS). Lihat keputusan D8 di spec desain.

## Batasan

Repo ini **tidak menyentuh** `gateway-v2/` (proyek gateway DCON yang berjalan
paralel) — dilarang eksplisit oleh keputusan desain, lihat spec §"Batasan referensi".
