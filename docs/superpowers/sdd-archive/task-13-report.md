# Task 13 — Config, secrets, state, WiFi, main skeleton (build + smoke di hardware)

## Status: SELESAI (kode benar, WiFi tidak konek — AP tidak terjangkau di lokasi bench, bukan bug firmware)

## Commit
`fe5d0b3adbfa71b4f8acd1819f3ce16aa7c70968` — `feat(fw): skeleton main + wifi_mgr (backoff, country ID) + state`
Branch: `feat/initial-implementation`

8 file berubah, 126 insertion:
- `firmware/platformio.ini` (+1 baris)
- `firmware/src/config.h` (baru)
- `firmware/src/main.cpp` (baru)
- `firmware/src/secrets.example.h` (baru)
- `firmware/src/state.cpp` (baru)
- `firmware/src/state.h` (baru)
- `firmware/src/wifi_mgr.cpp` (baru)
- `firmware/src/wifi_mgr.h` (baru)

`firmware/src/secrets.h` **TIDAK** ikut commit — diverifikasi via `git check-ignore -v firmware/src/secrets.h` → cocok `firmware/.gitignore:2:src/secrets.h`, dan `git status`/`git show --stat HEAD` tidak pernah menampilkannya sebagai untracked/staged/tracked.

## Isi file (semua verbatim sesuai brief kecuali dua fix build wajib — lihat §Deviasi)

Semua 8 file (`config.h`, `secrets.example.h`, `secrets.h` lokal, `state.h/.cpp`, `wifi_mgr.h/.cpp`, `main.cpp`) ditulis persis sesuai kode di brief `task-13-brief.md`. `secrets.h` diisi kredensial asli dari instruksi tugas (WiFi `BIMA MANGGALA 2.4GHz` / `<wifi-pass>`, MQTT `mqtt://mqtt-dev.bepbatt.id:1883` user `guest`).

## Deviasi dari brief (wajib untuk build SUCCESS)

Brief menyatakan kode `src/` verbatim, tapi build **gagal** dengan kode itu apa adanya, di dua titik:

1. **`platformio.ini`** — env `esp32c6` cuma punya `-DARDUINO_USB_CDC_ON_BOOT=1`. ESP32-C6 tidak punya native USB OTG (hanya USB-Serial-JTAG), jadi tanpa `-DARDUINO_USB_MODE=1` framework Arduino salah pilih cabang `#define Serial USBSerial` (native USB) padahal seharusnya `HWCDCSerial` (JTAG-CDC) → compile error `'USBSerial' was not declared`. **Fix:** tambah `-DARDUINO_USB_MODE=1` ke `build_flags` env `esp32c6`.
2. **`main.cpp`** — kode brief memakai `WiFi.RSSI()` dan `WiFi.localIP()` tanpa `#include <WiFi.h>` (hanya `wifi_mgr.h` yang di-include, yang tidak transitively include `WiFi.h`). Compile error `'WiFi' was not declared in this scope`. **Fix:** tambah `#include <WiFi.h>` di baris kedua `main.cpp`.

Tanpa dua fix ini, `pio run -e esp32c6` gagal total (verified — build pertama FAILED dengan tepat dua error di atas sebelum fix diterapkan).

## Verifikasi build & test

- `pio run -e esp32c6` → **SUCCESS** (RAM 13.1%, Flash 72.3% dari partisi app0 1.31 MB)
- `pio test -e native` → **19/19 test PASSED** (test_native_crc, test_native_decode, test_native_frame, test_native_payload — tidak terpengaruh karena `test_build_src = false` di env native)

## Verifikasi hardware (gateway ESP32-C6, COM3, gw `58E6C5218C78`)

### Flash
`pio run -e esp32c6 -t upload --upload-port COM3` awalnya gagal 4x berturut-turut dengan `PermissionError(13, 'A device attached to the system is not functioning.', None, 31)` — port COM3 wedge di level driver Windows (dikonfirmasi juga lewat `mode COM3` dan `pyserial` langsung, bukan masalah proses lain yang mengunci port). User diminta mencabut-colok ulang kabel USB gateway secara fisik. Setelah itu, `pio device list` mengonfirmasi device tetap muncul sebagai **COM3** (SER=58:E6:C5:21:8C:78), dan upload langsung **SUCCESS** (8.44 detik, 966672 bytes tertulis, hash terverifikasi, hard reset via RTS pin).

