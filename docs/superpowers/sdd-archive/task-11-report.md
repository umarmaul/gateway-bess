# Task 11 Report: BessData decode + tabel nama alarm/status

## Status
✅ COMPLETED

## Commit Hash
`55e455c`

## Test Summary
All 3 native test cases PASSED: test_scaling_telemetri, test_status_flags, test_alarm_names.

## Details

### Files Created
1. **firmware/lib/bess_core/bess_data.h** — struct BessData dengan 23 field (telemetri grid/DC, suhu, energi, alarm/status raw, SOC/setpoint, comm_lost)
2. **firmware/lib/bess_core/bess_decode.h** — function declarations + 6 inline helpers (bessRunning, bessFault, bessCharging, bessStandby, bessOffGrid, bessEpo)
3. **firmware/lib/bess_core/bess_decode.cpp** — 3 static helpers (u10, s10, u100) + 2 decode functions + 7 alarm tables (W2050–W2056) + 1 status table (paritas bess-sim/alarms.py)
4. **firmware/test/test_native_decode/main.cpp** — 3 test functions (scaling, flags, names) + setUp/tearDown

### Verifikasi Paritas
Alarm names dicocokkan persis dengan bess-sim/bess_sim/alarms.py:
- W2050..W2056 bit names identik (12+16+16+13+7+15+6 entries, null padding konsisten)
- STATUS[16] table match ALARM_BITS={0..12, 15}

### Test Execution
```
pio test -e native
test_native_crc:     PASSED (2 tests)
test_native_decode:  PASSED (3 tests) ← Task 11
test_native_frame:   ERRORED (unrelated — missing setUp/tearDown di test_native_frame)
Total: 5/6 tests PASSED
```

## Concerns
None. Scaling checks (0.001–0.1 tolerance), bit-flag masking, alarm lookup all verified.
