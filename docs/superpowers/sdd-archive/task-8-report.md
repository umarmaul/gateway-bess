# Task 8 Report: Master Probe (Alat Uji Modbus dari Laptop)

**Status:** ✅ SELESAI

**Commit hash:** `0c6d1e8`

**Test summary:** 57 pytest PASSED (0 failures, no import regressions)

## Implementasi

Dibuat file `bess-sim/tools/master_probe.py` sesuai brief — tool Modbus master mandiri untuk menguji simulator atau device BESS dari laptop via serial kedua.

### Fitur:
- **Perintah:** `status`, `on`, `off`, `setp --pct N`, `read --start N --count N`
- **Interface:** Konsumsi `bess_sim.crc` (append_crc/check_crc) dan `bess_sim.alarms.STATUS_BITS`
- **Frame:** Modbus RTU FC3/FC5/FC6 dengan CRC16, register status 2057 (`0x0809`), setpoint kontrol mode 0x0BEA
- **Keamanan:** No real serial port opened — tool hanya dibuat, test manual saat ada dua port COM

### Frame yang dibangun:
- **ON (FC5 coil set):** `[node, 5, 0x13, 0xBA, 0xFF, 0x00]`
- **OFF (FC5 coil clear):** `[node, 5, 0x13, 0xBA, 0x00, 0x00]`
- **SETP (FC6 write):** `[node, 6, 0x0B, 0xEA, pct_hi, pct_lo]` (pct × 10 → u16)
- **STATUS (FC3 read):** `[node, 3, 0x08, 0x09, 0x00, 0x01]` → decode STATUS_BITS
- **READ (FC3 generic):** `[node, 3, start_hi, start_lo, count_hi, count_lo]`

### Mekanisme:
- Jeda 110 ms antar-frame (asli ≥100 ms)
- CRC check otomatis pada balasan
- Exception handling (0x80 di byte 1 = function code+0x80 → print error)
- Timeout serial 50 ms, read 300 byte max

## Verifikasi

```
============================= test session starts =============================
...
tests/test_crc.py ✓ | tests/test_modbus_slave.py ✓ | tests/test_physics.py ✓ |
tests/test_registers.py ✓ | tests/test_scenario.py ✓ | tests/test_sim.py ✓ |
tests/test_state_machine.py ✓ | tests/test_transport.py ✓ | tests/test_cli.py ✓
============================= 57 passed in 0.17s ================================
```

Tidak ada regresi import; semua test bess-sim tetap PASSED.

## Concerns

**Tidak ada.** Tool siap pakai saat ada dua port serial COM. Frame Modbus sudah teruji di Task 1–3 (modbus_slave.py). Implementasi mengikuti VERBATIM dari brief.

---

**Log:** `git add bess-sim/tools && git commit -m "feat(sim): master_probe alat uji Modbus dari laptop"` ✓
