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
