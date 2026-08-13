# Fase Fondasi Paritas gateway-bess — Rencana Implementasi

> **Untuk pekerja agentik:** SUB-SKILL WAJIB: pakai superpowers:subagent-driven-development (disarankan) atau superpowers:executing-plans untuk mengerjakan rencana ini task demi task. Langkah memakai sintaks checkbox (`- [ ]`) untuk penanda.

**Goal:** Menyiapkan fondasi paritas gateway-bess dengan firmware rekan kerja — tabel partisi dua slot OTA + coredump, diagnosis akar penyebab reboot, hardening MQTT, dan penyelarasan kosakata command — supaya sub-proyek besar berikutnya (provisioning, OTA, dashboard) punya ruang flash dan alat diagnosis.

**Architecture:** Logika murni (parsing, pemangkasan, pemetaan nama, aritmetika waktu) hidup di `firmware/lib/bess_core/` supaya bisa diuji native tanpa hardware; kode yang menyentuh WiFi/esp-mqtt/FreeRTOS tetap di `firmware/src/` dan diverifikasi di bench. Dikerjakan sebagai dua langkah flash terpisah: partisi + diagnostik dulu (Task 1–3), sisanya sesudah akar penyebab reboot jelas (Task 4–10).

**Tech Stack:** PlatformIO + pioarduino `53.03.13`, Arduino ESP32-C6, ArduinoJson 7, Unity (tes native), esp-mqtt (ESP-IDF), Preferences (NVS), Python `uv` + paho-mqtt untuk probe bench.

## Global Constraints

- Spec acuan: `docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md`. Semua keputusan desain ada di sana; rencana ini tidak menambah keputusan baru.
- Bahasa komentar kode dan dokumen: **Indonesia**.
- Firmware paritas acuan: `BEPESP32_WiFi_Extension` branch **`origin/gateway-mqtt`** (HEAD `d994bb1`) — **read-only, jangan pernah diubah**. Baca dengan `git show origin/gateway-mqtt:src/<file>`.
- **DILARANG** memakai `gateway-v2/` sebagai referensi kode (batasan proyek yang masih berlaku).
- Tes native **hanya jalan lewat tool Bash** dengan `export PATH="/c/Users/legio/bin:$PATH"`; `gcc` tidak ada di PATH PowerShell.
- **23 tes native yang sudah ada harus tetap hijau** di setiap commit.
- Semua commit di branch **`fondasi-paritas-13aug`**.
- `firmware/src/secrets.h` **tidak pernah** ikut commit (sudah di-gitignore). Jangan menyalin kredensial ke README, plan, atau pesan commit.
- Bench: gateway di **COM3**; simulator di dongle **USB-SERIAL CH340** yang nomor port-nya berpindah-pindah — cari dengan `Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' }`.
- Rated BESS pada simulator = **50 kW** (register `3146`), jadi 5000 W = 10,0%.

## Struktur File

| File | Tanggung jawab | Task |
|---|---|---|
| `firmware/partitions.csv` (baru) | Tabel partisi 2×1,875 MB + spiffs + coredump | 1 |
| `firmware/platformio.ini` (ubah) | Daftarkan `board_build.partitions` | 1 |
| `firmware/lib/bess_core/reset_info.h/.cpp` (baru) | Pemetaan kode alasan reset → nama string. Murni, tanpa dependensi ESP | 2 |
| `firmware/lib/bess_core/payload.h/.cpp` (ubah) | Tambah `last_reset_reason` + `boot_count` ke telemetri | 2 |
| `firmware/src/main.cpp` (ubah) | Baca `esp_reset_reason()`, cacah boot di NVS, cetak diagnostik boot | 2 |
| `bess-sim/tools/cloud_probe.py` (ubah) | Tampilkan alasan reset & cacah boot saat `watch` | 2 |
| `bess-sim/tools/kick_probe.py` (baru) | Alat reproduksi: rebut `client_id`, amati pemulihan | 3 |
| `firmware/src/mqtt_link.cpp` (ubah) | Buffer, penjaga pesan terpotong, ack via enqueue | 4 |
| `firmware/lib/bess_core/commands.h/.cpp` (ubah) | `set_output` + alias, `target`, `planPowerPct` | 5, 6 |
| `firmware/src/task_cmd.cpp/.h` (ubah) | Pakai `planPowerPct`, validasi `target`, antrean luapan | 6, 7 |
| `firmware/lib/bess_core/timeutil.h/.cpp` (ubah) | `timeAfter()` aman rollover | 8 |
| `firmware/src/wifi_mgr.cpp` (ubah) | Pakai `timeAfter()` | 8 |
| `firmware/src/modbus_port.cpp`, `src/task_bess.cpp` (ubah) | Catat kode exception Modbus | 9 |
| `firmware/README.md` (ubah) | Kontrak MQTT terbaru | 10 |
| `firmware/test/test_native_payload/main.cpp` (ubah) | Tes untuk semua logika murni di atas | 2, 5, 6, 8 |

---

## LANGKAH 1 — Partisi, diagnostik, akar penyebab reboot

### Task 1: Tabel partisi dua slot OTA + coredump

**Files:**
- Create: `firmware/partitions.csv`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Consumes: —
- Produces: slot aplikasi `0x1E0000` (1.966.080 B) dan partisi `coredump` 64 KB di `0x3F0000`, dipakai Task 2 dan 3.

- [ ] **Step 1: Buat tabel partisi**

Buat `firmware/partitions.csv` — disalin apa adanya dari `BEPESP32_WiFi_Extension:origin/gateway-mqtt:partitions.csv` karena hardware-nya identik:

```csv
# MQTT butuh slot aplikasi lebih besar dari default Arduino ESP32-C6.
# Dua slot OTA dipertahankan, plus SPIFFS kecil dan partisi coredump.
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,   data, spiffs,  0x3D0000,0x20000,
coredump, data, coredump,0x3F0000,0x10000,
```

- [ ] **Step 2: Daftarkan di platformio.ini**

Di `firmware/platformio.ini`, pada blok `[env:esp32c6]`, tambahkan satu baris tepat sesudah `framework = arduino`:

```ini
board_build.partitions = partitions.csv
```

- [ ] **Step 3: Build dan periksa ukuran slot berubah**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6
```

Harapan: SUCCESS, dan baris `Flash:` sekarang menyebut **`from 1966080 bytes`** (bukan `1310720`), dengan persentase turun ke sekitar **59%**. Kalau angkanya masih 1310720, `board_build.partitions` tidak terbaca — periksa nama file dan lokasinya (harus di `firmware/`, sejajar `platformio.ini`).

- [ ] **Step 4: Hapus flash lalu upload**

Offset `nvs` dan `otadata` berpindah, jadi isi lama tidak valid lagi. Wajib erase penuh:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t erase --upload-port COM3 && pio run -e esp32c6 -t upload --upload-port COM3
```

