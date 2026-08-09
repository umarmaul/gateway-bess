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
