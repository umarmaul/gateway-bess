# Gateway BESS + Simulator — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Simulator BESS (BSL AC series, Modbus RTU slave) di laptop + firmware gateway ESP32-C6 baru (Modbus master + MQTT) sehingga fungsi ekspor/enable/disable/set-power setara sistem DCON berjalan end-to-end di bench.

**Architecture:** Monorepo `gateway-bess/` berisi `bess-sim/` (Python, uv, pyserial — slave Modbus di COM10) dan `firmware/` (PlatformIO pioarduino ESP32-C6 — master Modbus di UART ex-DCON, esp-mqtt ke broker dev). Kode murni (CRC, frame, decode, payload, command) dipisah dari kode hardware supaya bisa diuji native tanpa device.

**Tech Stack:** Python 3.12 + uv + pyserial + pyyaml + pytest · PlatformIO + pioarduino `53.03.13` + Arduino framework + esp-mqtt (IDF, tersedia di arduino-esp32 3.x) + ArduinoJson 7 + Unity (native test).

**Spec:** `docs/superpowers/specs/2026-08-09-gateway-bess-design.md` — baca dulu.

## Global Constraints

- **DILARANG membuka/meniru kode `gateway-v2/`.** Referensi boleh: PDF BSL & C109 (di root repo ini), `bench-sim/` (pola), `BEPESP32_WiFi_Extension/` (fakta pin), `docs/17` (kontrak cloud).
- Modbus: **9600 bps, 8N1**, slave node **1**, jeda antar-frame **≥100 ms**, CRC16 Modbus urutan **Low-High**.
- Error frame slave: **01** fungsi tak dikenal · **02** ID di luar range / tak writable · **03** format/CRC/range nilai · **06** busy.
- Tanda daya: **positif = ekspor/discharge, negatif = charge** (konvensi C109). Reg `3050` = 0,1% × rated (`3146`, raw 500 = 50,0 kW).
- Pack C109: **108,86 kWh · 806,4 V nominal (705,6–907,2 V) · 135 Ah · efisiensi ~97%**.
- Pin gateway (fakta dari `BEPESP32_WiFi_Extension/src/Config.h`): RS485 ex-DCON **RX=21, TX=20, RE/DE=22**; LED DCON=18, LED WiFi=14, BOOT=9.
- MQTT: broker dev `mqtt-dev.bepbatt.id:1883`, topic `device/<gw>/telemetry|status|command|command/ack`, envelope V11, `data.device_type="bess"`, telemetri tiap 60 s, keepalive 300 s, network timeout 60 s, telemetri via `esp_mqtt_client_enqueue`.
- Ack: `{id, cmd, result: accepted|clamped|rejected, detail, applied, ts}` (bentuk sama docs/17 §5).
- Rahasia (`secrets.h`, kredensial WiFi/MQTT) **tidak pernah di-commit**; sediakan `secrets.example.h`.
- Python: package `bess_sim`, semua test `uv run pytest` dari `bess-sim/`. Firmware: test native `pio test -e native` dari `firmware/`.
- Commit kecil & sering di repo `gateway-bess/` (sudah `git init`, branch `master`).

---

## Bagian 1 — Simulator `bess-sim/` (Task 1–8)

### Task 1: Scaffold proyek + CRC16

**Files:**
- Create: `bess-sim/pyproject.toml`, `bess-sim/bess_sim/__init__.py`, `bess-sim/bess_sim/crc.py`
- Test: `bess-sim/tests/test_crc.py`

**Interfaces:**
- Produces: `crc16(data: bytes) -> int` (nilai integer CRC, byte rendah = dikirim duluan), `append_crc(frame: bytes) -> bytes`, `check_crc(frame: bytes) -> bool` (frame utuh termasuk 2 byte CRC di ekor).

- [ ] **Step 1: Scaffold**

`bess-sim/pyproject.toml`:

```toml
[project]
name = "bess-sim"
version = "0.1.0"
description = "Simulator BESS BSL AC series (Modbus RTU slave) — PT Bima Eco Power"
requires-python = ">=3.12"
dependencies = ["pyserial>=3.5,<4", "pyyaml>=6,<7"]

[project.scripts]
bess-sim = "bess_sim.cli:main"

[dependency-groups]
dev = ["pytest>=8,<9"]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["bess_sim"]

[tool.pytest.ini_options]
testpaths = ["tests"]
```

Buat `bess_sim/__init__.py` kosong dan `tests/` kosong. Jalankan `uv sync` di `bess-sim/`.

- [ ] **Step 2: Test CRC gagal dulu** — `tests/test_crc.py`. Test vector diambil **langsung dari contoh di PDF protokol** (§4.2.2, §4.5.3, §4.7.1, §4.4.2):

```python
from bess_sim.crc import crc16, append_crc, check_crc

# (frame tanpa CRC, dua byte CRC persis contoh PDF: low dulu)
VECTORS = [
    (bytes.fromhex("0103041A0003"), bytes.fromhex("253C")),  # read 1050 x3
    (bytes.fromhex("010306089808980898"), bytes.fromhex("8404")),  # jawaban
    (bytes.fromhex("01060BEA03E8"), bytes.fromhex("AAA4")),  # FC6 3050=1000
    (bytes.fromhex("010513BAFF00"), bytes.fromhex("A95B")),  # FC5 5050 ON
    (bytes.fromhex("010308020001"), bytes.fromhex("27AA")),  # read 2050 x1
    (bytes.fromhex("0103020400"), bytes.fromhex("BA84")),  # jawaban status
]

def test_crc16_vectors_pdf():
    for frame, crc in VECTORS:
        v = crc16(frame)
        assert bytes([v & 0xFF, v >> 8]) == crc, frame.hex()

def test_append_and_check():
    for frame, crc in VECTORS:
        full = append_crc(frame)
        assert full == frame + crc
        assert check_crc(full)
        assert not check_crc(full[:-1] + bytes([full[-1] ^ 0xFF]))

def test_check_too_short():
    assert not check_crc(b"\x01")
```

- [ ] **Step 3: Jalankan, pastikan FAIL** — `uv run pytest tests/test_crc.py -v` → FAIL (`ModuleNotFoundError`).

- [ ] **Step 4: Implementasi** — `bess_sim/crc.py`:

```python
"""CRC16 Modbus RTU (polinomial A001, init 0xFFFF, urutan kirim Low-High)."""

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def append_crc(frame: bytes) -> bytes:
    v = crc16(frame)
    return frame + bytes([v & 0xFF, v >> 8])

def check_crc(frame: bytes) -> bool:
    if len(frame) < 4:
        return False
    return crc16(frame[:-2]) == frame[-2] | (frame[-1] << 8)
```

- [ ] **Step 5: Test PASS** — `uv run pytest tests/test_crc.py -v` → 3 passed.

- [ ] **Step 6: Commit**

```bash
git add bess-sim
git commit -m "feat(sim): scaffold bess-sim + CRC16 Modbus dengan vector dari PDF"
```

### Task 2: Register map lengkap

**Files:**
- Create: `bess-sim/bess_sim/registers.py`
- Test: `bess-sim/tests/test_registers.py`

**Interfaces:**
- Produces: `class RegisterMap` dengan `read_block(start:int, count:int) -> list[int]`, `write_single(id_:int, value:int) -> None`, `write_block(start:int, values:list[int]) -> None`, `get(id_) -> int`, `get_signed(id_) -> int`, `set_raw(id_, value) -> None` (untuk mapper fisika, boleh menulis register read-only), `set_signed(id_, value)`. Semua kegagalan → `ModbusError(code)` dengan `code` 2 (address) atau 3 (data). `COILS = {5050, 5051}` (BUKAN bagian RegisterMap — coil tidak bisa dibaca FC3).
- Konstanta: `ID_P_SET = 3050`, `ID_RATED = 3146`, `ID_SOC = 3184`, `ID_STATUS = 2057`, `ID_ALARM0 = 2050`, `ID_TELEM0 = 1050`.

- [ ] **Step 1: Test gagal dulu** — `tests/test_registers.py`:

```python
import pytest
from bess_sim.registers import RegisterMap, ModbusError, ID_P_SET, ID_RATED, ID_SOC

def test_defaults_sesuai_pdf():
    r = RegisterMap()
    assert r.get(3050) == 50        # total active power default 50 (5.0%)
    assert r.get(3052) == 1000      # power factor default
    assert r.get(3062) == 2000      # active rate of change
    assert r.get(3146) == 500       # rated power 50.0 kW
    assert r.get(3147) == 230       # rated phase voltage
    assert r.get(3157) == 0         # on-grid
    assert r.get(3182) == 1         # module address
    assert r.get_signed(3081) == -100  # grid under-frequency level 1
    assert r.get(1000) >= 0         # versi ARM ada

def test_read_block_telemetri():
    r = RegisterMap()
    vals = r.read_block(1050, 59)   # 1050..1108 seluruh area analog
    assert len(vals) == 59

def test_read_undefined_id_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.read_block(1109, 1)
    assert e.value.code == 2

def test_read_meliputi_lubang_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError):
        r.read_block(1105, 10)      # 1109+ tidak terdefinisi

def test_write_dalam_range():
    r = RegisterMap()
    r.write_single(3050, 100)
    assert r.get(3050) == 100

def test_write_negatif_dua_komplemen():
    r = RegisterMap()
    r.write_single(3050, 0x10000 - 200)   # -20.0%
    assert r.get_signed(3050) == -200

def test_write_di_luar_range_error_3():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.write_single(3050, 1300)  # max 1200
    assert e.value.code == 3

def test_write_register_readonly_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.write_single(1060, 1)     # telemetri = read-only
    assert e.value.code == 2

def test_set_raw_boleh_tulis_readonly():
    r = RegisterMap()
    r.set_signed(1060, -50)
    assert r.get_signed(1060) == -50
```

- [ ] **Step 2: Verifikasi FAIL** — `uv run pytest tests/test_registers.py -v`.

- [ ] **Step 3: Implementasi** — `bess_sim/registers.py`. Peta **harus lengkap** sesuai PDF §4.1–§4.6. Gunakan helper supaya ringkas:

```python
"""Register map BSL AC series V2.1.0 — sumber: PDF protokol §4."""
from dataclasses import dataclass

class ModbusError(Exception):
    def __init__(self, code: int):
        super().__init__(f"modbus exception {code}")
        self.code = code

@dataclass(frozen=True)
class RegDef:
    default: int
    lo: int = 0
    hi: int = 0xFFFF
    signed: bool = False
    writable: bool = False

def RO(default=0, signed=False):
    return RegDef(default, 0, 0xFFFF, signed, False)

def RW(default, lo, hi, signed=False):
    return RegDef(default, lo, hi, signed, True)

ID_TELEM0, ID_ALARM0, ID_STATUS = 1050, 2050, 2057
ID_P_SET, ID_RATED, ID_SOC = 3050, 3146, 3184
COILS = {5050, 5051}

REGS: dict[int, RegDef] = {}

# --- 4.1 Basic product info (RO) ---
for i, v in zip(range(1000, 1008), [2, 1, 0, 1, 2, 1, 0, 1]):
    REGS[i] = RO(v)

# --- 4.2 Analog telemetry 1050..1108 (RO). Signed sesuai kolom Value Type PDF.
_SIGNED_TELEM = {1059, 1060, 1061, 1064, 1065, 1069, 1070, 1074, 1075, 1076,
                 1078, 1079, 1080, 1081, 1082, 1083, 1084, 1085, 1086,
                 1096, 1097, 1098, 1099, 1100, 1101}
for i in range(1050, 1109):
    REGS[i] = RO(0, signed=i in _SIGNED_TELEM)

# --- 4.3/4.4 Alarm 2050..2056 + status 2057 (RO) ---
for i in range(2050, 2058):
    REGS[i] = RO(0)

# --- 4.6 Tanggal & jam (RW) ---
REGS[1500] = RW(0, 0, 23)
REGS[1501] = RW(0, 0, 59)
REGS[1502] = RW(0, 0, 59)
REGS[1503] = RW(2026, 0, 2099)
REGS[1504] = RW(1, 1, 12)
REGS[1505] = RW(1, 1, 31)

# --- 4.5.1 Control parameter setting ---
for i in (3050, 3053, 3054, 3055):
    REGS[i] = RW(50, -1200, 1200, signed=True)      # active power 0.1%
for i in (3051, 3056, 3057, 3058):
    REGS[i] = RW(0, -1200, 1200, signed=True)       # reactive power
for i in (3052, 3059, 3060, 3061):
    REGS[i] = RW(1000, -1000, 1000, signed=True)    # power factor
REGS[3062] = RW(2000, 1, 30000)                     # active rate of change %/s
for i, d in zip(range(3063, 3069), [110, 120, 130, 135, 140, 108]):
    REGS[i] = RW(d, 105, 150)                       # grid OV level 1-5 + recovery
for i, d in zip(range(3069, 3075), [85, 80, 60, 40, 20, 87]):
    REGS[i] = RW(d, 10, 95)                         # grid UV
for i, d in zip(range(3075, 3081), [100, 200, 300, 400, 500, 80]):
    REGS[i] = RW(d, 20, 500)                        # grid OF (+0.01Hz)
for i, d in zip(range(3081, 3087), [-100, -200, -300, -400, -500, -80]):
    REGS[i] = RW(d, -500, -20, signed=True)         # grid UF
# 3087..3128: pasangan UINT32 Hi-Lo waktu proteksi (ms). Kata per kata RW mentah;
# range gabungan 0..3600000 TIDAK divalidasi per kata (perilaku device asli untuk
# tulisan sebagian tidak terdokumentasi — keputusan spec D-catatan).
_U32_DEFAULTS = [15000, 2000, 100, 100, 20, 5000, 3000, 2000, 1500, 1000,
                 30000, 10000, 5000, 1000, 100, 30000, 10000, 5000, 1000, 100, 30000]
for k, d32 in enumerate(_U32_DEFAULTS):
    hi_id = 3087 + 2 * k
    REGS[hi_id] = RW(d32 >> 16, 0, 0xFFFF)
    REGS[hi_id + 1] = RW(d32 & 0xFFFF, 0, 0xFFFF)
for i, (d, lo, hi) in {3129: (160, 0, 600), 3130: (100, 0, 100),
                        3131: (100, 1, 30000), 3132: (160, 0, 600),
                        3133: (100, 0, 100)}.items():
    REGS[i] = RW(d, lo, hi)
REGS[3134] = RW(0, 0, 3)                            # battery type
REGS[3135] = RW(6000, 2000, 18000)
REGS[3136] = RW(6200, 2000, 18000)
REGS[3137] = RW(9000, 2000, 18000)
REGS[3138] = RW(8800, 2000, 18000)
REGS[3139] = RW(2000, 0, 4000)
REGS[3140] = RW(2000, 0, 4000)
REGS[3141] = RW(8700, 2000, 18000)
REGS[3142] = RW(6300, 2000, 18000)
REGS[3143] = RW(7500, 3000, 18000)
REGS[3144] = RW(100, 1, 1000)
REGS[3145] = RW(9500, 3000, 18000)
REGS[3146] = RW(500, 100, 3000)                     # rated power (0.1 kW)
REGS[3147] = RW(230, 100, 600)
REGS[3148] = RW(50, 50, 60)
REGS[3149] = RW(0, 0, 1)
for i in (3150, 3151, 3153, 3154, 3156):
    REGS[i] = RW(1 if i == 3156 else 0, 0, 1)
REGS[3152] = RW(2, 0, 2)
REGS[3155] = RW(0, 0, 1)
REGS[3157] = RW(0, 0, 1)                            # on/off-grid command
REGS[3158] = RW(0, 0, 1)
REGS[3159] = RW(0, 0, 3)                            # grid control mode PQ/MPPT/CV/VSG
REGS[3160] = RW(1, 0, 1)                            # off-grid VSG/VF
for i in range(3161, 3165):
    REGS[i] = RW(0, 0, 0xFFFF)                      # reserved
REGS[3165] = RW(500, 100, 3000)
REGS[3166] = RW(500, 0, 10000)
REGS[3167] = RW(500, 0, 30000)
REGS[3168] = RW(0, 0, 1)
REGS[3169] = RW(0, 0, 1)
REGS[3170] = RW(200, 0, 10000)
REGS[3171] = RW(30, 0, 2000)
REGS[3172] = RW(1000, 0, 2000)
REGS[3173] = RW(-1000, -2000, 0, signed=True)
REGS[3174] = RW(125, 0, 1000)
REGS[3175] = RW(50, 0, 1000)
REGS[3176] = RW(1000, 0, 2000)
REGS[3177] = RW(-1000, -1000, 0, signed=True)
REGS[3178] = RW(0, 0, 1)
for i in range(3179, 3182):
    REGS[i] = RW(0, 0, 0xFFFF)
REGS[3182] = RW(1, 1, 15)
REGS[3183] = RW(1, 1, 10)
REGS[3184] = RW(0, 0, 1000)                         # battery SOC (0.1%)
REGS[3185] = RW(0, 0, 1)

# --- 4.5.2 Communication parameter setting ---
for i in (3301, 3302, 3303, 3304, 3305, 3306):
    REGS[i] = RW(0, 0, 1)
REGS[3326] = RW(30, 5, 300)


def _to_u16(v: int) -> int:
    return v & 0xFFFF


def _from_u16(raw: int, signed: bool) -> int:
    return raw - 0x10000 if signed and raw >= 0x8000 else raw


class RegisterMap:
    def __init__(self):
        self.values: dict[int, int] = {i: _to_u16(d.default) for i, d in REGS.items()}

    def read_block(self, start: int, count: int) -> list[int]:
        if not 1 <= count <= 123:
            raise ModbusError(3)
        out = []
        for i in range(start, start + count):
            if i not in self.values:
                raise ModbusError(2)
            out.append(self.values[i])
        return out

    def write_single(self, id_: int, value: int) -> None:
        d = REGS.get(id_)
        if d is None or not d.writable:
            raise ModbusError(2)
        v = _from_u16(_to_u16(value), d.signed)
        if not d.lo <= v <= d.hi:
            raise ModbusError(3)
        self.values[id_] = _to_u16(value)

    def write_block(self, start: int, values: list[int]) -> None:
        for k, i in enumerate(range(start, start + len(values))):
            d = REGS.get(i)
            if d is None or not d.writable:
                raise ModbusError(2)
        for k, i in enumerate(range(start, start + len(values))):
            self.write_single(i, values[k])

    def get(self, id_: int) -> int:
        return self.values[id_]

    def get_signed(self, id_: int) -> int:
        return _from_u16(self.values[id_], True)

    def set_raw(self, id_: int, value: int) -> None:
        if id_ not in self.values:
            raise KeyError(id_)
        self.values[id_] = _to_u16(int(value))

    def set_signed(self, id_: int, value: int) -> None:
        self.set_raw(id_, int(value) & 0xFFFF)

    def set_u32(self, hi_id: int, value: int) -> None:
        v = max(0, int(value)) & 0xFFFFFFFF
        self.set_raw(hi_id, v >> 16)
        self.set_raw(hi_id + 1, v & 0xFFFF)
```

