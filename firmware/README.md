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
[boot] reset=POWERON boot_count=12 heap=371764 min_heap=366776
[boot] gateway-bess bess-0.2.0
[boot] gw=58E6C5218C78
[wifi] OK rssi=-54 ip=192.168.18.52
[mqtt] connected
[bess] OK p=0.0kW soc=60.0% vdc=826.6V status=0x8B00
```

`gw` = MAC address (12 hex, tanpa pemisah) — dipakai sebagai identitas device di
semua topic MQTT. Client MQTT baru di-start begitu WiFi pertama kali tersambung
(`mqttTick`), jadi tidak ada lagi percobaan connect pertama yang gagal DNS; setelah
itu esp-mqtt reconnect sendiri. Jika BESS/simulator belum menyala, `[bess]` akan
berkata `COMM_LOST` (bukan macet) setelah **~8–9 dtk** — lihat §"comm_lost" di bawah.

Kalau boot sebelumnya berakhir crash (panic, termasuk task watchdog), baris ini
muncul sekali dan ringkasannya ikut telemetri sebagai `last_crash`:

```
[crash] coredump ditemukan: task=task_cmd pc=0x42001234 mcause=7
```

## Arsitektur task (FreeRTOS)

| Task/modul | Prioritas | Tugas |
|---|---|---|
| `loop()` (Arduino) | 1 | Tick WiFi reconnect, LED status, kirim telemetri MQTT tiap `TELEMETRY_PERIOD_MS` (60 dtk) |
| `task_bess` (`task_bess.cpp`) | 3 | Poll Modbus BESS tiap `POLL_PERIOD_MS` (1,5 dtk): telemetri `1050..1108` → alarm `2050..2057` → setpoint `3050` → param `3146..3184`; decode ke `BessData`; tandai `comm_lost` setelah `COMM_LOST_AFTER`=3 siklus gagal beruntun |
| `task_cmd` (`task_cmd.cpp`) | 2 | Antrian command dari MQTT (`taskCmdSubmit`); eksekusi `enable`/`disable`/`set_output` (alias `set_power`) via Modbus, tunggu bukti nyata (bit status atau readback), kirim ack |
| `mqtt_link` (`mqtt_link.cpp`) | — (event esp-mqtt) | Start client saat WiFi pertama naik, LWT `device/<gw>/status`, subscribe `device/<gw>/command` |
| `mqtt_tx` (`mqtt_link.cpp`) | 1 | **Satu-satunya** pemanggil `esp_mqtt_client_enqueue` (QoS1). `loop()` menitip telemetri terbaru (latest wins), `task_cmd` menitip ack ke antrean 8 slot yang ditahan sampai MQTT terhubung (basi >10 menit dibuang). Sengaja **tidak** diawasi watchdog: dialah yang menanggung penantian lock esp-mqtt saat link tercekik |
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
    "firmware_version": "bess-0.2.0",
    "device_id": "58E6C5218C78",
    "uptime_ms": 723004,
    "time_valid": true,
    "last_reset_reason": "POWERON",
    "boot_count": 12,
    "free_heap_bytes": 301234,
    "min_free_heap_bytes": 287000,
    "last_crash": null,
    "network": {"ssid": "...", "ip": "192.168.18.52", "rssi_dbm": -54},
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
~3–4 KB (terukur ~3,3 KB) (vs ±14 KB blok `dcon`+`bms` di sistem lama).

**`ts`**: detik epoch UTC, atau **`0` kalau jam gateway belum sinkron NTP** — jangan
dibaca sebagai tahun 1970. Berlaku untuk `ts` di envelope maupun di ack, dan
`data.time_valid` adalah cerminan langsung dari `ts != 0`.

**`last_reset_reason` / `boot_count`**: alasan reset terakhir (nama enum ESP-IDF, mis.
`POWERON`, `PANIC`, `BROWNOUT`; `UNKNOWN_<angka>` untuk nilai tak dikenal) dan pencacah
boot monotonik dari NVS. `boot_count` yang naik tanpa sebab yang diketahui = gateway
restart sendiri; itu sinyal, bukan derau.

**`free_heap_bytes` / `min_free_heap_bytes`**: heap bebas saat telemetri dibangun dan
titik terendahnya sejak boot. `min_free_heap_bytes` yang terus turun antar-telemetri
(pada `boot_count` yang sama) = kebocoran — terlihat dari cloud sebelum berakhir crash.

**`last_crash`**: `null`, atau `{"task","pc","mcause","boot_count"}` dari coredump
crash terakhir. Saat boot, coredump di flash diringkas ke NVS lalu dihapus, jadi
nilainya bertahan lintas reboot sampai crash berikutnya menggantikannya; `boot_count`
di dalamnya = boot pertama sesudah crash itu. Untuk crash watchdog ada field tambahan
`wdt_tasks` (mis. `"task_cmd"`) — nama task yang tak memberi makan watchdog, ditangkap
hook ISR watchdog ke RAM RTC; `task`/`pc` coredump pada kasus ini biasanya `IDLE`
(task yang sedang jalan saat interrupt), jadi pakai `wdt_tasks` untuk mencari pelakunya. `pc` (hex) dicocokkan ke kode dengan
`riscv32-esp-elf-addr2line -e .pio/build/esp32c6/firmware.elf <pc>` pada build yang
sama; `mcause` = kode trap RISC-V. Ini yang dulu hilang pada reboot 13 Agustus.

**Watchdog**: `loop()`, `task_bess`, dan `task_cmd` terdaftar di task watchdog
(`WDT_TIMEOUT_S`=120 dtk, panic → reboot). Task yang macet lebih lama dari itu memicu
reboot dengan `last_reset_reason:"TASK_WDT"`, dan nama task yang macet ikut tercatat
di `last_crash.wdt_tasks`. Ketiganya tidak pernah menunggu lock esp-mqtt — lock itu
bisa tertahan >120 dtk saat link WiFi tercekik (tulisan parsial diulang, connect
DNS+TCP+CONNACK) — karena semua kiriman lewat task `mqtt_tx` yang tidak diawasi.
Link lambat = telemetri/ack tertunda, bukan reboot.

**`comm_lost`**: begitu true, `active_power_kw`/`soc_percent`/dll **mempertahankan
nilai terakhir yang diketahui** (bukan dipaksa nol) — flag `comm_lost` itu sendiri
adalah sinyal kebenarannya, konsumen di cloud harus memeriksanya, bukan menyimpulkan
dari angka nol.

### Command

| Command | Payload | Aksi di gateway | Ack sukses |
|---|---|---|---|
| `enable` | `{"cmd":"enable"}` | FC5 `5050=0xFF00`, tunggu bit *Run* (bit 6) di reg 2057 ≤10 dtk | `result:"accepted"` |
| `disable` | `{"cmd":"disable"}` | FC5 `5050=0x0000`, tunggu bit *Shutdown* (bit 11) ≤10 dtk | `result:"accepted"` |
| `set_output` | `{"cmd":"set_output","args":{"power_w":5000}}` | Validasi rated (`3146`) → pangkas ke ±120% → FC6 `3050` (0,1% dari rated; **positif = ekspor/discharge, negatif = charge**) → baca balik untuk konfirmasi | `result:"accepted"` (atau `"clamped"`), `applied:{"power_pct":10,"power_w":5000}` |
| `set_power` | sama dengan `set_output` | Alias lama fase 1, perilaku identik | sama |

Tanpa `dcon_code` — konsep itu khusus firmware DCON lama; BESS asli tidak
memilikinya dan simulator wajib meniru device asli apa adanya (lihat D5 di spec
desain). Pengaman pengganti: `enable` ditolak saat `fault` aktif atau `comm_lost`.
`disable` **tidak** digerbang fault — di simulator, `disable` saat FAULT sekaligus
me-reset fault bila penyebabnya sudah hilang (asumsi simulator, PDF tidak mengatur
reset fault; verifikasi di device asli).

### Bentuk ack

```json
{"id":"878f4bb6","cmd":"enable","result":"accepted","detail":"","applied":{},"ts":1786299831}
```

`result` bernilai `"accepted"`, `"clamped"`, `"rejected"`, atau `"timeout"`.
`"clamped"` berarti perintah dijalankan tetapi nilainya dipangkas ke batas device
(±120% rated) — `applied` selalu berisi nilai yang **benar-benar dipakai**, bukan yang
diminta. `"timeout"` (hanya `enable`/`disable`, `detail:"status_timeout"`) berarti
tulisan Modbus **sudah diterima device**, tetapi bit status bukti transisinya tidak
muncul dalam 10 dtk — hasil akhirnya **tidak diketahui** (device mungkin masih
berpindah state). Cloud jangan menganggapnya "tidak terjadi"; baca `running`/`standby`
di telemetri berikutnya sebelum mengirim ulang.

Alasan tolak (`detail`):

| `detail` | Kapan |
|---|---|
| `bad_json` | Payload command bukan JSON valid |
| `unsupported_cmd` | `cmd` bukan `enable`/`disable`/`set_output`/`set_power` |
| `bad_value` | `set_output`/`set_power` tanpa `power_w`, atau `args.target` selain `1` |
| `comm_lost` | Modbus ke BESS sedang putus (≥3 poll gagal beruntun) — command tidak dicoba sama sekali |
| `bess_fault` | `enable` ditolak karena BESS sedang dalam kondisi fault |
| `bess_no_ack` | Tulisan Modbus gagal (timeout/exception non-busy) setelah retry |
| `rated_unknown` | `set_output`/`set_power` sebelum rated power (`3146`) pernah terbaca — watt tak bisa dikonversi ke persen |
| `status_timeout` | (dengan `result:"timeout"`) `enable`/`disable` tertulis, tapi bit Run/Shutdown tidak muncul dalam 10 dtk |
| `bess_busy` | `set_output`/`set_power` ditolak dengan exception Modbus 06 (device sedang di tengah transisi state) |
| `readback_mismatch` | `set_output`/`set_power` tertulis tapi nilai baca-balik dari register tidak cocok dengan yang ditulis |
| `queue_full` | antrean perintah penuh; perintah tidak dijalankan, silakan kirim ulang |
| `payload_too_large` | payload command > 2048 B (`CMD_JSON_MAX`); `id` di ack kosong karena pesan tak di-parse |

`applied` kosong (`{}`) untuk `enable`/`disable`; untuk `set_output`/`set_power` sukses
berisi `power_pct` (persen rated yang benar-benar tertulis) dan `power_w` (setara watt).

## `comm_lost`

Dinaikkan oleh `task_bess` setelah 3 siklus poll Modbus gagal beruntun
(`COMM_LOST_AFTER`, `config.h`). Turun otomatis begitu satu siklus poll sukses lagi
— tidak perlu reboot gateway maupun restart manual apa pun. Command yang masuk
selagi `comm_lost=true` langsung ditolak (`detail:"comm_lost"`) tanpa mencoba
Modbus sama sekali, supaya tidak menggantung menunggu bus yang memang sedang mati.

**Biaya waktu nyata: ~8–9 dtk saat device benar-benar tidak merespons.** Satu siklus
poll berhenti di blok pertama yang **timeout** (device diam total: 3 percobaan ×
(500 ms + jeda 105 ms) ≈ 1,8 dtk), tetapi **tetap lanjut** ke blok berikutnya bila
device menjawab dengan exception — jadi kode exception tiap blok tetap tercatat di log.
Tiga siklus gagal beruntun (`COMM_LOST_AFTER`=3) + dua jeda antar-siklus 1,5 dtk ⇒
**≈3×1,8 + 2×1,5 ≈ 8–9 dtk**. (Di `bess-0.1.x` sempat ~25 dtk karena keempat blok
selalu dicoba penuh walau device diam; itu juga menahan bus dari `task_cmd` sampai
~7,2 dtk per siklus.) Tim cloud yang menyetel timeout command dari angka ini cukup
memakai **~10 dtk** untuk deteksi putus, ditambah ≤10 dtk tunggu bukti status.

## Non-scope fase ini

Provisioning/captive portal, OTA, dashboard web lokal, auto-control SOC,
fault-history ring buffer, TLS 8883 produksi (bench pakai broker dev `1883` polos).