### Monitor (log verbatim, digabung dari beberapa sesi capture ~20–45 detik)

```
--- Miniterm on COM3  115200,8,N,1 ---
[boot] gateway-bess bess-0.1.0
E (366) wifi:sta is connecting, return error
[   122][E][STA.cpp:417] connect(): STA connect failed! 0x3007: ESP_ERR_WIFI_CONN
E (4388) wifi:sta is connecting, return error
[  4143][E][STA.cpp:417] connect(): STA connect failed! 0x3007: ESP_ERR_WIFI_CONN
[wifi] putus rssi=0 ip=0.0.0.0
[wifi] putus rssi=0 ip=0.0.0.0
[wifi] putus rssi=0 ip=0.0.0.0
```

(Dari sesi capture terpisah, pola `[wifi] putus rssi=0 ip=0.0.0.0` berulang konsisten setiap ~5 detik selama **total ~90 detik pengamatan** lintas beberapa capture window, termasuk satu window 45 detik tanpa jeda — WiFi **tidak pernah** mencapai status `OK`.)

**Log `[boot] gateway-bess bess-0.1.0` MUNCUL** — terverifikasi.
**Log `[wifi] OK rssi=... ip=...` TIDAK PERNAH MUNCUL** — gateway tetap `putus` (disconnected) sepanjang pengamatan.

### Diagnosis kegagalan WiFi (bukan bug firmware)

- `netsh wlan show networks` dari PC kerja (di lokasi bench yang sama) hanya mendeteksi SSID **`Lantai 2`** (WPA2-Personal, signal 82%). SSID **`BIMA MANGGALA 2.4GHz`** (yang diisi ke `secrets.h` sesuai instruksi tugas) **tidak terdeteksi sama sekali** dalam scan tersebut.
- Kesimpulan paling mungkin: AP `BIMA MANGGALA 2.4GHz` **tidak terjangkau/tidak broadcasting** di lokasi bench saat ini — bukan salah kredensial (SSID/password di `secrets.h` sudah persis sesuai yang diberikan), dan bukan bug di `wifi_mgr.cpp` (kode verbatim brief, backoff & country-code "ID" berjalan sesuai desain — device mencoba reconnect berulang di 4s/8s/16s/... tapi tidak pernah menemukan AP-nya).
- Catatan kecil (bukan penyebab kegagalan total): setiap siklus reconnect memicu `E wifi:sta is connecting, return error` / `STA connect failed! 0x3007: ESP_ERR_WIFI_CONN` — karena `wifiTick()` pertama kali dipanggil nyaris bersamaan dengan `wifiInit()` (yang sudah memanggil `WiFi.begin()`), sehingga `WiFi.begin()` kedua tabrakan dengan attempt pertama yang masih `connecting`. ini konsekuensi dari kode verbatim brief (tidak diubah, sesuai instruksi), dan tidak menjelaskan kegagalan total karena siklus-siklus berikutnya (di t≈4s, 12s, 28s) tetap punya window bersih 4–16 detik tanpa hasil — cukup lama untuk asosiasi WPA2 normal bila AP ada dalam jangkauan.

## Concerns

1. **WiFi tidak terverifikasi konek ke jaringan nyata** — perlu SSID/password bench yang benar-benar broadcast di lokasi ini (mis. `Lantai 2`, sesuai yang terdeteksi scan), atau pastikan AP `BIMA MANGGALA 2.4GHz` dinyalakan/didekatkan sebelum smoke test lanjutan (Task 14+ yang butuh WiFi hidup, mis. MQTT uplink).
2. **Dua deviasi dari kode verbatim brief** (`-DARDUINO_USB_MODE=1` di `platformio.ini`, `#include <WiFi.h>` di `main.cpp`) — wajib untuk build sukses di ESP32-C6, sudah didokumentasikan di atas, tapi perlu diketahui kalau brief lain menyalin ulang kode ini secara verbatim asumsi sudah compile.
3. **Port COM3 sempat wedge di level driver Windows** (`ERROR_GEN_FAILURE`/"device not functioning") setelah upload gagal — butuh cabut-colok fisik untuk pulih; ini quirk USB-CDC ESP32-C6 di Windows yang sudah pernah dicatat di project lain (`gateway-v2`), bukan hal baru, tapi baik dicatat untuk task-task berikutnya yang butuh flash ulang berkali-kali.
4. Format tanggal commit menunjukkan `Aug 10 00:52:57 2026` (timezone lokal mesin) — bukan masalah, hanya catatan konsistensi.