- [ ] **Step 4: Test PASS** — `uv run pytest tests/test_registers.py -v`.

- [ ] **Step 5: Commit** — `git add bess-sim && git commit -m "feat(sim): register map lengkap BSL V2.1.0 + validasi range"`

### Task 3: Mesin slave Modbus RTU

**Files:**
- Create: `bess-sim/bess_sim/modbus_slave.py`
- Test: `bess-sim/tests/test_modbus_slave.py`

**Interfaces:**
- Consumes: `RegisterMap`, `ModbusError`, `COILS`, `crc.append_crc/check_crc`.
- Produces: `class ModbusSlave(node: int, regs: RegisterMap, coil_handler, busy_fn)` dengan `handle(frame: bytes) -> bytes | None`. `coil_handler(id_: int, on: bool) -> None` (boleh raise `ModbusError`). `busy_fn() -> bool` — bila True, semua **tulisan** (FC5/6/16) dibalas error 06; baca tetap dilayani. Return `None` = diam (bukan node kita / frame terlalu pendek).

- [ ] **Step 1: Test gagal dulu** — `tests/test_modbus_slave.py`:

```python
from bess_sim.crc import append_crc
from bess_sim.registers import RegisterMap, ModbusError
from bess_sim.modbus_slave import ModbusSlave

def make(node=1, busy=False, coil_log=None):
    regs = RegisterMap()
    log = coil_log if coil_log is not None else []
    return ModbusSlave(node, regs, lambda i, on: log.append((i, on)),
                       lambda: busy), regs, log

def test_contoh_pdf_telemetri_roundtrip():
    s, regs, _ = make()
    for i in (1050, 1051, 1052):
        regs.set_raw(i, 0x0898)  # 220.0 V
    resp = s.handle(bytes.fromhex("0103041A0003253C"))
    assert resp == bytes.fromhex("0103060898089808988404")

def test_node_lain_diam():
    s, _, _ = make(node=1)
    assert s.handle(bytes.fromhex("0203041A0003" ) + b"\x00\x00") is None

def test_crc_salah_error_03():
    s, _, _ = make()
    bad = bytes.fromhex("0103041A0003253D")
    resp = s.handle(bad)
    assert resp[:3] == bytes([1, 0x83, 3])

def test_fc_tak_dikenal_error_01():
    s, _, _ = make()
    resp = s.handle(append_crc(bytes([1, 0x11, 0, 0])))
    assert resp[:3] == bytes([1, 0x91, 1])

def test_read_id_salah_error_02():
    s, _, _ = make()
    resp = s.handle(append_crc(bytes([1, 3, 0x04, 0x55, 0, 1])))  # 1109
    assert resp[:3] == bytes([1, 0x83, 2])

def test_fc6_write_echo():
    s, regs, _ = make()
    req = bytes.fromhex("01060BEA03E8AAA4")  # 3050 = 1000 (contoh PDF)
    assert s.handle(req) == req
    assert regs.get(3050) == 1000

def test_fc6_out_of_range_error_03():
    s, _, _ = make()
    resp = s.handle(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x05, 0x14])))  # 1300
    assert resp[:3] == bytes([1, 0x86, 3])

def test_fc5_coil_on_contoh_pdf():
    s, _, log = make()
    req = bytes.fromhex("010513BAFF00A95B")
    assert s.handle(req) == req
    assert log == [(5050, True)]

def test_fc5_data_bukan_ff00_0000_error_03():
    s, _, _ = make()
    resp = s.handle(append_crc(bytes([1, 5, 0x13, 0xBA, 0x12, 0x34])))
    assert resp[:3] == bytes([1, 0x85, 3])

def test_fc6_ke_coil_diteruskan():
    s, _, log = make()
    s.handle(append_crc(bytes([1, 6, 0x13, 0xBA, 0x00, 0x00])))
    assert log == [(5050, False)]

def test_baca_coil_fc3_error_02():
    s, _, _ = make()
    resp = s.handle(append_crc(bytes([1, 3, 0x13, 0xBA, 0, 1])))
    assert resp[:3] == bytes([1, 0x83, 2])

def test_busy_tolak_tulis_error_06_tapi_baca_jalan():
    s, _, _ = make(busy=True)
    resp = s.handle(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x00, 0x64])))
    assert resp[:3] == bytes([1, 0x86, 6])
    assert s.handle(bytes.fromhex("0103041A0003253C"))[1] == 3

def test_fc16_block_write():
    s, regs, _ = make()
    payload = bytes([1, 16, 0x0B, 0xEA, 0, 2, 4, 0x00, 0x64, 0x00, 0x32])
    resp = s.handle(append_crc(payload))
    assert resp[:6] == bytes([1, 16, 0x0B, 0xEA, 0, 2])
    assert regs.get(3050) == 100 and regs.get(3051) == 50
```

- [ ] **Step 2: Verifikasi FAIL** — `uv run pytest tests/test_modbus_slave.py -v`.

- [ ] **Step 3: Implementasi** — `bess_sim/modbus_slave.py`:

```python
"""Mesin slave Modbus RTU sesuai PDF §3 (FC3/4/5/6/16 + error frame)."""
from .crc import append_crc, check_crc
from .registers import RegisterMap, ModbusError, COILS


class ModbusSlave:
    def __init__(self, node, regs: RegisterMap, coil_handler, busy_fn=lambda: False):
        self.node = node
        self.regs = regs
        self.coil_handler = coil_handler
        self.busy_fn = busy_fn

    def handle(self, frame: bytes):
        if len(frame) < 4 or frame[0] != self.node:
            return None
        fc = frame[1]
        if not check_crc(frame):
            return self._err(fc, 3)
        body = frame[1:-2]
        try:
            if fc in (3, 4):
                return self._read(fc, body)
            if fc in (5, 6):
                return self._write_word(fc, body, frame)
            if fc == 16:
                return self._write_block(body)
            return self._err(fc, 1)
        except ModbusError as e:
            return self._err(fc, e.code)

    def _err(self, fc, code):
        return append_crc(bytes([self.node, fc | 0x80, code]))

    @staticmethod
    def _u16(b, i):
        return (b[i] << 8) | b[i + 1]

    def _read(self, fc, body):
        if len(body) != 5:
            raise ModbusError(3)
        start, count = self._u16(body, 1), self._u16(body, 3)
        vals = self.regs.read_block(start, count)
        out = bytes([self.node, fc, 2 * count])
        for v in vals:
            out += bytes([v >> 8, v & 0xFF])
        return append_crc(out)

    def _write_word(self, fc, body, frame):
        if len(body) != 5:
            raise ModbusError(3)
        if self.busy_fn():
            raise ModbusError(6)
        id_, data = self._u16(body, 1), self._u16(body, 3)
        if id_ in COILS:
            if data not in (0xFF00, 0x0000):
                raise ModbusError(3)
            self.coil_handler(id_, data == 0xFF00)
        elif fc == 5:
            raise ModbusError(2)      # FC5 hanya untuk coil
        else:
            self.regs.write_single(id_, data)
        return frame                   # echo persis

    def _write_block(self, body):
        if len(body) < 6:
            raise ModbusError(3)
        if self.busy_fn():
            raise ModbusError(6)
        start, count, nbytes = self._u16(body, 1), self._u16(body, 3), body[5]
        if not 1 <= count <= 123 or nbytes != 2 * count or len(body) != 6 + nbytes:
            raise ModbusError(3)
        vals = [self._u16(body, 6 + 2 * k) for k in range(count)]
        self.regs.write_block(start, vals)
        return append_crc(bytes([self.node, 16]) +
                          bytes([start >> 8, start & 0xFF, count >> 8, count & 0xFF]))
```

- [ ] **Step 4: Test PASS** — `uv run pytest -v` (seluruh suite).

- [ ] **Step 5: Commit** — `git add bess-sim && git commit -m "feat(sim): mesin slave Modbus RTU FC3/4/5/6/16 + error frame"`

### Task 4: Inti fisika

**Files:**
- Create: `bess-sim/bess_sim/physics.py`
- Test: `bess-sim/tests/test_physics.py`

**Interfaces:**
- Produces: `class Physics(soc: float = 0.5, seed: int = 0)` dengan atribut `soc` (0..1), `p_ac_kw` (aktual, + = ekspor), `tube_temp_c`, `ambient_c`, method `step(dt_s, target_kw, rate_pct_per_s, rated_kw, running: bool)`, `v_dc() -> float`, `i_dc() -> float` (+ = discharge), `p_dc_kw() -> float`, `grid() -> dict` (v_ab/bc/ca, i per fasa, f per fasa — dengan noise kecil deterministik per seed), `wh_charge`, `wh_discharge` (akumulator).
- Konstanta kelas: `CAP_KWH=108.86`, `V_MIN=705.6`, `V_MAX=907.2`, `EFF=0.97`.

- [ ] **Step 1: Test gagal dulu** — `tests/test_physics.py`:

```python
from bess_sim.physics import Physics

def run(p, seconds, **kw):
    for _ in range(int(seconds * 10)):
        p.step(0.1, **kw)

def test_soc_turun_saat_ekspor():
    p = Physics(soc=0.5)
    run(p, 3600, target_kw=10.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    # 10 kW AC selama 1 jam → DC ≈ 10/0.97 ≈ 10.31 kWh dari 108.86 kWh ≈ 9.5%
    assert 0.395 < p.soc < 0.41
    assert abs(p.p_ac_kw - 10.0) < 0.01

def test_soc_naik_saat_charge():
    p = Physics(soc=0.5)
    run(p, 3600, target_kw=-10.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    assert 0.585 < p.soc < 0.60

def test_ramp_dibatasi_rate():
    p = Physics(soc=0.5)
    # rate 10 %/s dari 50 kW = 5 kW/s → setelah 1 s baru ~5 kW
    run(p, 1, target_kw=50.0, rate_pct_per_s=10, rated_kw=50.0, running=True)
    assert 4.0 < p.p_ac_kw < 6.0

def test_not_running_menuju_nol():
    p = Physics(soc=0.5)
    run(p, 2, target_kw=20.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    run(p, 2, target_kw=20.0, rate_pct_per_s=2000, rated_kw=50.0, running=False)
    assert p.p_ac_kw == 0.0

def test_vdc_mengikuti_soc_dan_beban():
    hi, lo = Physics(soc=0.9), Physics(soc=0.1)
    assert hi.v_dc() > lo.v_dc()
    assert 705.6 <= lo.v_dc() <= 907.2
    idle = Physics(soc=0.5).v_dc()
    p = Physics(soc=0.5)
    run(p, 5, target_kw=50.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    assert p.v_dc() < idle          # sag saat discharge

def test_arah_arus():
    p = Physics(soc=0.5)
    run(p, 5, target_kw=10.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    assert p.i_dc() > 0
    run(p, 10, target_kw=-10.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    assert p.i_dc() < 0

def test_grid_nilai_masuk_akal():
    g = Physics(soc=0.5).grid()
    assert 395 < g["v_ab"] < 405 and 49.9 < g["f_a"] < 50.1

def test_suhu_naik_dengan_beban():
    p = Physics(soc=0.5)
    t0 = p.tube_temp_c
    run(p, 600, target_kw=50.0, rate_pct_per_s=2000, rated_kw=50.0, running=True)
    assert p.tube_temp_c > t0 + 5
```

- [ ] **Step 2: Verifikasi FAIL**, lalu **Step 3: Implementasi** — `bess_sim/physics.py`:

```python
"""Fisika pack C109: 108.86 kWh LFP 806.4 V + konverter 50 kW, eff 97%."""
import math
import random


class Physics:
    CAP_KWH = 108.86
    V_MIN, V_MAX = 705.6, 907.2
    EFF = 0.97
    SAG_V_PER_KW = 0.15          # sag tegangan DC per kW discharge

    def __init__(self, soc: float = 0.5, seed: int = 0):
        self.soc = float(soc)
        self.p_ac_kw = 0.0
        self.ambient_c = 30.0
        self.tube_temp_c = 35.0
        self.wh_charge = 0.0
        self.wh_discharge = 0.0
        self._rng = random.Random(seed)

    def step(self, dt_s, target_kw, rate_pct_per_s, rated_kw, running):
        tgt = float(target_kw) if running else 0.0
        max_step = rate_pct_per_s / 100.0 * rated_kw * dt_s
        delta = max(-max_step, min(max_step, tgt - self.p_ac_kw))
        self.p_ac_kw += delta
        if not running and abs(self.p_ac_kw) < max(0.05, max_step):
            self.p_ac_kw = 0.0
        p_dc = self.p_dc_kw()
        self.soc = min(1.0, max(0.0, self.soc - p_dc * dt_s / 3600.0 / self.CAP_KWH))
        e_wh = abs(self.p_ac_kw) * dt_s / 3.6
        if self.p_ac_kw > 0:
            self.wh_discharge += e_wh
        elif self.p_ac_kw < 0:
            self.wh_charge += e_wh
        t_target = self.ambient_c + 5.0 + 25.0 * abs(self.p_ac_kw) / max(rated_kw, 1.0)
        self.tube_temp_c += (t_target - self.tube_temp_c) * dt_s / 60.0

    def p_dc_kw(self) -> float:
        if self.p_ac_kw >= 0:
            return self.p_ac_kw / self.EFF      # discharge: baterai memasok lebih
        return self.p_ac_kw * self.EFF          # charge: baterai menerima lebih sedikit

    def v_dc(self) -> float:
        base = self.V_MIN + self.soc * (self.V_MAX - self.V_MIN)
        return max(self.V_MIN, min(self.V_MAX, base - self.p_ac_kw * self.SAG_V_PER_KW))

    def i_dc(self) -> float:
        return self.p_dc_kw() * 1000.0 / self.v_dc()

    def grid(self) -> dict:
        n = lambda a: self._rng.uniform(-a, a)
        i_ph = abs(self.p_ac_kw) * 1000.0 / (math.sqrt(3) * 400.0)
        return {
            "v_ab": 400.0 + n(1.5), "v_bc": 400.0 + n(1.5), "v_ca": 400.0 + n(1.5),
            "i_a": i_ph + n(0.2), "i_b": i_ph + n(0.2), "i_c": i_ph + n(0.2),
            "f_a": 50.0 + n(0.02), "f_b": 50.0 + n(0.02), "f_c": 50.0 + n(0.02),
        }
```

