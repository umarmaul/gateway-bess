# Task 3 — Reproduksi dan diagnosis reboot

**Kesimpulan: BELUM TEREPRODUKSI.** Tiga percobaan rebutan `client_id` tidak menaikkan
`boot_count` sama sekali. Tidak ada perbaikan yang ditulis, karena tidak ada akar penyebab
yang terbukti. Instrumentasi Task 2 tetap terpasang supaya kejadian berikutnya tertangkap
sendiri.

---

## 1. Persiapan

| Hal | Nilai |
|---|---|
| Board | ESP32-C6, COM3 (`USB Serial Device`), gw `58E6C5218C78` |
| Simulator | `uv run bess-sim run --port COM11 --soc 60` (CH340 = COM11), hidup selama seluruh tes |
| Broker | `mqtt-dev.bepbatt.id:1883` |
| Alat | `bess-sim/tools/kick_probe.py` (baru), plus skrip tangkap serial tanpa goyang DTR/RTS |

**Firmware yang di-flash.** Baseline pertama (sebelum apa pun disentuh) melaporkan
`boot=11 reset=UNKNOWN_11` — bukti bahwa board masih menjalankan build `532e93f`, yaitu
sebelum `a38ea7d` menambahkan nama `USB`/`JTAG` ke tabel. Board karena itu di-flash ulang
ke HEAD (`a38ea7d`) supaya instrumentasi yang diuji adalah yang sudah di-commit:

```
Flash: [======    ]  56.3% (used 1106717 bytes from 1966080 bytes)
Wrote 1131456 bytes (690264 compressed) at 0x00010000 in 4.0 seconds
Hard resetting via RTS pin...
```

Sesudah flash, telemetri melaporkan `boot=12 reset=USB` — pemetaan `a38ea7d` terbukti
bekerja: angka 11 yang tadinya tampil sebagai `UNKNOWN_11` kini bernama `USB`.

**Baseline sebelum tes (150 detik, tanpa rebutan):**

```
[   0.4s] status = 'online' retain=True
[  42.8s] telemetri seq=18 boot=11 reset=UNKNOWN_11 uptime_ms=1080535 (3406 B)
[ 102.9s] telemetri seq=19 boot=11 reset=UNKNOWN_11 uptime_ms=1140609 (3401 B)
```

---

## 2. Tiga percobaan reproduksi

Tiap percobaan: klien kedua menyambung dengan `client_id` = `58E6C5218C78`, ditahan
**90 detik**, lalu dilepas; sesudahnya diamati. Serial COM3 ditangkap bersamaan dengan
skrip yang **tidak menggoyang DTR/RTS** dan membuka ulang port sendiri kalau USB-CDC
re-enumerate — supaya alat ukurnya tidak ikut membuat gejala yang sedang diukur.

| # | Tahan | Amati | Durasi tangkap MQTT / serial | `boot_count` sebelum → sesudah | Reboot? |
|---|---|---|---|---|---|
| 1 | 90 s | 120 s | 213 s / 220 s | **12 → 12** | tidak |
| 2 | 90 s | 120 s | 213 s / 220 s | **12 → 12** | tidak |
| 3 | 90 s | 180 s | 274 s / 280 s | **12 → 12** | tidak |

Total pengamatan di bawah tekanan rebutan `client_id`: **±11,7 menit**, `uptime_ms` naik
mulus dari 60.353 ms sampai 742.603 ms **tanpa satu pun diskontinuitas**.

### Percobaan 1 (ringkas)

```
[   2.1s] --- merebut client_id 58E6C5218C78 ---
[   2.4s] status = 'offline' retain=False
[  13.6s] telemetri seq=1 boot=12 reset=USB uptime_ms=60353 (3386 B)
[  86.4s] telemetri seq=2 boot=12 reset=USB uptime_ms=133135 (3396 B)
[  92.1s] --- melepas client_id ---
[ 146.0s] telemetri seq=3 boot=12 reset=USB uptime_ms=193169 (3384 B)
[ 206.1s] telemetri seq=4 boot=12 reset=USB uptime_ms=253254 (3389 B)
```

### Percobaan 2 (ringkas)

