# Laporan fix wave — review akhir seluruh branch (fondasi-paritas-13aug)

Tanggal: 14 Agustus 2026. Tiga temuan dari review akhir branch, semuanya diperbaiki dalam
satu putaran.

## Finding 1 (Important) — loop retry tidak keluar saat bus mati

**Perbaikan.** `firmware/src/task_cmd.cpp`, `doOnOff`: kondisi keluar loop diubah dari
`if (st == MB_OK || (st == MB_EXCEPTION && exc != 6)) break;` menjadi
`if (st != MB_EXCEPTION || exc != 6) break;` — plus komentar yang menjelaskan kenapa.
Sekarang: `MB_OK` keluar, exception non-busy keluar, timeout/CRC/frame-cacat keluar, hanya
exception busy (06) yang mengulang.

**Bukti bench (wajib, terpenuhi).** Board di-reflash (memicu hard-reset via RTS oleh
esptool — terbukti bekerja; percobaan awal memakai pulsa RTS manual pada koneksi pyserial
lama GAGAL mereset karena board native-USB-CDC re-enumerate saat reset, sesuai catatan di
spec §3.2). Simulator BESS **tidak dijalankan** (bus mati). Command `enable` dipublish
begitu gateway sendiri melaporkan `[mqtt] connected` di serial (~15,7 dtk pasca reset, di
bawah ambang `comm_lost` ~25 dtk sehingga `comm_lost` masih `false` — dikonfirmasi baris
serial `[bess] OK ...` bukan `COMM_LOST` pada siklus itu).

Hasil terukur:
```
[ 15.671s] publish enable id=f2daa60a
[ 18.256s] [serial] [cmd] enable -> rejected bess_no_ack
[ 18.719s] [mqtt] ACK diterima: {"id":"f2daa60a","cmd":"enable","result":"rejected","detail":"bess_no_ack",...}
[ 18.744s] LATENSI publish->ack = 3.049 dtk
```
Waktu tangani internal (publish → baris `[cmd] enable -> rejected bess_no_ack` di serial)
= **2,585 dtk**, konsisten dengan estimasi "±2 dtk" (satu putaran `mbWrite5`: 3 percobaan ×
(105 ms jeda + 500 ms timeout) ≈ 1,8 dtk + overhead). Latensi total publish→ack (termasuk
MQTT + serial print) = **3,049 dtk**. Sebelum perbaikan, kode lama akan menjalankan sampai
20 iterasi × ~2,3 dtk (1,8 dtk `mbWrite5` + 500 ms delay) ≈ **46 dtk minimum**, dan makin
lama dengan mutex Modbus yang sekarang dipegang `task_bess` ~7,2 dtk per siklus (Finding 3,
Task 9). Sekarang tersedia dalam hitungan detik.

## Finding 2 (Important) — premis salah di catatan Task 1/3

**Verifikasi fakta.** `diff` byte-per-byte `firmware/partitions.csv` vs
`framework-arduinoespressif32/tools/partitions/default.csv` bawaan toolchain:
`nvs` (`0x9000,0x5000`), `otadata` (`0xe000,0x2000`), `coredump` (`0x3F0000,0x10000`)
**identik** di kedua tabel. Hanya `app0` (ukuran), `app1` (offset+ukuran), dan `spiffs`
(offset+ukuran) yang berubah.

**Perbaikan (dokumentasi saja, tanpa kode).**
- `docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md` §3.1 — justifikasi erase
  penuh diganti: bukan karena `nvs`/`otadata` berpindah (tidak berpindah), melainkan
  kehati-hatian saat memperbesar `app0`/menggeser `app1`/`spiffs`. Klaim "partisi coredump
  jadi berguna karena tabel baru ini" juga dikoreksi — partisi itu sudah ada persis di
  offset/ukuran yang sama sebelum spec ini.
