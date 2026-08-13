"""Rebut client_id gateway supaya broker menendangnya, lalu amati pemulihan.

Dipakai untuk mereproduksi reboot yang terlihat 13 Agustus 2026. Lihat
docs/superpowers/specs/2026-08-13-fondasi-paritas-design.md §3.2.
"""
import argparse
import json
import time

import paho.mqtt.client as mqtt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="mqtt-dev.bepbatt.id")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user"); ap.add_argument("--passwd")
    ap.add_argument("--gw", required=True)
    ap.add_argument("--hold", type=int, default=90, help="detik menahan client_id")
    ap.add_argument("--observe", type=int, default=120, help="detik mengamati sesudahnya")
    ap.add_argument("--no-kick", action="store_true",
                    help="hanya mengamati (baseline), tanpa merebut client_id")
    a = ap.parse_args()
    t0 = time.time()

    def stamp():
        return f"[{time.time() - t0:6.1f}s]"

    watcher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if a.user:
        watcher.username_pw_set(a.user, a.passwd)

    def on_msg(_c, _u, m):
        if m.topic.endswith("/status"):
            print(f"{stamp()} status = {m.payload.decode()!r} retain={m.retain}", flush=True)
            return
        d = json.loads(m.payload)["data"]
        doc = json.loads(m.payload)
        print(f"{stamp()} telemetri seq={doc['seq']} boot={d.get('boot_count')} "
              f"reset={d.get('last_reset_reason')} uptime_ms={d.get('uptime_ms')} "
              f"({len(m.payload)} B)", flush=True)

    watcher.on_message = on_msg
    watcher.connect(a.host, a.port, 60)
    watcher.subscribe([(f"device/{a.gw}/telemetry", 1), (f"device/{a.gw}/status", 1)])
    watcher.loop_start()
    time.sleep(2)

    if a.no_kick:
        # Mode baseline: cuma mendengarkan selama --observe detik.
        time.sleep(a.observe)
        watcher.loop_stop()
        print(f"{stamp()} selesai (tanpa rebutan)", flush=True)
        return

    print(f"{stamp()} --- merebut client_id {a.gw} ---", flush=True)
    impostor = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=a.gw)
    if a.user:
        impostor.username_pw_set(a.user, a.passwd)
    impostor.connect(a.host, a.port, 60)
    impostor.loop_start()
    time.sleep(a.hold)
    print(f"{stamp()} --- melepas client_id ---", flush=True)
    impostor.loop_stop()
    impostor.disconnect()

    time.sleep(a.observe)
    watcher.loop_stop()
    print(f"{stamp()} selesai", flush=True)


if __name__ == "__main__":
    main()
