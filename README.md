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
├── docs/
│   ├── Dokumentasi_Gateway_BESS_Fase1.docx       Dokumentasi lengkap fase 1
│   └── superpowers/
│       ├── specs/    Spec desain: fase 1 (2026-08-09) + fondasi paritas (2026-08-13)
│       ├── plans/    Rencana implementasi per-task untuk kedua fase
│       └── sdd-archive/   Ledger + laporan verifikasi tiap task (bukti bench)
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

## Riwayat versi

[`CHANGELOG.md`](CHANGELOG.md) — perubahan per versi firmware, termasuk perubahan
kontrak yang harus diketahui tim cloud dan daftar verifikasi bench yang masih wajib.

## Spec & rencana

- Fase 1 — desain (disetujui, per bagian A/B/C): [`docs/superpowers/specs/2026-08-09-gateway-bess-design.md`](docs/superpowers/specs/2026-08-09-gateway-bess-design.md)
- Fase 1 — rencana implementasi 16 task: [`docs/superpowers/plans/2026-08-09-gateway-bess.md`](docs/superpowers/plans/2026-08-09-gateway-bess.md)
- Fase 2 (fondasi paritas vs `BEPESP32_WiFi_Extension` branch `gateway-mqtt`): [`specs/2026-08-13-fondasi-paritas-design.md`](docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md) + [`plans/2026-08-13-fondasi-paritas.md`](docs/superpowers/plans/2026-08-13-fondasi-paritas.md)
- Ledger + laporan verifikasi tiap task (termasuk bukti bench hardware): [`docs/superpowers/sdd-archive/`](docs/superpowers/sdd-archive/) (fase 1 di akar, fase 2 di subfolder `2026-08-13-fondasi-paritas/`)

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

## Cakupan & non-scope

Sejak `bess-0.3.0`, sub-proyek **E** (provisioning SoftAP + captive portal),
**F** (jadwal + auto-control SOC), **G** (OTA gateway via MQTT, Ed25519 +
rollback), dan **H** (dashboard + API lokal) sudah diimplementasikan — lihat
[`CHANGELOG.md`](CHANGELOG.md) dan spec
[`2026-09-23-subproyek-EFGH-design.md`](docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md).
Semuanya **belum diuji di hardware**; checklist bench per sub-proyek ada di CHANGELOG.

Masih non-scope: ring buffer `fault_history`, TLS 8883 produksi (bench pakai broker
dev `1883` tanpa TLS), dan OTA ke perangkat BESS itu sendiri (produk pihak ketiga,
tak ada jalur flash lewat gateway — beda dari OTA proxy DCON di gateway-v2).

## Batasan

Repo ini **tidak menyentuh** `gateway-v2/` (proyek gateway DCON yang berjalan
paralel) — dilarang eksplisit oleh keputusan desain, lihat spec §"Batasan referensi".
