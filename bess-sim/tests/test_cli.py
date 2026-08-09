"""Test CLI bess-sim: run (serial nyata) dan selftest (tanpa hardware)."""
from bess_sim.cli import selftest


def test_selftest_lulus():
    assert selftest() == 0
