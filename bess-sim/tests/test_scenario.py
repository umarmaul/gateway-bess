"""Test skenario YAML dan tabel nama alarm."""
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
