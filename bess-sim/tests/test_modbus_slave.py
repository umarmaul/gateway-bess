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
