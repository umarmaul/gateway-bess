# Task 6 — Pemangkasan daya + hasil `clamped` — Laporan

## Step 1-2: Tes gagal (bukti TDD)

Empat tes disisipkan di `firmware/test/test_native_payload/main.cpp` (setelah
`test_parse_target_eksplisit`, sebelum `test_parse_command_truncation`), dan
didaftarkan di `main()`.

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Output relevan (gagal kompilasi seperti diharapkan):
```
test\test_native_payload\main.cpp:207:22: error: use of undeclared identifier 'planPowerPct'
  207 |     TEST_ASSERT_TRUE(planPowerPct(5000.0f, 50000.0f, pct, clamped));
      |                      ^~~~~~~~~~~~
test\test_native_payload\main.cpp:214:22: error: use of undeclared identifier 'planPowerPct'
  214 |     TEST_ASSERT_TRUE(planPowerPct(70000.0f, 50000.0f, pct, clamped));
      |                      ^~~~~~~~~~~~
test\test_native_payload\main.cpp:221:22: error: use of undeclared identifier 'planPowerPct'
  221 |     TEST_ASSERT_TRUE(planPowerPct(-70000.0f, 50000.0f, pct, clamped));
      |                      ^~~~~~~~~~~~
test\test_native_payload\main.cpp:229:23: error: use of undeclared identifier 'planPowerPct'
  229 |     TEST_ASSERT_FALSE(planPowerPct(5000.0f, 0.0f, pct, clamped));
      |                       ^~~~~~~~~~~~
6 warnings and 4 errors generated.
*** [.pio\build\native\test\test_native_payload\main.o] Error 1
Building stage has failed, see errors above. Use `pio test -vvv` option to enable verbose output.
------------ native:test_native_payload [ERRORED] Took 1.38 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_payload  ERRORED   00:00:01.379
================== 1 test cases: 0 succeeded in 00:00:01.379 ==================
```

Gagal karena alasan yang benar: fungsi belum dideklarasikan sama sekali (bukan
kegagalan assertion), sesuai ekspektasi brief.

## Step 3: Implementasi

- `firmware/lib/bess_core/commands.h`: tambah `#define POWER_PCT_LIMIT 120.0f`
  dan deklarasi `bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped);`
- `firmware/lib/bess_core/commands.cpp`: implementasi persis sesuai brief —
  `rated_w <= 0` (termasuk NAN) → `false`; hitung `power_w/rated_w*100`, pangkas
  ke `[-120, +120]`, set `clamped` sesuai kejadian pemangkasan.

## Step 4: Suite penuh — hijau

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Output (potongan relevan):
```
test\test_native_payload\main.cpp:249: test_telemetry_envelope	[PASSED]
test\test_native_payload\main.cpp:250: test_reset_reason_name	[PASSED]
test\test_native_payload\main.cpp:251: test_telemetry_diagnostik_boot	[PASSED]
test\test_native_payload\main.cpp:252: test_network_rssi_dbm	[PASSED]
test\test_native_payload\main.cpp:253: test_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:254: test_ts_diteruskan_saat_ntp_sinkron	[PASSED]
test\test_native_payload\main.cpp:255: test_ack_ts_nol_saat_ntp_belum_sinkron	[PASSED]
test\test_native_payload\main.cpp:256: test_parse_enable	[PASSED]
test\test_native_payload\main.cpp:257: test_parse_set_power	[PASSED]
test\test_native_payload\main.cpp:258: test_parse_set_output_nama_resmi	[PASSED]
test\test_native_payload\main.cpp:259: test_parse_target_default_satu	[PASSED]
test\test_native_payload\main.cpp:260: test_parse_target_eksplisit	[PASSED]
test\test_native_payload\main.cpp:261: test_plan_power_dalam_rentang	[PASSED]
test\test_native_payload\main.cpp:262: test_plan_power_dipangkas_atas	[PASSED]
test\test_native_payload\main.cpp:263: test_plan_power_dipangkas_bawah	[PASSED]
test\test_native_payload\main.cpp:264: test_plan_power_rated_belum_diketahui	[PASSED]
test\test_native_payload\main.cpp:265: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:266: test_ack	[PASSED]
test\test_native_payload\main.cpp:267: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:268: test_parse_command_truncation	[PASSED]
------------ native:test_native_payload [PASSED] Took 2.13 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.644
native         test_native_decode   PASSED    00:00:01.088
native         test_native_frame    PASSED    00:00:01.078
native         test_native_payload  PASSED    00:00:02.130
================= 32 test cases: 32 succeeded in 00:00:05.940 =================
```