```
[   2.1s] --- merebut client_id 58E6C5218C78 ---
[  30.5s] telemetri seq=5 boot=12 reset=USB uptime_ms=318172 (3391 B)
[  91.9s] telemetri seq=6 boot=12 reset=USB uptime_ms=379670 (3394 B)
[  92.1s] --- melepas client_id ---
[ 151.6s] telemetri seq=7 boot=12 reset=USB uptime_ms=439680 (3392 B)
[ 211.6s] telemetri seq=8 boot=12 reset=USB uptime_ms=499779 (3394 B)
```

Perhatikan: `seq=5` muncul lagi di percobaan ini — dan **tidak** diikuti reboot. Jadi
"`seq` menyentuh 5" bukan pemicu apa-apa; lompatan 5 → 1 kemarin hanya berarti boot baru
(nomor `seq` dihitung per-boot).

### Percobaan 3 (ringkas)

```
[   2.1s] --- merebut client_id 58E6C5218C78 ---
[  36.6s] telemetri seq=9  boot=12 reset=USB uptime_ms=562464 (3393 B)
[  92.1s] --- melepas client_id ---
[  95.8s] telemetri seq=10 boot=12 reset=USB uptime_ms=622494 (3398 B)
[ 155.8s] telemetri seq=11 boot=12 reset=USB uptime_ms=682500 (3400 B)
[ 216.0s] telemetri seq=12 boot=12 reset=USB uptime_ms=742603 (3398 B)
```

Ekor pengamatan diperpanjang jadi 180 detik justru karena kejadian kemarin muncul
**±50 detik sesudah impostor dilepas** — jendela itu terlewati dengan lapang, tetap bersih.

### Serial selama ketiga percobaan

Penyaringan `\[boot\]|panic|Guru|abort|Backtrace|WDT|watchdog|Rebooting|rst:|assert` pada
log percobaan 2 dan 3: **tidak ada satu pun kecocokan**. Port serial juga tidak pernah
hilang (tidak ada `port hilang/gagal`), yang berarti USB-CDC tidak pernah re-enumerate —
konfirmasi kedua bahwa board tidak reset.

Yang terlihat di serial hanyalah badai reconnect MQTT, persis seperti yang diharapkan:

```
[   6.0s] E (50361) mqtt_client: esp_mqtt_handle_transport_read_error: transport_read(): EOF
[   6.0s] E (50361) mqtt_client: esp_mqtt_handle_transport_read_error: transport_read() error: errno=128
[   6.0s] E (50366) mqtt_client: mqtt_process_receive: mqtt_message_receive() returned -2
[   6.0s] [mqtt] disconnected
[  16.2s] [mqtt] connected
[  17.5s] E (61832) ... transport_read(): EOF        <- ditendang lagi 1,3 detik kemudian
```

Selama 90 detik penahanan terjadi **7–8 siklus** connect→ditendang→retry (retry 10–16
detik), dan topic `status` berkedip online/offline 8 kali. Task Modbus (`[bess] OK`) dan
WiFi (`[wifi] OK`, RSSI −47…−59) berjalan normal sepanjang badai itu.

---

## 3. Coredump

Partisi `coredump` (64 KB @ `0x3F0000`) dibaca utuh dari flash:

```
=== 64 byte pertama partisi coredump ===
 ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
*
=== jumlah byte bukan 0xFF ===
total 65536 non-FF 0
```

**Kosong sepenuhnya — tidak ada dump tersimpan.** Artinya tidak pernah ada panic sejak
partisi ini ada.

⚠️ **Peringatan penting supaya ini tidak salah dibaca:** partisi `coredump` baru lahir di
commit `be760d3` (13 Agustus **21:53**), sedangkan reboot yang sedang diselidiki terjadi
**sebelum** itu. Jadi partisi kosong **tidak** membuktikan kejadian kemarin bukan panic —
saat itu memang belum ada tempat menyimpannya. Yang dibuktikannya hanya: tidak ada panic
sejak partisi terpasang, termasuk selama tiga percobaan di atas.

(`esp_coredump info_corefile` sendiri menolak jalan tanpa ESP-IDF terpasang —
*"Please set up ESP-IDF to complete the action"* — jadi isi partisi dibaca langsung dengan
`esptool read_flash`. Untuk partisi kosong itu sudah menjawab pertanyaannya; kalau nanti
ada dump sungguhan, ESP-IDF perlu dipasang dulu untuk membacanya.)

