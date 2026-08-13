# Task 7 — Laporan: Ack `queue_full` lewat antrean luapan

## Ringkasan

`firmware/src/task_cmd.cpp` diubah persis sesuai brief: antrean luapan 1-slot
(`q_luapan`) menampung payload JSON mentah saat antrean utama (4 slot) penuh;
`task_cmd` mengurasnya lebih dulu di setiap iterasi `run()` (sebelum memproses
item baru dari antrean utama), mem-parse `id`-nya di sana (bukan di event
handler esp-mqtt), dan membalas `{"result":"rejected","detail":"queue_full"}`.
Kalau slot luapan itu sendiri juga penuh, perintah benar-benar dibuang dan
dicatat ke serial: `[cmd] dibuang: antrean utama dan luapan penuh`.

Tidak ada tes native baru ditambahkan — sesuai penyimpangan yang disengaja
yang dijelaskan di brief §"Penyimpangan dari spec §5": tes bentuk ack
`queue_full` akan langsung hijau saat pertama ditulis karena `buildAckJson`
sudah menerima `result`/`detail` apa pun dan bentuknya sudah dikunci oleh
`test_ack` yang ada, jadi tidak membuktikan apa-apa. Verifikasi dilakukan
di bench nyata.

## Hasil tes native

```
=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.210
native         test_native_decode   PASSED    00:00:01.156
native         test_native_frame    PASSED    00:00:01.132
native         test_native_payload  PASSED    00:00:01.669
================= 34 test cases: 34 succeeded in 00:00:05.168 =================
```

34/34, tetap hijau seperti sebelum perubahan.

## Build & upload

```
Compressed 1132944 bytes to 690957...
...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
========================= [SUCCESS] Took 11.01 seconds =========================
```
Diupload ke board COM3 (`58E6C5218C78`) via `pio run -e esp32c6 -t upload --upload-port COM3`.

## Bench: membanjiri 7 perintah `enable` tanpa jeda

Simulator dijalankan dari `bess-sim` (`uv run bess-sim run --port COM11 --soc 60`,
port CH340 ditemukan lewat `Get-CimInstance Win32_PnPEntity`). Listener ack
(paho-mqtt, subscribe ke `device/58E6C5218C78/command/ack`) dibuka dan
dikonfirmasi `subscribed` **sebelum** publish pertama. Ketujuh perintah
`enable` (`{"id":...,"ts":...,"cmd":"enable","args":{"target":1},"api_schema_version":1}`)
dipublish beruntun tanpa jeda dari satu skrip Python (`bench_flood.py`).
Log serial COM3 direkam paralel dengan `serial_capture.py` (DTR/RTS dipaksa
`False` sebelum `open()`, sehingga board tidak reset saat port dibuka —
`pio device monitor` sengaja dihindari karena itu mereset board).

### Akuntansi 7 id yang dikirim

| id | dikirim (unix) | ack? | result | detail | delta |
|---|---|---|---|---|---|
| bench7-1-713509 | 1786640713.510 | ya | accepted | "" | 4.09s |
| bench7-2-713509 | 1786640713.510 | ya | accepted | "" | 5.11s |
| bench7-3-713509 | 1786640713.510 | ya | accepted | "" | 5.12s |
| bench7-4-713509 | 1786640713.510 | ya | accepted | "" | 5.13s |
| bench7-5-713509 | 1786640713.510 | ya | accepted | "" | 6.12s |
| bench7-6-713509 | 1786640713.510 | ya | **rejected** | **queue_full** | 4.12s |
| bench7-7-713509 | 1786640713.510 | **tidak ada** | — | — | — |

**6 dari 7 dapat ack, 1 (`bench7-7-713509`) tanpa ack** — dan ketiadaan ack
itu justru dibuktikan oleh baris drop di serial log (lihat di bawah), bukan
hilang tanpa jejak. Sebelum perubahan ini, perintah semacam itu akan hilang
total tanpa jejak apa pun di sisi cloud maupun serial.

### Ack `queue_full` verbatim

```json
{"id": "bench7-6-713509", "cmd": "enable", "result": "rejected", "detail": "queue_full", "applied": {}, "ts": 1786640718}
```

id-nya (`bench7-6-713509`) cocok persis dengan salah satu dari 7 perintah
yang dikirim.

### Baris drop di serial log (verbatim)

