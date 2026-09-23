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
| `task_ota` (`task_ota.cpp`) | 2 | OTA gateway via MQTT (sub-proyek G) — lihat §OTA di bawah. **Diawasi** task watchdog (sejak audit 23 Sep 2026): selama job aktif semua command ditolak `ota_in_progress`, jadi task macet tanpa watchdog = kendali BESS hilang sampai power-cycle. Erase partisi di `esp_ota_begin` (puluhan detik) masih jauh di bawah 120 dtk; publish lewat `mqtt_tx`, tak pernah menunggu lock esp-mqtt |
| `task_web` (`web.cpp`) | 1 | **Baru (temuan review 23 Sep 2026)**: `handleClient()` untuk SEMUA rute HTTP (E/F/H — `/wifi`, `/api/wifi/*`, `/api/auto/config`, `/`, `/api/data`, `/api/command`, `/api/acks`, `/api/firmware_versions`). **Tidak** didaftarkan ke task watchdog -- `WebServer::_parseRequest()` (library core) membaca body POST tanpa batas waktu total, klien "slowloris" (1 byte tiap <5 dtk) bisa menahannya lama. Lihat §Kerangka web server |
| `mqtt_link` (`mqtt_link.cpp`) | — (event esp-mqtt) | Start client saat WiFi pertama naik, LWT `device/<gw>/status`, subscribe `device/<gw>/command` + `device/<gw>/ota/{manifest,chunk}`, panggil `otaOnMqttConnected()` tiap `MQTT_EVENT_CONNECTED` |
| `mqtt_tx` (`mqtt_link.cpp`) | 1 | **Satu-satunya** pemanggil `esp_mqtt_client_enqueue` (QoS1). `loop()` menitip telemetri terbaru (latest wins); `task_cmd`/`task_ota` menitip pesan (`mqttPublish`) ke antrean generik 8 slot `{topic,retain,json}` (ack/ota_ack/ota_status) yang ditahan sampai MQTT terhubung (basi >10 menit dibuang; `retain` per pesan — status OTA retained, ack tidak). Sengaja **tidak** diawasi watchdog: dialah yang menanggung penantian lock esp-mqtt saat link tercekik |
| `wifi_mgr` | — (dipanggil dari `loop()`) | Station WiFi, `country code "ID"`, reconnect exponential backoff (tidak blocking boot); kredensial disuntikkan `prov.cpp` |
| `prov` (`prov.cpp`) | — (`provTick()` dari `loop()`; accessor dipanggil dari `task_web` juga) | Provisioning (sub-proyek E) — lihat §Provisioning di bawah: gateway_code, SoftAP fallback + captive DNS, mDNS, tombol factory reset, reboot terjadwal. State yang disentuh `loop()` DAN `task_web` (AP aktif/SSID/pass, mDNS hostname, flag reboot terjadwal) dilindungi mutex sejak `task_web` ada (lihat §Kerangka web server) |
| `web` (`web.cpp`) | — (handler dipanggil dari `task_web`) | `WebServer` sinkron port 80: `/wifi` + `/api/wifi/*` (sub-proyek E); kerangka router untuk F/H |
| `state.h` (`g_state`) | — | `BessData` + `seq` tunggal, dilindungi mutex (`stateLock`/`stateUnlock`) — dibaca `task_bess` (tulis) dan `loop()`/`task_cmd`/`task_web` (baca) |

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

### Kerangka web server (dipakai E/F/H)

`src/web.cpp` mendaftarkan `/wifi` + `/api/wifi/*` (E) dan `/api/auto/config`
(F) di atas `WebServer` sinkron biasa. `webServer()` (`web.h`) mengekspos
instance `WebServer&` supaya modul lain mendaftarkan rute tambahan tanpa
membuat server sendiri -- `src/web_dashboard.cpp` (H) memakainya untuk `/`,
`/api/data`, `/api/command`, `/api/acks`, `/api/firmware_versions` (lihat
§Dashboard & API lokal di bawah).