- [ ] **Step 5: Pastikan gateway hidup normal di layout baru**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio device monitor --port COM3 --baud 115200
```

Harapan dalam 30 detik pertama: baris `[boot] gateway-bess bess-0.1.0`, `[boot] gw=58E6C5218C78`, lalu `[wifi] OK ...` dan `[mqtt] connected`. Hentikan monitor dengan Ctrl+C.

- [ ] **Step 6: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/partitions.csv firmware/platformio.ini && git commit -m "build(fw): tabel partisi 2x1,875 MB + partisi coredump

Disalin dari BEPESP32_WiFi_Extension (branch gateway-mqtt) karena
hardware identik. Slot aplikasi 1,25 MB -> 1,875 MB (84,3% -> ~59%),
dua slot OTA siap, dan partisi coredump 64 KB mengaktifkan penyimpanan
panic ke flash (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH sudah y di sdkconfig
bawaan pioarduino).

Flash pertama sesudah perubahan ini WAJIB erase penuh.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Diagnostik boot — alasan reset & cacah boot ke serial dan telemetri

**Files:**
- Create: `firmware/lib/bess_core/reset_info.h`, `firmware/lib/bess_core/reset_info.cpp`
- Modify: `firmware/lib/bess_core/payload.h`, `firmware/lib/bess_core/payload.cpp`, `firmware/src/main.cpp`, `bess-sim/tools/cloud_probe.py`
- Test: `firmware/test/test_native_payload/main.cpp`

**Interfaces:**
- Consumes: `SysInfo` dan `buildTelemetryJson(const SysInfo&, const BessData&, char*, size_t)` dari `payload.h`.
- Produces:
  - `const char* resetReasonName(int reason, char* out, size_t cap)` — menulis nama ke `out`, selalu NUL-terminated, mengembalikan `out`.
  - Konstanta `RESET_UNKNOWN=0, RESET_POWERON=1, RESET_EXT=2, RESET_SW=3, RESET_PANIC=4, RESET_INT_WDT=5, RESET_TASK_WDT=6, RESET_WDT=7, RESET_DEEPSLEEP=8, RESET_BROWNOUT=9, RESET_SDIO=10`.
  - Field baru di `SysInfo`: `const char* last_reset_reason; uint32_t boot_count;`
  - Field baru di telemetri: `data.last_reset_reason` (string), `data.boot_count` (integer).

- [ ] **Step 1: Tulis tes yang gagal untuk pemetaan nama alasan reset**

Di `firmware/test/test_native_payload/main.cpp`, tambahkan `#include "reset_info.h"` di blok include atas, lalu sisipkan fungsi tes ini tepat sebelum `static void test_parse_enable() {`:

```cpp
static void test_reset_reason_name() {
    char buf[24];
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(RESET_POWERON, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(RESET_PANIC, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(RESET_BROWNOUT, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(RESET_TASK_WDT, buf, sizeof(buf)));
    // Nilai tak dikenal tidak boleh hilang diam-diam
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_99", resetReasonName(99, buf, sizeof(buf)));
}
```

- [ ] **Step 2: Tulis tes yang gagal untuk field telemetri baru**

Sisipkan tepat sesudah fungsi di Step 1:

```cpp
static void test_telemetry_diagnostik_boot() {
    SysInfo s = sys_();
    s.last_reset_reason = "PANIC";
    s.boot_count = 42;
    BessData d{};
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("PANIC", doc["data"]["last_reset_reason"]);
    TEST_ASSERT_EQUAL(42, (int)doc["data"]["boot_count"]);
}
```

Daftarkan keduanya di `main()`, tepat sesudah baris `RUN_TEST(test_telemetry_envelope);`:

```cpp
    RUN_TEST(test_reset_reason_name);
    RUN_TEST(test_telemetry_diagnostik_boot);
```

- [ ] **Step 3: Jalankan tes, pastikan GAGAL**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Harapan: build **error** `reset_info.h: No such file or directory`. Itu belum kegagalan yang benar — lanjut ke Step 4 dulu, lalu jalankan lagi dan pastikan yang muncul adalah **kegagalan assert**, bukan error kompilasi.

- [ ] **Step 4: Buat header dengan konstanta, tanpa implementasi**

Buat `firmware/lib/bess_core/reset_info.h`:

```cpp
#ifndef RESET_INFO_H
#define RESET_INFO_H

#include <stddef.h>

// Nilai cermin dari esp_reset_reason_t (ESP-IDF). Dicocokkan lewat static_assert
// di src/main.cpp supaya ketidakcocokan ketahuan saat kompilasi, bukan di lapangan.
#define RESET_UNKNOWN    0
#define RESET_POWERON    1
#define RESET_EXT        2
#define RESET_SW         3
#define RESET_PANIC      4
#define RESET_INT_WDT    5
#define RESET_TASK_WDT   6
#define RESET_WDT        7
#define RESET_DEEPSLEEP  8
#define RESET_BROWNOUT   9
#define RESET_SDIO       10

// Menulis nama alasan reset ke out (selalu NUL-terminated), mengembalikan out.
// Nilai tak dikenal menjadi "UNKNOWN_<angka>" supaya tidak ada informasi hilang.
const char* resetReasonName(int reason, char* out, size_t cap);

#endif
```

Buat `firmware/lib/bess_core/reset_info.cpp` berisi implementasi kosong dulu supaya kegagalannya berupa assert, bukan linker error:

```cpp
#include "reset_info.h"
#include <stdio.h>

const char* resetReasonName(int reason, char* out, size_t cap) {
    (void)reason;
    snprintf(out, cap, "BELUM");
    return out;
}
```

- [ ] **Step 5: Jalankan tes lagi, pastikan gagal karena assert**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Harapan: `test_reset_reason_name` FAILED dengan `Expected 'POWERON' Was 'BELUM'`, dan `test_telemetry_diagnostik_boot` FAILED karena `last_reset_reason` belum ada di JSON.

- [ ] **Step 6: Implementasi pemetaan nama**

Ganti seluruh isi `firmware/lib/bess_core/reset_info.cpp`:

```cpp
#include "reset_info.h"
#include <stdio.h>
#include <string.h>

const char* resetReasonName(int reason, char* out, size_t cap) {
    static const char* NAMA[] = {
        "UNKNOWN", "POWERON", "EXT", "SW", "PANIC", "INT_WDT",
        "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO",
    };
    const int n = (int)(sizeof(NAMA) / sizeof(NAMA[0]));
    if (reason > 0 && reason < n) {
        snprintf(out, cap, "%s", NAMA[reason]);
    } else if (reason == RESET_UNKNOWN) {
        snprintf(out, cap, "UNKNOWN");
    } else {
        snprintf(out, cap, "UNKNOWN_%d", reason);
    }
    return out;
}
```

- [ ] **Step 7: Tambahkan field diagnostik ke payload**

Di `firmware/lib/bess_core/payload.h`, ubah struct `SysInfo` — tambahkan dua field tepat sesudah baris `char ip[16];`:

```cpp
    const char* last_reset_reason;   // hasil resetReasonName(), mis. "PANIC"
    uint32_t boot_count;             // pencacah monotonik di NVS
```

Di `firmware/lib/bess_core/payload.cpp`, tambahkan dua baris tepat sesudah `data["time_valid"] = ts != 0;`:

```cpp
    data["last_reset_reason"] = s.last_reset_reason ? s.last_reset_reason : "UNKNOWN";
    data["boot_count"] = s.boot_count;
```

- [ ] **Step 8: Jalankan seluruh suite native, pastikan hijau**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Harapan: **25 test cases, 25 succeeded** (23 lama + 2 baru).

- [ ] **Step 9: Sambungkan di main.cpp**