- [ ] **Step 4: Test PASS** — `uv run pytest tests/test_physics.py -v`.

- [ ] **Step 5: Commit** — `git add bess-sim && git commit -m "feat(sim): fisika pack C109 (SOC, ramp, Vdc, grid, termal)"`

### Task 5: State machine + BessSim (integrasi)

**Files:**
- Create: `bess-sim/bess_sim/state_machine.py`, `bess-sim/bess_sim/sim.py`
- Test: `bess-sim/tests/test_state_machine.py`, `bess-sim/tests/test_sim.py`

**Interfaces:**
- Consumes: `Physics`, `RegisterMap`, `ModbusSlave`.
- Produces:
  - `state_machine.St` (Enum: `STOP, PRECHARGE, SOFTSTART, RELAY, RUN, STOPPING, STANDBY, FAULT, EPO`) dan `class StateMachine` dengan `power_on()`, `power_off()`, `standby(on: bool)`, `trip()`, `clear_fault()`, `tick(dt_s)`, `busy() -> bool` (True di PRECHARGE/SOFTSTART/RELAY/STOPPING), `running() -> bool`, `status_word(charging: bool) -> int` (bit map 2057 persis PDF §4.4.1).
  - `sim.BessSim(node=1, soc=0.5, seed=0)` dengan `handle_frame(bytes) -> bytes|None`, `tick(dt_s)`, `set_alarm(reg_id, bit, value, trip=False)`, properti `regs`, `physics`, `sm`. Coil 5050 → power_on/off; 5051 → standby. `tick` menyalin fisika → register (scaling PDF), status → 2057, SOC → 3184, total charge/discharge → 1105–1108.

- [ ] **Step 1: Test state machine gagal dulu** — `tests/test_state_machine.py`:

```python
from bess_sim.state_machine import StateMachine, St

def tick(sm, s):
    for _ in range(int(s * 10)):
        sm.tick(0.1)

def test_urutan_start():
    sm = StateMachine()
    sm.power_on()
    assert sm.state == St.PRECHARGE and sm.busy()
    tick(sm, 1.1); assert sm.state == St.SOFTSTART
    tick(sm, 1.1); assert sm.state == St.RELAY
    tick(sm, 1.1); assert sm.state == St.RUN
    assert sm.running() and not sm.busy()

def test_stop_lewat_stopping():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    sm.power_off()
    assert sm.state == St.STOPPING
    tick(sm, 1.1)
    assert sm.state == St.STOP

def test_status_word_run():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    w = sm.status_word(charging=False)
    for bit in (0, 1, 2, 3, 6, 8, 9, 15):   # precharge..relay, run, master, init done
        assert w & (1 << bit), bit
    assert not w & (1 << 7)
    assert sm.status_word(charging=True) & (1 << 5)

def test_status_word_stop():
    w = StateMachine().status_word(charging=False)
    assert w & (1 << 11) and w & (1 << 15)   # shutdown, param init
    assert not w & (1 << 6)

def test_trip_dari_run():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    sm.trip()
    assert sm.state == St.FAULT
    assert sm.status_word(False) & (1 << 7)
    sm.power_on()                             # di FAULT: diabaikan
    assert sm.state == St.FAULT
    sm.clear_fault()
    assert sm.state == St.STOP

def test_standby():
    sm = StateMachine()
    sm.standby(True); assert sm.state == St.STANDBY
    assert sm.status_word(False) & (1 << 10)
    sm.power_on(); tick(sm, 3.5)
    assert sm.state == St.RUN
```

- [ ] **Step 2: Test BessSim gagal dulu** — `tests/test_sim.py`:

```python
from bess_sim.crc import append_crc
from bess_sim.sim import BessSim
from bess_sim.state_machine import St

ON  = bytes.fromhex("010513BAFF00A95B")
OFF = append_crc(bytes([1, 5, 0x13, 0xBA, 0x00, 0x00]))

def read(sim, start, count):
    req = append_crc(bytes([1, 3, start >> 8, start & 0xFF, count >> 8, count & 0xFF]))
    resp = sim.handle_frame(req)
    assert resp is not None and resp[1] == 3, resp
    n = resp[2] // 2
    return [(resp[3 + 2*k] << 8) | resp[4 + 2*k] for k in range(n)]

def tick(sim, s):
    for _ in range(int(s * 10)):
        sim.tick(0.1)

def test_on_lalu_run_lalu_ekspor():
    sim = BessSim(soc=0.5)
    assert sim.handle_frame(ON) == ON
    tick(sim, 3.5)
    assert sim.sm.state == St.RUN
    # default 3050=50 → 5% × 50 kW = 2.5 kW ekspor
    tick(sim, 5)
    p_raw = read(sim, 1060, 1)[0]
    assert 20 <= p_raw <= 30          # 2.5 kW → raw 25 (0.1 kW)
    assert read(sim, 2057, 1)[0] & (1 << 6)

def test_write_saat_transisi_busy():
    sim = BessSim()
    sim.handle_frame(ON)              # masuk PRECHARGE
    resp = sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0, 100])))
    assert resp[:3] == bytes([1, 0x86, 6])

def test_soc_terpantul_ke_3184_dan_vdc():
    sim = BessSim(soc=0.6)
    tick(sim, 1)
    assert abs(read(sim, 3184, 1)[0] - 600) <= 5
    vdc = read(sim, 1063, 1)[0] / 10.0
    assert 700 < vdc < 910

def test_set_power_lalu_daya_berubah():
    sim = BessSim(soc=0.5)
    sim.handle_frame(ON); tick(sim, 3.5)
    sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x00, 0xC8])))  # 200=20%=10kW
    tick(sim, 10)
    assert abs(read(sim, 1060, 1)[0] - 100) <= 10   # 10 kW → raw 100

def test_charge_negatif():
    sim = BessSim(soc=0.5)
    sim.handle_frame(ON); tick(sim, 3.5)
    sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0xFF, 0x38])))  # -200 = -20%
    tick(sim, 10)
    p = read(sim, 1060, 1)[0]
    assert p >= 0x8000                 # negatif dua-komplemen
    assert read(sim, 2057, 1)[0] & (1 << 5)   # bit charging

def test_off_kembali_stop():
    sim = BessSim(soc=0.5)
    sim.handle_frame(ON); tick(sim, 3.5)
    sim.handle_frame(OFF)
    tick(sim, 3)
    assert sim.sm.state == St.STOP
    assert read(sim, 1060, 1)[0] == 0

def test_alarm_injeksi_dan_trip():
    sim = BessSim(soc=0.5)
    sim.handle_frame(ON); tick(sim, 3.5)
    sim.set_alarm(2052, 1, 1, trip=True)   # grid under-voltage
    tick(sim, 1)
    assert read(sim, 2052, 1)[0] & 0b10
    assert sim.sm.state == St.FAULT

def test_over_discharge_otomatis():
    sim = BessSim(soc=0.021)
    sim.handle_frame(ON); tick(sim, 3.5)
    sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x03, 0xE8])))  # 100%
    tick(sim, 60)                      # kuras sampai <2%
    assert sim.sm.state == St.FAULT
    assert read(sim, 2055, 1)[0] & (1 << 14)   # battery over-discharge
```

- [ ] **Step 3: Verifikasi FAIL** — `uv run pytest tests/test_state_machine.py tests/test_sim.py -v`.

- [ ] **Step 4: Implementasi** — `bess_sim/state_machine.py`:

```python
"""State machine on/off BESS: urutan precharge → soft start → relay → run."""
from enum import Enum, auto


class St(Enum):
    STOP = auto(); PRECHARGE = auto(); SOFTSTART = auto(); RELAY = auto()
    RUN = auto(); STOPPING = auto(); STANDBY = auto(); FAULT = auto(); EPO = auto()


_SEQ = [St.PRECHARGE, St.SOFTSTART, St.RELAY, St.RUN]
STAGE_S = 1.0


class StateMachine:
    def __init__(self):
        self.state = St.STOP
        self._t = 0.0

    def power_on(self):
        if self.state in (St.STOP, St.STANDBY):
            self.state, self._t = St.PRECHARGE, 0.0

    def power_off(self):
        if self.state in (St.RUN, St.PRECHARGE, St.SOFTSTART, St.RELAY):
            self.state, self._t = St.STOPPING, 0.0

    def standby(self, on: bool):
        if on and self.state == St.STOP:
            self.state = St.STANDBY
        elif not on and self.state == St.STANDBY:
            self.state = St.STOP

    def trip(self):
        if self.state != St.EPO:
            self.state, self._t = St.FAULT, 0.0

    def clear_fault(self):
        if self.state == St.FAULT:
            self.state = St.STOP

    def tick(self, dt_s: float):
        self._t += dt_s
        if self.state in (St.PRECHARGE, St.SOFTSTART, St.RELAY) and self._t >= STAGE_S:
            self.state = _SEQ[_SEQ.index(self.state) + 1]
            self._t = 0.0
        elif self.state == St.STOPPING and self._t >= STAGE_S:
            self.state, self._t = St.STOP, 0.0

    def busy(self) -> bool:
        return self.state in (St.PRECHARGE, St.SOFTSTART, St.RELAY, St.STOPPING)

    def running(self) -> bool:
        return self.state == St.RUN

    def status_word(self, charging: bool) -> int:
        s, w = self.state, 0
        stage = {St.PRECHARGE: 0, St.SOFTSTART: 1, St.RELAY: 2}.get(s)
        closed = 4 if s in (St.RUN, St.STOPPING) else (stage or 0)
        if s in (St.RUN, St.STOPPING) or stage is not None:
            for b in range(min(closed, 4)):
                w |= 1 << b
        if s == St.RUN:
            w |= 0b1111 | (1 << 6)
        if charging and s == St.RUN:
            w |= 1 << 5
        if s == St.FAULT:
            w |= 1 << 7
        w |= (1 << 8) | (1 << 9)          # master machine + master cabinet
        if s == St.STANDBY:
            w |= 1 << 10
        if s in (St.STOP, St.STANDBY, St.FAULT):
            w |= 1 << 11                   # shutdown
        if s == St.EPO:
            w |= 1 << 12
        w |= 1 << 15                       # parameter initialization completed
        return w
```

`bess_sim/sim.py`:

```python
"""BessSim — mengikat register, fisika, state machine, dan slave Modbus."""
from .modbus_slave import ModbusSlave
from .physics import Physics
from .registers import RegisterMap, ID_P_SET, ID_RATED, ID_SOC, ID_STATUS
from .state_machine import StateMachine, St


class BessSim:
    def __init__(self, node: int = 1, soc: float = 0.5, seed: int = 0):
        self.regs = RegisterMap()
        self.physics = Physics(soc=soc, seed=seed)
        self.sm = StateMachine()
        self.slave = ModbusSlave(node, self.regs, self._coil, self.sm.busy)
        self._forced_alarms: dict[tuple[int, int], int] = {}
        self.regs.set_raw(ID_SOC, round(self.physics.soc * 1000))

    def _coil(self, id_: int, on: bool):
        if id_ == 5050:
            self.sm.power_on() if on else self.sm.power_off()
        elif id_ == 5051:
            self.sm.standby(on)

    def handle_frame(self, frame: bytes):
        return self.slave.handle(frame)

    def set_alarm(self, reg_id: int, bit: int, value: int, trip: bool = False):
        self._forced_alarms[(reg_id, bit)] = value
        if value and trip:
            self.sm.trip()

    def tick(self, dt_s: float):
        rated_kw = self.regs.get(ID_RATED) / 10.0
        setpoint_kw = self.regs.get_signed(ID_P_SET) / 1000.0 * rated_kw
        rate = self.regs.get(3062)
        self.sm.tick(dt_s)
        self.physics.step(dt_s, setpoint_kw, rate, rated_kw, self.sm.running())
        self._auto_protect()
        self._map_to_regs(rated_kw)

    def _auto_protect(self):
        p = self.physics
        if p.soc <= 0.02 and p.p_ac_kw > 0:
            self.set_alarm(2055, 14, 1, trip=True)     # battery over-discharge
        if p.soc >= 0.98 and p.p_ac_kw < 0:
            self.set_alarm(2055, 13, 1, trip=True)     # battery over-charge

    def _map_to_regs(self, rated_kw: float):
        r, p = self.regs, self.physics
        g = p.grid()
        r.set_raw(1050, round(g["v_ab"] * 10)); r.set_raw(1051, round(g["v_bc"] * 10))
        r.set_raw(1052, round(g["v_ca"] * 10))
        r.set_raw(1053, round(g["i_a"] * 10)); r.set_raw(1054, round(g["i_b"] * 10))
        r.set_raw(1055, round(g["i_c"] * 10))
        r.set_raw(1056, round(g["f_a"] * 100)); r.set_raw(1057, round(g["f_b"] * 100))
        r.set_raw(1058, round(g["f_c"] * 100))
        r.set_signed(1059, 100 if p.p_ac_kw >= 0 else -100)
        r.set_signed(1060, round(p.p_ac_kw * 10))
        r.set_signed(1061, 0)
        r.set_raw(1062, abs(round(p.p_ac_kw * 10)))
        r.set_raw(1063, round(p.v_dc() * 10))
        r.set_signed(1064, round(p.i_dc() * 10))
        r.set_signed(1065, round(p.p_dc_kw() * 10))
        r.set_raw(1066, round(p.v_dc() * 10))
        r.set_raw(1067, round(p.v_dc() * 5)); r.set_raw(1068, round(p.v_dc() * 5))
        r.set_signed(1069, 0); r.set_signed(1070, 0)
        r.set_raw(1071, 10000); r.set_raw(1072, 10000); r.set_raw(1073, 0)
        r.set_signed(1074, round(p.tube_temp_c))
        r.set_signed(1075, round(p.tube_temp_c - 3))
        r.set_signed(1076, round(p.ambient_c))
        r.set_raw(1077, 970)
        third = p.p_ac_kw / 3.0
        for base, val in ((1078, 100), (1079, 100), (1080, 100)):
            r.set_signed(base, val)
        for base in (1081, 1082, 1083):
            r.set_signed(base, round(third * 10))
        for base in (1084, 1085, 1086):
            r.set_signed(base, 0)
        for base in (1087, 1088, 1089):
            r.set_raw(base, abs(round(third * 10)))
        for src, dst in ((1050, 1090), (1051, 1091), (1052, 1092),
                         (1053, 1093), (1054, 1094), (1055, 1095)):
            r.set_raw(dst, r.get(src))
        r.set_u32(1105, round(p.wh_charge / 100))       # 0.1 kWh
        r.set_u32(1107, round(p.wh_discharge / 100))
        for (reg_id, bit), v in self._forced_alarms.items():
            cur = r.get(reg_id)
            r.set_raw(reg_id, (cur | (1 << bit)) if v else (cur & ~(1 << bit)))
        r.set_raw(ID_STATUS, self.sm.status_word(charging=p.p_ac_kw < 0))
        r.set_raw(ID_SOC, round(p.soc * 1000))
```

