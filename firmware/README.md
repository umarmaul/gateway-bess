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
JSON command/ack, framing Modbus, parser+validator manifest/chunk OTA):

```bash
pio test -e native
```

Log boot yang sehat:

```
[boot] reset=POWERON boot_count=12 heap=371764 min_heap=366776
[boot] gateway-bess bess-0.3.0
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
| `task_cmd` (`task_cmd.cpp`) | 2 | Antrian command dari MQTT (`taskCmdSubmit`); eksekusi `enable`/`disable`/`set_output` (alias `set_power`) via Modbus, tunggu bukti nyata (bit status atau readback), kirim ack. Menolak semua command dengan `ota_in_progress` selama job OTA aktif |
| `task_ota` (`task_ota.cpp`) | 2 | OTA gateway via MQTT (sub-proyek G) — lihat §OTA di bawah. **Tidak** didaftarkan ke task watchdog (`esp_ota_write` bisa lambat karena erase flash, dan menunggu `mqtt_tx` tidak boleh berujung reboot) |
| `mqtt_link` (`mqtt_link.cpp`) | — (event esp-mqtt) | Start client saat WiFi pertama naik, LWT `device/<gw>/status`, subscribe `device/<gw>/command` + `device/<gw>/ota/{manifest,chunk}`, panggil `otaOnMqttConnected()` tiap `MQTT_EVENT_CONNECTED` |
| `mqtt_tx` (`mqtt_link.cpp`) | 1 | **Satu-satunya** pemanggil `esp_mqtt_client_enqueue` (QoS1). `loop()` menitip telemetri terbaru (latest wins); `task_cmd`/`task_ota` menitip pesan (`mqttPublish`) ke antrean generik 8 slot `{topic,retain,json}` (ack/ota_ack/ota_status) yang ditahan sampai MQTT terhubung (basi >10 menit dibuang; `retain` per pesan — status OTA retained, ack tidak). Sengaja **tidak** diawasi watchdog: dialah yang menanggung penantian lock esp-mqtt saat link tercekik |
| `wifi_mgr` | — (dipanggil dari `loop()`) | Station WiFi, `country code "ID"`, reconnect exponential backoff (tidak blocking boot); kredensial disuntikkan `prov.cpp` |
| `prov` (`prov.cpp`) | — (dipanggil dari `loop()`) | Provisioning (sub-proyek E) — lihat §Provisioning di bawah: gateway_code, SoftAP fallback + captive DNS, mDNS, tombol factory reset, reboot terjadwal |
| `web` (`web.cpp`) | — (dipanggil dari `loop()`) | `WebServer` sinkron port 80: `/wifi` + `/api/wifi/*` (sub-proyek E); kerangka router untuk F/H |
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
| `device/<gw>/ota/manifest` | cloud → gateway | Manifest OTA (sub-proyek G) — lihat §OTA |
| `device/<gw>/ota/chunk` | cloud → gateway | Potongan firmware base64 (sub-proyek G) |
| `device/<gw>/ota/ack` | gateway → cloud | Ack manifest/chunk (sub-proyek G) |
| `device/<gw>/ota/status` | gateway → cloud | **Retained** — status job OTA (sub-proyek G) |

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
    "firmware_version": "bess-0.3.0",
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
    },
    "ota": {"state": "idle", "id": "", "running_partition": "app0", "pending_verify": false}
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
| `ota_in_progress` | job OTA (sub-proyek G) sedang aktif — command biasa ditolak sampai job selesai/gagal, lihat §OTA |

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

## OTA gateway (MQTT, Ed25519)

Sub-proyek G. Update firmware gateway sendiri (bukan BESS) lewat MQTT dengan
tanda tangan Ed25519 -- **satu-satunya** jalur OTA (tidak ada endpoint HTTP
`/update`; jalur tanpa tanda tangan berarti melewati verifikasi). Paritas
kontrak dengan `BEPESP32_WiFi_Extension` (branch `gateway-mqtt`) KECUALI
`image_type`/`hardware`, yang sengaja dibedakan.

| Topic | QoS | Retained | Arah |
|---|---|---|---|
| `device/<gw>/ota/manifest` | 1 | tidak | cloud → gateway |
| `device/<gw>/ota/chunk` | 1 | tidak | cloud → gateway |
| `device/<gw>/ota/ack` | 1 | tidak | gateway → cloud |
| `device/<gw>/ota/status` | 1 | **ya** | gateway → cloud |

### Manifest

```json
{"id":"ota-20260923-001","image_type":"gateway","hardware":"bep-gateway-bess-v1",
 "version":"bess-0.3.1","encoding":"base64","image_size":1245184,
 "sha256":"<64 hex char>","signature":"<base64, 64 byte Ed25519 detached>",
 "chunk_count":1081}
```

Validasi (`lib/bess_core/ota_logic.cpp::otaParseManifest`, native-tested):
`id` non-kosong ≤128 char; `image_type=="gateway"` **dan**
`hardware=="bep-gateway-bess-v1"` (mismatch → `hardware_mismatch`, dicek
SEBELUM field lain -- lihat "Kenapa hardware berbeda" di bawah);
`encoding=="base64"`; `image_size` 1..`0x1E0000` (ukuran satu slot app OTA);
`sha256` persis 64 hex char; `signature` base64 yang men-decode ke **persis**
64 byte; `chunk_count == ceil(image_size / 1152)` **persis**. Field lain
invalid → `invalid_manifest`.

**Tanda tangan**: Ed25519 detached atas **32 byte digest SHA-256 biner**
(bukan manifest JSON, bukan image mentah) — `sha256` di-decode ke biner,
itulah pesan yang diverifikasi. Diverifikasi dengan libsodium
(`crypto_sign_verify_detached`, `src/task_ota.cpp`) **SEBELUM** `esp_ota_begin`
dipanggil sama sekali -- gagal verifikasi = tidak ada satu byte pun ditulis
ke flash. Public key 32 byte dari `OTA_ED25519_PUBKEY_B64` (`config.h`,
default kunci tim, bisa ditimpa di `secrets.h` untuk bench — lihat
`secrets.example.h`).

**Kenapa `hardware` berbeda dari kontrak tim** (`"bep-gateway-bess-v1"` vs
`"bep-gateway-v1"` milik gateway DCON): papan fisik ESP32-C6 **identik**
antara gateway DCON dan gateway BESS. Tanpa pembeda ini, image firmware
DCON-gateway yang ditandatangani sah oleh kunci yang sama bisa ter-flash ke
gateway BESS (dan sebaliknya) hanya karena tanda tangannya valid — mismatch
sengaja ditolak lebih dulu dari validasi field lain.

### Chunk

```json
{"id":"ota-20260923-001","index":0,"data":"<base64, maks 1536 char>"}
```

Base64 ≤1536 char → biner ≤1152 byte (muat dalam `MQTT_READ_BUFFER`=2048
bersama envelope JSON). **Wajib berurutan dari index 0**, satu chunk
in-flight (server kirim berikutnya HANYA setelah ack `accepted`). Duplikat
index terakhir (retry aman QoS1) → `accepted`/`duplicate` (idempotent, tidak
ditulis ulang). Index lebih lama dari itu → `stale_chunk`; melompat maju /
job sudah penuh → `unexpected_chunk`; `id` tidak cocok job aktif (atau tidak
ada job aktif) → `wrong_job`. Setiap chunk **baru** langsung ditulis ke
partisi (`esp_ota_write`) dan di-hash inkremental (mbedTLS `mbedtls_sha256_*`)
— **tidak ada image penuh di RAM**.

**Deviasi dari kontrak tim** (tidak didaftarkan eksplisit di dokumentasi
mereka): amplop chunk yang tidak bisa diurai sama sekali (JSON rusak, atau
field `id`/`index`/`data` hilang) dipetakan ke `unexpected_chunk` juga — job
mana yang dimaksud tidak diketahui pasti dalam kasus ini, jadi ini nilai
`detail` paling umum yang tersedia, bukan kegagalan job.

Base64 rusak / mendekode ke ukuran salah, atau `esp_ota_write` gagal → **job
langsung gagal** (`invalid_chunk_data` / `write_failed`), bukan sekadar
menolak chunk itu — harus dimulai ulang dari manifest baru.

### Ack & status

```json
// ack manifest
{"id":"ota-20260923-001","kind":"manifest","result":"accepted","detail":"","ts":1785480000}
// ack chunk
{"id":"ota-20260923-001","kind":"chunk","index":0,"result":"accepted","detail":"",
 "received_bytes":1152,"next_index":1,"ts":1785480000}
// status (retained)
{"id":"ota-20260923-001","state":"downloading","image_type":"gateway",
 "hardware":"bep-gateway-bess-v1","gateway_id":"58E6C5218C78",
 "gateway_firmware_version":"bess-0.3.1","running_gateway_firmware_version":"bess-0.3.0",
 "running_partition":"app0","sha256":"...","received_bytes":1152,"detail":"","ts":1785480000}
```

`kind` = `"manifest"` (tanpa `index`/`received_bytes`/`next_index`, field itu
hanya relevan untuk kemajuan per-chunk) atau `"chunk"`. `detail` ringkasan
alasan: `""` (sukses biasa), `invalid_manifest`, `hardware_mismatch`,
`signature_invalid`, `ota_begin_failed`, `busy` (manifest kedua saat job
aktif — **tanpa publish status**), `duplicate`, `unexpected_chunk`,
`stale_chunk`, `wrong_job`, `invalid_chunk_data`, `write_failed`,
`size_mismatch`, `sha256_mismatch`, `timeout` (120 dtk tanpa chunk baru),
`boot_partition_mismatch` (lihat rollback).

`state` (status, retained): `idle → downloading → verifying → restarting →
installed|failed`. Urutan finalisasi setelah chunk terakhir: cek
`received_bytes==image_size` → hitung SHA-256 final vs manifest → `verifying`
→ `esp_ota_end` + `esp_ota_set_boot_partition` → simpan `{id,version,sha256,
partition}` ke NVS `mqtt_ota` → `restarting` → **reboot 1 detik kemudian**.

### Rollback otomatis

Bootloader sudah `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (bawaan pioarduino
53.03.13, terverifikasi di `sdkconfig` framework). Firmware meng-override
`verifyRollbackLater()` (symbol weak `esp32-hal-misc.c`, **wajib** `extern
"C"` di sisi kita) supaya image baru boot dalam status `PENDING_VERIFY` dan
**baru** ditandai valid (`esp_ota_mark_app_valid_cancel_rollback`) saat MQTT
tersambung **pertama kali** pasca-boot. Crash/reboot sebelum itu → bootloader
otomatis kembali ke image lama di boot berikutnya. Kalau **15 menit** tak
kunjung tersambung sama sekali sementara masih `PENDING_VERIFY`, firmware
menandai dirinya invalid dan reboot paksa ke image lama
(`esp_ota_mark_app_invalid_rollback_and_reboot`).

Setelah reconnect (boot mana pun, bukan cuma pasca-OTA), status persisted
dipublikasikan **sekali** dari NVS `mqtt_ota`: `installed` kalau partisi yang
sedang berjalan == yang disimpan sebelum reboot, `failed`/
`boot_partition_mismatch` kalau tidak (rollback terjadi). Ditandai `reported`
di NVS supaya tidak terulang tiap reconnect dalam boot yang sama (status
tetap **retained** di broker untuk subscriber baru).

### `data.ota` (telemetri)

```json
"ota": {"state": "downloading", "id": "ota-20260923-001", "running_partition": "app0", "pending_verify": false}
```

`state` sama dengan enum status MQTT. `pending_verify` = image yang sedang
berjalan masih menunggu mark-valid (rollback masih aktif).

### Bench (tanpa hardware asli)

```bash
# sekali: buat keypair dev (JANGAN pakai kunci tim untuk bench sembarangan)
cd bess-sim
uv run --with cryptography python tools/ota_publish.py --gen-key dev_key.pem
# tempel public key yang dicetak ke firmware/src/secrets.h:
#   #define OTA_ED25519_PUBKEY_B64 "<hasil --gen-key>"

# kirim firmware.bin ke gateway
uv run --with paho-mqtt --with cryptography python tools/ota_publish.py \
    --gw 58E6C5218C78 --host mqtt-dev.bepbatt.id --user USER --passwd PASS \
    --key dev_key.pem --version bess-0.3.1 --firmware .pio/build/esp32c6/firmware.bin
```

Belum diuji di hardware fisik (lihat `CHANGELOG.md` untuk checklist bench
yang masih wajib sebelum dipakai di lapangan).

## Provisioning (WiFi + captive portal)

Sub-proyek E (spec `docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md`
§E). Paritas pola dengan `BEPESP32_WiFi_Extension` (branch `gateway-mqtt`),
**kecuali** satu penyimpangan sadar: password AP fallback **wajib** 8-63
karakter (tim membolehkan kosong) -- lihat catatan keselamatan di
`lib/bess_core/prov_logic.h`.

### Alur operator (bench/lapangan baru, belum pernah di-provisioning)

1. Nyalakan gateway. Tanpa kredensial router valid di NVS, **SoftAP fallback
   selalu menyala**: SSID `BEP-CONNECT-<gateway_code>` (6 karakter A-Z0-9,
   dibangkitkan sekali per perangkat, dipertahankan lintas reboot di NVS
   `device_id`), password default `AP_PASS` (`bepgateway`, timpa di
   `secrets.h` untuk lapangan -- lihat `secrets.example.h`).
2. Sambungkan HP/laptop ke SSID itu. **Captive portal** (`DNSServer`)
   biasanya membuka halaman provisioning otomatis; kalau tidak, buka
   `http://192.168.4.1/wifi` manual.
3. Halaman `/wifi` menampilkan status (SSID router, STA IP, AP SSID+status,
   mDNS, **`gateway_code`** -- ditampilkan APA ADANYA karena halaman ini
   hanya bisa diakses lewat AP yang sudah WPA2) + 3 form:
   - **Simpan WiFi router** (`ssid`, `pass`, `mdns`, opsional IP statis) →
     `POST /api/wifi/save`.
   - **Ganti AP fallback** (`ap_ssid` opsional, `ap_pass` wajib 8-63 char) →
     `POST /api/wifi/ap`.
   - **Lupakan WiFi router** (hapus `ssid`+`pass` router, AP config tetap) →
     `POST /api/wifi/forget`.
4. Semua form **wajib** field `gateway_code` (field `code`) -- tanpa itu,
   endpoint menolak `403 {"ok":false,"error":"forbidden"}` walau sudah di
   dalam AP WPA2 (LAN/AP "trusted" saja tidak cukup untuk gateway yang
   mengendalikan konverter 50 kW). Pembanding memakai waktu-konstan
   (`provCodeEquals`, `lib/bess_core/prov_logic.cpp`).
5. Sukses → `200 {"ok":true,"restarting":true}`, gateway reboot ~1 detik
   kemudian (jeda ini supaya respons HTTP sempat terkirim -- **bukan** reboot
   di dalam handler). Validasi gagal → `400 {"ok":false,"error":"<alasan>"}`
   (`invalid_ssid`/`invalid_pass`/`invalid_mdns`/`invalid_ip`/`nvs_error`).
6. Setelah reboot, gateway mencoba konek ke router yang baru disimpan.
   SoftAP **tetap menyala** sampai STA benar-benar tersambung, lalu tetap
   menyala **5 menit tambahan** (`PROV_AP_AFTER_CONNECT_MS`) sebelum mati --
   jalur pemulihan kalau kredensial yang baru saja disimpan ternyata salah.

### Di mana `gateway_code` terlihat

- Halaman `/wifi` (satu-satunya tempat -- tidak dikirim lewat MQTT/telemetri).
- Log serial USB-CDC saat boot (`[prov] gateway_code=... ap_ssid=... ...`).
- NVS `device_id` key `gateway_code` (kalau perlu dibaca lewat BEP App/JTAG).

### Factory reset

Tombol **BOOT** (GPIO9, bawaan devkit, aktif LOW) ditahan **8 detik** saat
gateway sedang berjalan → menghapus NVS `wifi_cfg` (kredensial router + AP
override) dan `app_cfg` (nanti dipakai sub-proyek F), lalu reboot. **TIDAK**
menyentuh `device_id` (`gateway_code` tetap sama), `mqtt_ota`, `boot`,
`crash` -- identitas perangkat dan histori OTA/crash bertahan.

### mDNS

Default `bep-bess-gateway.local` (**beda** dari `bep-dev-gateway` milik
gateway DCON tim -- dua gateway di satu LAN tidak bentrok nama), diiklankan
`_http._tcp:80`, retry `MDNS.begin()` tiap 5 detik selama STA connected dan
belum berhasil.

### Penyimpanan NVS

| Namespace | Key | Isi |
|---|---|---|
| `device_id` | `gateway_code` | 6 char A-Z0-9, sekali dibangkitkan, dipertahankan lintas factory reset |
| `wifi_cfg` | `ssid`,`pass`,`mdns`,`sta_static`,`sta_ip`,`sta_gw`,`sta_mask`,`sta_dns1`,`sta_dns2`,`ap_ssid`,`ap_pass` | Kredensial router + IP statis opsional + override AP fallback -- dihapus oleh factory reset |

### `data.network` (telemetri, field baru)

```json
"network": {"ssid": "...", "ip": "192.168.18.52", "rssi_dbm": -54, "ap_active": false, "mdns": "bep-bess-gateway"}
```

`ap_active` = SoftAP fallback sedang menyala. `mdns` = hostname yang sedang
diiklankan (tanpa `.local`).

### Kerangka web server (untuk F/H)

`src/web.cpp` mendaftarkan `/wifi` + `/api/wifi/*` di atas `WebServer`
sinkron biasa (`handleClient()` dari `loop()`, **harus cepat** -- `loop()`
diawasi task watchdog 120 dtk, jangan tunggu lock esp-mqtt/Modbus lama).
`webServer()` (`web.h`) mengekspos instance `WebServer&` supaya modul
berikutnya (F: `/api/auto/config`; H: `/`, `/api/data`, `/api/command`,
`/api/acks`, `/api/firmware_versions`) mendaftarkan rute tambahan tanpa
membuat server sendiri.

## Non-scope fase ini

Dashboard web lokal (H), auto-control SOC + jadwal (F),
fault-history ring buffer, TLS 8883 produksi (bench pakai broker dev `1883` polos).