- Blok **KOREKSI** ditambahkan di §7 (setelah paragraf risiko terakhir), menjelaskan bahwa
  `pio run -t erase` di Task 1 kemungkinan menghapus coredump yang sudah ada dari kejadian
  reboot 13 Agustus SEBELUM Task 3 sempat membacanya — sehingga coredump kosong yang
  ditemukan Task 3 bukan bukti "belum ada partisi saat kejadian", melainkan kemungkinan
  bukti yang sudah dihapus oleh langkah kita sendiri.
- `.superpowers/sdd/2026-08-13-fondasi-paritas/progress.md` (gitignored, tidak ikut commit
  — pola yang sama dengan gateway-v2) — entri KOREKSI ditambahkan **di bawah** baris Task 3
  asli (baris asli tidak dihapus/diubah), merujuk balik ke spec §3.1/§7.

Tidak ada perubahan kode untuk finding ini; commit `be760d3` (Task 1) dan pesannya
dibiarkan apa adanya sesuai instruksi.

## Finding 3 (Minor, cloud-facing) — README meremehkan latensi comm_lost

**Perbaikan.** `firmware/README.md`:
- Baris "Prasyarat" (dekat contoh log boot): `[bess]` "akan langsung berkata COMM_LOST"
  diganti "akan berkata COMM_LOST (bukan macet) setelah **~25 dtk**".
- Bagian `## comm_lost`: ditambah paragraf yang menjelaskan biaya waktu nyata — sejak
  short-circuit dihapus (Task 9), satu siklus poll saat bus mati mencoba keempat blok
  register penuh (~7,2 dtk), tiga siklus gagal beruntun + jeda antar-siklus ⇒ **≈24-25 dtk**
  (naik dari ~8-9 dtk sebelumnya), dan menegaskan tim cloud harus memakai angka ini untuk
  menyetel timeout command.

Tidak perlu verifikasi runtime terpisah untuk finding ini — namun angka ~25 dtk yang
dituliskan di sini **terbukti sendiri secara independen** oleh log bench Finding 1 di atas:
baris `[bess] COMM_LOST` pertama kali muncul pada **25,025 dtk** pasca reset (siklus gagal
ke-3, sesudah dua siklus "OK" pada 7,58 dtk dan 16,38 dtk — jeda antar-siklus ≈8,7-8,8 dtk),
persis cocok dengan angka yang dicatat di README dan di ledger Task 9.

## Verifikasi

- `pio test -e native`: **36/36 PASSED** (sebelum dan sesudah fix, tidak ada regresi).
- `pio run -e esp32c6`: **SUCCESS** (RAM 16,1%, Flash 56,4% — tidak berubah dari sebelum
  fix, sesuai ekspektasi karena perubahan hanya satu kondisi `if`).
- Bench Finding 1: lihat di atas — `bess_no_ack` dalam 3,049 dtk (vs ≥46 dtk sebelum fix).
- File yang dibaca ulang untuk Finding 2/3 dan baris yang dicek:
  - `firmware/partitions.csv` (baris 4-9) vs
    `C:\Users\legio\.platformio\packages\framework-arduinoespressif32\tools\partitions\default.csv`
    (diff langsung, bukan dari ingatan).
  - `docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md` §3.1 (baris 73-99) dan §7
    (baris 219-229 sebelum edit) — dibaca penuh sebelum menulis koreksi.
  - `firmware/README.md` baris 63-68 dan 196-202 (sebelum edit) — dibaca penuh, dikonfirmasi
    tidak ada penyebutan lain soal latensi `comm_lost` di file ini (`grep` "comm_lost").
  - `.superpowers/sdd/2026-08-13-fondasi-paritas/progress.md` baris 27-38 (blok Task 3 asli)
    dibaca sebelum menambah koreksi di bawahnya.

COM3 dilepas setelah pengujian (dikonfirmasi bisa dibuka ulang oleh proses lain). Tidak ada
proses `python`/`uv` yang tersisa.

## Commit

Satu commit `fix(fw): keluar retry enable/disable segera saat bus mati` mencakup fix
kode (Finding 1) + README (Finding 3). Spec design (Finding 2) dan progress.md (gitignored)
dikoreksi terpisah dari commit kode karena murni dokumentasi/ledger.