- [ ] **Step 5: Test PASS** — `uv run pytest -v` (seluruh suite hijau).

- [ ] **Step 6: Commit** — `git add bess-sim && git commit -m "feat(sim): state machine on/off + BessSim integrasi fisika-register"`

### Task 6: Nama alarm/status + skenario YAML

**Files:**
- Create: `bess-sim/bess_sim/alarms.py`, `bess-sim/bess_sim/scenario.py`, `bess-sim/scenarios/grid_undervoltage.yaml`, `bess-sim/scenarios/bms_comm_fail.yaml`
- Test: `bess-sim/tests/test_scenario.py`

**Interfaces:**
- Produces: `alarms.ALARM_BITS: dict[tuple[int,int], str]` (SEMUA bit bernama dari PDF §4.3), `alarms.STATUS_BITS: dict[int, str]` (bit 2057), `alarms.by_name(name) -> tuple[int,int]`; `scenario.Scenario.load(path) -> Scenario`, `.initial: dict`, `.apply(sim, t_s)` (jalankan event yang jatuh tempo, sekali saja).
- Format YAML: `initial: {soc: 60}` dan `events: [{at: <detik>, action: alarm|clear_alarm|set_soc, name: <nama alarm>, value: <int>, trip: <bool>}]`.

- [ ] **Step 1: Test gagal dulu** — `tests/test_scenario.py`:

```python
from bess_sim.alarms import ALARM_BITS, STATUS_BITS, by_name
from bess_sim.scenario import Scenario
from bess_sim.sim import BessSim
from bess_sim.state_machine import St

def test_nama_alarm_kunci():
    assert ALARM_BITS[(2050, 2)] == "dc_bus_overvoltage"
    assert ALARM_BITS[(2052, 1)] == "grid_undervoltage"
    assert ALARM_BITS[(2055, 14)] == "battery_over_discharge"
    assert ALARM_BITS[(2056, 2)] == "bms_comm_failure"
    assert by_name("grid_undervoltage") == (2052, 1)
    assert STATUS_BITS[6] == "running"
    assert len(ALARM_BITS) >= 60

def test_scenario_yaml(tmp_path):
    f = tmp_path / "s.yaml"
    f.write_text(
        "initial: {soc: 40}\n"
        "events:\n"
        "  - {at: 1, action: alarm, name: grid_undervoltage, trip: true}\n"
        "  - {at: 3, action: clear_alarm, name: grid_undervoltage}\n"
        "  - {at: 4, action: set_soc, value: 80}\n")
    sc = Scenario.load(f)
    sim = BessSim(soc=sc.initial.get("soc", 50) / 100.0)
    sc.apply(sim, 0.5)
    assert sim.sm.state != St.FAULT
    sc.apply(sim, 1.5)
    assert sim.sm.state == St.FAULT
    sim.tick(0.1)
    assert sim.regs.get(2052) & 0b10
    sc.apply(sim, 3.5); sim.sm.clear_fault(); sim.tick(0.1)
    assert not sim.regs.get(2052) & 0b10
    sc.apply(sim, 4.5)
    assert abs(sim.physics.soc - 0.8) < 0.01
```

- [ ] **Step 2: Verifikasi FAIL**, **Step 3: Implementasi** — `bess_sim/alarms.py` (tabel penuh dari PDF §4.3.1–4.3.7 + §4.4.1):

```python
"""Nama semua bit alarm (2050-2056) & status (2057) — sumber PDF §4.3-4.4."""
ALARM_BITS: dict[tuple[int, int], str] = {}

def _w(reg, names):
    for bit, name in enumerate(names):
        if name:
            ALARM_BITS[(reg, bit)] = name

_w(2050, ["positive_bus_overvoltage", "negative_bus_overvoltage",
          "dc_bus_overvoltage", "bus_half_voltage_unbalance",
          "dc_bus_short_circuit", "dc_overcurrent", "balance_bridge_overcurrent",
          "dc_voltage_reverse", "dc_voltage_low", "dc_voltage_high",
          "insulation_impedance_abnormal", "pv_power_low_shutdown"])
_w(2051, ["inverter_voltage_a_abnormal", "inverter_voltage_b_abnormal",
          "inverter_voltage_c_abnormal", "inverter_voltage_dc_a_abnormal",
          "inverter_voltage_dc_b_abnormal", "inverter_voltage_dc_c_abnormal",
          "output_overload_shutdown_a", "output_overload_shutdown_b",
          "output_overload_shutdown_c", "output_overcurrent_a",
          "output_overcurrent_b", "output_overcurrent_c",
          "output_short_circuit_a", "output_short_circuit_b",
          "output_short_circuit_c", "inverter_phase_desync"])
_w(2052, ["grid_overvoltage", "grid_undervoltage", "grid_overfrequency",
          "grid_underfrequency", "islanding_protection", "grid_wrong_phase",
          "ac_power_failure", "ac_current_limit_shutdown", "parallel_cable_fault",
          "carrier_sync_fault", "inverter_sync_fault", "parallel_comm_fault",
          "ac_fuse_failure", "power_tube_over_temperature", "power_supply_fault",
          "leakage_current_fault"])
_w(2053, ["dc_precharge_fault", "ac_precharge_fault", "dc_relay_short_circuit",
          "dc_relay_open_circuit", "ac_relay_short_a", "ac_relay_short_b",
          "ac_relay_short_c", "ac_relay_open_a", "ac_relay_open_b",
          "ac_relay_open_c", "bridge_arm_shoot_through_a",
          "bridge_arm_shoot_through_b", "bridge_arm_shoot_through_c"])
_w(2054, ["grid_current_zero_bias", "inverter_current_zero_bias",
          "inverter_current_dc_zero_bias", "dc_current_zero_bias",
          "balance_bridge_current_zero_bias", "leakage_current_zero_bias",
          "reference_2v5_abnormal"])
_w(2055, ["output_overload_alarm_a", "output_overload_alarm_b",
          "output_overload_alarm_c", "low_voltage_ride_through",
          "high_voltage_ride_through", "balance_bridge_current_limit_alarm",
          "balance_bridge_over_temperature", "ambient_over_temperature",
          "over_temperature_derating", "dc_spd_fault", "ac_spd_fault",
          "fan_fault_1", "fan_fault_2", "battery_over_charge",
          "battery_over_discharge"])
_w(2056, ["internal_comm_failure", "ems_comm_failure", "bms_comm_failure",
          "bms_fault", "dry_contact_1_fault", "dry_contact_2_fault"])

STATUS_BITS = {0: "dc_precharge", 1: "ac_soft_start", 2: "dc_relay",
               3: "ac_relay", 4: "off_grid", 5: "charging", 6: "running",
               7: "fault", 8: "master_machine", 9: "master_cabinet",
               10: "standby", 11: "shutdown", 12: "epo",
               15: "param_init_done"}

_BY_NAME = {v: k for k, v in ALARM_BITS.items()}

def by_name(name: str) -> tuple[int, int]:
    return _BY_NAME[name]
```

`bess_sim/scenario.py`:

```python
"""Skenario YAML: injeksi alarm & perubahan kondisi terjadwal."""
import yaml
from .alarms import by_name


class Scenario:
    def __init__(self, initial: dict, events: list[dict]):
        self.initial = initial
        self._events = sorted(events, key=lambda e: e["at"])
        self._done: set[int] = set()

    @classmethod
    def load(cls, path):
        with open(path, encoding="utf-8") as f:
            doc = yaml.safe_load(f) or {}
        return cls(doc.get("initial") or {}, doc.get("events") or [])

    def apply(self, sim, t_s: float):
        for idx, ev in enumerate(self._events):
            if idx in self._done or ev["at"] > t_s:
                continue
            self._done.add(idx)
            act = ev["action"]
            if act == "alarm":
                reg, bit = by_name(ev["name"])
                sim.set_alarm(reg, bit, ev.get("value", 1), trip=ev.get("trip", False))
            elif act == "clear_alarm":
                reg, bit = by_name(ev["name"])
                sim.set_alarm(reg, bit, 0)
            elif act == "set_soc":
                sim.physics.soc = ev["value"] / 100.0
```

Skenario bawaan `scenarios/grid_undervoltage.yaml`:

```yaml
# Grid drop 30 detik setelah start, pulih di detik 60.
initial: {soc: 60}
events:
  - {at: 30, action: alarm, name: grid_undervoltage, trip: true}
  - {at: 60, action: clear_alarm, name: grid_undervoltage}
```

`scenarios/bms_comm_fail.yaml`:

```yaml
initial: {soc: 55}
events:
  - {at: 45, action: alarm, name: bms_comm_failure}
  - {at: 90, action: clear_alarm, name: bms_comm_failure}
```

- [ ] **Step 4: Test PASS** — `uv run pytest -v`.

- [ ] **Step 5: Commit** — `git add bess-sim && git commit -m "feat(sim): tabel nama alarm/status + skenario YAML"`

### Task 7: Transport serial + CLI

**Files:**
- Create: `bess-sim/bess_sim/transport.py`, `bess-sim/bess_sim/cli.py`
- Test: `bess-sim/tests/test_transport.py`, `bess-sim/tests/test_cli.py`

**Interfaces:**
- Produces:
  - `transport.FrameSplitter(gap_s=0.004)` — akumulasi byte, `feed(data: bytes, now: float) -> list[bytes]` mengembalikan frame utuh saat jeda antar-byte > gap (3,5 char @9600 ≈ 4 ms).
  - `transport.SerialServer(port, sim, strict_timing=False, reply_delay=(0.01, 0.04))` — loop `run_once(now)` baca port, potong frame, panggil `sim.handle_frame`, tunda balasan; `strict_timing=True` → frame yang datang <100 ms sejak akhir frame sebelumnya **diabaikan** (device asli), default hanya `log.warning`.
  - `cli.main()` — subcommand `run` (`--port COM10 --node 1 --soc 50 --scenario file.yaml --strict-timing`) dan `selftest` (tanpa hardware).

- [ ] **Step 1: Test gagal dulu** — `tests/test_transport.py`:

```python
from bess_sim.transport import FrameSplitter

def test_split_by_gap():
    fs = FrameSplitter(gap_s=0.004)
    assert fs.feed(b"\x01\x03", 0.000) == []
    assert fs.feed(b"\x04\x1a\x00\x03\x25\x3c", 0.001) == []
    out = fs.feed(b"", 0.010)          # jeda 9 ms → frame selesai
    assert out == [bytes.fromhex("0103041A0003253C")]

def test_dua_frame_terpisah():
    fs = FrameSplitter(gap_s=0.004)
    fs.feed(bytes.fromhex("0103041A0003253C"), 0.0)
    f1 = fs.feed(bytes.fromhex("010513BA"), 0.100)
    assert f1 == [bytes.fromhex("0103041A0003253C")]
    f2 = fs.feed(bytes.fromhex("FF00A95B"), 0.101)
    assert f2 == []
    assert fs.feed(b"", 0.110) == [bytes.fromhex("010513BAFF00A95B")]
```

`tests/test_cli.py`:

```python
from bess_sim.cli import selftest

def test_selftest_lulus():
    assert selftest() == 0
```

- [ ] **Step 2: Verifikasi FAIL**, **Step 3: Implementasi** — `bess_sim/transport.py`:

```python
"""Transport serial RS485 + pemotong frame berbasis jeda antar-byte."""
import logging
import random
import time

log = logging.getLogger("bess_sim")


class FrameSplitter:
    def __init__(self, gap_s: float = 0.004):
        self.gap_s = gap_s
        self._buf = bytearray()
        self._last = None

    def feed(self, data: bytes, now: float) -> list[bytes]:
        out = []
        if self._buf and self._last is not None and now - self._last > self.gap_s:
            out.append(bytes(self._buf))
            self._buf.clear()
        if data:
            self._buf += data
            self._last = now
        return out


class SerialServer:
    def __init__(self, port: str, sim, strict_timing=False,
                 reply_delay=(0.01, 0.04), seed=0):
        import serial
        self.ser = serial.Serial(port, 9600, bytesize=8, parity="N",
                                 stopbits=1, timeout=0.002)
        self.sim = sim
        self.strict = strict_timing
        self.delay = reply_delay
        self._rng = random.Random(seed)
        self._split = FrameSplitter()
        self._last_frame_end = 0.0

    def run_once(self):
        now = time.monotonic()
        data = self.ser.read(256)
        for frame in self._split.feed(data, now):
            gap = now - self._last_frame_end
            self._last_frame_end = now
            if gap < 0.100:
                if self.strict:
                    log.warning("frame diabaikan: jeda %.0f ms < 100 ms", gap * 1e3)
                    continue
                log.warning("jeda antar-frame %.0f ms < 100 ms (device asli menuntut 100 ms)", gap * 1e3)
            resp = self.sim.handle_frame(frame)
            if resp is not None:
                time.sleep(self._rng.uniform(*self.delay))
                self.ser.write(resp)
                self.ser.flush()
                self._last_frame_end = time.monotonic()
```

`bess_sim/cli.py`:

```python
"""CLI bess-sim: run (serial nyata) dan selftest (tanpa hardware)."""
import argparse
import logging
import time

from .crc import append_crc
from .sim import BessSim
from .state_machine import St


def selftest() -> int:
    sim = BessSim(soc=0.5)
    on = append_crc(bytes([1, 5, 0x13, 0xBA, 0xFF, 0x00]))
    assert sim.handle_frame(on) == on
    for _ in range(40):
        sim.tick(0.1)
    assert sim.sm.state == St.RUN, sim.sm.state
    sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x00, 0xC8])))  # 20% = 10 kW
    for _ in range(100):
        sim.tick(0.1)
    p_kw = sim.regs.get_signed(1060) / 10.0
    soc = sim.regs.get(3184) / 10.0
    vdc = sim.regs.get(1063) / 10.0
    print(f"selftest: state=RUN p_ac={p_kw:.1f} kW vdc={vdc:.1f} V soc={soc:.1f}%")
    ok = 9.5 < p_kw < 10.5 and 700 < vdc < 910
    print("selftest:", "LULUS" if ok else "GAGAL")
    return 0 if ok else 1


def run(args) -> int:
    from .scenario import Scenario
    from .transport import SerialServer
    sim = BessSim(node=args.node, soc=args.soc / 100.0)
    sc = Scenario.load(args.scenario) if args.scenario else None
    if sc and "soc" in sc.initial:
        sim.physics.soc = sc.initial["soc"] / 100.0
    srv = SerialServer(args.port, sim, strict_timing=args.strict_timing)
    print(f"bess-sim AKTIF di {args.port} node {args.node} (SIMULATOR — bukan device asli)")
    t0 = time.monotonic()
    last_tick = last_print = t0
    try:
        while True:
            srv.run_once()
            now = time.monotonic()
            if now - last_tick >= 0.1:
                if sc:
                    sc.apply(sim, now - t0)
                sim.tick(now - last_tick)
                last_tick = now
            if now - last_print >= 2.0:
                p = sim.physics
                print(f"[{now - t0:7.1f}s] {sim.sm.state.name:9s} "
                      f"p_ac={p.p_ac_kw:+6.2f} kW soc={p.soc * 100:5.1f}% "
                      f"vdc={p.v_dc():6.1f} V", flush=True)
                last_print = now
    except KeyboardInterrupt:
        print("berhenti.")
        return 0


def main(argv=None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    ap = argparse.ArgumentParser(prog="bess-sim")
    sub = ap.add_subparsers(dest="cmd", required=True)
    pr = sub.add_parser("run", help="layani port serial nyata")
    pr.add_argument("--port", required=True)
    pr.add_argument("--node", type=int, default=1)
    pr.add_argument("--soc", type=float, default=50.0)
    pr.add_argument("--scenario")
    pr.add_argument("--strict-timing", action="store_true")
    sub.add_parser("selftest", help="uji tanpa hardware")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    return run(args)
```

- [ ] **Step 4: Test PASS + selftest** — `uv run pytest -v` lalu `uv run bess-sim selftest` → cetak `selftest: LULUS`.