**32 test cases, 32 succeeded** — sesuai ekspektasi (naik dari 28).

## Step 5: `task_cmd.cpp`

`doSetPower` diganti persis sesuai brief: pakai `planPowerPct`, kirim ack
`"clamped"` kalau dipangkas, `"accepted"` kalau tidak, `applied.power_w`
dihitung dari `applied_pct` (persen sesudah pemangkasan) dikali `rated_w`.
Build `pio run -e esp32c6` sukses (RAM 16.1%, Flash 56.3%).

## Step 6: Verifikasi di bench

Flash ke COM3 sukses. Simulator dijalankan dari `bess-sim`:
`uv run bess-sim run --port COM11 --soc 60` (COM11 = USB-SERIAL CH340,
gateway sendiri di COM3).

Urutan: `enable` (perlu supaya converter benar-benar RUN, bukan STOP) →
`set_output --watt 70000` → tunggu ramp → cek log simulator → `set_output
--watt 5000` → `disable` (bersih-bersih).

### Ack `clamped` untuk 70000 W (verbatim, via `cloud_probe.py`)
```
perintah terkirim: {'id': 'dd48bc1c', 'ts': 1786639624, 'cmd': 'set_output', 'args': {'power_w': 70000.0}, 'api_schema_version': 1}

== device/58E6C5218C78/status ==
online

== device/58E6C5218C78/command/ack ==
{
 "id": "dd48bc1c",
 "cmd": "set_output",
 "result": "clamped",
 "detail": "",
 "applied": {
  "power_pct": 120,
  "power_w": 60000
 },
 "ts": 1786639626
}
```

### Baris log simulator membuktikan clamp sampai ke kabel (verbatim)
```
[  144.1s] RUN       p_ac=+60.00 kW soc= 59.7% vdc= 816.9 V
```
(dan berlanjut stabil di +60.00 kW selama beberapa detik berikutnya — bukan
+70.00 kW. Clamp benar-benar diteruskan ke register 3050, bukan hanya
dilaporkan di ack.)

### Ack `accepted` untuk 5000 W (verbatim)
```
perintah terkirim: {'id': '7b8052ed', 'ts': 1786639659, 'cmd': 'set_output', 'args': {'power_w': 5000.0}, 'api_schema_version': 1}

== device/58E6C5218C78/status ==
online

== device/58E6C5218C78/command/ack ==
{
 "id": "7b8052ed",
 "cmd": "set_output",
 "result": "accepted",
 "detail": "",
 "applied": {
  "power_pct": 10,
  "power_w": 5000
 },
 "ts": 1786639661
}
```

Sesuai ekspektasi: `power_pct: 10`.

Bench kemudian dibersihkan: `disable` dikirim (ack `accepted`), log
simulator kembali ke `STOP p_ac= +0.00 kW`, proses simulator dan probe
dihentikan, port COM3/COM11 dilepas.

## Step 7: Commit

```
git add firmware/lib/bess_core/commands.h firmware/lib/bess_core/commands.cpp firmware/src/task_cmd.cpp firmware/test/test_native_payload/main.cpp
git commit -m "feat(fw): pangkas daya ke +-120% rated dengan hasil ack clamped ..."
```

Hash commit: **`aad0737`** (branch `fondasi-paritas-13aug`).

## Hal yang mengejutkan / catatan

