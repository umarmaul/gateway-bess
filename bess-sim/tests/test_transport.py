"""Test transport serial RS485 + pemotong frame berbasis jeda antar-byte."""
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