- [ ] **Step 5: Commit** — `git add bess-sim && git commit -m "feat(sim): transport serial + CLI run/selftest"`

### Task 8: Master probe (alat uji dari laptop)

**Files:**
- Create: `bess-sim/tools/master_probe.py`
- Test: (manual — alat bantu; logika frame sudah teruji di Task 1–3)

**Interfaces:**
- Consumes: `bess_sim.crc`, `bess_sim.alarms.STATUS_BITS`.
- Produces: skrip mandiri `uv run python tools/master_probe.py --port COMx {status|on|off|setp --pct N|read --start N --count N}` — master Modbus untuk menguji simulator (atau device asli kelak) langsung dari laptop lewat port serial kedua.

- [ ] **Step 1: Implementasi** — `tools/master_probe.py`:

```python
"""Master Modbus kecil untuk menguji bess-sim / device BESS dari laptop."""
import argparse
import sys
import time

sys.path.insert(0, ".")
from bess_sim.crc import append_crc, check_crc      # noqa: E402
from bess_sim.alarms import STATUS_BITS             # noqa: E402
import serial                                        # noqa: E402


def xfer(ser, frame, expect_silence=False):
    time.sleep(0.11)                 # jeda antar-frame >= 100 ms
    ser.reset_input_buffer()
    ser.write(append_crc(frame))
    ser.flush()
    time.sleep(0.3)
    resp = ser.read(300)
    if not resp:
        print("TIDAK ADA BALASAN"); return None
    if not check_crc(resp):
        print("CRC BALASAN SALAH:", resp.hex()); return None
    if resp[1] & 0x80:
        print(f"EXCEPTION code={resp[2]}"); return None
    return resp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--node", type=int, default=1)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("on")
    sub.add_parser("off")
    ps = sub.add_parser("setp"); ps.add_argument("--pct", type=float, required=True)
    prd = sub.add_parser("read")
    prd.add_argument("--start", type=int, required=True)
    prd.add_argument("--count", type=int, default=1)
    a = ap.parse_args()
    ser = serial.Serial(a.port, 9600, timeout=0.05)
    n = a.node
    if a.cmd == "on":
        r = xfer(ser, bytes([n, 5, 0x13, 0xBA, 0xFF, 0x00]))
        print("ON terkirim" if r else "gagal")
    elif a.cmd == "off":
        r = xfer(ser, bytes([n, 5, 0x13, 0xBA, 0x00, 0x00]))
        print("OFF terkirim" if r else "gagal")
    elif a.cmd == "setp":
        raw = int(round(a.pct * 10)) & 0xFFFF
        r = xfer(ser, bytes([n, 6, 0x0B, 0xEA, raw >> 8, raw & 0xFF]))
        print(f"setpoint {a.pct}% terkirim" if r else "gagal")
    elif a.cmd == "status":
        r = xfer(ser, bytes([n, 3, 0x08, 0x09, 0x00, 0x01]))
        if r:
            w = (r[3] << 8) | r[4]
            aktif = [nm for b, nm in STATUS_BITS.items() if w & (1 << b)]
            print(f"status 2057 = 0x{w:04X}: {', '.join(aktif)}")
    elif a.cmd == "read":
        r = xfer(ser, bytes([n, 3, a.start >> 8, a.start & 0xFF,
                             a.count >> 8, a.count & 0xFF]))
        if r:
            for k in range(r[2] // 2):
                v = (r[3 + 2 * k] << 8) | r[4 + 2 * k]
                print(f"  {a.start + k}: {v} (0x{v:04X})")


if __name__ == "__main__":
    sys.exit(main())
```

Catatan: register status 2057 = `0x0809`. (Contoh PDF membaca 2050 = `0x0802`.)

- [ ] **Step 2: Uji cepat tanpa kabel** — jalankan `uv run pytest -v` (memastikan tidak ada regresi import), lalu review manual: skrip hanya dipakai saat ada dua port serial.

- [ ] **Step 3: Commit** — `git add bess-sim/tools && git commit -m "feat(sim): master_probe alat uji Modbus dari laptop"`

---

## Bagian 2 — Firmware `firmware/` (Task 9–16)

### Task 9: Scaffold PlatformIO + CRC16 (native test)

**Files:**
- Create: `firmware/platformio.ini`, `firmware/lib/bess_core/crc16.h`, `firmware/lib/bess_core/crc16.cpp`, `firmware/.gitignore`
- Test: `firmware/test/native_crc/main.cpp`

**Interfaces:**
- Produces: `uint16_t mbCrc16(const uint8_t* data, size_t len)`; `void mbAppendCrc(uint8_t* buf, size_t len)` (menulis 2 byte low-high di `buf[len]`, `buf[len+1]`); `bool mbCheckCrc(const uint8_t* frame, size_t len)`.

- [ ] **Step 1: Scaffold** — `firmware/platformio.ini`:

```ini
[env:esp32c6]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=1
lib_deps = bblanchon/ArduinoJson@^7.0.4

[env:native]
platform = native
build_flags = -std=gnu++17 -DNATIVE_TEST
lib_deps = bblanchon/ArduinoJson@^7.0.4
lib_compat_mode = off
```

`firmware/.gitignore`:

```
.pio/
src/secrets.h
```

- [ ] **Step 2: Test gagal dulu** — `firmware/test/native_crc/main.cpp` (vector sama dengan Python, dari PDF):

```cpp
#include <unity.h>
#include <string.h>
#include "crc16.h"

static void test_vectors_pdf() {
    const uint8_t f1[] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03};
    TEST_ASSERT_EQUAL_HEX16(0x3C25, mbCrc16(f1, sizeof(f1)));  // kirim: 25 3C
    const uint8_t f2[] = {0x01, 0x06, 0x0B, 0xEA, 0x03, 0xE8};
    TEST_ASSERT_EQUAL_HEX16(0xA4AA, mbCrc16(f2, sizeof(f2)));  // kirim: AA A4
    const uint8_t f3[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_HEX16(0x5BA9, mbCrc16(f3, sizeof(f3)));  // kirim: A9 5B
}

static void test_append_check() {
    uint8_t buf[8] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03};
    mbAppendCrc(buf, 6);
    TEST_ASSERT_EQUAL_HEX8(0x25, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, buf[7]);
    TEST_ASSERT_TRUE(mbCheckCrc(buf, 8));
    buf[7] ^= 0xFF;
    TEST_ASSERT_FALSE(mbCheckCrc(buf, 8));
    TEST_ASSERT_FALSE(mbCheckCrc(buf, 3));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_vectors_pdf);
    RUN_TEST(test_append_check);
    return UNITY_END();
}
```

- [ ] **Step 3: Verifikasi FAIL** — `pio test -e native` (dari `firmware/`) → gagal compile (crc16 belum ada).

- [ ] **Step 4: Implementasi** — `lib/bess_core/crc16.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

uint16_t mbCrc16(const uint8_t* data, size_t len);
void mbAppendCrc(uint8_t* buf, size_t len);
bool mbCheckCrc(const uint8_t* frame, size_t len);
```

`lib/bess_core/crc16.cpp`:

```cpp
#include "crc16.h"

uint16_t mbCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

void mbAppendCrc(uint8_t* buf, size_t len) {
    uint16_t c = mbCrc16(buf, len);
    buf[len] = c & 0xFF;
    buf[len + 1] = c >> 8;
}

bool mbCheckCrc(const uint8_t* frame, size_t len) {
    if (len < 4) return false;
    uint16_t c = mbCrc16(frame, len - 2);
    return frame[len - 2] == (c & 0xFF) && frame[len - 1] == (c >> 8);
}
```

- [ ] **Step 5: Test PASS** — `pio test -e native` → 2 test PASS.

- [ ] **Step 6: Commit** — `git add firmware && git commit -m "feat(fw): scaffold PlatformIO + CRC16 Modbus (native test)"`

### Task 10: Frame Modbus master (build/parse murni)

**Files:**
- Create: `firmware/lib/bess_core/mb_frame.h`, `firmware/lib/bess_core/mb_frame.cpp`
- Test: `firmware/test/native_frame/main.cpp`

**Interfaces:**
- Produces:

```cpp
enum MbStatus : uint8_t { MB_OK, MB_TIMEOUT, MB_CRC, MB_EXCEPTION, MB_MALFORMED };
size_t mbBuildRead(uint8_t node, uint16_t start, uint16_t count, uint8_t out[8]);
size_t mbBuildWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t out[8]);
size_t mbBuildWrite5(uint8_t node, uint16_t id, bool on, uint8_t out[8]);
// resp = frame lengkap; count = jumlah register yang diminta; exc diisi bila MB_EXCEPTION
MbStatus mbParseReadResp(const uint8_t* resp, size_t n, uint8_t node,
                         uint16_t count, uint16_t* vals, uint8_t* exc);
MbStatus mbParseEcho(const uint8_t* resp, size_t n, uint8_t node,
                     uint8_t fc, uint8_t* exc);
size_t mbExpectedReadLen(uint16_t count);   // 5 + 2*count
```

- [ ] **Step 1: Test gagal dulu** — `firmware/test/native_frame/main.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "mb_frame.h"

static void test_build_read_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildRead(1, 1050, 3, buf);
    const uint8_t exp[] = {0x01, 0x03, 0x04, 0x1A, 0x00, 0x03, 0x25, 0x3C};
    TEST_ASSERT_EQUAL(8, n);
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, 8);
}

static void test_build_write6_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildWrite6(1, 3050, 1000, buf);
    const uint8_t exp[] = {0x01, 0x06, 0x0B, 0xEA, 0x03, 0xE8, 0xAA, 0xA4};
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, n);
}

static void test_build_write5_contoh_pdf() {
    uint8_t buf[8];
    size_t n = mbBuildWrite5(1, 5050, true, buf);
    const uint8_t exp[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00, 0xA9, 0x5B};
    TEST_ASSERT_EQUAL_MEMORY(exp, buf, n);
}

static void test_parse_read_resp() {
    const uint8_t resp[] = {0x01, 0x03, 0x06, 0x08, 0x98, 0x08, 0x98,
                            0x08, 0x98, 0x84, 0x04};
    uint16_t vals[3]; uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_OK, mbParseReadResp(resp, 11, 1, 3, vals, &exc));
    TEST_ASSERT_EQUAL_HEX16(0x0898, vals[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0898, vals[2]);
}

static void test_parse_exception() {
    uint8_t resp[5] = {0x01, 0x83, 0x02};
    mbAppendCrc(resp, 3);
    uint16_t vals[1]; uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_EXCEPTION, mbParseReadResp(resp, 5, 1, 1, vals, &exc));
    TEST_ASSERT_EQUAL(2, exc);
}

static void test_parse_crc_salah() {
    uint8_t resp[] = {0x01, 0x03, 0x02, 0x04, 0x00, 0xBA, 0x85};
    uint16_t vals[1]; uint8_t exc;
    TEST_ASSERT_EQUAL(MB_CRC, mbParseReadResp(resp, 7, 1, 1, vals, &exc));
}

static void test_parse_echo_write() {
    uint8_t resp[] = {0x01, 0x05, 0x13, 0xBA, 0xFF, 0x00, 0xA9, 0x5B};
    uint8_t exc = 0;
    TEST_ASSERT_EQUAL(MB_OK, mbParseEcho(resp, 8, 1, 5, &exc));
    uint8_t ex6[5] = {0x01, 0x86, 0x06};
    mbAppendCrc(ex6, 3);
    TEST_ASSERT_EQUAL(MB_EXCEPTION, mbParseEcho(ex6, 5, 1, 6, &exc));
    TEST_ASSERT_EQUAL(6, exc);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_build_read_contoh_pdf);
    RUN_TEST(test_build_write6_contoh_pdf);
    RUN_TEST(test_build_write5_contoh_pdf);
    RUN_TEST(test_parse_read_resp);
    RUN_TEST(test_parse_exception);
    RUN_TEST(test_parse_crc_salah);
    RUN_TEST(test_parse_echo_write);
    return UNITY_END();
}
```

- [ ] **Step 2: Verifikasi FAIL**, **Step 3: Implementasi** — `lib/bess_core/mb_frame.cpp` (`mb_frame.h` = deklarasi di atas + `#include "crc16.h"`):

```cpp
#include "mb_frame.h"
#include "crc16.h"

static size_t build4(uint8_t node, uint8_t fc, uint16_t a, uint16_t b, uint8_t out[8]) {
    out[0] = node; out[1] = fc;
    out[2] = a >> 8; out[3] = a & 0xFF;
    out[4] = b >> 8; out[5] = b & 0xFF;
    mbAppendCrc(out, 6);
    return 8;
}

size_t mbBuildRead(uint8_t node, uint16_t start, uint16_t count, uint8_t out[8]) {
    return build4(node, 3, start, count, out);
}
size_t mbBuildWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t out[8]) {
    return build4(node, 6, id, val, out);
}
size_t mbBuildWrite5(uint8_t node, uint16_t id, bool on, uint8_t out[8]) {
    return build4(node, 5, id, on ? 0xFF00 : 0x0000, out);
}
size_t mbExpectedReadLen(uint16_t count) { return 5 + 2 * (size_t)count; }

static MbStatus preCheck(const uint8_t* r, size_t n, uint8_t node,
                         uint8_t fc, uint8_t* exc) {
    if (n < 5) return MB_MALFORMED;
    if (!mbCheckCrc(r, n)) return MB_CRC;
    if (r[0] != node) return MB_MALFORMED;
    if (r[1] == (fc | 0x80)) { *exc = r[2]; return MB_EXCEPTION; }
    if (r[1] != fc) return MB_MALFORMED;
    return MB_OK;
}

MbStatus mbParseReadResp(const uint8_t* r, size_t n, uint8_t node,
                         uint16_t count, uint16_t* vals, uint8_t* exc) {
    MbStatus st = preCheck(r, n, node, 3, exc);
    if (st != MB_OK) return st;
    if (n != mbExpectedReadLen(count) || r[2] != 2 * count) return MB_MALFORMED;
    for (uint16_t k = 0; k < count; k++)
        vals[k] = ((uint16_t)r[3 + 2 * k] << 8) | r[4 + 2 * k];
    return MB_OK;
}

MbStatus mbParseEcho(const uint8_t* r, size_t n, uint8_t node,
                     uint8_t fc, uint8_t* exc) {
    MbStatus st = preCheck(r, n, node, fc, exc);
    if (st != MB_OK) return st;
    return (n == 8) ? MB_OK : MB_MALFORMED;
}
```

- [ ] **Step 4: Test PASS** — `pio test -e native`.

- [ ] **Step 5: Commit** — `git add firmware && git commit -m "feat(fw): build/parse frame Modbus master (native test, vector PDF)"`

### Task 11: BessData + decode & scaling + nama alarm

**Files:**
- Create: `firmware/lib/bess_core/bess_data.h`, `firmware/lib/bess_core/bess_decode.h`, `firmware/lib/bess_core/bess_decode.cpp`
- Test: `firmware/test/native_decode/main.cpp`

**Interfaces:**
- Produces:

```cpp
// bess_data.h
struct BessData {
    float grid_v_ab, grid_v_bc, grid_v_ca;
    float grid_i_a, grid_i_b, grid_i_c;
    float grid_f_hz, power_factor;
    float active_power_kw, reactive_power_kvar, apparent_power_kva;
    float dc_voltage_v, dc_current_a, dc_power_kw;
    float tube_temp_c, ambient_temp_c, efficiency_pct;
    float total_charge_kwh, total_discharge_kwh;
    uint16_t alarm_raw[7];   // 2050..2056
    uint16_t status_raw;     // 2057
    float soc_pct;           // reg 3184 / 10
    float rated_kw;          // reg 3146 / 10
    float setpoint_pct;      // reg 3050 (signed) / 10
    bool comm_lost;
    uint32_t last_ok_ms;
};
// bess_decode.h
void bessDecodeTelemetry(const uint16_t regs[59], BessData& d);  // 1050..1108
void bessDecodeAlarmStatus(const uint16_t regs[8], BessData& d); // 2050..2057
const char* bessAlarmName(int word, int bit);   // word 0..6 = 2050..2056; NULL bila tak terdefinisi
const char* bessStatusName(int bit);            // NULL bila tak terdefinisi
inline bool bessRunning(const BessData& d)  { return d.status_raw & (1u << 6); }
inline bool bessFault(const BessData& d)    { return d.status_raw & (1u << 7); }
inline bool bessCharging(const BessData& d) { return d.status_raw & (1u << 5); }
inline bool bessStandby(const BessData& d)  { return d.status_raw & (1u << 10); }
inline bool bessOffGrid(const BessData& d)  { return d.status_raw & (1u << 4); }
inline bool bessEpo(const BessData& d)      { return d.status_raw & (1u << 12); }
```

