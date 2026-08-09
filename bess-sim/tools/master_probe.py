"""Master Modbus kecil untuk menguji bess-sim / device BESS dari laptop."""
import argparse
import sys
import time

sys.path.insert(0, ".")
from bess_sim.crc import append_crc, check_crc      # noqa: E402
from bess_sim.alarms import STATUS_BITS             # noqa: E402
import serial                                        # noqa: E402


def xfer(ser, frame, expect_silence=False):
    time.sleep(0.11)                 # jeda antar-frame >= 100 ms
    ser.reset_input_buffer()
    ser.write(append_crc(frame))
    ser.flush()
    time.sleep(0.3)
    resp = ser.read(300)
    if not resp:
        print("TIDAK ADA BALASAN"); return None
    if not check_crc(resp):
        print("CRC BALASAN SALAH:", resp.hex()); return None
    if resp[1] & 0x80:
        print(f"EXCEPTION code={resp[2]}"); return None
    return resp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--node", type=int, default=1)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("on")
    sub.add_parser("off")
    ps = sub.add_parser("setp"); ps.add_argument("--pct", type=float, required=True)
    prd = sub.add_parser("read")
    prd.add_argument("--start", type=int, required=True)
    prd.add_argument("--count", type=int, default=1)
    a = ap.parse_args()
    ser = serial.Serial(a.port, 9600, timeout=0.05)
    n = a.node
    if a.cmd == "on":
        r = xfer(ser, bytes([n, 5, 0x13, 0xBA, 0xFF, 0x00]))
        print("ON terkirim" if r else "gagal")
    elif a.cmd == "off":
        r = xfer(ser, bytes([n, 5, 0x13, 0xBA, 0x00, 0x00]))
        print("OFF terkirim" if r else "gagal")
    elif a.cmd == "setp":
        raw = int(round(a.pct * 10)) & 0xFFFF
        r = xfer(ser, bytes([n, 6, 0x0B, 0xEA, raw >> 8, raw & 0xFF]))
        print(f"setpoint {a.pct}% terkirim" if r else "gagal")
    elif a.cmd == "status":
        r = xfer(ser, bytes([n, 3, 0x08, 0x09, 0x00, 0x01]))
        if r:
            w = (r[3] << 8) | r[4]
            aktif = [nm for b, nm in STATUS_BITS.items() if w & (1 << b)]
            print(f"status 2057 = 0x{w:04X}: {', '.join(aktif)}")
    elif a.cmd == "read":
        r = xfer(ser, bytes([n, 3, a.start >> 8, a.start & 0xFF,
                             a.count >> 8, a.count & 0xFF]))
        if r:
            for k in range(r[2] // 2):
                v = (r[3 + 2 * k] << 8) | r[4 + 2 * k]
                print(f"  {a.start + k}: {v} (0x{v:04X})")


if __name__ == "__main__":
    sys.exit(main())
