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


def test_akumulator_energi_charge_dan_discharge():
    p = Physics(soc=0.5)
    for _ in range(3600):                       # 1 jam @10 kW ekspor, rate besar
        p.step(1.0, 10.0, 100.0, 50.0, True)
    assert abs(p.wh_discharge - 10000) < 50     # ~10 kWh
    assert p.wh_charge == 0
    for _ in range(1800):                       # 30 menit @-10 kW charge
        p.step(1.0, -10.0, 100.0, 50.0, True)
    assert abs(p.wh_charge - 5000) < 100
