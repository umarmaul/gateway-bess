# firmware (gateway-bess)

Firmware ESP32-C6 (PlatformIO + pioarduino + FreeRTOS) untuk gateway BESS: **master
Modbus RTU** ke BESS (BSL AC series) di jalur RS485 bekas DCON, dan **uplink MQTT**
ke cloud dengan kontrak yang sama (broker/topic/envelope) dengan sistem DCON lama —
hanya blok data yang berbeda (`device_type: "bess"`).

Lihat `../README.md` untuk peta bench dan `../bess-sim/README.md` untuk lawan
bicara Modbus-nya (simulator, sebelum unit BESS fisik tersedia).

## Prasyarat

- [PlatformIO Core](https://platformio.org/) (`pio` di PATH — via `pip install platformio`
  atau ekstensi VS Code).
- ESP32-C6 devkit tersambung USB (di bench: **COM3**, USB-CDC bawaan chip, dipakai
  untuk flash **dan** log — tidak perlu USB-to-serial terpisah).
- Kredensial WiFi bench + user/password broker MQTT dev (`mqtt-dev.bepbatt.id:1883`)
  — minta ke pemilik proyek bila belum ada.

## Salin secrets

```bash
cp src/secrets.example.h src/secrets.h
```

Isi `src/secrets.h` (file ini **gitignored** — `.gitignore` berisi `src/secrets.h`,
jangan pernah commit):

```cpp
#define WIFI_SSID     "isi-ssid"
#define WIFI_PASS     "isi-password"
#define MQTT_URI      "mqtt://mqtt-dev.bepbatt.id:1883"
#define MQTT_USER     "isi-user"
#define MQTT_PASSWD   "isi-pass"
```

## Build / upload / monitor

```bash
pio run -e esp32c6                                     # build saja
pio run -e esp32c6 -t upload --upload-port COM3         # flash
pio device monitor -p COM3 -b 115200                    # log serial (Ctrl+C keluar)
```

Native unit test (logika murni, tanpa hardware — CRC16, decode register, parser
JSON command/ack, framing Modbus):

```bash
pio test -e native
```

Log boot yang sehat:

