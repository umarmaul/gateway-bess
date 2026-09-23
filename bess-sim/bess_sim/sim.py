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
        # Alarm yang memicu FAULT dan masih aktif. FAULT hanya bisa direset
        # (FC5 OFF) setelah himpunan ini kosong.
        self._trip_causes: set[tuple[int, int]] = set()
        # Alarm proteksi otomatis bersifat latch: dibersihkan oleh reset itu
        # sendiri, bukan oleh kondisinya (SOC tidak bisa pulih selama FAULT).
        self._latched: set[tuple[int, int]] = set()
        self.regs.set_raw(ID_SOC, round(self.physics.soc * 1000))

    def _coil(self, id_: int, on: bool):
        if id_ == 5050:
            if not on and self.sm.state == St.FAULT:
                self._reset_fault()
            self.sm.power_on() if on else self.sm.power_off()
        elif id_ == 5051:
            self.sm.standby(on)

    def handle_frame(self, frame: bytes):
        return self.slave.handle(frame)

    def set_alarm(self, reg_id: int, bit: int, value: int, trip: bool = False):
        self._forced_alarms[(reg_id, bit)] = value
        if value and trip:
            self._trip_causes.add((reg_id, bit))
            self.sm.trip()
        elif not value:
            self._trip_causes.discard((reg_id, bit))

    def _reset_fault(self):
        # PDF tidak mendefinisikan reset fault. Asumsi simulator: FC5 OFF saat
        # FAULT = reset, hanya berhasil bila tak ada penyebab trip yang masih
        # aktif (alarm skenario harus dibersihkan dulu).
        for key in self._latched:
            self.set_alarm(*key, 0)
        self._latched.clear()
        if not self._trip_causes:
            self.sm.clear_fault()

    def tick(self, dt_s: float):
        rated_kw = self.regs.get(ID_RATED) / 10.0
        setpoint_kw = self.regs.get_signed(ID_P_SET) / 1000.0 * rated_kw
        rate = self.regs.get(3062)
        self.sm.tick(dt_s)
        if self.sm.state in (St.FAULT, St.EPO, St.STOP, St.STANDBY):
            # Relay AC terbuka: tak ada jalur daya, jadi daya langsung nol —
            # bukan meluruh mengikuti laju 3062 (yang bisa 1 %/s).
            self.physics.p_ac_kw = 0.0
        self.physics.step(dt_s, setpoint_kw, rate, rated_kw, self.sm.running())
        self._auto_protect()
        self._map_to_regs(rated_kw)

    def _auto_protect(self):
        p = self.physics
        if p.soc <= 0.02 and p.p_ac_kw > 0:
            self._latched.add((2055, 14))
            self.set_alarm(2055, 14, 1, trip=True)     # battery over-discharge
        if p.soc >= 0.98 and p.p_ac_kw < 0:
            self._latched.add((2055, 13))
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
