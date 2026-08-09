"""Transport serial RS485 + pemotong frame berbasis jeda antar-byte."""
import logging
import random
import time

log = logging.getLogger("bess_sim")


class FrameSplitter:
    def __init__(self, gap_s: float = 0.004):
        self.gap_s = gap_s
        self._buf = bytearray()
        self._last = None

    def feed(self, data: bytes, now: float) -> list[bytes]:
        out = []
        if self._buf and self._last is not None and now - self._last > self.gap_s:
            out.append(bytes(self._buf))
            self._buf.clear()
        if data:
            self._buf += data
            self._last = now
        return out


class SerialServer:
    def __init__(self, port: str, sim, strict_timing=False,
                 reply_delay=(0.01, 0.04), seed=0):
        import serial
        self.ser = serial.Serial(port, 9600, bytesize=8, parity="N",
                                 stopbits=1, timeout=0.002)
        self.sim = sim
        self.strict = strict_timing
        self.delay = reply_delay
        self._rng = random.Random(seed)
        self._split = FrameSplitter()
        self._last_frame_end = 0.0

    def run_once(self):
        now = time.monotonic()
        data = self.ser.read(256)
        for frame in self._split.feed(data, now):
            gap = now - self._last_frame_end
            self._last_frame_end = now
            if gap < 0.100:
                if self.strict:
                    log.warning("frame diabaikan: jeda %.0f ms < 100 ms", gap * 1e3)
                    continue
                log.warning("jeda antar-frame %.0f ms < 100 ms (device asli menuntut 100 ms)", gap * 1e3)
            resp = self.sim.handle_frame(frame)
            if resp is not None:
                time.sleep(self._rng.uniform(*self.delay))
                self.ser.write(resp)
                self.ser.flush()
                self._last_frame_end = time.monotonic()