---

## ADDENDUM — smoke test ulang dengan kredensial WiFi bench yang benar

Koreksi dari koordinator: SSID/password bench yang benar adalah **`Lantai 2` / `<wifi-pass>`** (kredensial test terdokumentasi proyek), bukan `BIMA MANGGALA 2.4GHz`. `firmware/src/secrets.h` (lokal, untracked, tidak pernah ikut commit) diupdate:

```cpp
#define WIFI_SSID     "Lantai 2"
#define WIFI_PASS     "<wifi-pass>"
```

Field `MQTT_URI`/`MQTT_USER`/`MQTT_PASSWD` tidak diubah. Tidak ada commit baru untuk perubahan ini (secrets.h tetap di luar git, sesuai `.gitignore`).

### Build + upload ulang

- `pio run -e esp32c6` → **SUCCESS** lagi (5.75 detik, RAM 13.1%, Flash 72.3%; hanya `wifi_mgr.cpp` yang recompile karena `secrets.h` di-include di situ).
- `pio run -e esp32c6 -t upload --upload-port COM3` → **SUCCESS langsung**, tanpa port wedge kali ini (8.16 detik, 966656 bytes tertulis, hash terverifikasi).

### Monitor ±30 detik — log verbatim

Sesi monitor 30 detik (setelah upload, tanpa reset manual — menangkap steady-state):

```
--- Terminal on COM3 | 115200 8-N-1
[wifi] OK rssi=-52 ip=192.168.18.52
[wifi] OK rssi=-52 ip=192.168.18.52
[wifi] OK rssi=-46 ip=192.168.18.52
[wifi] OK rssi=-42 ip=192.168.18.52
[wifi] OK rssi=-43 ip=192.168.18.52
[wifi] OK rssi=-43 ip=192.168.18.52
```

Sesi terpisah (reset paksa via esptool + miniterm untuk menangkap baris boot dari awal):

```
--- Miniterm on COM3  115200,8,N,1 ---
[boot] gateway-bess bess-0.1.0
E (367) wifi:sta is connecting, return error
[   123][E][STA.cpp:417] connect(): STA connect failed! 0x3007: ESP_ERR_WIFI_CONN
[wifi] OK rssi=-53 ip=192.168.18.52
[wifi] OK rssi=-53 ip=192.168.18.52
```

**Kedua log yang diminta brief sekarang terverifikasi lengkap:**
- `[boot] gateway-bess bess-0.1.0` — MUNCUL
- `[wifi] OK rssi=... ip=...` — MUNCUL, konsisten di seluruh jendela pengamatan (IP `192.168.18.52`, RSSI stabil di kisaran −42 s.d. −53 dBm, tidak pernah drop ke `putus` sekali sudah connect)

Catatan: error tunggal `sta is connecting, return error` / `ESP_ERR_WIFI_CONN` di awal boot (t≈123 ms) tetap muncul — ini konsekuensi dari race kondisi `wifiTick()` pertama dipanggil hampir bersamaan dengan `WiFi.begin()` di `wifiInit()` (sudah dicatat di analisis sebelumnya), bersifat kosmetik: koneksi tetap berhasil beberapa detik kemudian tanpa intervensi tambahan.

Dugaan sebelumnya soal AP "Lantai 2" historis di kanal 12/13 **terbukti tidak jadi masalah** — `esp_wifi_set_country_code("ID", true)` di `wifiInit()` (kanal 1–13 aktif) sudah menangani ini; asosiasi berhasil tanpa hambatan.

### Kesimpulan addendum

Task 13 **selesai penuh** — build SUCCESS, native test 19/19, hardware ter-flash, `[boot]` dan `[wifi] OK` keduanya terverifikasi di hardware nyata dengan kredensial bench yang benar. Tidak ada commit tambahan (perubahan hanya di `secrets.h` lokal yang gitignored).
