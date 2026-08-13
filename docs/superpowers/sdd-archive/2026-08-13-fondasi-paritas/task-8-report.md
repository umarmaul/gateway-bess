# Task 8 Report: Perbaikan rollover `millis()` di wifiTick

## Status
DONE

## Commits
1. `341ba5f fix(fw): wifiTick tahan rollover millis()`
2. `3b62a3d fix(fw): jaga next_try_ms tetap segar saat connected`

## Test Results

### Failing Test Output (sebelum implementasi)
```
test\test_native_payload\main.cpp:263:22: error: use of undeclared identifier 'timeAfter'
test\test_native_payload\main.cpp:264:22: error: use of undeclared identifier 'timeAfter'
test\test_native_payload\main.cpp:265:23: error: use of undeclared identifier 'timeAfter'
test\test_native_payload\main.cpp:271:22: error: use of undeclared identifier 'timeAfter'
test\test_native_payload\main.cpp:272:23: error: use of undeclared identifier 'timeAfter'
6 warnings and 5 errors generated.
```

### Passing Test Output (setelah implementasi)
```
test\test_native_payload\main.cpp:299: test_time_after_biasa	[PASSED]
test\test_native_payload\main.cpp:300: test_time_after_rollover	[PASSED]

================= 36 test cases: 36 succeeded in 00:00:06.346 =================
```

Test suite berhasil: 36 tests all passed (naik dari 34 ke 36, sesuai rencana).

## Boot Verification

WiFi connection berhasil dalam hitungan detik setelah boot:

```
[wifi] OK rssi=-44 ip=192.168.18.52
[bess] COMM_LOST p=0.0kW soc=0.0% vdc=0.0V status=0x0000
[wifi] OK rssi=-45 ip=192.168.18.52
[mqtt] connected
```

Pesan `[wifi] OK` muncul pada output pertama, mengindikasikan WiFi reconnect scheduler berfungsi normal setelah flash firmware.

## Changes Made

1. **firmware/lib/bess_core/timeutil.h**: Tambah deklarasi `bool timeAfter(uint32_t now, uint32_t deadline);`
2. **firmware/lib/bess_core/timeutil.cpp**: Implementasi dengan selisih bertanda `(int32_t)(now - deadline) >= 0;`
3. **firmware/src/wifi_mgr.cpp**: 
   - Tambah `#include "timeutil.h"`
   - Ganti `if (now >= next_try_ms)` dengan `if (timeAfter(now, next_try_ms))`
4. **firmware/test/test_native_payload/main.cpp**: 
   - Tambah `#include "timeutil.h"`
   - Tambah dua test cases: `test_time_after_biasa()` dan `test_time_after_rollover()`
   - Daftarkan tests di `main()`

## Catatan

- TDD diterapkan: tests ditulis terlebih dahulu dan gagal sebelum implementasi
- Tidak ada test yang pass di run pertama (sesuai harapan)
- Rollover behavior tidak bisa diuji di bench (butuh 49 hari uptime) — oleh karena itu native tests adalah verifikasi utama
- WiFi tetap reconnect normal post-flash, mengkonfirmasi tidak ada regresi pada behavior sehari-hari

---

## Temuan & Perbaikan Lanjutan: Bug Residual (Commit 2)

Coordinator menemukan varian lain dari bug kelas sama: saat gateway **terkoneksi lama** (24,8+ hari), `next_try_ms` tetap basi di masa lalu. Bila koneksi putus setelah itu, selisih `now - next_try_ms` melewati jendela 2^31 ms yang diperlukan `timeAfter()` untuk bekerja benar, sehingga percobaan reconnect pertama gagal dievaluasi.

### Perbaikan
Di `firmware/src/wifi_mgr.cpp`, tambah `next_try_ms = millis();` di branch connected:
```cpp
if (WiFi.status() == WL_CONNECTED) {
    backoff_ms = 4000;
    // Jaga next_try_ms tetap dekat dengan waktu sekarang. Kalau dibiarkan
    // basi berminggu-minggu, selisihnya melewati jendela 2^31 ms yang
    // dibutuhkan timeAfter() dan percobaan reconnect pertama sesudah putus
    // akan gagal dievaluasi — varian dari bug rollover yang sama.
    next_try_ms = millis();
    return;
}
```

### Verifikasi Konsekuensi
- **Dengan deadline = "just now"**, evaluasi pertama setelah disconnect: `timeAfter(now, next_try_ms)` ≈ true
- Percobaan reconnect start **segera tanpa menunggu** — ini **diinginkan** karena `backoff_ms` baru saja direset ke 4000
- Ladder backoff tetap mulai dari 4s: `next_try_ms = now + backoff_ms` (baris 26 di disconnect path)
- Tidak ada tight retry loop: setelah percobaan pertama, backoff exponential dimulai normal

### Test & Boot Check
```
Native suite rerun (post-fix):
================= 36 test cases: 36 succeeded in 00:00:04.851 =================

Boot output (dtk-free serial reader):
[wifi] OK rssi=-40 ip=192.168.18.52
```

- Semua 36 native tests **tetap PASS** (fix tidak punya permukaan testable di native, existing tests adalah regression net)
- Boot check: `[wifi] OK` muncul segera — WiFi reconnect tetap berfungsi normal, tanpa tight loop atau delay aneh
- **24.8-hari scenario tidak bisa diobservasi di bench** — fix divalidasi via reasoning dan regression test