1. **Simulator perlu `enable` eksplisit sebelum daya benar-benar mengalir.**
   Percobaan pertama kirim `set_output 70000` saat BESS masih `STOP` — ack
   `clamped` tetap benar, tapi log simulator tetap `p_ac=+0.00 kW` karena
   converter belum jalan. Ini bukan bug — cuma urutan operasional bench yang
   perlu diikuti (enable dulu, baru set_output), sesuai model command BESS.
2. **Output Python ter-buffer saat di-redirect ke file.** `cloud_probe.py`
   dijalankan dengan `timeout N ... > file` (karena `loop_forever()` tidak
   pernah keluar sendiri); tanpa flag `-u` (unbuffered), isi file kosong
   walau proses sudah menerima balasan — ditambahkan `python -u` supaya ack
   ter-tulis sebelum proses dibunuh oleh `timeout`.
3. Dua file log residual tak terkait task ini (`firmware/build.log`,
   `firmware/build_output.log`) sudah ada sebagai untracked sebelum task
   dimulai — tidak disentuh, tidak ikut di-`git add`.
4. `data.boot_count` tidak diperiksa eksplisit di bench ini (fokus hanya
   `clamped`/`accepted`); tidak ada indikasi reboot tak terduga (satu kali
   flash normal via esptool).

---

## Ronde review 1 — fix: guard NaN asimetris pada `planPowerPct`

**Temuan reviewer:** `rated_w` dijaga terhadap NaN (`!(rated_w > 0.0f)`), tapi
`power_w` tidak. `power_w` NaN membuat `raw` jadi NaN, kedua perbandingan
`raw > LIMIT` / `raw < -LIMIT` bernilai false, fungsi tetap `return true` dengan
`clamped=false` dan `pct=NaN` — lalu `doSetPower` memanggil
`(int16_t)lroundf(NaN * 10.0f)` (UB) dan berpotensi menulis nilai sembarang ke
register daya sambil membalas `"accepted"`. Defence-in-depth pada register
safety-relevant, bukan bug yang terbukti dapat dicapai lewat `parseCommand`
(JSON ketat tidak punya literal NaN).

### Tes baru (ditulis dulu, dijalankan, dibuktikan gagal)

Disisipkan di `firmware/test/test_native_payload/main.cpp` setelah
`test_plan_power_rated_belum_diketahui`, didaftarkan di `main()` setelah
`RUN_TEST(test_plan_power_rated_belum_diketahui);`:

```cpp
static void test_plan_power_nan_ditolak() {
    float pct = 0; bool clamped = false;
    TEST_ASSERT_FALSE(planPowerPct(NAN, 50000.0f, pct, clamped));
}

static void test_plan_power_tepat_di_batas_bukan_clamp() {
    float pct = 0; bool clamped = true;
    TEST_ASSERT_TRUE(planPowerPct(60000.0f, 50000.0f, pct, clamped));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 120.0, pct);
    TEST_ASSERT_FALSE(clamped);
}
```

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Output (gagal, sebelum fix diterapkan):
```
test\test_native_payload\main.cpp:235: test_plan_power_nan_ditolak: Expected FALSE Was TRUE	[FAILED]
test\test_native_payload\main.cpp:243: test_plan_power_tepat_di_batas_bukan_clamp: Expected FALSE Was TRUE	[FAILED]
...
============ 23 test cases: 2 failed, 20 succeeded in 00:00:02.263 ============
```

