"""Transport serial RS485 + pemotong frame berbasis jeda antar-byte."""
import logging
import random
import time

log = logging.getLogger("bess_sim")

BYTE_S = 10 / 9600          # 1 byte 8N1 @9600 = 10 bit


def wire_end(write_start: float, nbytes: int) -> float:
    """Saat byte terakhir benar-benar selesai di kabel. Dihitung, bukan diambil
    dari waktu kembalinya flush(): driver USB Windows bisa kembali sebelum
    data keluar, dan jeda antar-frame lalu terukur terlalu besar."""
    return write_start + nbytes * BYTE_S


class FrameSplitter:
    # 30 ms, bukan 3,5 karakter (~4 ms): dongle USB-serial dan timer Windows
    # (tick ~15,6 ms) menyerahkan byte berkelompok, sehingga satu query bisa
    # tiba dalam dua potongan berjarak >4 ms lalu gagal CRC -> exception 03.
    # Spec menjamin jeda >=100 ms antar frame, jadi 30 ms masih jauh aman.
    def __init__(self, gap_s: float = 0.030):
        self.gap_s = gap_s
        self._buf = bytearray()
        self._last = None
        self._start = None
        # Untuk frame yang terakhir dikeluarkan feed(): perkiraan saat byte
        # pertama tiba di kabel, dan saat byte terakhirnya diterima.
        self.last_start = None
        self.last_end = None

    def feed(self, data: bytes, now: float) -> list[bytes]:
        out = []
        if self._buf and self._last is not None and now - self._last > self.gap_s:
            out.append(bytes(self._buf))
            self.last_start, self.last_end = self._start, self._last
            self._buf.clear()
        if data:
            if not self._buf:
                # `now` = saat potongan ini selesai terbaca; byte pertamanya
                # sudah mulai di kabel len×BYTE_S lebih awal.
                self._start = now - len(data) * BYTE_S
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
        self._last_resp = None
        self._last_resp_end = 0.0

    def run_once(self):
        data = self.ser.read(256)
        # Stempel SESUDAH read(): read() memblokir sampai timeout, jadi waktu
        # sebelum read selalu lebih awal dari kedatangan byte sebenarnya.
        now = time.monotonic()
        for frame in self._split.feed(data, now):
            # Jeda = akhir frame sebelumnya (balasan kita, atau query lain)
            # sampai AWAL query ini — definisi yang dipakai spec §2.4.
            gap = self._split.last_start - self._last_frame_end
            self._last_frame_end = self._split.last_end
            if (frame == self._last_resp
                    and self._split.last_start - self._last_resp_end < 0.100):
                # Echo dongle yang lolos reset_input_buffer (flush() di Windows
                # bisa kembali sebelum data keluar). Master sah tak mungkin
                # mengirim ulang frame identik balasan kita dalam <100 ms.
                log.debug("echo balasan sendiri diabaikan")
                continue
            if gap < 0.100:
                if self.strict:
                    log.warning("frame diabaikan: jeda %.0f ms < 100 ms", gap * 1e3)
                    continue
                log.warning("jeda antar-frame %.0f ms < 100 ms (device asli menuntut 100 ms)", gap * 1e3)
            resp = self.sim.handle_frame(frame)
            if resp is not None:
                time.sleep(self._rng.uniform(*self.delay))
                t_write = time.monotonic()
                self.ser.write(resp)
                self.ser.flush()
                # Dongle yang meng-echo TX-nya sendiri akan menyerahkan balasan
                # kita sebagai "query" baru; echo FC5/FC6 identik dengan query
                # sehingga dieksekusi ulang tanpa akhir. Buang sisa input.
                self.ser.reset_input_buffer()
                self._last_frame_end = max(wire_end(t_write, len(resp)),
                                           self._last_frame_end)
                self._last_resp, self._last_resp_end = resp, self._last_frame_end
