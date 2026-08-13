# Task 5 — Laporan: `set_output` resmi, `set_power` alias, `target` divalidasi

## Step 2 — Tes gagal (bukti dijalankan sebelum implementasi)

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native -f test_native_payload
```

Output relevan (error kompilasi, sesuai harapan brief — field `target` belum ada):
```
test\test_native_payload\main.cpp:160:36: error: no member named 'target' in 'Command'
  160 |     TEST_ASSERT_EQUAL_UINT32(1u, c.target);
      |                                  ~ ^
test\test_native_payload\main.cpp:167:36: error: no member named 'target' in 'Command'
  167 |     TEST_ASSERT_EQUAL_UINT32(2u, c.target);
      |                                  ~ ^
6 warnings and 2 errors generated.
*** [.pio\build\native\test\test_native_payload\main.o] Error 1
Building stage has failed, see errors above. Use `pio test -vvv` option to enable verbose output.
------------ native:test_native_payload [ERRORED] Took 1.49 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_payload  ERRORED   00:00:01.493
================== 1 test cases: 0 succeeded in 00:00:01.493 ==================
```

Kegagalan tepat seperti yang diprediksi brief: compile error karena `struct Command` belum punya member `target`.

## Step 3–4 — Implementasi

- `firmware/lib/bess_core/commands.h`: tambah `uint32_t target;` di `struct Command`, sesudah `bool has_power;`.
- `firmware/lib/bess_core/commands.cpp`: `out.target = doc["args"]["target"] | 1u;` ditambahkan sebelum pengenalan nama command; blok `set_power` diperluas jadi `set_output` (resmi) ATAU `set_power` (alias lama).

## Step 5 — Suite penuh, hijau

Perintah:
```
cd "D:/PT Bima Eco Power/embedded-system/gateway-bess/firmware" && export PATH="/c/Users/legio/bin:$PATH" && pio test -e native
```

Output (ringkas):
```
test\test_native_payload\main.cpp:229: test_parse_enable	[PASSED]
test\test_native_payload\main.cpp:230: test_parse_set_power	[PASSED]
test\test_native_payload\main.cpp:231: test_parse_set_output_nama_resmi	[PASSED]
test\test_native_payload\main.cpp:232: test_parse_target_default_satu	[PASSED]
test\test_native_payload\main.cpp:233: test_parse_target_eksplisit	[PASSED]
test\test_native_payload\main.cpp:234: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:235: test_ack	[PASSED]
test\test_native_payload\main.cpp:236: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:237: test_parse_command_truncation	[PASSED]
------------ native:test_native_payload [PASSED] Took 2.30 seconds ------------