```
[cap    0.0s] port terbuka
[   1.1s] [wifi] OK rssi=-51 ip=192.168.18.52
[   3.0s] [cmd] dibuang: antrean utama dan luapan penuh
[   4.2s] [bess] OK p=0.0kW soc=59.0% vdc=824.6V status=0x8301
[   6.1s] [wifi] OK rssi=-49 ip=192.168.18.52
[   6.7s] [cmd] enable -> accepted
[   6.7s] [cmd] enable -> rejected queue_full
[   7.5s] [cmd] enable -> accepted
[   7.5s] [cmd] enable -> accepted
[   7.8s] [cmd] enable -> accepted
[  11.2s] [cmd] enable -> accepted
```

Persis satu baris `[cmd] dibuang: antrean utama dan luapan penuh` muncul —
cocok dengan tepat 1 id yang tidak mendapat ack (`bench7-7-713509`).

## Penjelasan pola hasil (mengapa 5 accepted, bukan hanya 4)

Perintah `enable` pertama (bench7-1) diproses sendirian oleh `task_cmd`
(antrean utama baru berisi 1 item saat itu diambil), menahan ~4s menunggu
bit status Run — cocok dengan catatan brief soal blocking hingga 10 detik.
Empat perintah berikutnya (bench7-2..5) sempat masuk ke antrean utama
(kapasitas 4) sebelum `task_cmd` sempat menariknya; begitu diproses, bit
status sudah `Run=1` dari hasil bench7-1, sehingga `waitStatusBit` langsung
lolos di iterasi pertama alih-alih menunggu penuh 10 detik — makanya
delta-nya jauh lebih pendek (~1s antar-ack) daripada 10 detik. bench7-6
gagal masuk antrean utama (sudah penuh 4 slot) sehingga jatuh ke slot
luapan dan dibalas `queue_full` segera setelah bench7-1 selesai diproses
(sesuai desain: "kuras luapan lebih dulu"). bench7-7 gagal masuk baik ke
antrean utama maupun slot luapan (keduanya sudah terisi) → dibuang dan
dicatat ke serial pada t=3.0s (sebelum bench7-1 pun selesai diproses,
karena drop terjadi saat publish, bukan saat proses).

## Commit

```
ecca35d feat(fw): balas queue_full alih-alih membuang perintah diam-diam
```
1 file changed (`firmware/src/task_cmd.cpp`), 15 insertions(+), 1 deletion(-).

## Hal yang mengejutkan / dicatat

- Perubahan pola waktu ack (4s lalu ~1s beruntun) awalnya terlihat aneh
  sampai disadari itu efek dari `waitStatusBit` yang langsung lolos begitu
  bit Run sudah `1` dari hasil `enable` sebelumnya — bukan bug, konsisten
  dengan kode `doOnOff`/`waitStatusBit` yang tidak berubah di task ini.
- Baris drop di serial muncul di t=3.0s, LEBIH AWAL dari ack pertama
  (t≈4.09s sejak kirim / lihat offset serial ~6.7s dari boot) — ini
  masuk akal karena `taskCmdSubmit` (dipanggil dari event handler MQTT)
  membuang perintah ketujuh SAAT PUBLISH tiba, independen dari kapan
  `task_cmd` selesai memproses item pertama.
- `q_luapan` berkapasitas 1, jadi kalau ada burst >6 command sekaligus
  (4 antrean utama + 1 luapan) sisanya akan dibuang diam-diam ke serial
  sebagaimana didesain — ini bukan bug, memang itu batas yang disengaja
  di brief (di luar scope untuk memperbesar slot luapan).
- File-file build log tak terlacak (`firmware/build.log`,
  `firmware/build_output.log`) sudah ada di working tree sebelum task ini
  dimulai (bukan hasil kerja task 7) dan sengaja tidak di-`git add`.
- **Cleanup simulator tidak langsung berhasil di percobaan pertama.** `kill
  $SIM_PID` dan `pkill -f "bess-sim run"` di skrip orkestrasi bash
  (Git Bash/MSYS) gagal diam-diam mematikan proses native Windows
  `python.exe` yang sebenarnya menjalankan simulator — PID yang dilaporkan
  `$!` di MSYS ternyata tidak memetakan ke PID Windows asli prosesnya.
  Dua proses `python.exe` (`.venv\Scripts\bess-sim.exe run --port COM11`)
  masih hidup dan menahan COM11 sesudah skrip bench "selesai". Ketahuan
  dari `tasklist` + `Get-CimInstance Win32_Process` (cek command line),
  lalu dimatikan paksa dengan `Stop-Process -Id ... -Force` di PowerShell.
  Diverifikasi bersih sesudahnya (`tasklist` untuk `python.exe`/`uv.exe`
  kosong). **Pelajaran: di Git Bash/MSYS Windows, jangan percaya `$!`/`kill`
  untuk proses native yang di-spawn lewat `uv run` — verifikasi lewat
  `tasklist`/`Get-Process` Windows asli, bukan asumsi bash berhasil.**