- Nama alarm **identik** dengan `bess_sim/alarms.py` (paritas simulator ⇄ firmware).

- [ ] **Step 1: Test gagal dulu** — `firmware/test/native_decode/main.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "bess_data.h"
#include "bess_decode.h"

static void test_scaling_telemetri() {
    uint16_t regs[59]; memset(regs, 0, sizeof(regs));
    regs[0] = 4000;                    // 1050: 400.0 V
    regs[6] = 5001;                    // 1056: 50.01 Hz
    regs[10] = (uint16_t)(int16_t)-125; // 1060: -12.5 kW (charge)
    regs[13] = 8064;                   // 1063: 806.4 V
    regs[14] = (uint16_t)(int16_t)-155; // 1064: -15.5 A
    regs[24] = 45;                     // 1074: 45 C
    regs[27] = 970;                    // 1077: 97.0 %
    regs[57] = 0; regs[58] = 1234;     // 1107/1108: 123.4 kWh discharge
    BessData d{};
    bessDecodeTelemetry(regs, d);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 400.0, d.grid_v_ab);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 50.01, d.grid_f_hz);
    TEST_ASSERT_FLOAT_WITHIN(0.01, -12.5, d.active_power_kw);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 806.4, d.dc_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.01, -15.5, d.dc_current_a);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 45, d.tube_temp_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 97.0, d.efficiency_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 123.4, d.total_discharge_kwh);
}

static void test_status_flags() {
    uint16_t regs[8]; memset(regs, 0, sizeof(regs));
    regs[7] = (1u << 6) | (1u << 5) | 0b1111 | (1u << 8) | (1u << 15);
    BessData d{};
    bessDecodeAlarmStatus(regs, d);
    TEST_ASSERT_TRUE(bessRunning(d));
    TEST_ASSERT_TRUE(bessCharging(d));
    TEST_ASSERT_FALSE(bessFault(d));
}

static void test_alarm_names() {
    TEST_ASSERT_EQUAL_STRING("dc_bus_overvoltage", bessAlarmName(0, 2));
    TEST_ASSERT_EQUAL_STRING("grid_undervoltage", bessAlarmName(2, 1));
    TEST_ASSERT_EQUAL_STRING("battery_over_discharge", bessAlarmName(5, 14));
    TEST_ASSERT_EQUAL_STRING("bms_comm_failure", bessAlarmName(6, 2));
    TEST_ASSERT_NULL(bessAlarmName(0, 15));
    TEST_ASSERT_EQUAL_STRING("running", bessStatusName(6));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scaling_telemetri);
    RUN_TEST(test_status_flags);
    RUN_TEST(test_alarm_names);
    return UNITY_END();
}
```

- [ ] **Step 2: Verifikasi FAIL**, **Step 3: Implementasi** — `lib/bess_core/bess_decode.cpp`:

```cpp
#include "bess_decode.h"
#include <stddef.h>

static inline float u10(uint16_t r)  { return r / 10.0f; }
static inline float s10(uint16_t r)  { return (int16_t)r / 10.0f; }
static inline float u100(uint16_t r) { return r / 100.0f; }

void bessDecodeTelemetry(const uint16_t r[59], BessData& d) {
    d.grid_v_ab = u10(r[0]);  d.grid_v_bc = u10(r[1]);  d.grid_v_ca = u10(r[2]);
    d.grid_i_a = u10(r[3]);   d.grid_i_b = u10(r[4]);   d.grid_i_c = u10(r[5]);
    d.grid_f_hz = u100(r[6]);
    d.power_factor = (int16_t)r[9] / 100.0f;
    d.active_power_kw = s10(r[10]);
    d.reactive_power_kvar = s10(r[11]);
    d.apparent_power_kva = u10(r[12]);
    d.dc_voltage_v = u10(r[13]);
    d.dc_current_a = s10(r[14]);
    d.dc_power_kw = s10(r[15]);
    d.tube_temp_c = (float)(int16_t)r[24];
    d.ambient_temp_c = (float)(int16_t)r[26];
    d.efficiency_pct = u10(r[27]);
    d.total_charge_kwh = (((uint32_t)r[55] << 16) | r[56]) / 10.0f;   // 1105/1106
    d.total_discharge_kwh = (((uint32_t)r[57] << 16) | r[58]) / 10.0f; // 1107/1108
}

void bessDecodeAlarmStatus(const uint16_t r[8], BessData& d) {
    for (int i = 0; i < 7; i++) d.alarm_raw[i] = r[i];
    d.status_raw = r[7];
}

// Nama identik dengan bess_sim/alarms.py — jaga paritas!
static const char* W2050[16] = {"positive_bus_overvoltage", "negative_bus_overvoltage",
    "dc_bus_overvoltage", "bus_half_voltage_unbalance", "dc_bus_short_circuit",
    "dc_overcurrent", "balance_bridge_overcurrent", "dc_voltage_reverse",
    "dc_voltage_low", "dc_voltage_high", "insulation_impedance_abnormal",
    "pv_power_low_shutdown", NULL, NULL, NULL, NULL};
static const char* W2051[16] = {"inverter_voltage_a_abnormal", "inverter_voltage_b_abnormal",
    "inverter_voltage_c_abnormal", "inverter_voltage_dc_a_abnormal",
    "inverter_voltage_dc_b_abnormal", "inverter_voltage_dc_c_abnormal",
    "output_overload_shutdown_a", "output_overload_shutdown_b",
    "output_overload_shutdown_c", "output_overcurrent_a", "output_overcurrent_b",
    "output_overcurrent_c", "output_short_circuit_a", "output_short_circuit_b",
    "output_short_circuit_c", "inverter_phase_desync"};
static const char* W2052[16] = {"grid_overvoltage", "grid_undervoltage",
    "grid_overfrequency", "grid_underfrequency", "islanding_protection",
    "grid_wrong_phase", "ac_power_failure", "ac_current_limit_shutdown",
    "parallel_cable_fault", "carrier_sync_fault", "inverter_sync_fault",
    "parallel_comm_fault", "ac_fuse_failure", "power_tube_over_temperature",
    "power_supply_fault", "leakage_current_fault"};
static const char* W2053[16] = {"dc_precharge_fault", "ac_precharge_fault",
    "dc_relay_short_circuit", "dc_relay_open_circuit", "ac_relay_short_a",
    "ac_relay_short_b", "ac_relay_short_c", "ac_relay_open_a", "ac_relay_open_b",
    "ac_relay_open_c", "bridge_arm_shoot_through_a", "bridge_arm_shoot_through_b",
    "bridge_arm_shoot_through_c", NULL, NULL, NULL};
static const char* W2054[16] = {"grid_current_zero_bias", "inverter_current_zero_bias",
    "inverter_current_dc_zero_bias", "dc_current_zero_bias",
    "balance_bridge_current_zero_bias", "leakage_current_zero_bias",
    "reference_2v5_abnormal", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const char* W2055[16] = {"output_overload_alarm_a", "output_overload_alarm_b",
    "output_overload_alarm_c", "low_voltage_ride_through", "high_voltage_ride_through",
    "balance_bridge_current_limit_alarm", "balance_bridge_over_temperature",
    "ambient_over_temperature", "over_temperature_derating", "dc_spd_fault",
    "ac_spd_fault", "fan_fault_1", "fan_fault_2", "battery_over_charge",
    "battery_over_discharge", NULL};
static const char* W2056[16] = {"internal_comm_failure", "ems_comm_failure",
    "bms_comm_failure", "bms_fault", "dry_contact_1_fault", "dry_contact_2_fault",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static const char** ALARM_WORDS[7] = {W2050, W2051, W2052, W2053, W2054, W2055, W2056};
static const char* STATUS[16] = {"dc_precharge", "ac_soft_start", "dc_relay",
    "ac_relay", "off_grid", "charging", "running", "fault", "master_machine",
    "master_cabinet", "standby", "shutdown", "epo", NULL, NULL, "param_init_done"};

const char* bessAlarmName(int word, int bit) {
    if (word < 0 || word > 6 || bit < 0 || bit > 15) return NULL;
    return ALARM_WORDS[word][bit];
}
const char* bessStatusName(int bit) {
    if (bit < 0 || bit > 15) return NULL;
    return STATUS[bit];
}
```

- [ ] **Step 4: Test PASS** — `pio test -e native`.

- [ ] **Step 5: Commit** — `git add firmware && git commit -m "feat(fw): BessData decode + tabel nama alarm/status (paritas sim)"`

### Task 12: Payload telemetri JSON + parser command + ack

**Files:**
- Create: `firmware/lib/bess_core/payload.h`, `firmware/lib/bess_core/payload.cpp`, `firmware/lib/bess_core/commands.h`, `firmware/lib/bess_core/commands.cpp`
- Test: `firmware/test/native_payload/main.cpp`

**Interfaces:**
- Produces:

```cpp
// payload.h
struct SysInfo {
    char gw[13];              // MAC 12 hex + NUL
    const char* fw_version;   // "bess-0.1.0"
    uint32_t uptime_ms, seq, ts;
    bool time_valid;
    int rssi; const char* ssid; char ip[16];
};
size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap);
// commands.h
struct Command {
    enum Type { NONE, ENABLE, DISABLE, SET_POWER, UNSUPPORTED, BAD_JSON } type;
    char id[40];        // "" bila tak ada
    char name[24];      // nama cmd mentah
    float power_w;      // hanya SET_POWER
    bool has_power;
};
void parseCommand(const char* json, size_t len, Command& out);
// applied_pct pakai NAN bila tidak relevan
size_t buildAckJson(const Command& c, const char* result, const char* detail,
                    float applied_pct, float applied_w, uint32_t ts,
                    char* out, size_t cap);
```

- Telemetri berisi: `gw, ts, seq, api_schema_version=1`, `data.device_type="bess"`, `data.firmware_version`, `data.device_id`, `data.uptime_ms`, `data.network{ssid,ip,rssi}`, `data.time_valid`, `data.bess{...}` — field per spec §6.1, termasuk `alarms_decoded` (SEMUA nama bit, nilai bool) dan `status_decoded`.

- [ ] **Step 1: Test gagal dulu** — `firmware/test/native_payload/main.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include <math.h>
#include <ArduinoJson.h>
#include "bess_data.h"
#include "payload.h"
#include "commands.h"

static SysInfo sys_() {
    SysInfo s{};
    strcpy(s.gw, "AABBCCDDEEFF");
    s.fw_version = "bess-0.1.0";
    s.uptime_ms = 123456; s.seq = 7; s.ts = 1785000000; s.time_valid = true;
    s.rssi = -55; s.ssid = "Lantai 2"; strcpy(s.ip, "192.168.1.50");
    return s;
}

static void test_telemetry_envelope() {
    SysInfo s = sys_();
    BessData d{};
    d.active_power_kw = 5.0f; d.soc_pct = 47.5f; d.rated_kw = 50.0f;
    d.status_raw = (1u << 6) | (1u << 15);
    d.alarm_raw[2] = 0b10;    // grid_undervoltage
    static char buf[8192];
    size_t n = buildTelemetryJson(s, d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < sizeof(buf));
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, buf) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF", doc["gw"]);
    TEST_ASSERT_EQUAL(7, (int)doc["seq"]);
    TEST_ASSERT_EQUAL(1, (int)doc["api_schema_version"]);
    TEST_ASSERT_EQUAL_STRING("bess", doc["data"]["device_type"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 5.0, doc["data"]["bess"]["active_power_kw"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 47.5, doc["data"]["bess"]["soc_percent"]);
    TEST_ASSERT_TRUE(doc["data"]["bess"]["running"].as<bool>());
    TEST_ASSERT_TRUE(doc["data"]["bess"]["alarms_decoded"]["grid_undervoltage"].as<bool>());
    TEST_ASSERT_FALSE(doc["data"]["bess"]["alarms_decoded"]["dc_bus_overvoltage"].as<bool>());
    TEST_ASSERT_TRUE(doc["data"]["bess"]["status_decoded"]["running"].as<bool>());
}

static void test_parse_enable() {
    Command c;
    const char* j = "{\"id\":\"a1\",\"cmd\":\"enable\",\"args\":{}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::ENABLE, c.type);
    TEST_ASSERT_EQUAL_STRING("a1", c.id);
}

static void test_parse_set_power() {
    Command c;
    const char* j = "{\"id\":\"b2\",\"cmd\":\"set_power\",\"args\":{\"power_w\":5000}}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::SET_POWER, c.type);
    TEST_ASSERT_TRUE(c.has_power);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, c.power_w);
}

static void test_parse_unsupported_dan_bad_json() {
    Command c;
    const char* j = "{\"id\":\"x\",\"cmd\":\"fly\"}";
    parseCommand(j, strlen(j), c);
    TEST_ASSERT_EQUAL(Command::UNSUPPORTED, c.type);
    parseCommand("{oops", 5, c);
    TEST_ASSERT_EQUAL(Command::BAD_JSON, c.type);
}

static void test_ack() {
    Command c{};
    c.type = Command::SET_POWER;
    strcpy(c.id, "b2"); strcpy(c.name, "set_power");
    static char buf[512];
    size_t n = buildAckJson(c, "accepted", "", 10.0f, 5000.0f, 1785000001, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    JsonDocument doc;
    deserializeJson(doc, buf);
    TEST_ASSERT_EQUAL_STRING("b2", doc["id"]);
    TEST_ASSERT_EQUAL_STRING("accepted", doc["result"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0, doc["applied"]["power_pct"]);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 5000, doc["applied"]["power_w"]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_envelope);
    RUN_TEST(test_parse_enable);
    RUN_TEST(test_parse_set_power);
    RUN_TEST(test_parse_unsupported_dan_bad_json);
    RUN_TEST(test_ack);
    return UNITY_END();
}
```

- [ ] **Step 2: Verifikasi FAIL**, **Step 3: Implementasi** — `lib/bess_core/payload.cpp`:

```cpp
#include "payload.h"
#include <ArduinoJson.h>
#include "bess_decode.h"

size_t buildTelemetryJson(const SysInfo& s, const BessData& d, char* out, size_t cap) {
    JsonDocument doc;
    doc["gw"] = s.gw;
    doc["ts"] = s.ts;
    doc["seq"] = s.seq;
    doc["api_schema_version"] = 1;
    JsonObject data = doc["data"].to<JsonObject>();
    data["device_type"] = "bess";
    data["api_schema_version"] = 1;
    data["firmware_version"] = s.fw_version;
    data["device_id"] = s.gw;
    data["uptime_ms"] = s.uptime_ms;
    data["time_valid"] = s.time_valid;
    JsonObject net = data["network"].to<JsonObject>();
    net["ssid"] = s.ssid; net["ip"] = s.ip; net["rssi"] = s.rssi;
    JsonObject b = data["bess"].to<JsonObject>();
    b["grid_voltage_ab_v"] = d.grid_v_ab;
    b["grid_voltage_bc_v"] = d.grid_v_bc;
    b["grid_voltage_ca_v"] = d.grid_v_ca;
    b["grid_current_a_a"] = d.grid_i_a;
    b["grid_current_b_a"] = d.grid_i_b;
    b["grid_current_c_a"] = d.grid_i_c;
    b["grid_frequency_hz"] = d.grid_f_hz;
    b["power_factor"] = d.power_factor;
    b["active_power_kw"] = d.active_power_kw;
    b["reactive_power_kvar"] = d.reactive_power_kvar;
    b["apparent_power_kva"] = d.apparent_power_kva;
    b["dc_voltage_v"] = d.dc_voltage_v;
    b["dc_current_a"] = d.dc_current_a;
    b["dc_power_kw"] = d.dc_power_kw;
    b["power_tube_temp_c"] = d.tube_temp_c;
    b["ambient_temp_c"] = d.ambient_temp_c;
    b["efficiency_percent"] = d.efficiency_pct;
    b["soc_percent"] = d.soc_pct;
    b["rated_power_kw"] = d.rated_kw;
    b["power_setpoint_percent"] = d.setpoint_pct;
    b["total_charge_kwh"] = d.total_charge_kwh;
    b["total_discharge_kwh"] = d.total_discharge_kwh;
    b["running"] = bessRunning(d);
    b["charging"] = bessCharging(d);
    b["standby"] = bessStandby(d);
    b["fault"] = bessFault(d);
    b["grid_connected"] = !bessOffGrid(d);
    b["epo"] = bessEpo(d);
    b["comm_lost"] = d.comm_lost;
    JsonObject al = b["alarms_decoded"].to<JsonObject>();
    for (int w = 0; w < 7; w++)
        for (int bit = 0; bit < 16; bit++) {
            const char* nm = bessAlarmName(w, bit);
            if (nm) al[nm] = (d.alarm_raw[w] >> bit) & 1;
        }
    JsonObject stt = b["status_decoded"].to<JsonObject>();
    for (int bit = 0; bit < 16; bit++) {
        const char* nm = bessStatusName(bit);
        if (nm) stt[nm] = (d.status_raw >> bit) & 1;
    }
    return serializeJson(doc, out, cap);
}
```

