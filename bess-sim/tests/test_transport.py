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


def test_default_tahan_jitter_usb():
    """Dongle USB + timer Windows (tick ~15,6 ms) bisa menyerahkan satu query
    8 byte dalam dua potongan berjarak >4 ms. Spec menjamin jeda >=100 ms antar
    frame, jadi ambang default boleh jauh lebih longgar dari 3,5 karakter."""
    fs = FrameSplitter()
    assert fs.feed(bytes.fromhex("010513BA"), 0.000) == []
    assert fs.feed(bytes.fromhex("FF00A95B"), 0.016) == []
    assert fs.feed(b"", 0.060) == [bytes.fromhex("010513BAFF00A95B")]


def test_splitter_mencatat_awal_frame():
    """--strict-timing harus mengukur jeda sampai AWAL query, bukan akhirnya."""
    fs = FrameSplitter()
    fs.feed(bytes.fromhex("010313"), 1.000)
    fs.feed(bytes.fromhex("BA0001A0A5"), 1.005)
    out = fs.feed(b"", 1.100)
    assert len(out) == 1
    # awal = waktu potongan pertama dikurangi durasi kirim 3 byte @9600
    assert abs(fs.last_start - (1.000 - 3 * 10 / 9600)) < 1e-9

def test_akhir_balasan_dihitung_dari_durasi_kabel():
    from bess_sim.transport import wire_end
    # 123 byte @9600 8N1 = 128,1 ms sesudah mulai kirim
    assert abs(wire_end(2.0, 123) - (2.0 + 123 * 10 / 9600)) < 1e-9


class _FakeSerial:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.written = []
    def read(self, n):
        return self.chunks.pop(0) if self.chunks else b""
    def write(self, b):
        self.written.append(bytes(b))
    def flush(self):
        pass
    def reset_input_buffer(self):
        pass


def test_echo_balasan_sendiri_diabaikan():
    """Echo dongle yang tiba sesudah reset_input_buffer identik dengan balasan
    kita; FC6 echo = query, jadi tanpa penjaga ia dieksekusi & di-echo lagi."""
    import random
    import time as _t
    from bess_sim.sim import BessSim
    from bess_sim.transport import SerialServer, FrameSplitter
    from bess_sim.crc import append_crc
    q = append_crc(bytes([1, 6, 0x0B, 0xEA, 0, 100]))
    srv = object.__new__(SerialServer)
    srv.sim, srv.strict, srv.delay = BessSim(), False, (0, 0)
    srv._rng, srv._split, srv._last_frame_end = random.Random(0), FrameSplitter(), 0.0
    srv._last_resp, srv._last_resp_end = None, 0.0
    srv.ser = _FakeSerial([q])
    srv.run_once(); _t.sleep(0.05); srv.run_once()      # query -> 1 balasan
    assert srv.ser.written == [q]
    srv.ser.chunks = [q]                                  # echo balasan kita
    srv.run_once(); _t.sleep(0.05); srv.run_once()
    assert srv.ser.written == [q]                         # tidak dibalas lagi