**Temuan review 23 Sep 2026 (TINGGI) -- slowloris bisa mereboot gateway,
diperbaiki dengan `task_web`.** `WebServer::_parseRequest()` (library core,
`framework-arduinoespressif32/libraries/WebServer/src/Parsing.cpp`) membaca
body POST lewat `readBytesWithTimeout()` SEBELUM handler mana pun dipanggil
(dan sebelum cek apa pun) -- fungsi itu me-reset jatah tunggunya
(`HTTP_MAX_POST_WAIT`, default **5000 ms**) setiap kali satu byte baru tiba.
Klien yang sengaja mengirim body 1 byte tiap <5 dtk ("slowloris") bisa
menahan `handleClient()` menggantung nyaris tanpa batas. Dulu ini dipanggil
dari `loop()` (diawasi task watchdog `WDT_TIMEOUT_S`=120 dtk) -- klien
seperti itu memicu panic `TASK_WDT` → **reboot seluruh gateway**, walau tidak
ada yang salah di jalur BESS/MQTT.

Fix: `handleClient()` sekarang dipanggil dari task terpisah, **`task_web`**
(`webTaskStart()`, prioritas 1), yang **SENGAJA TIDAK didaftarkan ke task
watchdog** -- pola sama dengan `mqtt_tx` (juga dikecualikan karena bisa
menunggu lama di luar kendali kode kita). `loop()`
**tidak lagi** memanggil `webTick()`/`handleClient()` sama sekali.

`-DHTTP_MAX_POST_WAIT=2000` **TIDAK** ditambahkan ke `build_flags` --
dicek dulu di `WebServer.h` milik framework, dan define itu **TIDAK**
dibungkus `#ifndef` (beda dari define lain seperti `OTA_ED25519_PUBKEY_B64`),
jadi menimpanya lewat `-D` akan menabrak (macro redefinition) alih-alih
menang bersih. Tidak dipaksakan -- dicatat di sini saja.

Residual risk (setelah fix, TIDAK lagi bisa mereboot gateway):
1. **Satu koneksi lambat masih bisa membuat server HTTP itu sendiri (satu
   koneksi diproses per `handleClient()`) tak responsif sementara** untuk
   klien LAIN yang mencoba mengakses dashboard/API di waktu yang sama --
   tapi BESS/Modbus (`task_bess`/`task_cmd`), MQTT (`mqtt_tx`/`mqtt_link`),
   dan command tetap berjalan normal di task-task lain, tidak terpengaruh.
2. **Body yang dikirim CEPAT (bukan slowloris) tetap memakan heap
   sementara** selama request diproses -- `readBytesWithTimeout()` di
   library core mem-`malloc`/`realloc` seluruh body sebelum handler
   dipanggil, dibatasi hanya oleh `Content-Length` yang dikirim klien
   sendiri (tidak ada guard tambahan di luar itu di kode kami).
3. **`prov.cpp` (state provisioning yang dibaca `task_web` DAN `loop()`
   sekaligus -- AP aktif/SSID/password, hostname mDNS, flag reboot
   terjadwal) sekarang dilindungi mutex** (audit thread-safety, lihat
   commit terkait) -- KECUALI dua accessor pointer `provMdnsHostname()`/
   `provApSsid()` yang tetap bisa membaca string yang SEDANG ditulis
   `provSaveWifi()`/`provSaveAp()` (jendela ~1 dtk sebelum
   `provScheduleReboot()` benar-benar reboot) -- murni kosmetik (tampilan
   `/wifi` atau `data.network.mdns` sesaat), tidak memengaruhi keselamatan
   BESS/Modbus. Lihat komentar di `src/prov.cpp`.

## Jadwal + auto-control SOC

Sub-proyek F (spec `docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md`
§F). Satu window harian per gateway (bukan array jadwal), disimpan NVS
`app_cfg` (namespace yang sama dihapus tombol factory reset di §Provisioning).
Logika murni (parsing, clamp, evaluasi window, keputusan) ada di
`lib/bess_core/sched_logic.*` -- 33 test native mencakup lintas tengah
malam, tz, waktu belum sinkron, dan tabel kasus edge/retry di bawah.

