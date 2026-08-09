# Task 7 Report: Transport serial + CLI

**Status:** SELESAI

**Commit:** `87a3fd3` (feat(sim): transport serial + CLI run/selftest)

**Test Summary:** 57/57 passed; selftest outputs "selftest: LULUS" ✓

**Files Created:**
- `bess-sim/bess_sim/transport.py` — FrameSplitter (gap-based frame splitting), SerialServer (serial loop + frame splitting + sim handling)
- `bess-sim/bess_sim/cli.py` — selftest() (hardware-independent self-test), run() (serial server loop), main() (CLI dispatcher)
- `bess-sim/tests/test_transport.py` — 2 tests (single-frame, multi-frame scenarios)
- `bess-sim/tests/test_cli.py` — 1 test (selftest return code)

**Test Results:**
```
tests/test_transport.py::test_split_by_gap PASSED
tests/test_transport.py::test_dua_frame_terpisah PASSED
tests/test_cli.py::test_selftest_lulus PASSED
[... 54 other tests from existing suite remain PASSED ...]
============================= 57 passed in 0.18s ==============================
```

**Selftest Output:**
```
selftest: state=RUN p_ac=10.0 kW vdc=804.8 V soc=50.0%
selftest: LULUS
```

**Concerns:** None. TDD workflow complete: tests first (FAIL) → implement → all tests PASS → selftest verified → commit.

**Notes:**
- FrameSplitter.feed() correctly accumulates bytes and returns complete frames when gap > gap_s (4 ms).
- SerialServer.run_once() reads from serial, splits frames, calls sim.handle_frame(), enforces 100 ms inter-frame gap (strict_timing mode).
- CLI entry point `bess-sim` already registered in pyproject.toml; commands: `run` (--port, --node, --soc, --scenario, --strict-timing) and `selftest`.
- Selftest validates: state=RUN, power_ac ≈ 10 kW (9.5-10.5), vdc in [700, 910] range — all constraints met.
- No new dependencies added; pyproject.toml already includes pyserial and pyyaml.