Di `firmware/src/main.cpp`, tambahkan include berikut di blok include atas:

```cpp
#include <Preferences.h>
#include <esp_system.h>
#include "reset_info.h"
```

Tambahkan static_assert dan variabel global tepat sebelum `void setup() {`:

```cpp
// Kalau ESP-IDF pernah menggeser nilai enum-nya, build gagal di sini —
// bukan diam-diam salah label di telemetri lapangan.
static_assert(ESP_RST_POWERON  == RESET_POWERON,  "nilai enum reset bergeser");
static_assert(ESP_RST_SW       == RESET_SW,       "nilai enum reset bergeser");
static_assert(ESP_RST_PANIC    == RESET_PANIC,    "nilai enum reset bergeser");
static_assert(ESP_RST_INT_WDT  == RESET_INT_WDT,  "nilai enum reset bergeser");
static_assert(ESP_RST_TASK_WDT == RESET_TASK_WDT, "nilai enum reset bergeser");
static_assert(ESP_RST_BROWNOUT == RESET_BROWNOUT, "nilai enum reset bergeser");

static char g_reset_reason[24] = "UNKNOWN";
static uint32_t g_boot_count = 0;
```

Di dalam `setup()`, tepat sesudah baris `Serial.begin(115200);`, sisipkan:

```cpp
    delay(200);                       // beri waktu USB-CDC siap sebelum baris pertama
    resetReasonName((int)esp_reset_reason(), g_reset_reason, sizeof(g_reset_reason));
    Preferences bootprefs;
    if (bootprefs.begin("boot", false)) {
        g_boot_count = bootprefs.getUInt("count", 0) + 1;
        bootprefs.putUInt("count", g_boot_count);
        bootprefs.end();
    }
    Serial.printf("[boot] reset=%s boot_count=%u heap=%u min_heap=%u\n",
                  g_reset_reason, g_boot_count,
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)esp_get_minimum_free_heap_size());
```

Di blok telemetri dalam `loop()`, tepat sesudah `si.fw_version = FW_VERSION;`, sisipkan:

```cpp
        si.last_reset_reason = g_reset_reason;
        si.boot_count = g_boot_count;
```

- [ ] **Step 10: Build firmware**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6
```

Harapan: SUCCESS. Kalau salah satu `static_assert` gagal, jangan diakali dengan mengubah `reset_info.h` sembarangan — periksa nilai enum sebenarnya di `esp_system.h` SDK, perbaiki tabel `NAMA` **dan** konstantanya bersama-sama, lalu jalankan ulang tes native.

- [ ] **Step 11: Tampilkan field baru di probe**

Di `bess-sim/tools/cloud_probe.py`, di dalam `on_msg`, ganti baris `print(f"seq={doc['seq']} ...")` (blok `if "data" in doc:`) menjadi:

```python
                d = doc["data"]
                print(f"seq={doc['seq']} type={d.get('device_type')} "
                      f"p={b.get('active_power_kw')}kW soc={b.get('soc_percent')}% "
                      f"running={b.get('running')} comm_lost={b.get('comm_lost')} "
                      f"reset={d.get('last_reset_reason')} boot={d.get('boot_count')}")
```

- [ ] **Step 12: Flash dan verifikasi di bench**

Nyalakan simulator lebih dulu (ganti `COM11` dengan port CH340 yang sedang aktif):

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/bess-sim" && uv run bess-sim run --port COM11 --soc 60
```

Di sesi lain, flash lalu pantau:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3 && pio device monitor --port COM3 --baud 115200
```

Harapan di serial: baris `[boot] reset=SW boot_count=<n> heap=... min_heap=...` (`SW` karena reset via upload). Nilai `boot_count` harus **lebih besar satu** dari boot sebelumnya, dan tidak kembali ke 1 kecuali NVS dihapus.

Lalu tunggu satu telemetri (≤90 detik) dengan probe — jalankan dari `bess-sim/`, isi `--user`/`--passwd` dari `firmware/src/secrets.h` **tanpa menuliskannya ke file atau pesan commit mana pun**:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/bess-sim" && uv run --with paho-mqtt python -u tools/cloud_probe.py --gw 58E6C5218C78 --user "$MQ_USER" --passwd "$MQ_PASS" watch
```

Harapan: baris telemetri memuat `reset=SW boot=<n>` yang cocok dengan serial.

- [ ] **Step 13: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/lib/bess_core/reset_info.h firmware/lib/bess_core/reset_info.cpp firmware/lib/bess_core/payload.h firmware/lib/bess_core/payload.cpp firmware/src/main.cpp firmware/test/test_native_payload/main.cpp bess-sim/tools/cloud_probe.py && git commit -m "feat(fw): diagnostik boot - alasan reset + cacah boot ke serial dan telemetri

Reboot 13 Agustus tidak meninggalkan jejak karena USB-CDC re-enumerate
saat reset dan monitor serial mati diam-diam. Sekarang alasan reset dan
cacah boot terbit di telemetri, jadi reboot di lapangan terlihat dari
cloud tanpa kabel.

Nilai enum dicocokkan dengan ESP-IDF lewat static_assert supaya
pergeseran nilai ketahuan saat kompilasi.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Reproduksi dan diagnosis reboot

**Files:**
- Create: `bess-sim/tools/kick_probe.py`
- Modify: (bergantung temuan — mungkin `firmware/src/mqtt_link.cpp` atau `firmware/src/main.cpp`)

**Interfaces:**
- Consumes: `data.last_reset_reason` dan `data.boot_count` dari Task 2; partisi `coredump` dari Task 1.
- Produces: kesimpulan tertulis — akar penyebab + perbaikan, **atau** pernyataan jujur "belum tereproduksi".

- [ ] **Step 1: Masukkan alat reproduksi ke repo**

Buat `bess-sim/tools/kick_probe.py`:

```python
"""Rebut client_id gateway supaya broker menendangnya, lalu amati pemulihan.

Dipakai untuk mereproduksi reboot yang terlihat 13 Agustus 2026. Lihat
docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md §3.2.
"""
import argparse
import json
import time

import paho.mqtt.client as mqtt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="mqtt-dev.bepbatt.id")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user"); ap.add_argument("--passwd")
    ap.add_argument("--gw", required=True)
    ap.add_argument("--hold", type=int, default=90, help="detik menahan client_id")
    ap.add_argument("--observe", type=int, default=120, help="detik mengamati sesudahnya")
    a = ap.parse_args()
    t0 = time.time()

    def stamp():
        return f"[{time.time() - t0:6.1f}s]"

    watcher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if a.user:
        watcher.username_pw_set(a.user, a.passwd)

    def on_msg(_c, _u, m):
        if m.topic.endswith("/status"):
            print(f"{stamp()} status = {m.payload.decode()!r} retain={m.retain}", flush=True)
            return
        d = json.loads(m.payload)["data"]
        doc = json.loads(m.payload)
        print(f"{stamp()} telemetri seq={doc['seq']} boot={d.get('boot_count')} "
              f"reset={d.get('last_reset_reason')} ({len(m.payload)} B)", flush=True)

    watcher.on_message = on_msg
    watcher.connect(a.host, a.port, 60)
    watcher.subscribe([(f"device/{a.gw}/telemetry", 1), (f"device/{a.gw}/status", 1)])
    watcher.loop_start()
    time.sleep(2)

    print(f"{stamp()} --- merebut client_id {a.gw} ---", flush=True)
    impostor = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=a.gw)
    if a.user:
        impostor.username_pw_set(a.user, a.passwd)
    impostor.connect(a.host, a.port, 60)
    impostor.loop_start()
    time.sleep(a.hold)
    print(f"{stamp()} --- melepas client_id ---", flush=True)
    impostor.loop_stop()
    impostor.disconnect()

    time.sleep(a.observe)
    watcher.loop_stop()
    print(f"{stamp()} selesai", flush=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Catat baseline sebelum tes**

Jalankan probe `watch` sebentar dan catat `boot_count` saat ini. Angka ini yang dipakai membuktikan ada/tidaknya reboot — **jangan** mengandalkan log serial, karena monitor mati diam-diam saat board reset.

- [ ] **Step 3: Jalankan reproduksi, tiga kali**

Simulator harus hidup. Untuk tiap percobaan:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/bess-sim" && uv run --with paho-mqtt python -u tools/kick_probe.py --gw 58E6C5218C78 --user "$MQ_USER" --passwd "$MQ_PASS"
```

