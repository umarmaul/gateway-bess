# Task 1: Tabel Partisi 2 Slot OTA + Coredump — Report

**Status:** DONE  
**Commit:** `be760d3`  
**Date:** 13 Agustus 2026

## Ringkasan Pekerjaan

Berhasil mengganti tabel partisi default Arduino ESP32-C6 dengan tabel yang lebih besar untuk mendukung MQTT dan OTA:
- Slot aplikasi: 1,25 MB → 1,875 MB (app0 `0x1E0000` = 1.966.080 B)
- Dua slot OTA (`ota_0`, `ota_1`) dipertahankan untuk OTA duplex
- Partisi coredump 64 KB di `0x3F0000` untuk penyimpanan panic dump
- SPIFFS 128 KB di `0x3D0000` untuk LittleFS

## Perubahan File

### `firmware/partitions.csv` (BARU)
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
Disalin langsung dari `BEPESP32_WiFi_Extension` branch `origin/gateway-mqtt` karena hardware identik.

### `firmware/platformio.ini` (MODIFIKASI)
Ditambahkan satu baris di blok `[env:esp32c6]` setelah `framework = arduino`:
```ini
board_build.partitions = partitions.csv
```

## Hasil Build

Perintah: `pio run -e esp32c6`

**Output kritis:**
```
Flash: [======    ]  56.2% (used 1104783 bytes from 1966080 bytes)
```

✓ Denominasi berubah dari `1310720` ke `1966080` (persis 1.875 MB)  
✓ Persentase turun dari 84,3% ke 56,2%  
✓ Partisi terbaca dan dipakai oleh build system

## Flash dan Upload

Karena offset `nvs` dan `otadata` berubah, dilakukan erase penuh:

**Erase:**
```
Erasing flash (this may take a while)...
Chip erase completed successfully in 1.9 seconds.
```

**Upload:**
```
Writing at 0x0012316a... (100 %)
Wrote 1129200 bytes (689208 compressed) at 0x00010000 in 2.6 seconds
Hash of data verified.
```

✓ Upload sukses, hash terverifikasi

## Boot dan Verifikasi

Monitor COM3 (115200 baud) menunjukkan firmware beroperasi normal setelah upload:
```
[wifi] OK rssi=-52 ip=192.168.18.52
[mqtt] connected
[bess] COMM_LOST p=0.0kW soc=0.0% vdc=0.0V status=0x0000
```

✓ WiFi terhubung ke jaringan lokal  
✓ MQTT client tersambung ke broker  
✓ Task BESS berjalan (membaca status, COMM_LOST expected tanpa simulator)  
✓ Tidak ada crash, reboot, atau exception

## Commit

```
commit be760d3
Author: Claude <noreply@anthropic.com>
Date:   Wed Aug 13 ... 2026

    build(fw): tabel partisi 2x1,875 MB + partisi coredump
    
    Disalin dari BEPESP32_WiFi_Extension (branch gateway-mqtt) karena
    hardware identik. Slot aplikasi 1,25 MB -> 1,875 MB (84,3% -> ~59%),
    dua slot OTA siap, dan partisi coredump 64 KB mengaktifkan penyimpanan
    panic ke flash (CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH sudah y di sdkconfig
    bawaan pioarduino).
    
    Flash pertama sesudah perubahan ini WAJIB erase penuh.
```

Commit termasuk kedua file: `partitions.csv` (baru) dan `platformio.ini` (modifikasi).

## Catatan

- Peringatan Git `LF will be replaced by CRLF` adalah normal di Windows; tidak mempengaruhi fungsionalitas.
- `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` sudah ada di sdkconfig bawaan pioarduino 53.03.13, jadi coredump partition langsung fungsional tanpa konfigurasi tambahan.
- Build time: 11.31 detik (clean build tanpa cache).
- Flash utilization setelah perubahan: 56,2% (dari 84,3%), memberikan ruang untuk OTA dan ekstensi firmware di masa depan.

## Kesiapan Tahap Lanjut

Task 1 selesai dan terverifikasi. Deliverable untuk Task 2 dan Task 3:
- ✓ Slot aplikasi `0x1E0000` (1.966.080 B) siap dipakai untuk build firmware
- ✓ Partisi coredump `0x3F0000` (64 KB) tersedia untuk penyimpanan exception
- ✓ Firmware berjalan stabil di layout partisi baru