`lib/bess_core/commands.cpp`:

```cpp
#include "commands.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

static void scopy(char* dst, size_t cap, const char* src) {
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

void parseCommand(const char* json, size_t len, Command& out) {
    out = Command{};
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
        out.type = Command::BAD_JSON;
        return;
    }
    scopy(out.id, sizeof(out.id), doc["id"] | "");
    scopy(out.name, sizeof(out.name), doc["cmd"] | "");
    if (!strcmp(out.name, "enable")) out.type = Command::ENABLE;
    else if (!strcmp(out.name, "disable")) out.type = Command::DISABLE;
    else if (!strcmp(out.name, "set_power")) {
        out.type = Command::SET_POWER;
        JsonVariant p = doc["args"]["power_w"];
        out.has_power = !p.isNull();
        out.power_w = out.has_power ? p.as<float>() : 0.0f;
    } else out.type = Command::UNSUPPORTED;
}

size_t buildAckJson(const Command& c, const char* result, const char* detail,
                    float applied_pct, float applied_w, uint32_t ts,
                    char* out, size_t cap) {
    JsonDocument doc;
    doc["id"] = c.id;
    doc["cmd"] = c.name[0] ? c.name : "unknown";
    doc["result"] = result;
    doc["detail"] = detail;
    JsonObject ap = doc["applied"].to<JsonObject>();
    if (!isnan(applied_pct)) ap["power_pct"] = applied_pct;
    if (!isnan(applied_w)) ap["power_w"] = applied_w;
    doc["ts"] = ts;
    return serializeJson(doc, out, cap);
}
```

- [ ] **Step 4: Test PASS** — `pio test -e native` (empat suite native semua hijau).

- [ ] **Step 5: Commit** — `git add firmware && git commit -m "feat(fw): payload telemetri V11 blok bess + parser command & ack"`

### Task 13: Config, secrets, state, WiFi, main skeleton (build + smoke di hardware)

**Files:**
- Create: `firmware/src/config.h`, `firmware/src/secrets.example.h`, `firmware/src/secrets.h` (lokal, TIDAK di-commit), `firmware/src/state.h`, `firmware/src/state.cpp`, `firmware/src/wifi_mgr.h`, `firmware/src/wifi_mgr.cpp`, `firmware/src/main.cpp`

**Interfaces:**
- Produces: `g_state` (`AppState{BessData bess; uint32_t seq; SemaphoreHandle_t mtx;}` + `stateInit/stateLock/stateUnlock`), `wifiInit()`, `wifiTick()` (satu-satunya penggerak reconnect, backoff 4 s ×2 cap 60 s), `wifiConnected()`, `wifiGw(char out[13])` (MAC 12 hex uppercase), konstanta pin/konfig di `config.h`. `main.cpp` membuat task di Task 14–15 (sementara: hanya WiFi + log).

- [ ] **Step 1: Implementasi** — `src/config.h`:

```cpp
#pragma once
// Pin — fakta hardware dari BEPESP32_WiFi_Extension/src/Config.h
#define PIN_BESS_RX        21
#define PIN_BESS_TX        20
#define PIN_BESS_REDE      22
#define PIN_LED_BESS       18
#define PIN_LED_WIFI       14
// Modbus BESS (BSL AC series V2.1.0)
#define BESS_BAUD          9600
#define BESS_NODE          1
#define MB_FRAME_GAP_MS    105     // spec: >= 100 ms
#define MB_TIMEOUT_MS      500
#define MB_RETRIES         2       // total 3 percobaan
#define POLL_PERIOD_MS     1500
#define COMM_LOST_AFTER    3       // siklus gagal beruntun
// Register kunci
#define REG_TELEM_START    1050
#define REG_TELEM_COUNT    59
#define REG_ALARM_START    2050
#define REG_ALARM_COUNT    8
#define REG_P_SET          3050
#define REG_PARAM_START    3146
#define REG_PARAM_COUNT    39      // 3146..3184 (rated .. soc)
#define REG_ONOFF          5050
// MQTT
#define MQTT_KEEPALIVE_S       300
#define MQTT_NETWORK_TIMEOUT_MS 60000
#define TELEMETRY_PERIOD_MS    60000
#define FW_VERSION         "bess-0.1.0"
```

`src/secrets.example.h` (salin ke `secrets.h`, isi kredensial asli; `secrets.h` sudah di `.gitignore`):

```cpp
#pragma once
#define WIFI_SSID     "isi-ssid"
#define WIFI_PASS     "isi-password"
#define MQTT_URI      "mqtt://mqtt-dev.bepbatt.id:1883"
#define MQTT_USER     "isi-user"
#define MQTT_PASSWD   "isi-pass"
```

`src/state.h`:

```cpp
#pragma once
#include <Arduino.h>
#include "bess_data.h"

struct AppState {
    BessData bess;
    uint32_t seq = 0;
    SemaphoreHandle_t mtx = nullptr;
};
extern AppState g_state;
void stateInit();
void stateLock();
void stateUnlock();
```

`src/state.cpp`:

```cpp
#include "state.h"
AppState g_state;
void stateInit() { g_state.mtx = xSemaphoreCreateMutex(); }
void stateLock() { xSemaphoreTake(g_state.mtx, portMAX_DELAY); }
void stateUnlock() { xSemaphoreGive(g_state.mtx); }
```

`src/wifi_mgr.cpp` (`wifi_mgr.h` = deklarasi fungsi di Interfaces):

```cpp
#include "wifi_mgr.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "secrets.h"

static uint32_t next_try_ms = 0;
static uint32_t backoff_ms = 4000;

void wifiInit() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_country_code("ID", true);      // kanal 1-13 (pelajaran reason=203)
    WiFi.setAutoReconnect(false);               // wifiTick satu-satunya driver
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void wifiTick() {
    if (WiFi.status() == WL_CONNECTED) { backoff_ms = 4000; return; }
    uint32_t now = millis();
    if (now >= next_try_ms) {
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        next_try_ms = now + backoff_ms;
        backoff_ms = min(backoff_ms * 2, (uint32_t)60000);
    }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

void wifiGw(char out[13]) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
```

`src/main.cpp` (skeleton — task dibuat di Task 14–15):

```cpp
#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "state.h"
#include "wifi_mgr.h"

void setup() {
    Serial.begin(115200);           // USB-CDC (COM3)
    pinMode(PIN_LED_WIFI, OUTPUT);
    pinMode(PIN_LED_BESS, OUTPUT);
    stateInit();
    wifiInit();
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[boot] gateway-bess " FW_VERSION);
}

void loop() {
    wifiTick();
    digitalWrite(PIN_LED_WIFI, wifiConnected() ? HIGH : LOW);
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        Serial.printf("[wifi] %s rssi=%d ip=%s\n",
                      wifiConnected() ? "OK" : "putus", WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
    }
    delay(100);
}
```

- [ ] **Step 2: Build** — `pio run -e esp32c6` → SUCCESS. (`pio test -e native` tetap hijau — file `src/` tidak ikut build native.)

- [ ] **Step 3: Smoke di hardware** — salin `secrets.example.h` → `secrets.h`, isi kredensial WiFi bench + MQTT dev (minta ke user bila belum ada). `pio run -e esp32c6 -t upload --upload-port COM3` lalu `pio device monitor -p COM3 -b 115200` → log `[wifi] OK` + IP muncul.

- [ ] **Step 4: Commit** — `git add firmware && git commit -m "feat(fw): skeleton main + wifi_mgr (backoff, country ID) + state"` — pastikan `git status` TIDAK menampilkan `secrets.h`.

### Task 14: ModbusMaster runtime + task poll BESS (uji bench vs simulator)

**Files:**
- Create: `firmware/src/modbus_port.h`, `firmware/src/modbus_port.cpp`, `firmware/src/task_bess.h`, `firmware/src/task_bess.cpp`
- Modify: `firmware/src/main.cpp` (buat task)

**Interfaces:**
- Consumes: `mb_frame.h`, `bess_decode.h`, `g_state`.
- Produces:

```cpp
// modbus_port.h — dipakai task_bess DAN task_cmd (Task 15); thread-safe via mutex internal
void mbPortInit();     // Serial1 9600 8N1 RX21/TX20 + REDE22
MbStatus mbReadRegs(uint8_t node, uint16_t start, uint16_t count,
                    uint16_t* out, uint8_t* exc);   // retry internal MB_RETRIES
MbStatus mbWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t* exc);
MbStatus mbWrite5(uint8_t node, uint16_t id, bool on, uint8_t* exc);
// task_bess.h
void taskBessStart();  // xTaskCreate loop poll
```

- [ ] **Step 1: Implementasi** — `src/modbus_port.cpp`:

```cpp
#include "modbus_port.h"
#include <Arduino.h>
#include "config.h"
#include "crc16.h"
#include "mb_frame.h"

static SemaphoreHandle_t mb_mtx;
static uint32_t last_frame_ms = 0;

void mbPortInit() {
    mb_mtx = xSemaphoreCreateMutex();
    pinMode(PIN_BESS_REDE, OUTPUT);
    digitalWrite(PIN_BESS_REDE, LOW);           // RX
    Serial1.begin(BESS_BAUD, SERIAL_8N1, PIN_BESS_RX, PIN_BESS_TX);
}

static size_t xfer(const uint8_t* req, size_t reqlen, uint8_t* resp, size_t cap) {
    while (millis() - last_frame_ms < MB_FRAME_GAP_MS) vTaskDelay(pdMS_TO_TICKS(5));
    while (Serial1.available()) Serial1.read();  // buang sisa
    digitalWrite(PIN_BESS_REDE, HIGH);
    Serial1.write(req, reqlen);
    Serial1.flush();
    digitalWrite(PIN_BESS_REDE, LOW);
    size_t got = 0;
    uint32_t t0 = millis(), last_rx = millis();
    while (millis() - t0 < MB_TIMEOUT_MS) {
        while (Serial1.available() && got < cap) {
            resp[got++] = Serial1.read();
            last_rx = millis();
        }
        // frame dianggap selesai: ada data & sunyi 10 ms
        if (got >= 5 && millis() - last_rx > 10) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    last_frame_ms = millis();
    return got;
}

template <typename ParseFn>
static MbStatus doReq(const uint8_t* req, size_t reqlen, ParseFn parse) {
    xSemaphoreTake(mb_mtx, portMAX_DELAY);
    MbStatus st = MB_TIMEOUT;
    for (int attempt = 0; attempt <= MB_RETRIES; attempt++) {
        uint8_t resp[300];
        size_t n = xfer(req, reqlen, resp, sizeof(resp));
        st = (n == 0) ? MB_TIMEOUT : parse(resp, n);
        if (st == MB_OK || st == MB_EXCEPTION) break;   // exception = jawaban sah
    }
    xSemaphoreGive(mb_mtx);
    return st;
}

MbStatus mbReadRegs(uint8_t node, uint16_t start, uint16_t count,
                    uint16_t* out, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildRead(node, start, count, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseReadResp(r, len, node, count, out, exc);
    });
}

MbStatus mbWrite6(uint8_t node, uint16_t id, uint16_t val, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildWrite6(node, id, val, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseEcho(r, len, node, 6, exc);
    });
}

MbStatus mbWrite5(uint8_t node, uint16_t id, bool on, uint8_t* exc) {
    uint8_t req[8];
    size_t n = mbBuildWrite5(node, id, on, req);
    return doReq(req, n, [&](const uint8_t* r, size_t len) {
        return mbParseEcho(r, len, node, 5, exc);
    });
}
```

`src/task_bess.cpp`:

```cpp
#include "task_bess.h"
#include <Arduino.h>
#include "config.h"
#include "modbus_port.h"
#include "bess_decode.h"
#include "state.h"

static void pollOnce(bool& ok) {
    uint16_t telem[REG_TELEM_COUNT], alst[REG_ALARM_COUNT], pset[1],
             param[REG_PARAM_COUNT];
    uint8_t exc;
    ok = mbReadRegs(BESS_NODE, REG_TELEM_START, REG_TELEM_COUNT, telem, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_ALARM_START, REG_ALARM_COUNT, alst, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_P_SET, 1, pset, &exc) == MB_OK
      && mbReadRegs(BESS_NODE, REG_PARAM_START, REG_PARAM_COUNT, param, &exc) == MB_OK;
    if (!ok) return;
    stateLock();
    BessData& d = g_state.bess;
    bessDecodeTelemetry(telem, d);
    bessDecodeAlarmStatus(alst, d);
    d.setpoint_pct = (int16_t)pset[0] / 10.0f;
    d.rated_kw = param[0] / 10.0f;               // 3146
    d.soc_pct = param[38] / 10.0f;               // 3184
    d.comm_lost = false;
    d.last_ok_ms = millis();
    stateUnlock();
}

static void run(void*) {
    int fail = 0;
    for (;;) {
        bool ok = false;
        pollOnce(ok);
        if (ok) {
            fail = 0;
            digitalWrite(PIN_LED_BESS, HIGH);
        } else if (++fail >= COMM_LOST_AFTER) {
            stateLock();
            g_state.bess.comm_lost = true;       // nilai lama dipertahankan
            stateUnlock();
            digitalWrite(PIN_LED_BESS, LOW);
        }
        static uint32_t lastlog = 0;
        if (millis() - lastlog > 5000) {
            lastlog = millis();
            stateLock();
            Serial.printf("[bess] %s p=%.1fkW soc=%.1f%% vdc=%.1fV status=0x%04X\n",
                          g_state.bess.comm_lost ? "COMM_LOST" : "OK",
                          g_state.bess.active_power_kw, g_state.bess.soc_pct,
                          g_state.bess.dc_voltage_v, g_state.bess.status_raw);
            stateUnlock();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void taskBessStart() {
    xTaskCreate(run, "task_bess", 4096, nullptr, 3, nullptr);
}
```

Modifikasi `src/main.cpp`: tambah `#include "modbus_port.h"` + `#include "task_bess.h"`, dan di akhir `setup()`: `mbPortInit(); taskBessStart();`.

- [ ] **Step 2: Build + native tetap hijau** — `pio run -e esp32c6` dan `pio test -e native`.

- [ ] **Step 3: Uji bench nyata** — di terminal 1: `cd bess-sim && uv run bess-sim run --port COM10 --soc 60`. Flash & monitor gateway. **Verifikasi di log COM3:** `[bess] OK ... soc=60.0% vdc=8xx.xV status=0x...` (bukan COMM_LOST), dan di terminal simulator muncul aktivitas query. Cabut kabel RS485 sebentar → log berubah `COMM_LOST` → colok lagi → pulih.

