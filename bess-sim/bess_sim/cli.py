"""CLI bess-sim: run (serial nyata) dan selftest (tanpa hardware)."""
import argparse
import logging
import time

from .crc import append_crc
from .sim import BessSim
from .state_machine import St


def selftest() -> int:
    sim = BessSim(soc=0.5)
    on = append_crc(bytes([1, 5, 0x13, 0xBA, 0xFF, 0x00]))
    assert sim.handle_frame(on) == on
    for _ in range(40):
        sim.tick(0.1)
    assert sim.sm.state == St.RUN, sim.sm.state
    sim.handle_frame(append_crc(bytes([1, 6, 0x0B, 0xEA, 0x00, 0xC8])))  # 20% = 10 kW
    for _ in range(100):
        sim.tick(0.1)
    p_kw = sim.regs.get_signed(1060) / 10.0
    soc = sim.regs.get(3184) / 10.0
    vdc = sim.regs.get(1063) / 10.0
    print(f"selftest: state=RUN p_ac={p_kw:.1f} kW vdc={vdc:.1f} V soc={soc:.1f}%")
    ok = 9.5 < p_kw < 10.5 and 700 < vdc < 910
    print("selftest:", "LULUS" if ok else "GAGAL")
    return 0 if ok else 1


def run(args) -> int:
    from .scenario import Scenario
    from .transport import SerialServer
    sim = BessSim(node=args.node, soc=args.soc / 100.0, ip65=args.ip65)
    sc = Scenario.load(args.scenario) if args.scenario else None
    if sc and "soc" in sc.initial:
        sim.physics.soc = sc.initial["soc"] / 100.0
    srv = SerialServer(args.port, sim, strict_timing=args.strict_timing)
    print(f"bess-sim AKTIF di {args.port} node {args.node} (SIMULATOR — bukan device asli)")
    t0 = time.monotonic()
    last_tick = last_print = t0
    try:
        while True:
            srv.run_once()
            now = time.monotonic()
            if now - last_tick >= 0.1:
                if sc:
                    sc.apply(sim, now - t0)
                sim.tick(now - last_tick)
                last_tick = now
            if now - last_print >= 2.0:
                p = sim.physics
                print(f"[{now - t0:7.1f}s] {sim.sm.state.name:9s} "
                      f"p_ac={p.p_ac_kw:+6.2f} kW soc={p.soc * 100:5.1f}% "
                      f"vdc={p.v_dc():6.1f} V", flush=True)
                last_print = now
    except KeyboardInterrupt:
        print("berhenti.")
        return 0


def main(argv=None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    ap = argparse.ArgumentParser(prog="bess-sim")
    sub = ap.add_subparsers(dest="cmd", required=True)
    pr = sub.add_parser("run", help="layani port serial nyata")
    pr.add_argument("--port", required=True)
    pr.add_argument("--node", type=int, default=1)
    pr.add_argument("--soc", type=float, default=50.0)
    pr.add_argument("--scenario")
    pr.add_argument("--strict-timing", action="store_true")
    pr.add_argument("--ip65", action="store_true",
                    help="modul IP65: juga jawab node 160, 3182 via 160 ganti alamat")
    sub.add_parser("selftest", help="uji tanpa hardware")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest()
    return run(args)