Catat `boot_count` sesudah tiap percobaan. Naik = reboot terjadi.

- [ ] **Step 4: Kalau reboot terjadi — baca coredump**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3 --target nobuild 2>/dev/null; python -m esp_coredump info_corefile --port COM3 --chip esp32c6 .pio/build/esp32c6/firmware.elf
```

Kalau `esp_coredump` belum terpasang, jalankan `pip install esp-coredump` lebih dulu. Bacalah `last_reset_reason` di telemetri sebagai konfirmasi silang: `PANIC` berarti exception yang seharusnya punya coredump; `TASK_WDT`/`INT_WDT` berarti watchdog; `BROWNOUT` berarti masalah catu daya, bukan perangkat lunak — dan kalau `BROWNOUT` yang muncul, hentikan pencarian di perangkat lunak dan laporkan sebagai temuan hardware.

- [ ] **Step 5: Perbaiki akar penyebab, lalu buktikan**

Perbaikan bergantung temuan, jadi tidak bisa ditulis di muka. Yang tidak boleh berubah: perbaikannya harus menjawab persis apa yang ditunjukkan coredump/alasan reset, dan sesudah diperbaiki **tes yang sama di Step 3 dijalankan tiga kali lagi** dengan `boot_count` yang tidak naik sama sekali.

- [ ] **Step 6: Kalau tidak tereproduksi dalam tiga percobaan — laporkan apa adanya**

Tulis temuan di pesan commit: berapa kali dicoba, `boot_count` awal dan akhir, berapa lama tiap percobaan. **Jangan menulis "diperbaiki".** Instrumentasi dari Task 2 tetap terpasang supaya kejadian berikutnya tertangkap sendiri.

- [ ] **Step 7: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add bess-sim/tools/kick_probe.py && git commit -m "test(bench): alat reproduksi rebutan client_id + hasil percobaan

<Isi hasil sebenarnya: tereproduksi/tidak, boot_count sebelum-sesudah,
alasan reset, dan akar penyebab kalau ketemu.>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## LANGKAH 2 — Hardening dan penyelarasan kontrak

### Task 4: Hardening MQTT

**Files:**
- Modify: `firmware/src/mqtt_link.cpp`

**Interfaces:**
- Consumes: `taskCmdSubmit(const char*, size_t)` dari `task_cmd.h`.
- Produces: tidak ada perubahan tanda tangan; hanya perilaku.

- [ ] **Step 1: Naikkan ukuran buffer**

Di `firmware/src/mqtt_link.cpp`, di dalam `mqttInit()`, tambahkan dua baris tepat sesudah `cfg.network.timeout_ms = MQTT_NETWORK_TIMEOUT_MS;`:

```cpp
    cfg.buffer.out_size = MQTT_WRITE_BUFFER;   // telemetri ~3,3 KB butuh margin
    cfg.buffer.size = MQTT_READ_BUFFER;
```

Di `firmware/src/config.h`, tambahkan dua baris tepat sesudah `#define MQTT_NETWORK_TIMEOUT_MS 60000`:

```cpp
#define MQTT_WRITE_BUFFER      24576   // sama dengan BEPESP32_WiFi_Extension
#define MQTT_READ_BUFFER       2048
```

- [ ] **Step 2: Tambahkan penjaga pesan terpotong**

Di `firmware/src/mqtt_link.cpp`, ganti seluruh blok `case MQTT_EVENT_DATA:` menjadi:

```cpp
        case MQTT_EVENT_DATA: {
            // esp-mqtt memotong pesan yang lebih besar dari buffer masuk.
            // Potongan pertama BUKAN JSON utuh — memprosesnya menghasilkan
            // ack bad_json yang menyesatkan. Hanya proses pesan lengkap.
            bool utuh = e->current_data_offset == 0 &&
                        e->data_len == e->total_data_len;
            if (!utuh) {
                Serial.printf("[mqtt] pesan terpotong diabaikan (%d/%d B)\n",
                              e->data_len, e->total_data_len);
                break;
            }
            if (e->topic_len == (int)strlen(t_command) &&
                !strncmp(e->topic, t_command, e->topic_len))
                taskCmdSubmit(e->data, e->data_len);
            break;
        }
```

- [ ] **Step 3: Ack lewat enqueue, bukan publish**

Ganti isi `mqttPublishAck` menjadi:

```cpp
bool mqttPublishAck(const char* json, size_t n) {
    if (!cli || !connected) return false;
    // enqueue, bukan publish: publish menulis soket di task pemanggil sambil
    // memegang lock client — kalau TX tercekik, task_cmd ikut terblokir.
    return esp_mqtt_client_enqueue(cli, t_ack, json, n, 1, 0, true) >= 0;
}
```

- [ ] **Step 4: Build**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6
```

Harapan: SUCCESS. Catat angka RAM — kenaikannya harus sekitar 26 KB dan totalnya tetap jauh di bawah 50%.

- [ ] **Step 5: Verifikasi di bench**

Flash, lalu jalankan satu round-trip perintah dan tunggu satu telemetri:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/bess-sim" && uv run --with paho-mqtt python -u tools/cloud_probe.py --gw 58E6C5218C78 --user "$MQ_USER" --passwd "$MQ_PASS" enable
```

Harapan: ack `accepted` tetap datang seperti sebelumnya, dan telemetri berikutnya tetap ~3,3 KB. Perubahan ini tidak boleh mengubah apa pun yang terlihat dari luar — kalau ada yang berubah, itu regresi.

- [ ] **Step 6: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/src/mqtt_link.cpp firmware/src/config.h && git commit -m "fix(fw): hardening MQTT - buffer, penjaga pesan terpotong, ack via enqueue

Mengikuti MqttManager.cpp (BEPESP32_WiFi_Extension branch gateway-mqtt).
Penjaga pesan terpotong yang paling substantif: tanpa itu potongan
pertama command >1 KB diproses sebagai JSON utuh dan dijawab bad_json.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Kosakata command — `set_output` resmi, `set_power` alias, argumen `target`