```
[boot] gateway-bess bess-0.1.0
[boot] gw=58E6C5218C78
[wifi] OK rssi=-54 ip=192.168.18.52
[mqtt] connected
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

`gw` = MAC address (12 hex, tanpa pemisah) — dipakai sebagai identitas device di
semua topic MQTT. Percobaan konek MQTT **pertama** setelah boot lazim gagal (DNS
belum siap sebelum WiFi selesai asosiasi) — esp-mqtt retry otomatis, `[mqtt]
connected` menyusul dalam beberapa detik; ini bukan bug. Jika BESS/simulator belum
menyala, `[bess]` akan langsung berkata `COMM_LOST` (bukan macet) — lihat
§"comm_lost" di bawah.

## Arsitektur task (FreeRTOS)

| Task/modul | Prioritas | Tugas |
|---|---|---|
| `loop()` (Arduino) | 1 | Tick WiFi reconnect, LED status, kirim telemetri MQTT tiap `TELEMETRY_PERIOD_MS` (60 dtk) |
| `task_bess` (`task_bess.cpp`) | 3 | Poll Modbus BESS tiap `POLL_PERIOD_MS` (1,5 dtk): telemetri `1050..1108` → alarm `2050..2057` → setpoint `3050` → param `3146..3184`; decode ke `BessData`; tandai `comm_lost` setelah `COMM_LOST_AFTER`=3 siklus gagal beruntun |
| `task_cmd` (`task_cmd.cpp`) | 2 | Antrian command dari MQTT (`taskCmdSubmit`); eksekusi `enable`/`disable`/`set_power` via Modbus, tunggu bukti nyata (bit status atau readback), kirim ack |
| `mqtt_link` (`mqtt_link.cpp`) | — (event esp-mqtt) | Connect + LWT `device/<gw>/status`, subscribe `device/<gw>/command`, publish telemetri (`enqueue`, non-blocking) & ack (`publish`, QoS1) |
| `wifi_mgr` | — (dipanggil dari `loop()`) | Station WiFi, `country code "ID"`, reconnect exponential backoff (tidak blocking boot) |
| `state.h` (`g_state`) | — | `BessData` + `seq` tunggal, dilindungi mutex (`stateLock`/`stateUnlock`) — dibaca `task_bess` (tulis) dan `loop()`/`task_cmd` (baca) |

Arbitrase bus RS485 tunggal: hanya `task_bess` (poll) dan `task_cmd` (tulis
command) yang menyentuh `modbus_port`; keduanya lewat fungsi `mbReadRegs`/`mbWrite5`/
`mbWrite6` yang sama-sama menegakkan jeda ≥100 ms (`MB_FRAME_GAP_MS`) dan
timeout+retry (`MB_TIMEOUT_MS`=500 ms, `MB_RETRIES`=2).

Pin RS485 (fakta hardware `BEPESP32_WiFi_Extension/src/Config.h`, jalur ex-DCON):
RX **GPIO21**, TX **GPIO20**, RE/DE **GPIO22**. LED status: BESS GPIO18, WiFi GPIO14.

## Kontrak MQTT

Broker/topic/envelope **sama persis** dengan sistem DCON — hanya isi `data.bess`
yang baru.

| Topic | Arah | Isi |
|---|---|---|
| `device/<gw>/telemetry` | gateway → cloud | Envelope V11, tiap 60 dtk |
| `device/<gw>/status` | gateway → cloud | `online`/`offline`, **retained**, `offline` juga jadi LWT |
| `device/<gw>/command` | cloud → gateway | `{"cmd": ..., "args": {...}}` |
| `device/<gw>/command/ack` | gateway → cloud (broadcast, semua subscriber) | `{"id","cmd","result","detail","applied","ts"}` |

### Contoh payload telemetri (dipersingkat)

```json
{
  "gw": "58E6C5218C78",
  "ts": 1786299826,
  "seq": 12,
  "api_schema_version": 1,
  "data": {
    "device_type": "bess",
    "api_schema_version": 1,
    "firmware_version": "bess-0.1.0",
    "device_id": "58E6C5218C78",
    "uptime_ms": 723004,
    "time_valid": true,
    "network": {"ssid": "...", "ip": "192.168.18.52", "rssi": -54},
    "bess": {
      "grid_voltage_ab_v": 398.2, "grid_voltage_bc_v": 397.9, "grid_voltage_ca_v": 398.5,
      "grid_current_a_a": 7.2, "grid_current_b_a": 7.1, "grid_current_c_a": 7.3,
      "grid_frequency_hz": 50.01, "power_factor": 0.99,
      "active_power_kw": 5.0, "reactive_power_kvar": 0.1, "apparent_power_kva": 5.01,
      "dc_voltage_v": 825.7, "dc_current_a": 6.06, "dc_power_kw": 5.0,
      "power_tube_temp_c": 38.4, "ambient_temp_c": 29.1, "efficiency_percent": 97.2,
      "soc_percent": 59.9, "rated_power_kw": 50.0, "power_setpoint_percent": 10.0,
      "total_charge_kwh": 0.0, "total_discharge_kwh": 1.02,
      "running": true, "charging": false, "standby": false, "fault": false,
      "grid_connected": true, "epo": false, "comm_lost": false,
      "alarms_decoded": { "positive_bus_overvoltage": 0, "...": 0 },
      "status_decoded": { "running": 1, "dc_relay": 1, "ac_relay": 1, "...": 0 }
    }
  }
}
```

`alarms_decoded` berisi **semua** bit bernama dari register `2050–2056` (lihat
`bess_sim/alarms.py` — nama identik persis antara Python simulator dan C++
firmware), `status_decoded` berisi semua bit bernama dari `2057`. Ukuran payload
±2–3 KB (vs ±14 KB blok `dcon`+`bms` di sistem lama).

**`comm_lost`**: begitu true, `active_power_kw`/`soc_percent`/dll **mempertahankan
nilai terakhir yang diketahui** (bukan dipaksa nol) — flag `comm_lost` itu sendiri
adalah sinyal kebenarannya, konsumen di cloud harus memeriksanya, bukan menyimpulkan
dari angka nol.

### Command

| Command | Payload | Aksi di gateway | Ack sukses |
|---|---|---|---|
| `enable` | `{"cmd":"enable"}` | FC5 `5050=0xFF00`, tunggu bit *Run* (bit 6) di reg 2057 ≤10 dtk | `result:"accepted"` |
| `disable` | `{"cmd":"disable"}` | FC5 `5050=0x0000`, tunggu bit *Shutdown* (bit 11) ≤10 dtk | `result:"accepted"` |
| `set_power` | `{"cmd":"set_power","args":{"power_w":5000}}` | Validasi ≤120% rated (`3146`) → FC6 `3050` (0,1% dari rated; **positif = ekspor/discharge, negatif = charge**) → baca balik untuk konfirmasi | `result:"accepted"`, `applied:{"power_pct":10,"power_w":5000}` |

Tanpa `dcon_code` — konsep itu khusus firmware DCON lama; BESS asli tidak
memilikinya dan simulator wajib meniru device asli apa adanya (lihat D5 di spec
desain). Pengaman pengganti: `enable` ditolak saat `fault` aktif atau `comm_lost`.

### Bentuk ack

```json
{"id":"878f4bb6","cmd":"enable","result":"accepted","detail":"","applied":{},"ts":1786299831}
```

`result` selalu `"accepted"` atau `"rejected"`. Alasan tolak (`detail`):

| `detail` | Kapan |
|---|---|
| `bad_json` | Payload command bukan JSON valid |
| `unsupported_cmd` | `cmd` bukan `enable`/`disable`/`set_power` |
| `bad_value` | `set_power` tanpa `power_w`, atau \|power_w\| > 120% rated power |
| `comm_lost` | Modbus ke BESS sedang putus (≥3 poll gagal beruntun) — command tidak dicoba sama sekali |
| `bess_fault` | `enable` ditolak karena BESS sedang dalam kondisi fault |
| `bess_no_ack` | Tulisan Modbus gagal (timeout/exception non-busy) setelah retry, atau bukti transisi (bit status) tidak muncul dalam 10 dtk |
| `bess_busy` | `set_power` ditolak dengan exception Modbus 06 (device sedang di tengah transisi state) |
| `readback_mismatch` | `set_power` tertulis tapi nilai baca-balik dari register tidak cocok dengan yang ditulis |

`applied` kosong (`{}`) untuk `enable`/`disable`; untuk `set_power` sukses berisi
`power_pct` (persen rated yang benar-benar tertulis) dan `power_w` (setara watt).

## `comm_lost`

Dinaikkan oleh `task_bess` setelah 3 siklus poll Modbus gagal beruntun
(`COMM_LOST_AFTER`, `config.h`). Turun otomatis begitu satu siklus poll sukses lagi
— tidak perlu reboot gateway maupun restart manual apa pun. Command yang masuk
selagi `comm_lost=true` langsung ditolak (`detail:"comm_lost"`) tanpa mencoba
Modbus sama sekali, supaya tidak menggantung menunggu bus yang memang sedang mati.

## Non-scope fase ini

Provisioning/captive portal, OTA, dashboard web lokal, auto-control SOC,
fault-history ring buffer, TLS 8883 produksi (bench pakai broker dev `1883` polos).
