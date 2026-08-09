from bess_sim.crc import crc16, append_crc, check_crc

# (frame tanpa CRC, dua byte CRC persis contoh PDF: low dulu)
VECTORS = [
    (bytes.fromhex("0103041A0003"), bytes.fromhex("253C")),  # read 1050 x3
    (bytes.fromhex("010306089808980898"), bytes.fromhex("8404")),  # jawaban
    (bytes.fromhex("01060BEA03E8"), bytes.fromhex("AAA4")),  # FC6 3050=1000
    (bytes.fromhex("010513BAFF00"), bytes.fromhex("A95B")),  # FC5 5050 ON
    (bytes.fromhex("010308020001"), bytes.fromhex("27AA")),  # read 2050 x1
    (bytes.fromhex("0103020400"), bytes.fromhex("BA84")),  # jawaban status
]

def test_crc16_vectors_pdf():
    for frame, crc in VECTORS:
        v = crc16(frame)
        assert bytes([v & 0xFF, v >> 8]) == crc, frame.hex()

def test_append_and_check():
    for frame, crc in VECTORS:
        full = append_crc(frame)
        assert full == frame + crc
        assert check_crc(full)
        assert not check_crc(full[:-1] + bytes([full[-1] ^ 0xFF]))

def test_check_too_short():
    assert not check_crc(b"\x01")