**Files:**
- Modify: `firmware/lib/bess_core/commands.h`, `firmware/lib/bess_core/commands.cpp`, `firmware/src/task_cmd.cpp`
- Test: `firmware/test/test_native_payload/main.cpp`

**Interfaces:**
- Consumes: `Command`, `parseCommand()`, `buildAckJson()` dari `commands.h`.
- Produces: field baru `uint32_t target;` di `Command` (default `1` kalau `args.target` tidak ada); `Command::SET_POWER` sekarang cocok untuk nama `set_output` maupun `set_power`.

- [ ] **Step 1: Tulis tes yang gagal**

Di `firmware/test/test_native_payload/main.cpp`, sisipkan tepat sesudah `test_parse_set_power`:

```cpp
static void test_parse_set_output_nama_resmi() {
    Command c;
    const char* j = "{\"id\":\"c3\",\"cmd\":\"set_output\",\"args\":{\"power_w\":5000}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_POWER, c.type);
    TEST_ASSERT_TRUE(c.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, c.power_w);
    // ack harus menggemakan nama yang dikirim cloud, bukan nama internal
    TEST_ASSERT_EQUAL_STRING("set_output", c.name);
}

static void test_parse_target_default_satu() {
    Command c;
    const char* j = "{\"id\":\"d4\",\"cmd\":\"enable\",\"args\":{}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL_UINT32(1u, c.target);
}

static void test_parse_target_eksplisit() {
    Command c;
    const char* j = "{\"id\":\"e5\",\"cmd\":\"enable\",\"args\":{\"target\":2}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL_UINT32(2u, c.target);
}
```

Daftarkan ketiganya di `main()` tepat sesudah `RUN_TEST(test_parse_set_power);`:

```cpp
    RUN_TEST(test_parse_set_output_nama_resmi);
    RUN_TEST(test_parse_target_default_satu);
    RUN_TEST(test_parse_target_eksplisit);
```

- [ ] **Step 2: Jalankan, pastikan gagal**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Harapan: error kompilasi `'struct Command' has no member named 'target'`. Tambahkan field-nya lebih dulu (Step 3), lalu jalankan lagi dan pastikan yang tersisa adalah kegagalan assert.

- [ ] **Step 3: Tambahkan field target**

Di `firmware/lib/bess_core/commands.h`, tambahkan satu baris di dalam `struct Command` tepat sesudah `bool has_power;`:

```cpp
    uint32_t target;    // node tujuan; default 1 (BESS node tunggal)
```

- [ ] **Step 4: Implementasi parsing**

Di `firmware/lib/bess_core/commands.cpp`, di dalam `parseCommand`, ganti blok pengenalan nama menjadi:

```cpp
    out.target = doc["args"]["target"] | 1u;
    if (!strcmp(out.name, "enable")) out.type = Command::ENABLE;
    else if (!strcmp(out.name, "disable")) out.type = Command::DISABLE;
    else if (!strcmp(out.name, "set_output") || !strcmp(out.name, "set_power")) {
        // set_output = nama resmi (selaras BEPESP32_WiFi_Extension);
        // set_power = alias lama fase 1, dipertahankan supaya cloud tidak rusak.
        out.type = Command::SET_POWER;
        JsonVariant p = doc["args"]["power_w"];
        out.has_power = !p.isNull();
        out.power_w = out.has_power ? p.as<float>() : 0.0f;
    } else out.type = Command::UNSUPPORTED;
```

- [ ] **Step 5: Jalankan seluruh suite, pastikan hijau**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Harapan: **28 test cases, 28 succeeded**.

- [ ] **Step 6: Tolak target selain 1 di task_cmd**

Di `firmware/src/task_cmd.cpp`, di dalam `run()`, sisipkan tepat sesudah `parseCommand(rc.json, rc.len, c);`:

```cpp
        // BESS adalah node tunggal. Sebelumnya target diabaikan diam-diam,
        // sehingga perintah untuk node lain dijalankan di node ini.
        if ((c.type == Command::ENABLE || c.type == Command::DISABLE ||
             c.type == Command::SET_POWER) && c.target != 1) {
            sendAck(c, "rejected", "bad_value");
            continue;
        }
```

- [ ] **Step 7: Ajarkan probe mengenal `set_output`**

Tanpa ini `set_output` tidak bisa diuji di bench sama sekali. Di `bess-sim/tools/cloud_probe.py`, ganti baris `ps = sub.add_parser("set_power"); ps.add_argument("--watt", type=float, required=True)` menjadi:

```python
    for nama in ("set_output", "set_power"):     # set_output resmi, set_power alias
        ps = sub.add_parser(nama); ps.add_argument("--watt", type=float, required=True)
```

Dan ganti baris `args = {"power_w": a.watt} if a.cmd == "set_power" else {}` menjadi:

```python
        args = {"power_w": a.watt} if a.cmd in ("set_output", "set_power") else {}
```

- [ ] **Step 8: Build dan verifikasi di bench**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3
```

Lalu, dari `bess-sim/`, kirim ketiganya dan bandingkan ack-nya:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/bess-sim" && uv run --with paho-mqtt python -u tools/cloud_probe.py --gw 58E6C5218C78 --user "$MQ_USER" --passwd "$MQ_PASS" set_output --watt 5000
```

Harapan: ack `accepted` dengan `"cmd":"set_output"` dan `applied.power_pct` = 10. Ulangi dengan subcommand `set_power` — ack harus identik kecuali `"cmd":"set_power"`. Terakhir, kirim `enable` dengan `args.target` = 2 lewat skrip singkat mana pun; harapan `rejected` + `bad_value`.

- [ ] **Step 9: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/lib/bess_core/commands.h firmware/lib/bess_core/commands.cpp firmware/src/task_cmd.cpp firmware/test/test_native_payload/main.cpp bess-sim/tools/cloud_probe.py && git commit -m "feat(fw): set_output jadi nama resmi, set_power alias, target divalidasi

Menyelaraskan kosakata dengan BEPESP32_WiFi_Extension untuk hardware
yang identik. target sebelumnya diabaikan diam-diam sehingga perintah
untuk node lain tetap dijalankan di node ini.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Pemangkasan daya + hasil `clamped`

**Files:**
- Modify: `firmware/lib/bess_core/commands.h`, `firmware/lib/bess_core/commands.cpp`, `firmware/src/task_cmd.cpp`
- Test: `firmware/test/test_native_payload/main.cpp`

**Interfaces:**
- Consumes: `Command` dari Task 5.
- Produces: `bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped)` — `false` kalau `rated_w <= 0`; kalau `true`, `pct` sudah dipangkas ke `[-120, +120]` dan `clamped` menandakan pemangkasan benar-benar terjadi.

- [ ] **Step 1: Tulis tes yang gagal**

Sisipkan di `firmware/test/test_native_payload/main.cpp`, tepat sesudah tes dari Task 5:

```cpp
static void test_plan_power_dalam_rentang() {
    float pct = 0; bool clamped = true;
    TEST_ASSERT_TRUE(planPowerPct(5000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, pct);
    TEST_ASSERT_FALSE(clamped);
}

static void test_plan_power_dipangkas_atas() {
    float pct = 0; bool clamped = false;
    TEST_ASSERT_TRUE(planPowerPct(70000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 120.0, pct);
    TEST_ASSERT_TRUE(clamped);
}

static void test_plan_power_dipangkas_bawah() {
    float pct = 0; bool clamped = false;
    TEST_ASSERT_TRUE(planPowerPct(-70000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, -120.0, pct);
    TEST_ASSERT_TRUE(clamped);
}

static void test_plan_power_rated_belum_diketahui() {
    float pct = 0; bool clamped = false;
    // rated 0 = comm_lost sejak boot; tidak ada acuan untuk memangkas
    TEST_ASSERT_FALSE(planPowerPct(5000.0f, 0.0f, pct, clamped));
}
```

Daftarkan keempatnya di `main()` tepat sesudah `RUN_TEST(test_parse_target_eksplisit);`:

```cpp
    RUN_TEST(test_plan_power_dalam_rentang);
    RUN_TEST(test_plan_power_dipangkas_atas);
    RUN_TEST(test_plan_power_dipangkas_bawah);
    RUN_TEST(test_plan_power_rated_belum_diketahui);
```

- [ ] **Step 2: Jalankan, pastikan gagal**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Harapan: error kompilasi `'planPowerPct' was not declared in this scope`.

- [ ] **Step 3: Deklarasi dan implementasi**

Di `firmware/lib/bess_core/commands.h`, tambahkan tepat sebelum `#endif`:

```cpp
// Batas device: register 3050 berjangkauan -1200..1200 dalam satuan 0,1% rated.
#define POWER_PCT_LIMIT 120.0f

// Menghitung setpoint persen dari watt dan memangkasnya ke +-POWER_PCT_LIMIT.
// Mengembalikan false kalau rated_w tidak valid (<= 0) — tidak ada acuan untuk
// memangkas, jadi pemanggil harus menolak perintahnya.
bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped);
```

Di `firmware/lib/bess_core/commands.cpp`, tambahkan di akhir file:

```cpp
bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped) {
    if (!(rated_w > 0.0f)) return false;     // juga menangkap NAN
    float raw = power_w / rated_w * 100.0f;
    clamped = false;
    if (raw > POWER_PCT_LIMIT) { raw = POWER_PCT_LIMIT; clamped = true; }
    if (raw < -POWER_PCT_LIMIT) { raw = -POWER_PCT_LIMIT; clamped = true; }
    pct = raw;
    return true;
}
```

- [ ] **Step 4: Jalankan seluruh suite, pastikan hijau**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Harapan: **32 test cases, 32 succeeded**.

- [ ] **Step 5: Pakai di task_cmd**

Di `firmware/src/task_cmd.cpp`, ganti seluruh isi `doSetPower` menjadi:

```cpp
static void doSetPower(const Command& c) {
    if (!c.has_power) { sendAck(c, "rejected", "bad_value"); return; }
    stateLock();
    bool lost = g_state.bess.comm_lost;
    float rated_w = g_state.bess.rated_kw * 1000.0f;
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    float pct = 0.0f;
    bool clamped = false;
    if (!planPowerPct(c.power_w, rated_w, pct, clamped)) {
        sendAck(c, "rejected", "bess_no_ack");   // rated belum diketahui
        return;
    }
    int16_t raw = (int16_t)lroundf(pct * 10.0f);
    uint8_t exc = 0;
    if (mbWrite6(BESS_NODE, REG_P_SET, (uint16_t)raw, &exc) != MB_OK) {
        sendAck(c, "rejected", exc == 6 ? "bess_busy" : "bess_no_ack");
        return;
    }
    uint16_t rb;
    if (mbReadRegs(BESS_NODE, REG_P_SET, 1, &rb, &exc) != MB_OK ||
        (int16_t)rb != raw) {
        sendAck(c, "rejected", "readback_mismatch");
        return;
    }
    float applied_pct = raw / 10.0f;
    sendAck(c, clamped ? "clamped" : "accepted", "",
            applied_pct, applied_pct / 100.0f * rated_w);
}
```

- [ ] **Step 6: Build dan verifikasi di bench**

Flash, lalu kirim permintaan yang melampaui batas (70 kW pada BESS 50 kW). Harapan ack:

```json
{"id":"...","cmd":"set_output","result":"clamped","detail":"","applied":{"power_pct":120,"power_w":60000},"ts":...}
```

Simulator harus benar-benar berjalan pada 60 kW, bukan 70 kW — periksa baris `RUN p_ac= +60.00 kW` di log simulator. Lalu kirim 5000 W dan pastikan hasilnya kembali `accepted` dengan `power_pct: 10`.

- [ ] **Step 7: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/lib/bess_core/commands.h firmware/lib/bess_core/commands.cpp firmware/src/task_cmd.cpp firmware/test/test_native_payload/main.cpp && git commit -m "feat(fw): pangkas daya ke +-120% rated dengan hasil ack clamped

Sebelumnya nilai di luar rentang ditolak bad_value; sekarang dipangkas
ke batas device (register 3050 = -1200..1200 satuan 0,1% rated) dan
applied melaporkan nilai sesudah pemangkasan. Selaras dengan perilaku
BEPESP32_WiFi_Extension yang memangkas ke 3500 W milik DCON.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Ack `queue_full` lewat antrean luapan

**Files:**
- Modify: `firmware/src/task_cmd.cpp`

**Interfaces:**
- Consumes: `taskCmdSubmit()` dipanggil dari event handler esp-mqtt (Task 4).
- Produces: tidak ada perubahan tanda tangan publik.

**Penyimpangan dari spec §5 yang disengaja.** Spec menyebut "bentuk ack `queue_full`" sebagai salah satu tes native. Tes semacam itu **akan langsung hijau saat pertama ditulis** — `buildAckJson` sudah menerima `result`/`detail` apa pun dan bentuknya sudah dikunci `test_ack` yang ada. Tes yang tidak pernah bisa merah tidak membuktikan apa pun, jadi task ini diverifikasi di bench saja. Kalau pelaksana menemukan cara membuatnya benar-benar merah lebih dulu, silakan tambahkan.

- [ ] **Step 1: Tambahkan antrean luapan**

Di `firmware/src/task_cmd.cpp`, ganti deklarasi antrean dan `taskCmdSubmit` menjadi:

```cpp
struct RawCmd { char json[512]; size_t len; };
static QueueHandle_t q;
static QueueHandle_t q_luapan;   // 1 slot: perintah yang ditolak karena antrean penuh

void taskCmdSubmit(const char* json, size_t n) {
    RawCmd rc{};
    rc.len = min(n, sizeof(rc.json) - 1);
    memcpy(rc.json, json, rc.len);
    if (xQueueSend(q, &rc, 0) == pdTRUE) return;
    // Antrean utama penuh. JSON tidak boleh di-parse di sini (ini task jaringan
    // esp-mqtt), jadi payload mentah dititipkan ke slot luapan; task_cmd yang
    // mem-parse id-nya dan membalas queue_full.
    if (xQueueSend(q_luapan, &rc, 0) != pdTRUE)
        Serial.println("[cmd] dibuang: antrean utama dan luapan penuh");
}
```

- [ ] **Step 2: Kuras slot luapan di task**

Di `run()`, sisipkan tepat sesudah baris `if (xQueueReceive(q, &rc, portMAX_DELAY) != pdTRUE) continue;` — **sebelum** `Command c;`:

```cpp
        // Kuras luapan lebih dulu supaya cloud mendapat jawaban secepat mungkin
        RawCmd luapan;
        while (xQueueReceive(q_luapan, &luapan, 0) == pdTRUE) {
            Command lc;
            parseCommand(luapan.json, luapan.len, lc);
            sendAck(lc, "rejected", "queue_full");
        }
```

- [ ] **Step 3: Buat antreannya**

Di `taskCmdStart()`, sisipkan tepat sesudah `q = xQueueCreate(4, sizeof(RawCmd));`:

```cpp
    q_luapan = xQueueCreate(1, sizeof(RawCmd));
```

- [ ] **Step 4: Build**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3
```

- [ ] **Step 5: Verifikasi di bench dengan membanjiri perintah**

Setiap `enable`/`disable` menahan task sampai 10 detik (menunggu bit status), jadi antrean 4-slot mudah dipenuhi. Kirim 7 perintah `enable` beruntun tanpa jeda dari satu skrip, lalu kumpulkan seluruh ack.

Harapan: sebagian ack `accepted`/`rejected` biasa, dan **paling sedikit satu** ack `{"result":"rejected","detail":"queue_full"}` dengan `id` yang cocok dengan salah satu perintah yang dikirim. Yang membuktikan perbaikannya: **tidak ada perintah yang hilang tanpa jawaban** kecuali yang benar-benar melewati kapasitas slot luapan (dan yang itu muncul di log serial).

- [ ] **Step 6: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/src/task_cmd.cpp && git commit -m "feat(fw): balas queue_full alih-alih membuang perintah diam-diam

Antrean luapan 1-slot supaya parsing JSON tetap di task_cmd, bukan di
event handler esp-mqtt. Sengaja menyimpang dari BEPESP32_WiFi_Extension,
yang juga membuang diam-diam sehingga cloud menunggu ack selamanya.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Perbaikan rollover `millis()` di wifiTick

**Files:**
- Modify: `firmware/lib/bess_core/timeutil.h`, `firmware/lib/bess_core/timeutil.cpp`, `firmware/src/wifi_mgr.cpp`
- Test: `firmware/test/test_native_payload/main.cpp`

**Interfaces:**
- Consumes: —
- Produces: `bool timeAfter(uint32_t now, uint32_t deadline)` — benar kalau `now` sudah mencapai/melewati `deadline`, aman terhadap rollover `millis()`.

- [ ] **Step 1: Tulis tes yang gagal**

Sisipkan di `firmware/test/test_native_payload/main.cpp` tepat sesudah tes Task 6, dan tambahkan `#include "timeutil.h"` di blok include atas:

```cpp
static void test_time_after_biasa() {
    TEST_ASSERT_TRUE(timeAfter(1000u, 500u));
    TEST_ASSERT_TRUE(timeAfter(500u, 500u));     // tepat di batas = sudah waktunya
    TEST_ASSERT_FALSE(timeAfter(499u, 500u));
}

static void test_time_after_rollover() {
    // millis() berputar di hari ke-49. deadline dekat 0xFFFFFFFF, now sudah
    // berputar ke angka kecil: perbandingan biasa (now >= deadline) salah.
    TEST_ASSERT_TRUE(timeAfter(10u, 0xFFFFFF00u));
    TEST_ASSERT_FALSE(timeAfter(0xFFFFFF00u, 10u));
}
```

Daftarkan di `main()` tepat sesudah `RUN_TEST(test_plan_power_rated_belum_diketahui);`:

```cpp
    RUN_TEST(test_time_after_biasa);
    RUN_TEST(test_time_after_rollover);
```

- [ ] **Step 2: Jalankan, pastikan gagal**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Harapan: error kompilasi `'timeAfter' was not declared in this scope`.

- [ ] **Step 3: Implementasi**

Di `firmware/lib/bess_core/timeutil.h`, tambahkan tepat sebelum `#endif`:

```cpp
// Benar kalau now sudah mencapai/melewati deadline. Memakai selisih bertanda
// supaya tetap benar saat millis() berputar (hari ke-49).
bool timeAfter(uint32_t now, uint32_t deadline);
```

Di `firmware/lib/bess_core/timeutil.cpp`, tambahkan di akhir file:

```cpp
bool timeAfter(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
```

- [ ] **Step 4: Jalankan seluruh suite, pastikan hijau**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Harapan: **34 test cases, 34 succeeded**.

- [ ] **Step 5: Pakai di wifi_mgr**

Di `firmware/src/wifi_mgr.cpp`, tambahkan `#include "timeutil.h"` di blok include atas, lalu ganti baris `if (now >= next_try_ms) {` menjadi:

```cpp
    if (timeAfter(now, next_try_ms)) {
```

- [ ] **Step 6: Build dan pastikan WiFi tetap pulih normal**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3 && pio device monitor --port COM3 --baud 115200
```

Harapan: `[wifi] OK ...` muncul dalam 30 detik sesudah boot. Rollover-nya sendiri tidak bisa diamati di bench (butuh 49 hari) — itulah alasan perilakunya diuji native.

- [ ] **Step 7: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/lib/bess_core/timeutil.h firmware/lib/bess_core/timeutil.cpp firmware/src/wifi_mgr.cpp firmware/test/test_native_payload/main.cpp && git commit -m "fix(fw): wifiTick tahan rollover millis()

now >= next_try_ms salah saat millis() berputar di hari ke-49: gateway
berhenti mencoba reconnect sampai di-reboot. Diganti selisih bertanda,
diuji native karena tidak mungkin direproduksi di bench.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Catat kode exception Modbus

**Files:**
- Modify: `firmware/src/task_bess.cpp`

**Interfaces:**
- Consumes: `mbReadRegs(..., uint8_t* exc)` dari `modbus_port.h`.
- Produces: tidak ada perubahan tanda tangan.

- [ ] **Step 1: Simpan status dan kode exception per blok**

Di `firmware/src/task_bess.cpp`, ganti seluruh isi `pollOnce` menjadi:

```cpp
static void pollOnce(bool& ok) {
    uint16_t telem[REG_TELEM_COUNT], alst[REG_ALARM_COUNT], pset[1],
             param[REG_PARAM_COUNT];
    uint8_t exc = 0;
    struct { const char* nama; MbStatus st; uint8_t exc; } blok[4];
    blok[0] = {"telem", mbReadRegs(BESS_NODE, REG_TELEM_START, REG_TELEM_COUNT, telem, &exc), exc};
    exc = 0;
    blok[1] = {"alarm", mbReadRegs(BESS_NODE, REG_ALARM_START, REG_ALARM_COUNT, alst, &exc), exc};
    exc = 0;
    blok[2] = {"pset", mbReadRegs(BESS_NODE, REG_P_SET, 1, pset, &exc), exc};
    exc = 0;
    blok[3] = {"param", mbReadRegs(BESS_NODE, REG_PARAM_START, REG_PARAM_COUNT, param, &exc), exc};

    ok = true;
    for (int i = 0; i < 4; i++) {
        if (blok[i].st == MB_OK) continue;
        ok = false;
        if (blok[i].st == MB_EXCEPTION)
            Serial.printf("[bess] blok %s: exception 0x%02X\n", blok[i].nama, blok[i].exc);
        else
            Serial.printf("[bess] blok %s: gagal (status %d)\n", blok[i].nama, (int)blok[i].st);
    }
    if (!ok) return;
    stateLock();
    BessData& d = g_state.bess;
    bessDecodeTelemetry(telem, d);
    bessDecodeAlarmStatus(alst, d);
    d.setpoint_pct = (int16_t)pset[0] / 10.0f;
    d.rated_kw = param[0] / 10.0f;               // 3146
    d.soc_pct = param[38] / 10.0f;               // 3184
    d.comm_lost = false;
    d.last_ok_ms = millis();
    stateUnlock();
}
```

Perhatikan perubahan perilaku yang disengaja: keempat blok sekarang **selalu** dibaca, tidak lagi terhenti di kegagalan pertama (`&&` menghubung-singkat). Ini menukar sedikit waktu bus saat BESS bermasalah dengan diagnosis yang lengkap — blok mana saja yang gagal, bukan hanya yang pertama.

- [ ] **Step 2: Build**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio run -e esp32c6 -t upload --upload-port COM3
```