- [ ] **Step 4: Commit** — `git add firmware && git commit -m "feat(fw): modbus master runtime + task poll BESS (verified vs simulator)"`

### Task 15: MQTT uplink + command handler + ack (end-to-end bench)

**Files:**
- Create: `firmware/src/mqtt_link.h`, `firmware/src/mqtt_link.cpp`, `firmware/src/task_cmd.h`, `firmware/src/task_cmd.cpp`, `bess-sim/tools/cloud_probe.py`
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `payload.h`, `commands.h`, `modbus_port.h`, `g_state`, `wifiGw()`.
- Produces:

```cpp
// mqtt_link.h
void mqttInit(const char* gw);                       // connect + LWT + subscribe command
bool mqttConnected();
bool mqttEnqueueTelemetry(const char* json, size_t n); // esp_mqtt_client_enqueue QoS1
bool mqttPublishAck(const char* json, size_t n);       // esp_mqtt_client_publish QoS1
// task_cmd.h
void taskCmdStart();
void taskCmdSubmit(const char* json, size_t n);      // dipanggil dari event MQTT (copy ke queue)
```

- [ ] **Step 1: Implementasi** — `src/mqtt_link.cpp`:

```cpp
#include "mqtt_link.h"
#include <Arduino.h>
#include <mqtt_client.h>
#include "config.h"
#include "secrets.h"
#include "task_cmd.h"

static esp_mqtt_client_handle_t cli = nullptr;
static bool connected = false;
static char t_telemetry[48], t_status[48], t_command[48], t_ack[52];

static void onEvent(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    auto* e = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            esp_mqtt_client_publish(cli, t_status, "online", 0, 1, 1);
            esp_mqtt_client_subscribe(cli, t_command, 1);
            Serial.println("[mqtt] connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            Serial.println("[mqtt] disconnected");
            break;
        case MQTT_EVENT_DATA:
            if (e->topic_len == strlen(t_command) &&
                !strncmp(e->topic, t_command, e->topic_len))
                taskCmdSubmit(e->data, e->data_len);
            break;
        default: break;
    }
}

void mqttInit(const char* gw) {
    snprintf(t_telemetry, sizeof(t_telemetry), "device/%s/telemetry", gw);
    snprintf(t_status, sizeof(t_status), "device/%s/status", gw);
    snprintf(t_command, sizeof(t_command), "device/%s/command", gw);
    snprintf(t_ack, sizeof(t_ack), "device/%s/command/ack", gw);
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = MQTT_URI;
    cfg.credentials.username = MQTT_USER;
    cfg.credentials.authentication.password = MQTT_PASSWD;
    cfg.session.keepalive = MQTT_KEEPALIVE_S;
    cfg.network.timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    cfg.session.last_will.topic = t_status;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cli = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(cli, MQTT_EVENT_ANY, onEvent, nullptr);
    esp_mqtt_client_start(cli);
}

bool mqttConnected() { return connected; }

bool mqttEnqueueTelemetry(const char* json, size_t n) {
    if (!cli) return false;
    return esp_mqtt_client_enqueue(cli, t_telemetry, json, n, 1, 0, true) >= 0;
}

bool mqttPublishAck(const char* json, size_t n) {
    if (!cli || !connected) return false;
    return esp_mqtt_client_publish(cli, t_ack, json, n, 1, 0) >= 0;
}
```

`src/task_cmd.cpp` — eksekusi perintah (validasi → tulis register → verifikasi → ack):

```cpp
#include "task_cmd.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "commands.h"
#include "config.h"
#include "mqtt_link.h"
#include "modbus_port.h"
#include "bess_decode.h"
#include "state.h"

struct RawCmd { char json[512]; size_t len; };
static QueueHandle_t q;

void taskCmdSubmit(const char* json, size_t n) {
    RawCmd rc{};
    rc.len = min(n, sizeof(rc.json) - 1);
    memcpy(rc.json, json, rc.len);
    xQueueSend(q, &rc, 0);
}

static uint32_t nowTs() { return (uint32_t)time(nullptr); }

static void sendAck(const Command& c, const char* result, const char* detail,
                    float pct = NAN, float w = NAN) {
    static char buf[512];
    size_t n = buildAckJson(c, result, detail, pct, w, nowTs(), buf, sizeof(buf));
    mqttPublishAck(buf, n);
    Serial.printf("[cmd] %s -> %s %s\n", c.name, result, detail);
}

static bool waitStatusBit(int bit, bool want, uint32_t timeout_ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < timeout_ms) {
        uint16_t w; uint8_t exc;
        if (mbReadRegs(BESS_NODE, 2057, 1, &w, &exc) == MB_OK &&
            (bool)((w >> bit) & 1) == want)
            return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

static void doOnOff(const Command& c, bool on) {
    stateLock();
    bool lost = g_state.bess.comm_lost;
    bool fault = bessFault(g_state.bess);
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    if (on && fault) { sendAck(c, "rejected", "bess_fault"); return; }
    uint8_t exc = 0;
    MbStatus st = MB_TIMEOUT;
    for (int i = 0; i < 20; i++) {                    // busy (exc 6) → coba lagi
        st = mbWrite5(BESS_NODE, REG_ONOFF, on, &exc);
        if (st == MB_OK || (st == MB_EXCEPTION && exc != 6)) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (st != MB_OK) { sendAck(c, "rejected", "bess_no_ack"); return; }
    // bukti nyata: bit Run (6) untuk on, bit Shutdown (11) untuk off
    bool okBit = on ? waitStatusBit(6, true, 10000) : waitStatusBit(11, true, 10000);
    sendAck(c, okBit ? "accepted" : "rejected", okBit ? "" : "bess_no_ack");
}

static void doSetPower(const Command& c) {
    if (!c.has_power) { sendAck(c, "rejected", "bad_value"); return; }
    stateLock();
    bool lost = g_state.bess.comm_lost;
    float rated_w = g_state.bess.rated_kw * 1000.0f;
    stateUnlock();
    if (lost) { sendAck(c, "rejected", "comm_lost"); return; }
    if (rated_w <= 0) { sendAck(c, "rejected", "bess_no_ack"); return; }
    float pct = c.power_w / rated_w * 100.0f;
    if (fabsf(pct) > 120.0f) { sendAck(c, "rejected", "bad_value"); return; }
    int16_t raw = (int16_t)lroundf(pct * 10.0f);
    uint8_t exc = 0;
    if (mbWrite6(BESS_NODE, REG_P_SET, (uint16_t)raw, &exc) != MB_OK) {
        sendAck(c, "rejected", exc == 6 ? "bess_busy" : "bess_no_ack");
        return;
    }
    uint16_t rb;
    if (mbReadRegs(BESS_NODE, REG_P_SET, 1, &rb, &exc) != MB_OK ||
        (int16_t)rb != raw) {
        sendAck(c, "rejected", "readback_mismatch");
        return;
    }
    sendAck(c, "accepted", "", raw / 10.0f, raw / 1000.0f * rated_w / 10.0f * 10.0f);
}

static void run(void*) {
    RawCmd rc;
    for (;;) {
        if (xQueueReceive(q, &rc, portMAX_DELAY) != pdTRUE) continue;
        Command c;
        parseCommand(rc.json, rc.len, c);
        switch (c.type) {
            case Command::ENABLE:  doOnOff(c, true); break;
            case Command::DISABLE: doOnOff(c, false); break;
            case Command::SET_POWER: doSetPower(c); break;
            case Command::BAD_JSON: sendAck(c, "rejected", "bad_json"); break;
            default: sendAck(c, "rejected", "unsupported_cmd"); break;
        }
    }
}

void taskCmdStart() {
    q = xQueueCreate(4, sizeof(RawCmd));
    xTaskCreate(run, "task_cmd", 6144, nullptr, 2, nullptr);
}
```

Modifikasi `src/main.cpp` — setelah `taskBessStart()`:

```cpp
    static char gw[13];
    wifiGw(gw);
    taskCmdStart();
    mqttInit(gw);
```

dan di `loop()` tambah pengiriman telemetri 60 detik (sesudah blok log 5 detik):

```cpp
    static uint32_t last_telem = 0;
    if (millis() - last_telem > TELEMETRY_PERIOD_MS && wifiConnected()) {
        last_telem = millis();
        static char json[8192];
        SysInfo si{};
        wifiGw(si.gw);
        si.fw_version = FW_VERSION;
        si.uptime_ms = millis();
        si.ts = (uint32_t)time(nullptr);
        si.time_valid = si.ts > 1600000000u;
        si.rssi = WiFi.RSSI();
        si.ssid = WIFI_SSID;
        snprintf(si.ip, sizeof(si.ip), "%s", WiFi.localIP().toString().c_str());
        stateLock();
        si.seq = ++g_state.seq;
        BessData snapshot = g_state.bess;
        stateUnlock();
        size_t n = buildTelemetryJson(si, snapshot, json, sizeof(json));
        mqttEnqueueTelemetry(json, n);
    }
```

(+ `#include <WiFi.h>`, `#include "payload.h"`, `#include "mqtt_link.h"`, `#include "task_cmd.h"` di main.cpp.)

- [ ] **Step 2: Alat uji cloud** — `bess-sim/tools/cloud_probe.py` (jalankan `uv run --with paho-mqtt python tools/cloud_probe.py ...`):

```python
"""Probe MQTT: lihat telemetri/ack gateway-bess & kirim perintah."""
import argparse
import json
import time
import uuid

import paho.mqtt.client as mqtt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="mqtt-dev.bepbatt.id")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user"); ap.add_argument("--passwd")
    ap.add_argument("--gw", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("watch")
    sub.add_parser("enable"); sub.add_parser("disable")
    ps = sub.add_parser("set_power"); ps.add_argument("--watt", type=float, required=True)
    a = ap.parse_args()

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if a.user:
        cli.username_pw_set(a.user, a.passwd)
    cli.connect(a.host, a.port, 60)

    def on_msg(_c, _u, m):
        print(f"\n== {m.topic} ==")
        try:
            doc = json.loads(m.payload)
            if "data" in doc:
                b = doc["data"].get("bess", {})
                print(f"seq={doc['seq']} type={doc['data'].get('device_type')} "
                      f"p={b.get('active_power_kw')}kW soc={b.get('soc_percent')}% "
                      f"running={b.get('running')} comm_lost={b.get('comm_lost')}")
            else:
                print(json.dumps(doc, indent=1))
        except json.JSONDecodeError:
            print(m.payload.decode(errors="replace"))

    cli.on_message = on_msg
    cli.subscribe([(f"device/{a.gw}/telemetry", 1),
                   (f"device/{a.gw}/command/ack", 1),
                   (f"device/{a.gw}/status", 1)])
    if a.cmd != "watch":
        args = {"power_w": a.watt} if a.cmd == "set_power" else {}
        payload = {"id": str(uuid.uuid4())[:8], "ts": int(time.time()),
                   "cmd": a.cmd, "args": args, "api_schema_version": 1}
        cli.publish(f"device/{a.gw}/command", json.dumps(payload), qos=1)
        print("perintah terkirim:", payload)
    cli.loop_forever()


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Build + native hijau** — `pio run -e esp32c6` && `pio test -e native`.

- [ ] **Step 4: Uji end-to-end di bench** — simulator jalan di COM10, firmware di-flash:
  1. `uv run --with paho-mqtt python tools/cloud_probe.py --gw <MAC> --user ... --passwd ... watch` → telemetri `device_type=bess` masuk tiap 60 s, `status` = `online`.
  2. `... enable` → ack `accepted`; terminal simulator menunjukkan transisi PRECHARGE→RUN; telemetri berikutnya `running=true`.
  3. `... set_power --watt 5000` → ack `accepted` `applied.power_pct=10`; telemetri `active_power_kw≈5.0`, SOC menurun perlahan.
  4. `... set_power --watt 99999` → ack `rejected bad_value`.
  5. `... disable` → ack `accepted`, `running=false`, daya kembali 0.
  6. Matikan simulator → telemetri berikutnya `comm_lost=true`; `enable` → `rejected comm_lost`.

- [ ] **Step 5: Commit** — `git add firmware bess-sim/tools && git commit -m "feat(fw): mqtt uplink + command enable/disable/set_power + ack (e2e bench)"`

### Task 16: Checklist verifikasi akhir + README

**Files:**
- Create: `README.md` (root repo gateway-bess), `bess-sim/README.md`, `firmware/README.md`
- Modify: `docs/superpowers/plans/2026-08-09-gateway-bess.md` (centang)

- [x] **Step 1: Jalankan seluruh verifikasi otomatis**

```bash
cd bess-sim && uv run pytest -v && uv run bess-sim selftest
cd ../firmware && pio test -e native && pio run -e esp32c6
```

Semua harus hijau/SUCCESS. **Hasil: 57/57 pytest PASSED, selftest LULUS, 19/19 native
test PASSED, `pio run -e esp32c6` SUCCESS** (Flash 84,3%, RAM 16,1%) — lihat
`task-16-report.md` untuk output verbatim.

- [x] **Step 2: Ulangi checklist e2e Task 15 Step 4** dari kondisi dingin (reboot gateway, restart simulator) — pastikan boot tanpa WiFi/BESS tidak menggantung (gateway tetap boot, `comm_lost` jujur, reconnect jalan). **Dilakukan di bench nyata (COM3/COM10)** dengan deviasi yang diizinkan (reset gateway via toggle RTS/DTR, bukan cabut USB fisik): cold boot dengan simulator mati → gateway tetap boot, WiFi & MQTT connect, `[bess] COMM_LOST` jujur (bukan macet) → simulator dinyalakan → `[bess] OK` pulih → simulator dimatikan lagi → `COMM_LOST` kembali jujur → simulator direstart → `[bess] OK` pulih lagi. Bukti log verbatim di `task-16-report.md`.

- [x] **Step 3: Tulis README** — `README.md` root: peta repo (bess-sim, firmware, docs), diagram bench, cara menjalankan (3 perintah: simulator, flash, probe), tautan spec & plan, catatan "BESS asli menggantikan simulator tanpa perubahan firmware". `bess-sim/README.md`: instal uv, perintah run/selftest, format skenario, tabel register yang disimulasikan + keputusan fidelity (SOC di 3184, busy saat transisi, strict-timing). `firmware/README.md`: prasyarat PlatformIO, salin secrets, build/upload/monitor, arsitektur task, kontrak MQTT (contoh payload + 3 perintah + tabel detail ack).

- [x] **Step 4: Commit terakhir**

```bash
git add -A && git commit -m "docs: README repo + sim + firmware; checklist e2e lengkap"
```

---

## Self-review (sudah dijalankan penulis plan)

1. **Cakupan spec:** §3 arsitektur → Task 13–15; §4 simulator (register penuh Task 2, fisika Task 4, state machine Task 5, skenario Task 6, timing/strict Task 7, fidelity D6/D7 Task 4–5); §5 firmware (modbus Task 9–10+14, decode Task 11, wifi Task 13, mqtt Task 15); §6.1 telemetri Task 12; §6.2 command+ack Task 12+15; §7 error handling Task 14–15 (comm_lost, retry, readback); §8 testing Task 1–16 (vector PDF, master_probe Task 8, e2e Task 15–16). Non-scope (OTA/dashboard/dll) tidak dikerjakan — sesuai D8.
2. **Placeholder:** tidak ada TBD/TODO; semua step berisi kode/perintah nyata.
3. **Konsistensi tipe:** nama fungsi/struct dicek silang antar task (`mbReadRegs` dipakai Task 14–15 sesuai definisi Task 14; `buildTelemetryJson`/`parseCommand`/`buildAckJson` sesuai Task 12; `BessData` field sesuai Task 11; nama alarm Python ⇄ C identik Task 6 vs Task 11).
4. **Catatan sadar-risiko:** (a) `esp32-c6-devkitc-1` — bila board gateway butuh varian lain, ganti `board` saja; (b) `mqtt_client.h` tersedia di arduino-esp32 3.x (pioarduino) — bila build gagal, tambahkan `framework = arduino, espidf` sebagai fallback; (c) kredensial MQTT dev harus diminta dari user saat Task 13.