### Kebijakan (menyelaraskan keputusan owner 23 Juli, gateway-v2: "gateway
tak pernah auto-enable sendiri")

- **Proteksi SOC otonom = disable-only, SELALU aktif** (dengan atau tanpa
  jadwal): begitu BESS mengekspor (`active_power_kw > 0`) **dan**
  `soc_percent <= soc_stop_pct` **dan** sedang `running` → gateway mengirim
  `disable` sendiri. Default `soc_stop_pct=10`. SOC rendah saat **charging**
  (`active_power_kw <= 0`) **tidak** memicu ini -- baterai memang sedang diisi.
- **Jadwal = instruksi eksplisit dari cloud/operator**, jadi mengeksekusinya
  BUKAN keputusan otonom gateway: gateway **tidak pernah** memutuskan sendiri
  kapan boleh menyala, ia hanya menjalankan window yang sudah disetel.
  Bertindak **hanya** kalau `enabled=true` **dan** jam gateway sudah
  tersinkron NTP (`ts != 0`) -- sebelum itu, jadwal **tidak bertindak sama
  sekali** (tidak ENABLE, tidak DISABLE).
- **ENABLE terjadi PALING BANYAK SEKALI per window** (TEMUAN REVIEW 23 Sep
  2026, RENDAH, menggantikan perilaku lama "gagal syarat saat edge tidak
  pernah di-retry sampai window berikutnya") -- operator tetap boleh
  mematikan BESS manual di tengah window tanpa dinyalakan ulang oleh
  gateway, TAPI kegagalan syarat (fault/comm_lost/SOC di bawah recovery)
  tepat saat window baru mulai **sekarang dicoba lagi tiap siklus (~5 dtk)**
  selama masih di window yang sama, bukan dibuang sampai window berikutnya.
  - **Edge masuk window**: kalau `soc_percent >= soc_recovery_pct` **dan**
    tidak `fault` **dan** tidak `comm_lost` → `set_output power_w` lalu
    `enable`. Kalau syarat itu GAGAL tepat saat edge, gateway menandai
    **pending** dan mengevaluasi ulang syarat itu tiap siklus berikutnya
    SELAMA masih di window yang sama -- begitu syarat terpenuhi (mis. SOC
    naik melewati `soc_recovery_pct`, atau fault/comm_lost pulih),
    `set_output power_w` + `enable` langsung dijalankan saat itu juga.
    Setelah ENABLE berhasil sekali (langsung di edge atau lewat retry),
    **tidak ada ENABLE lagi** dalam window yang sama -- ini yang menjaga
    "operator mematikan manual di tengah window tidak dinyalakan ulang".
  - **Edge keluar window**: `disable`, tanpa syarat tambahan, dan
    menghapus status pending (kalau belum sempat enable sama sekali).
  - **Proteksi SOC (poin pertama) yang memicu DISABLE** juga menghapus
    status pending -- BESS yang baru dipaksa mati karena SOC rendah tidak
    langsung dicoba dinyalakan lagi oleh jadwal di siklus berikutnya.
- `battery_ready` (juga muncul di `data.auto`) = `soc_percent >=
  soc_recovery_pct` **dan** tidak fault **dan** tidak comm_lost -- independen
  dari jadwal aktif atau tidak, dipakai operator/cloud sebagai indikator
  "BESS layak dinyalakan" di luar mekanisme jadwal.
- **Command manual** (`enable`/`disable`/`set_output`/`set_power`, dari MQTT
  cloud ATAU `POST /api/command` H) **menonaktifkan jadwal**
  (`schedule_enabled` → `false`, tersimpan NVS) -- intervensi operator
  menang sampai jadwal diset ulang eksplisit lewat `set_schedule` atau
  `POST /api/auto/config`. Kontrak ack command manual **tidak berubah**;
  penonaktifan hanya terlihat di log serial + `data.auto.schedule_enabled`
  pada telemetri berikutnya. Aksi yang dijalankan **jadwal itu sendiri**
  (lewat `task_auto`) dikecualikan dari aturan ini -- kalau tidak, jadwal
  akan mematikan dirinya sendiri setiap kali ia enable BESS.

Eksekusi jadwal lewat `task_auto` (task baru, evaluasi tiap 5 dtk, diawasi
task watchdog) → mengantrekan command **internal** ke `task_cmd` (jalur
Modbus + safety + ack **sama persis** dengan command cloud). Ack aksi
otonom memakai `id:"auto-<epoch>"` supaya cloud melihatnya sebagai
tindakan gateway, bukan command yang mereka kirim sendiri.

### Command `set_schedule`

```json
{"id":"s1","cmd":"set_schedule","args":{
  "enabled":true,"start_hhmm":"17:00","end_hhmm":"21:00",
  "soc_stop_pct":10,"soc_recovery_pct":20,"power_w":1500,"tz_offset_min":420}}
```

Semua field di `args` **opsional** (partial update -- field yang tak
dikirim mempertahankan nilai tersimpan). `start_hhmm`/`end_hhmm` format
`"HH:MM"` ketat (dua digit jam dua digit menit); window boleh melewati
tengah malam (`start_hhmm > end_hhmm`, mis. `"22:00"..."06:00"`).
`tz_offset_min` = menit dari UTC (WIB = `420`).

Ack:

```json
{"id":"s1","cmd":"set_schedule","result":"accepted","detail":"",
 "applied":{"enabled":true,"start_hhmm":"17:00","end_hhmm":"21:00",
 "soc_stop_pct":10,"soc_recovery_pct":20,"power_w":1500,"tz_offset_min":420},
 "ts":1785000001}
```

`result`: `"accepted"`, `"clamped"`, atau `"rejected"`. `"clamped"` berarti
`soc_stop_pct` dipangkas ke 0-99, `soc_recovery_pct` dipangkas ke 0-100 lalu
**dipaksa naik** ke `soc_stop_pct+1` bila masih di bawahnya (berlaku juga
kalau `soc_recovery_pct` sendiri tidak dikirim kali ini -- invariant
recovery>stop selalu dijaga di config tersimpan), atau `tz_offset_min`
dipangkas ke -720..840 -- `applied` selalu berisi nilai yang **benar-benar
tersimpan**. `"rejected"`/`detail:"bad_value"` = `start_hhmm`/`end_hhmm`
bukan `"HH:MM"` valid, atau `power_w` NaN -- config tersimpan **TIDAK
disentuh** sama sekali, `applied` melaporkan config lama apa adanya.

⚠️ **Deviasi dari spec**: `power_w` **tidak dipangkas ±120% rated saat
disimpan** (beda dari `soc_stop_pct`/`soc_recovery_pct`/`tz_offset_min` di
atas) -- pemangkasan itu baru terjadi **saat eksekusi**, lewat jalur
`set_output` biasa (`planPowerPct`, lihat §Command di atas), karena rated
power device (`3146`) belum tentu diketahui saat command `set_schedule`
diterima (bisa saja belum pernah terbaca). `applied.power_w` di ack
`set_schedule` karena itu adalah nilai **mentah tersimpan**, bukan yang
sudah dipangkas.

### `GET`/`POST /api/auto/config`

```
GET /api/auto/config
-> {"ok":true,"enabled":true,"threshold":10.0,"recovery":20.0,
    "start":"17:00","end":"21:00","tz_offset":420,"power_w":1500.0,
    "in_window":false,"battery_ready":true}

POST /api/auto/config
    enabled=true&threshold=10&recovery=20&start=17:00&end=21:00&
    tz_offset=420&power_w=1500&code=<gateway_code>
-> (bentuk respons sama dengan GET, config SETELAH diterapkan)
```

Field form-encoded, **semua opsional** (partial update, sama semantik
dengan `set_schedule`): `enabled`, `threshold` (=`soc_stop_pct`),
`recovery` (=`soc_recovery_pct`), `start`, `end`, `tz_offset`, `power_w`.
`code` (=`gateway_code`, lihat §Provisioning) **wajib** -- tanpa itu atau
salah, `403 {"ok":false,"error":"forbidden"}` (endpoint ini mengendalikan
kapan konverter 50 kW menyala, LAN "trusted" saja tak cukup, paritas pola
E). `start`/`end` bukan `"HH:MM"` valid → `400
{"ok":false,"error":"bad_hhmm"}`, config tersimpan tidak berubah. `GET`
tidak butuh `code` (hanya membaca).

### `data.auto` (telemetri)

```json
"auto": {"schedule_enabled": false, "start_hhmm": "17:00", "end_hhmm": "21:00",
 "tz_offset_min": 420, "power_w": 1500.0, "soc_stop_pct": 10.0,
 "soc_recovery_pct": 20.0, "in_window": false, "battery_ready": true,
 "last_action": "none", "last_action_ts": 0}
```

`in_window`/`battery_ready` = evaluasi **saat ini** (dihitung ulang tiap
siklus `task_auto`, bukan cache basi). `last_action` = aksi jadwal terakhir
yang benar-benar dieksekusi -- `"none"` (belum pernah), `"enable_with_power"`,
atau `"disable"`; `last_action_ts` = 0 sampai aksi pertama terjadi.

## Dashboard & API lokal

Sub-proyek H (spec `docs/superpowers/specs/2026-09-23-subproyek-EFGH-design.md`
§H). `src/web_dashboard.cpp` mendaftarkan rute tambahan di atas `WebServer`
yang SAMA dengan E/F (`webServer()`, lihat §Kerangka web server di atas) --
tidak ada server HTTP kedua.

### Endpoint

| Method | Path | Auth | Keterangan |
|---|---|---|---|
| `GET` | `/` | tidak | Dashboard satu halaman (HTML/CSS/JS inline PROGMEM, ~17 KB, tanpa CDN) |
| `GET` | `/api/data` | tidak | JSON telemetri PERSIS sama builder dengan MQTT (`buildTelemetryJson`) -- `Cache-Control: no-store` |
| `GET` | `/api/acks` | tidak | 8 ack command terakhir (ring buffer), terbaru dulu, array objek ack ASLI |
| `POST` | `/api/command` | **ya** (`code`) | Body JSON command persis seperti MQTT -- lihat di bawah |
| `GET`/`POST` | `/api/auto/config` | GET tidak, POST **ya** | Jadwal (sub-proyek F, lihat §Jadwal di atas) |
| `GET` | `/api/firmware_versions` | tidak | `{firmware_version,running_partition,ota:{state,id,pending_verify}}` |
| `GET`/`POST` | `/wifi`, `/api/wifi/*` | POST **ya** | Provisioning (sub-proyek E, lihat §Provisioning di atas) |

Endpoint read-only (`GET /api/data`, `GET /api/acks`, `GET
/api/firmware_versions`, `GET /api/auto/config`, `GET /wifi`) tidak
memerlukan `code` -- tidak satu pun mengendalikan apa pun. Semua endpoint
yang MENGUBAH state (`POST /api/command`, `POST /api/auto/config`, `POST
/api/wifi/*`) wajib `code` (=`gateway_code`, lihat §Provisioning) --
paritas dengan aturan keselamatan E/F: LAN "trusted" saja tak cukup untuk
gateway yang mengendalikan konverter 50 kW.

### `POST /api/command`

Auth: `code` sebagai **query ATAU form arg** (`?code=<gateway_code>`) --
**BUKAN** field `"code"` di body JSON (body dipakai murni sebagai command,
identik dengan payload MQTT `device/<gw>/command`; satu bentuk auth
konsisten dengan `/api/wifi/*` dan `/api/auto/config`). Tanpa `code` benar
-> `403 {"ok":false,"error":"forbidden"}`.

Body: JSON command persis seperti MQTT. `id` **opsional** -- kalau tidak
dikirim (atau dikirim string kosong), gateway membangkitkan
`"web-<millis>"` supaya tetap bisa dikorelasikan lewat `GET /api/acks`.

```bash
curl -X POST "http://<ip>/api/command?code=<gateway_code>" \
  -H "Content-Type: application/json" \
  -d '{"cmd":"enable"}'
# -> 202 {"queued":true,"id":"web-123456"}

curl -X POST "http://<ip>/api/command?code=<gateway_code>" \
  -H "Content-Type: application/json" \
  -d '{"id":"op-1","cmd":"set_output","args":{"power_w":1500}}'
# -> 202 {"queued":true,"id":"op-1"}
```

Balasan sinkron HANYA konfirmasi "masuk antrean" -- hasil eksekusi
sebenarnya (`accepted`/`rejected`/`clamped`/`timeout` + `detail`) tetap
lewat jalur ack yang SAMA dengan command MQTT (topic
`device/<gw>/command/ack` + ring buffer `GET /api/acks`), karena eksekusi
command (tulis Modbus, tunggu bukti status sampai 10 dtk) dijalankan di
`task_cmd`, bukan di handler HTTP itu sendiri -- handler `/api/command`
hanya menitipkan command ke antrean lalu balas seketika (lihat §Kerangka
web server untuk `task_web`, task tempat handler HTTP ini berjalan).

Kode status:
- `202` -- masuk antrean (`{"queued":true,"id":...}`). **Belum tentu**
  `accepted` -- cek `GET /api/acks` atau `device/<gw>/command/ack`.
- `400` -- body kosong (`empty_body`).
- `403` -- `code` salah/kosong (`forbidden`).
- `413` -- body > `CMD_JSON_MAX` (2048 B, batas yang sama dengan command
  MQTT, lihat bess-0.2.0 di `../CHANGELOG.md`) (`payload_too_large`).
- `503` -- antrean command penuh (`queue_full`), coba lagi sebentar lagi.
  **Beda dari command MQTT**: command MQTT/jadwal yang datang saat antrean
  penuh tetap mendapat ack `queue_full` lewat slot luapan 1-slot
  (`task_cmd.cpp`); command web TIDAK memakai jalur itu -- HTTP `503` itu
  sendiri sudah jadi jawaban sinkron, jadi command web tidak ikut
  memperebutkan slot luapan yang dipakai command MQTT/jadwal.

Command dari `/api/command` diperlakukan **manual** (seperti cloud) --
**menonaktifkan jadwal** (`data.auto.schedule_enabled` -> `false`), sama
seperti command MQTT (lihat §Jadwal di atas, "Command manual...
menonaktifkan jadwal").

### `GET /api/acks`

```json
[
  {"id":"op-1","cmd":"set_output","result":"accepted","detail":"",
   "applied":{"power_pct":15.0,"power_w":1500.0},"ts":1785000005},
  {"id":"web-123456","cmd":"enable","result":"accepted","detail":"","applied":{},"ts":1785000001}
]
```

Array kosong `[]` kalau belum pernah ada command sejak boot. Ring buffer 8
slot (terbaru dulu) -- ack yang lebih lama tertimpa, **tidak persisted**
(hilang saat reboot). Berisi ack DARI SEMUA sumber command (MQTT, jadwal
otonom `id:"auto-<epoch>"`, dan `/api/command` sendiri), bukan cuma yang
dikirim lewat dashboard.

### `GET /api/firmware_versions`

```json
{"firmware_version":"bess-0.3.0","running_partition":"app0",
 "ota":{"state":"idle","id":"","pending_verify":false}}
```

### `GET /api/data`

Sama persis dengan payload telemetri MQTT (§Kontrak MQTT di atas),
termasuk `data.auto` (F) dan `data.ota` (G) -- SATU builder
(`buildTelemetryJson`, `lib/bess_core/payload.*`), SATU kontrak (diisi
lewat `fillSysInfo()`, `src/sysinfo.cpp`, fungsi yang sama dipakai
`main.cpp::loop()` untuk telemetri MQTT). Beda dari telemetri MQTT:
- `seq` = nilai `g_state.seq` **SAAT INI, TANPA increment** (`seq` itu
  milik telemetri MQTT, di-increment HANYA tiap kirim 60 dtk) -- jadi dua
  panggilan `/api/data` berturut-turut bisa mengembalikan `seq` yang SAMA
  kalau belum ada telemetri MQTT baru terkirim di antaranya.
- Header `Cache-Control: no-store` (jangan pernah menampilkan data basi
  dari cache browser/proxy).
- Tidak dikirim ke MQTT -- murni untuk dashboard lokal ini dan alat bench
  (`curl`, `bess-sim`).

### Dashboard `GET /`

Satu halaman HTML/CSS/JS inline (PROGMEM, ~17 KB, **TANPA CDN/font
eksternal** -- gateway sering tanpa akses internet), responsif dari lebar
360 px, mode gelap otomatis (`prefers-color-scheme`). Isi: status
koneksi WiFi (SSID/RSSI/IP/AP fallback dari `data.network`) + "DATA BASI"
kalau `data.bess.comm_lost` ATAU dua kali polling berturut-turut gagal
(MQTT connect/disconnect TIDAK diekspos lewat kontrak telemetri --
`buildTelemetryJson` sengaja tidak diubah untuk dashboard ini, lihat
`../CHANGELOG.md` bess-0.3.0 §H), kartu BESS (daya aktif, SOC, tegangan
DC, setpoint, status running/standby/fault/comm_lost), alarm aktif (hanya
`alarms_decoded` bernilai 1), panel kontrol (Enable/Disable dengan
`confirm()`, Set Output daya), panel jadwal (baca/tulis `/api/auto/config`),
8 ack terakhir, info firmware/OTA/crash/heap, tautan ke `/wifi`.

Poll `/api/data` tiap 2 dtk **berantai** (`fetch` -> tunggu
selesai/timeout 4 dtk -> `setTimeout` 2 dtk berikutnya) -- **BUKAN**
`setInterval`, supaya request tidak saling tumpang tindih di server
1-koneksi (pelajaran gateway-v2, lihat `../CLAUDE.md` §"26 Juli --
dashboard menghantam endpoint terberatnya sendiri"). `GET /api/acks`
(~tiap 10 dtk) dan `GET /api/firmware_versions` (~tiap 30 dtk) dirantai
ke siklus poll yang sama -- tidak pernah dua request terbang bersamaan.
Semua teks dari JSON masuk DOM lewat `textContent` (tidak pernah
`innerHTML`) -- nilai dari telemetri/ack (SSID, id command, dll) tidak
bisa menyuntik markup. `gateway_code` di panel kontrol disimpan
`sessionStorage` (dibungkus `try/catch` -- private browsing/quota bisa
melempar).

⚠️ **Penyimpangan dari spec: tidak ada upload OTA HTTP (`/update` milik
tim)**. Alasan: satu-satunya jalur OTA gateway adalah MQTT bertanda tangan
Ed25519 (sub-proyek G, lihat §OTA di atas) -- jalur `/update` HTTP polos
di LAN tidak memverifikasi tanda tangan sama sekali, jadi menambahkannya
berarti membuka jalur bypass persis di sebelah jalur bertanda tangan yang
sudah susah payah dibangun G. Dashboard hanya **menampilkan** status OTA
(`GET /api/firmware_versions`, blok `data.ota`) -- tidak bisa
memicu/mengunggah OTA dari sini.

## Non-scope fase ini

Fault-history ring buffer (topic MQTT `device/<gw>/fault` terpisah), TLS
8883 produksi (bench pakai broker dev `1883` polos).