- [ ] **Step 3: Verifikasi di bench — matikan simulator**

Jalankan monitor serial, lalu hentikan proses simulator.

Harapan: baris `[bess] blok telem: gagal (status ...)` untuk keempat blok, lalu `[bess] COMM_LOST ...` seperti biasa. Nyalakan lagi simulator dan pastikan kembali `[bess] OK` tanpa reboot.

- [ ] **Step 4: Commit**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/src/task_bess.cpp && git commit -m "feat(fw): catat kode exception Modbus per blok register

Sebelumnya kegagalan poll hanya terlihat sebagai COMM_LOST tanpa
petunjuk blok mana dan kenapa. Keempat blok kini selalu dibaca supaya
diagnosisnya lengkap, bukan berhenti di kegagalan pertama.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: Dokumentasi kontrak + verifikasi akhir

**Files:**
- Modify: `firmware/README.md`
- Modify: `docs/superpowers/plans/2026-08-13-fondasi-paritas.md` (centang seluruh langkah)

**Interfaces:**
- Consumes: seluruh perubahan Task 1–9.
- Produces: dokumen kontrak MQTT yang cocok dengan firmware.

- [ ] **Step 1: Perbarui tabel command di README**

Di `firmware/README.md`, ganti baris tabel `set_power` menjadi dua baris:

```markdown
| `set_output` | `{"cmd":"set_output","args":{"power_w":5000}}` | Validasi rated (`3146`) → pangkas ke ±120% → FC6 `3050` (0,1% dari rated; **positif = ekspor/discharge, negatif = charge**) → baca balik untuk konfirmasi | `result:"accepted"` (atau `"clamped"`), `applied:{"power_pct":10,"power_w":5000}` |
| `set_power` | sama dengan `set_output` | Alias lama fase 1, perilaku identik | sama |
```

- [ ] **Step 2: Perbarui bagian hasil ack di README**

Ganti kalimat `` `result` selalu `"accepted"` atau `"rejected"`. `` menjadi:

```markdown
`result` bernilai `"accepted"`, `"clamped"`, atau `"rejected"`. `"clamped"` berarti
perintah dijalankan tetapi nilainya dipangkas ke batas device (±120% rated) — `applied`
selalu berisi nilai yang **benar-benar dipakai**, bukan yang diminta.
```

Lalu tambahkan `queue_full` ke tabel alasan tolak yang ada, dengan keterangan: "antrean perintah penuh; perintah tidak dijalankan, silakan kirim ulang."

- [ ] **Step 3: Dokumentasikan field telemetri baru di README**

Di contoh payload telemetri, tambahkan dua baris tepat sesudah `"time_valid": true,`:

```json
    "last_reset_reason": "POWERON",
    "boot_count": 12,
```

Dan tambahkan penjelasan tepat sesudah paragraf `**`ts`**`:

```markdown
**`last_reset_reason` / `boot_count`**: alasan reset terakhir (nama enum ESP-IDF, mis.
`POWERON`, `PANIC`, `BROWNOUT`; `UNKNOWN_<angka>` untuk nilai tak dikenal) dan pencacah
boot monotonik dari NVS. `boot_count` yang naik tanpa sebab yang diketahui = gateway
restart sendiri; itu sinyal, bukan derau.
```

- [ ] **Step 4: Jalankan seluruh verifikasi otomatis**

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native && pio run -e esp32c6
```

Harapan: **34 test cases, 34 succeeded** dan build SUCCESS dengan `Flash:` menyebut `from 1966080 bytes`.

- [ ] **Step 5: Checklist bench dari kondisi dingin**

Dengan simulator hidup dan gateway baru di-flash, buktikan berurutan dan catat keluaran verbatim:

1. Boot bersih — `[boot] reset=... boot_count=...` muncul, `[mqtt] connected` menyusul.
2. Telemetri masuk memuat `rssi_dbm`, `last_reset_reason`, `boot_count`, `device_type: "bess"`.
3. `enable` → ack `accepted`, simulator `STOP → PRECHARGE → RELAY → RUN`.
4. `set_output --watt 5000` → ack `accepted`, `applied.power_pct` = 10.
5. `set_output --watt 70000` → ack `clamped`, simulator berjalan di 60 kW.
6. `set_power --watt 5000` (alias) → ack `accepted`.
7. Banjir 7 `enable` beruntun → paling sedikit satu ack `queue_full`, tidak ada perintah tanpa jawaban.
8. `disable` → ack `accepted`, simulator `STOPPING`.
9. Simulator dimatikan → `[bess] blok ...` tercatat, lalu `COMM_LOST` dengan nilai lama dipertahankan.
10. `boot_count` **tidak berubah** sepanjang seluruh checklist — kalau naik, ada reboot yang harus diselidiki sebelum fase ini dinyatakan selesai.

- [ ] **Step 6: Centang rencana dan commit**

Centang seluruh checkbox di file rencana ini, lalu:

```bash
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess" && git add firmware/README.md docs/superpowers/plans/2026-08-13-fondasi-paritas.md && git commit -m "docs: kontrak MQTT terbaru + centang rencana fase fondasi

set_output/set_power, hasil clamped, alasan tolak queue_full, dan field
telemetri last_reset_reason/boot_count.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Catatan penutup untuk pelaksana

**Yang sudah selesai sebelum rencana ini** dan tidak boleh diulang: `network.rssi` → `rssi_dbm`, `ts` = 0 saat NTP belum sinkron, `client_id` MQTT = MAC, `WiFi.setSleep(false)`, dan gerbang `mqttConnected()` pada telemetri. Kelimanya ada di working tree branch `fondasi-paritas-13aug` dan **belum di-commit** — commit lebih dulu sebelum memulai Task 1 supaya riwayatnya bersih.

**Gerbang `mqttConnected()` masih belum teruji** (spec §6). Kalau ada kesempatan saat Task 3 — putusnya MQTT selama >60 detik — periksa sekalian: sesudah tersambung lagi, telemetri harus datang **satu**, bukan beruntun.

**Kalau ada langkah yang tidak cocok dengan kenyataan kode**, hentikan dan laporkan alih-alih mengarang. Rencana ini ditulis dari pembacaan kode 13 Agustus 2026; kalau ada yang bergeser, spec yang menang, bukan potongan kode di rencana ini.
