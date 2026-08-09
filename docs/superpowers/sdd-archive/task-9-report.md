# Task 9 Report: Scaffold PlatformIO + CRC16 (native test)

## Status
✅ COMPLETED (with fix round 1 & 2 applied)

## Commit Hashes
- Initial: `c4fbde89202951195fbd35fb04ca612963d3cc9e`
- Fix round 1: `a41575abb260b309e5c763b77b52b7a268d95762` (incorrect structure, flat file workaround)
- Fix round 2: `ed8703bd260514189690de309e2dad0c7a5d9a3e` (corrected: test_native_crc folder pattern)

## Test Summary
2 test cases passed (test_vectors_pdf + test_append_check), CRC16 Modbus RTU verified against PDF vectors.

## Details

### Files Created (Final)
- `firmware/platformio.ini` — env:esp32c6 + env:native with Unity framework
- `firmware/.gitignore` — excludes .pio/ and src/secrets.h
- `firmware/lib/bess_core/crc16.h` — interface declarations (mbCrc16, mbAppendCrc, mbCheckCrc)
- `firmware/lib/bess_core/crc16.cpp` — Modbus RTU CRC16 implementation (0xA001 polynomial, LSB-first)
- `firmware/test/test_native_crc/main.cpp` — native unit tests (per-suite pattern with test_ prefix)

### Test Execution (Final - Fix Round 2)
```
pio test -e native
Verbosity level can be increased via `-v, -vv, or -vvv` option
Collected 1 tests

Processing test_native_crc in native environment
Building...
Library Manager: Installing bblanchon/ArduinoJson @ ^7.0.4
Library Manager: Installing throwtheswitch/Unity @ ^2.6.1
Testing...
test\test_native_crc\main.cpp:33: test_vectors_pdf	[PASSED]
test\test_native_crc\main.cpp:34: test_append_check	[PASSED]
-------------- native:test_native_crc [PASSED] Took 1.92 seconds --------------

=================================== SUMMARY ===================================
Environment    Test             Status    Duration
-------------  ---------------  --------  ------------
native         test_native_crc  PASSED    00:00:01.924
================== 2 test cases: 2 succeeded in 00:00:01.924 ==================
```

### Key Implementation Notes
1. **CRC polynomial**: 0xA001 (Modbus RTU standard, reflected)
2. **Initial value**: 0xFFFF
3. **Append format**: Low byte (LSB) at buf[len], high byte (MSB) at buf[len+1]
4. **Check validation**: Requires minimum 4 bytes (payload + 2 CRC bytes)
5. **Test vectors**: All 3 PDF vectors pass; append/check cycle verified

### Technical Decisions (Initial + Fix Round 1 & 2)
- **Library.json in lib/bess_core/** — removed (not required for build to work)
- **lib_compat_mode = off** — removed (not required for native platform)
- **framework = (empty)** — removed (native platform doesn't use framework directive)
- **test_build_src = false** — KEPT (required; ensures tests only compile lib/, not src/)
- **Unity lib_deps** — KEPT (required; PlatformIO auto-installs for testing, kept explicit for clarity)
- **Test folder naming** — **CORRECTED in Fix Round 2**: PlatformIO DOES support per-suite pattern `test/<suite>/main.cpp`, but ONLY when folder name starts with `test_` prefix (default test filter = `test_*`). Correct structure is `test/test_native_crc/main.cpp`, NOT `test/native_crc/main.cpp`.
- **setUp()/tearDown()** stubs — required by Unity framework

### Final platformio.ini [env:native]
```ini
[env:native]
platform = native
build_flags = -std=gnu++17 -DNATIVE_TEST
lib_deps =
    bblanchon/ArduinoJson@^7.0.4
    throwtheswitch/Unity@^2.6.1
test_build_src = false
```

### Future Test Suites (Tasks 10-12)
This structure supports multiple test suites via per-folder isolation:
- `test/test_native_crc/main.cpp` (CRC16, Task 9 — complete)
- `test/test_native_frame/main.cpp` (Frame parsing, Task 10)
- `test/test_native_decode/main.cpp` (Payload decoding, Task 11)
- `test/test_native_payload/main.cpp` (Payload encoding, Task 12)

Each folder with `test_` prefix is discovered and built as a separate test suite by PlatformIO.

## Concerns
**CORRECTED:** Earlier report stated PlatformIO native platform does not support per-suite pattern — this was incorrect. The platform fully supports it via the `test/<suite>/main.cpp` pattern where `<suite>` MUST start with `test_` prefix. This is the default filter for test discovery. No concerns with final structure.