---

## 4. Kontrol positif — membuktikan detektornya memang bekerja

"`boot_count` tidak naik" hanya berarti sesuatu kalau `boot_count` **bisa** naik. Itu
diuji dua kali, dan naik dua kali:

- reset keras esptool sesudah baca flash → `boot=12` menjadi `boot=13`
  (`telemetri seq=3 boot=13 reset=USB uptime_ms=180174`)
- reset keras esptool kedua → `boot=13` menjadi `boot=14`, dengan baris `[boot]` tertangkap
  verbatim:

```
[cap    0.0s] port terbuka
[   0.3s] [boot] reset=USB boot_count=14 heap=371764 min_heap=366776
[   0.4s] [boot] gateway-bess bess-0.1.0
[   0.4s] [boot] gw=58E6C5218C78
```

Jadi detektor terbukti: **naik saat reboot sungguhan, diam saat tes rebutan `client_id`.**

Catatan cara kerja: baris `[boot]` di atas hanya tertangkap karena port dibuka dalam
hitungan sepersekian detik sesudah reset. Di ketiga percobaan, port sudah terbuka dari awal
dan tidak pernah putus — memang tidak ada boot baru untuk dilaporkan.

---

## 5. Temuan sampingan (bukan penyebab, tapi terukur)

1. **`reset=USB` (enum 11) adalah alasan reset paling lazim di board ini.** Setiap reset
   yang dipicu host — flash, `read_flash`, `chip_id` — muncul sebagai `USB`. Kalau di
   lapangan `last_reset_reason` terbaca `USB` padahal tidak ada yang mencolok kabel, itu
   sinyal ada yang menyentuh port USB-nya.
2. **Menggoyang DTR/RTS lewat pyserial ternyata TIDAK mereset board ini.** Percobaan pulsa
   RTS eksplisit dengan port terbuka: log `[wifi] OK` tetap berdetak di 5,0 / 10,0 / 15,0
   detik tanpa putus, `uptime` berlanjut. Reset hanya terjadi lewat urutan reset esptool
   yang sesungguhnya. Ini **melemahkan** (tidak menghapus) hipotesis "reboot kemarin
   disebabkan monitor serial".
3. **Rebutan `client_id` membuat gateway masuk siklus connect→ditendang→retry** sepanjang
   impostor bertahan, dan LWT `offline` menyala berulang di broker. Tidak berbahaya bagi
   board, tapi berisik bagi cloud — bahan pertimbangan untuk Task 4 (hardening MQTT).
4. **Heap tidak ada di telemetri.** Saat ini `heap`/`min_heap` hanya dicetak sekali di
   baris `[boot]`. Kalau reboot kemarin dicurigai kehabisan memori, itu tidak akan terlihat
   dari cloud. Menerbitkan `free_heap`/`min_free_heap` di telemetri adalah kandidat kuat
   untuk task berikutnya.

---

## 6. Kesimpulan

Reboot 13 Agustus **tidak tereproduksi** dalam tiga percobaan rebutan `client_id` yang
mengikuti prosedur yang sama (tahan 90 detik, amati 120–180 detik). `boot_count` tetap
**12** dari awal sampai akhir ketiga percobaan, sementara kontrol positif membuktikan
pencacah itu memang naik saat board benar-benar reboot (12→13→14). Serial bersih dari
panic/watchdog, dan partisi coredump kosong.

Akar penyebabnya **tidak diketahui** dan sengaja tidak ditebak. Kejadian kemarin tetap
berstatus belum terjelaskan — satu kali, tanpa instrumentasi, tanpa tempat menyimpan
coredump. Yang berubah sekarang: kalau terulang, `boot_count` + `last_reset_reason` di
telemetri dan partisi coredump akan menangkapnya tanpa perlu ada orang yang sedang
menonton kabel serial.

**Tidak ada perubahan firmware yang dibuat untuk task ini.** Menulis "diperbaiki" tanpa
akar penyebab yang terbukti akan menutup kasus yang sebenarnya masih terbuka.
