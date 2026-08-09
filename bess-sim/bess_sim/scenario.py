"""Skenario YAML: injeksi alarm & perubahan kondisi terjadwal."""
import yaml
from .alarms import by_name


class Scenario:
    def __init__(self, initial: dict, events: list[dict]):
        self.initial = initial
        self._events = sorted(events, key=lambda e: e["at"])
        self._done: set[int] = set()

    @classmethod
    def load(cls, path):
        with open(path, encoding="utf-8") as f:
            doc = yaml.safe_load(f) or {}
        return cls(doc.get("initial") or {}, doc.get("events") or [])

    def apply(self, sim, t_s: float):
        for idx, ev in enumerate(self._events):
            if idx in self._done or ev["at"] > t_s:
                continue
            self._done.add(idx)
            act = ev["action"]
            if act == "alarm":
                reg, bit = by_name(ev["name"])
                sim.set_alarm(reg, bit, ev.get("value", 1), trip=ev.get("trip", False))
            elif act == "clear_alarm":
                reg, bit = by_name(ev["name"])
                sim.set_alarm(reg, bit, 0)
            elif act == "set_soc":
                sim.physics.soc = ev["value"] / 100.0