=================================== SUMMARY ===================================
Environment    Test                 Status    Duration
-------------  -------------------  --------  ------------
native         test_native_crc      PASSED    00:00:01.839
native         test_native_decode   PASSED    00:00:01.223
native         test_native_frame    PASSED    00:00:01.228
native         test_native_payload  PASSED    00:00:02.298
================= 28 test cases: 28 succeeded in 00:00:06.586 =================
```

**28 test cases, 28 succeeded** — sesuai harapan. Diulang sekali lagi setelah semua kerja bench selesai (sebelum commit), hasil identik: 28/28.

## Step 6 — Penolakan `target != 1` di `task_cmd.cpp`

Blok disisipkan tepat sesudah `parseCommand(rc.json, rc.len, c);`, sebelum `switch(c.type)`, sehingga `c.id`/`c.name` sudah terisi saat `sendAck` dipanggil (ack menggemakan id yang dikirim cloud). `continue` melompat ke iterasi `for(;;)` berikutnya, melewati `switch`.

## Step 7 — `cloud_probe.py`

`bess-sim/tools/cloud_probe.py` diubah: subparser `set_power` digandakan jadi loop `for nama in ("set_output", "set_power")`, dan pembentukan `args` memakai `a.cmd in ("set_output", "set_power")`.

## Step 8 — Verifikasi bench (firmware di-flash ke COM3, simulator di COM11)

Firmware di-build & di-upload ke COM3 (`pio run -e esp32c6 -t upload --upload-port COM3`) — SUCCESS. Simulator dijalankan dari `bess-sim/`: `uv run bess-sim run --port COM11 --soc 60` (COM11 = USB-SERIAL CH340, ditemukan lewat `Get-CimInstance Win32_PnPEntity`).

Kredensial broker dibaca dari `firmware/src/secrets.h` ke variabel shell (`MQ_USER`, `MQ_PASS`) di command yang sama, tidak pernah di-echo (hanya panjang string yang ditampilkan untuk verifikasi ekstraksi berhasil: 5 & 6 karakter).

### Ack 1 — `set_output --watt 5000`
```
{
 "id": "849ff883",
 "cmd": "set_output",
 "result": "accepted",
 "detail": "",
 "applied": {
  "power_pct": 10,
  "power_w": 5000
 },
 "ts": 1786638818
}
```

### Ack 2 — `set_power --watt 5000` (alias)
Dua percobaan pertama (id `e818664f`, `ab2f62b7`) tidak menerima ack dalam window tangkap 20–40 detik meski `status=online` retained diterima — kemungkinan link WiFi lemah saat itu (RSSI terukur turun sampai -88 dBm di monitor serial sesaat sebelumnya). Percobaan ketiga, sambil memonitor serial COM3 paralel, berhasil:
```
{
 "id": "40c322df",
 "cmd": "set_power",
 "result": "accepted",
 "detail": "",
 "applied": {
  "power_pct": 10,
  "power_w": 5000
 },
 "ts": 1786639031
}
```
Identik dengan ack `set_output` kecuali field `cmd`, sesuai harapan. Baris serial gateway yang berpadanan: `[cmd] set_power -> accepted`.

### Ack 3 — `enable` dengan `args.target = 2` (skrip Python singkat, listener terbukti aktif)
Skrip menunggu `on_subscribe` (bukti langganan terkonfirmasi broker) sebelum publish, lalu mencetak polling log tiap 0,5 dtk sampai ack diterima atau 15 dtk habis:
```
[23:37:54] connected rc=Success, subscribing...
[23:37:54] subscribe confirmed mid=1 -> mengirim command sekarang
[23:37:54] terkirim: {'id': '191ee9ad', 'ts': 1786639074, 'cmd': 'enable', 'args': {'target': 2}, 'api_schema_version': 1}
[23:37:54] menunggu... (0.5s)
[23:37:54] menunggu... (1.0s)
[23:37:55] PESAN DITERIMA di device/58E6C5218C78/command/ack: {"id":"191ee9ad","cmd":"enable","result":"rejected","detail":"bad_value","applied":{},"ts":1786639075}
[23:37:55] menunggu... (1.5s)
HASIL: ack diterima -> {"id":"191ee9ad","cmd":"enable","result":"rejected","detail":"bad_value","applied":{},"ts":1786639075}
```
Ack: `"result":"rejected","detail":"bad_value"` — sesuai harapan brief.

Simulator dan monitor serial dihentikan setelah verifikasi selesai (COM3 & COM11 dilepas).

## Commit

```
git add firmware/lib/bess_core/commands.h firmware/lib/bess_core/commands.cpp firmware/src/task_cmd.cpp firmware/test/test_native_payload/main.cpp bess-sim/tools/cloud_probe.py
git commit -m "feat(fw): set_output jadi nama resmi, set_power alias, target divalidasi ..."
```
Hash: lihat `git log -1 --oneline` sesudah commit (dicatat di ringkasan tugas).

## Hal yang mengejutkan

- `boot_count` naik dari 15 ke 17 (bukan 16) setelah satu kali upload — kemungkinan ada dua siklus boot (reset via RTS pin saat upload, lalu satu boot lagi sebelum WiFi/MQTT stabil). Tidak diselidiki lebih lanjut karena di luar cakupan task ini.
- Dua percobaan pertama `set_power` tidak menerima ack dalam window tangkap (20 dtk lalu 40 dtk) walau `status=online` diterima — RSSI gateway sempat terukur -88 dBm di monitor serial. Bukan bug kode (perilaku sama dengan `set_output` yang sukses di percobaan pertama); kemungkinan besar EMI/WiFi seperti dicatat di CLAUDE.md root, bukan regresi dari perubahan task ini. Percobaan ketiga (dengan monitor serial paralel, RSSI membaik ke -50..-60) sukses dan cocok dengan ack `set_output`.
- File `firmware/build.log` dan `firmware/build_output.log` sudah untracked sejak sebelum task ini dimulai (bukan hasil kerja task 5) — tidak disertakan dalam commit.
