# Task 12 Report: Payload Telemetri JSON + Parser Command & ACK

**Status:** COMPLETE + FIX ROUND 1

## Initial Implementation

**Commit Hash:** `9e33b38`

**Test Summary:** All 17 tests pass (12 existing + 5 new payload tests: telemetry_envelope, parse_enable, parse_set_power, parse_unsupported_dan_bad_json, ack).

## Fix Round 1 (Reviewer Findings)

**Finding 1 (Medium, wajib):** buildTelemetryJson kontrak truncation salah  
- Issue: ArduinoJson v7 `serializeJson()` mengembalikan `cap` (bukan 0) ketika buffer kurang, dan hasil TIDAK ber-NUL-terminator
- Fix: Gunakan `measureJson()` sebelum `serializeJson()`, return 0 jika `need + 1 > cap`
- Commit: `5584288` - `fix(fw): buildTelemetryJson kembalikan 0 saat buffer kurang + test truncation`

**Finding 2 (Low, done sekalian):** Tambah test truncation handling  
- Buffer kecil (256 byte) → buildTelemetryJson return 0 ✓
- Command id/name truncation → tidak overflow, ter-NUL-terminate dengan benar ✓

**Test Output (semua 19 pass):**
```
Processing test_native_payload in native environment
...
Testing...
test\test_native_payload\main.cpp:116: test_telemetry_envelope	[PASSED]
test\test_native_payload\main.cpp:117: test_parse_enable	[PASSED]
test\test_native_payload\main.cpp:118: test_parse_set_power	[PASSED]
test\test_native_payload\main.cpp:119: test_parse_unsupported_dan_bad_json	[PASSED]
test\test_native_payload\main.cpp:120: test_ack	[PASSED]
test\test_native_payload\main.cpp:121: test_telemetry_buffer_too_small	[PASSED]
test\test_native_payload\main.cpp:122: test_parse_command_truncation	[PASSED]
------------ native:test_native_payload [PASSED] Took 1.92 seconds ------------

================= 19 test cases: 19 succeeded in 00:00:06.066 =================
```

**Perintah:**
```bash
cd firmware && pio test -e native
```

**Concerns:** None. MSVC deprecation warnings for strcpy/strncpy are harmless (Windows MSVC-ism, not affecting functionality or embedded deployment).