**Temuan tambahan saat menulis tes batas (di luar instruksi reviewer, tapi
wajib diperbaiki supaya tes batas yang diminta reviewer bisa lulus jujur):**
tes `test_plan_power_tepat_di_batas_bukan_clamp` gagal bukan karena guard NaN,
tapi karena presisi `float`. Dibuktikan lewat perhitungan `numpy.float32`:
`60000.0f/50000.0f*100.0f = 120.00000762939453` — sedikit di atas
`POWER_PCT_LIMIT` akibat `60000/50000` (=1,2) tidak representable persis di
biner — sehingga perbandingan ketat `raw > 120.0f` salah menganggap tepat 120%
sebagai kelebihan dan memangkasnya. Diverifikasi ulang bahwa menghitung di
presisi `double` sebelum membagi (bukan sesudah) menghasilkan `120.0` persis
(`(double)60000.0f/(double)50000.0f*100.0 == 120.0`), sehingga perbaikannya
bukan mengubah `>`/`<` jadi `>=`/`<=` (itu justru akan menyembunyikan
perbandingan-tepat dari kasus lain), melainkan menghitung `raw` di presisi
`double`.

### Perbaikan

`firmware/lib/bess_core/commands.cpp`, `planPowerPct`:
```cpp
bool planPowerPct(float power_w, float rated_w, float& pct, bool& clamped) {
    if (!(rated_w > 0.0f)) return false;     // juga menangkap NAN
    if (isnan(power_w)) return false;        // NaN power_w tidak boleh lolos jadi pct NaN
    // Hitung di presisi double: pembagian float saja membulatkan 60000/50000*100
    // jadi ~120,00000763 (bukti float32), sehingga batas TEPAT 120% salah
    // terpangkas oleh perbandingan ketat di bawah. Presisi double menghindarinya.
    double raw_d = (double)power_w / (double)rated_w * 100.0;
    clamped = false;
    if (raw_d > (double)POWER_PCT_LIMIT) { raw_d = POWER_PCT_LIMIT; clamped = true; }
    if (raw_d < -(double)POWER_PCT_LIMIT) { raw_d = -POWER_PCT_LIMIT; clamped = true; }
    pct = (float)raw_d;
    return true;
}
```

`firmware/lib/bess_core/commands.h`, komentar di atas deklarasi diperluas
untuk menyebut kedua input diperiksa.

`firmware/src/task_cmd.cpp`, `doSetPower`, tepat sesudah cek `!c.has_power`:
```cpp
    if (isnan(c.power_w)) { sendAck(c, "rejected", "bad_value"); return; }
```

### Suite penuh — hijau, 34 tes

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Output (potongan relevan):
```
test\test_native_payload\main.cpp:275: test_plan_power_dalam_rentang	[PASSED]
test\test_native_payload\main.cpp:276: test_plan_power_dipangkas_atas	[PASSED]
test\test_native_payload\main.cpp:277: test_plan_power_dipangkas_bawah	[PASSED]
test\test_native_payload\main.cpp:278: test_plan_power_rated_belum_diketahui	[PASSED]
test\test_native_payload\main.cpp:279: test_plan_power_nan_ditolak	[PASSED]
test\test_native_payload\main.cpp:280: test_plan_power_tepat_di_batas_bukan_clamp	[PASSED]
test\test_native_payload\main.cpp:281: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:282: test_ack	[PASSED]
test\test_native_payload\main.cpp:283: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:284: test_parse_command_truncation	[PASSED]
------------ native:test_native_payload [PASSED] Took 2.41 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_payload  PASSED    00:00:02.407
================= 34 test cases: 34 succeeded in 00:00:06.701 =================
```

**34 test cases, 34 succeeded** (naik dari 32).

`pio run -e esp32c6` dijalankan ulang untuk memastikan kode yang diubah
(termasuk `task_cmd.cpp`) tetap kompilasi bersih untuk target ESP32 — SUCCESS,
RAM 16.1%, Flash 56.3%. **Tidak di-flash ulang dan tidak ada run bench baru**
sesuai instruksi reviewer (perbaikan ini hanya menambah jalur penolakan untuk
input yang tak bisa dihasilkan bench).

### Commit

Files: `firmware/lib/bess_core/commands.h`, `firmware/lib/bess_core/commands.cpp`,
`firmware/src/task_cmd.cpp`, `firmware/test/test_native_payload/main.cpp`.
Hash: **`77b0d4a`** (branch `fondasi-paritas-13aug`).
