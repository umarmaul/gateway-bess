"""Mesin slave Modbus RTU sesuai PDF §3 (FC3/4/5/6/16 + error frame)."""
from .crc import append_crc, check_crc
from .registers import RegisterMap, ModbusError, COILS


class ModbusSlave:
    def __init__(self, node, regs: RegisterMap, coil_handler, busy_fn=lambda: False):
        self.node = node
        self.regs = regs
        self.coil_handler = coil_handler
        self.busy_fn = busy_fn

    def handle(self, frame: bytes):
        if len(frame) < 4 or frame[0] != self.node:
            return None
        fc = frame[1]
        if not check_crc(frame):
            return self._err(fc, 3)
        body = frame[1:-2]
        try:
            if fc in (3, 4):
                return self._read(fc, body)
            if fc in (5, 6):
                return self._write_word(fc, body, frame)
            if fc == 16:
                return self._write_block(body)
            return self._err(fc, 1)
        except ModbusError as e:
            return self._err(fc, e.code)

    def _err(self, fc, code):
        return append_crc(bytes([self.node, fc | 0x80, code]))

    @staticmethod
    def _u16(b, i):
        return (b[i] << 8) | b[i + 1]

    def _read(self, fc, body):
        if len(body) != 5:
            raise ModbusError(3)
        start, count = self._u16(body, 1), self._u16(body, 3)
        vals = self.regs.read_block(start, count)
        out = bytes([self.node, fc, 2 * count])
        for v in vals:
            out += bytes([v >> 8, v & 0xFF])
        return append_crc(out)

    def _write_word(self, fc, body, frame):
        if len(body) != 5:
            raise ModbusError(3)
        id_, data = self._u16(body, 1), self._u16(body, 3)
        # Perintah OFF ke 5050 selalu diterima, termasuk di tengah sekuens
        # start: menolak stop dengan "busy" tidak pernah jadi perilaku yang
        # aman, dan state machine memang punya cabang PRECHARGE..RELAY -> STOPPING.
        if self.busy_fn() and not (id_ == 5050 and data == 0x0000):
            raise ModbusError(6)
        if id_ in COILS:
            if data not in (0xFF00, 0x0000):
                raise ModbusError(3)
            self.coil_handler(id_, data == 0xFF00)
        elif fc == 5:
            raise ModbusError(2)      # FC5 hanya untuk coil
        else:
            self.regs.write_single(id_, data)
        return frame                   # echo persis

    def _write_block(self, body):
        if len(body) < 6:
            raise ModbusError(3)
        if self.busy_fn():
            raise ModbusError(6)
        start, count, nbytes = self._u16(body, 1), self._u16(body, 3), body[5]
        if not 1 <= count <= 123 or nbytes != 2 * count or len(body) != 6 + nbytes:
            raise ModbusError(3)
        vals = [self._u16(body, 6 + 2 * k) for k in range(count)]
        self.regs.write_block(start, vals)
        return append_crc(bytes([self.node, 16]) +
                          bytes([start >> 8, start & 0xFF, count >> 8, count & 0xFF]))
