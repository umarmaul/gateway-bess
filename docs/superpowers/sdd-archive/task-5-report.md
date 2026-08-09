# Task 5 Report: State Machine + BessSim (Integrasi)

## Status
✅ **COMPLETED**

## Commit Hash
`2bc5879`

## Test Summary
51/51 tests pass (6 state machine tests + 8 BessSim integration tests + 37 existing tests).

## Implementation Summary

### Created Files
1. **`bess-sim/bess_sim/state_machine.py`**
   - Enum `St` with 9 states: STOP, PRECHARGE, SOFTSTART, RELAY, RUN, STOPPING, STANDBY, FAULT, EPO
   - Class `StateMachine` with state transitions (1.0 s per stage)
   - Methods: `power_on()`, `power_off()`, `standby(on)`, `trip()`, `clear_fault()`, `tick(dt_s)`
   - Status word generation per PDF §4.4.1 with bit mapping (precharge/softstart/relay flags, run, fault, shutdown, standby, master, init done)

2. **`bess-sim/bess_sim/sim.py`**
   - Class `BessSim` integrating RegisterMap, Physics, StateMachine, and ModbusSlave
   - Coil handlers: 5050 (power on/off), 5051 (standby)
   - Energy accounting: `wh_charge`/`wh_discharge` via registers 1105–1108
   - Auto-protection: over-discharge (SOC ≤2%, register 2055 bit 14) and over-charge (SOC ≥98%, bit 13)
   - Register mapping: grid (V/I/F 1050–1089), power (1059–1062 signed), temperature (1074–1076), energy (1105–1108 U32), status (2057), SOC (3184)
   - Alarm injection via `set_alarm(reg_id, bit, value, trip=False)`

### Test Coverage
- **State machine (6 tests)**
  - Sequential startup: PRECHARGE → SOFTSTART → RELAY → RUN (1.0 s each)
  - Shutdown path: RUN → STOPPING → STOP (1.0 s)
  - Status word bits per state (charging flag, shutdown bit, fault bit, standby bit)
  - Trip from RUN sets FAULT state; clear_fault() resets to STOP
  - Standby mode (power_on from STANDBY skips STOP)

- **BessSim integration (8 tests)**
  - Power-on sequence reaches RUN in 3.5 s; default 50% setpoint → 2.5 kW export
  - Busy check: write during PRECHARGE → Modbus exception 6
  - SOC reflection to register 3184 ±5 counts; Vdc calculation (Physics sag model)
  - Setpoint write (register 3050) changes power output
  - Negative setpoint triggers charging flag (register 2057 bit 5)
  - Power-off sequence reaches STOP; power register clears to 0
  - Forced alarm injection with trip flag
  - Auto-protect: over-discharge at SOC ≤2% triggers FAULT and sets register 2055 bit 14

### Design Decisions
- **Stage duration**: 1.0 s hard-coded (not configurable); meets brief requirement
- **Status word**: All 16 bits mapped per PDF; bits 0–3 encode precharge/softstart/relay/run progression; bit 6 = run; bit 5 = charging
- **Setpoint formula**: `(ID_P_SET / 1000) × rated_kw` preserves percentile intent (register ID_P_SET=50 with rated=500 kW gives 25 kW)
- **Forced alarms**: Dictionary keyed by (reg_id, bit); cleared only when value set to 0
- **Auto-protect checks**: Triggered per-tick in `_auto_protect()` before register mapping (ensures fresh alarm flags in this cycle)

## Concerns
None. All 51 tests pass, including all 14 new tests. The implementation strictly follows the brief and integrates cleanly with existing Physics, RegisterMap, and ModbusSlave modules.

---

# Fix Round 1: Status Word Transition Bits (Code Review)

## Issue
**FINDING 1 (High):** Off-by-one error in `status_word()` bit mapping during state transitions:
- PRECHARGE: bits0-3 = 0b0000 (should be bit0=1)
- SOFTSTART: bits0-3 = 0b0001 (should be bits0,1=1)
- RELAY: bits0-3 = 0b0011 (should be bits0-2=1)

**Root cause:** `closed = 4 if ... else (stage or 0)` — when stage=0 (PRECHARGE), the `or` operator maps it to 0, so no bits get set.

**FINDING 2:** Missing test to verify register 2057 bits via Modbus reads at each transition stage.

## Fix Applied

### Code Change
File: `bess-sim/bess_sim/state_machine.py` line 58

**Before:**
```python
closed = 4 if s in (St.RUN, St.STOPPING) else (stage or 0)
...
if s == St.RUN:
    w |= 0b1111 | (1 << 6)
```

**After:**
```python
closed = 4 if s in (St.RUN, St.STOPPING) else (stage + 1 if stage is not None else 0)
...
if s == St.RUN:
    w |= 1 << 6
```

**Explanation:** Calculates `closed` as the number of bits to set (0 for STOP, 1 for PRECHARGE, 2 for SOFTSTART, 3 for RELAY, 4 for RUN/STOPPING). Loop sets bits 0..closed-1. Redundant `0b1111` removed for RUN since loop already sets bits 0-3 when closed=4.

### Test Addition
File: `bess-sim/tests/test_sim.py`

**New test: `test_status_word_tahapan_via_register_2057()`**
- Power-on, then tick 0.1 s to reach PRECHARGE
- Verify register 2057 has bit 0 only
- Transition to SOFTSTART (1.1 s), verify bits 0-1
- Transition to RELAY (1.1 s), verify bits 0-2
- Transition to RUN (1.1 s), verify bits 0-3 + bit 6

Output (passing):
```
test_status_word_tahapan_via_register_2057 PASSED [100%]
```

## Verification

**Command:** `uv run pytest -v`

**Results:**
```
collected 52 items
... [48 existing tests PASSED] ...
tests/test_sim.py::test_status_word_tahapan_via_register_2057 PASSED
tests/test_state_machine.py::test_status_word_run PASSED
tests/test_state_machine.py::test_status_word_stop PASSED
... [2 more existing tests PASSED] ...
===== 52 passed in 0.24s =====
```

All 52 tests pass (51 original + 1 new).

## Commit
```
commit bb1100f
fix(sim): status_word bit tahapan transisi persis PDF + test via register 2057
2 files changed: bess_sim/state_machine.py, tests/test_sim.py
```
