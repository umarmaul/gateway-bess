"""Probe MQTT: lihat telemetri/ack gateway-bess & kirim perintah."""
import argparse
import json
import time
import uuid

import paho.mqtt.client as mqtt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="mqtt-dev.bepbatt.id")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user"); ap.add_argument("--passwd")
    ap.add_argument("--gw", required=True)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("watch")
    sub.add_parser("enable"); sub.add_parser("disable")
    ps = sub.add_parser("set_power"); ps.add_argument("--watt", type=float, required=True)
    a = ap.parse_args()

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if a.user:
        cli.username_pw_set(a.user, a.passwd)
    cli.connect(a.host, a.port, 60)

    def on_msg(_c, _u, m):
        print(f"\n== {m.topic} ==")
        try:
            doc = json.loads(m.payload)
            if "data" in doc:
                b = doc["data"].get("bess", {})
                d = doc["data"]
                print(f"seq={doc['seq']} type={d.get('device_type')} "
                      f"p={b.get('active_power_kw')}kW soc={b.get('soc_percent')}% "
                      f"running={b.get('running')} comm_lost={b.get('comm_lost')} "
                      f"reset={d.get('last_reset_reason')} boot={d.get('boot_count')}")
            else:
                print(json.dumps(doc, indent=1))
        except json.JSONDecodeError:
            print(m.payload.decode(errors="replace"))

    cli.on_message = on_msg
    cli.subscribe([(f"device/{a.gw}/telemetry", 1),
                   (f"device/{a.gw}/command/ack", 1),
                   (f"device/{a.gw}/status", 1)])
    if a.cmd != "watch":
        args = {"power_w": a.watt} if a.cmd == "set_power" else {}
        payload = {"id": str(uuid.uuid4())[:8], "ts": int(time.time()),
                   "cmd": a.cmd, "args": args, "api_schema_version": 1}
        cli.publish(f"device/{a.gw}/command", json.dumps(payload), qos=1)
        print("perintah terkirim:", payload)
    cli.loop_forever()


if __name__ == "__main__":
    main()
