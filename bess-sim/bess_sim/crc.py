"""CRC16 Modbus RTU (polinomial A001, init 0xFFFF, urutan kirim Low-High)."""

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def append_crc(frame: bytes) -> bytes:
    v = crc16(frame)
    return frame + bytes([v & 0xFF, v >> 8])

def check_crc(frame: bytes) -> bool:
    if len(frame) < 4:
        return False
    return crc16(frame[:-2]) == frame[-2] | (frame[-1] << 8)
