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
