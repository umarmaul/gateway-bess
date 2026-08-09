"""State machine on/off BESS: urutan precharge → soft start → relay → run."""
from enum import Enum, auto


class St(Enum):
    STOP = auto(); PRECHARGE = auto(); SOFTSTART = auto(); RELAY = auto()
    RUN = auto(); STOPPING = auto(); STANDBY = auto(); FAULT = auto(); EPO = auto()


_SEQ = [St.PRECHARGE, St.SOFTSTART, St.RELAY, St.RUN]
STAGE_S = 1.0


class StateMachine:
    def __init__(self):
        self.state = St.STOP
        self._t = 0.0

    def power_on(self):
        if self.state in (St.STOP, St.STANDBY):
            self.state, self._t = St.PRECHARGE, 0.0

    def power_off(self):
        if self.state in (St.RUN, St.PRECHARGE, St.SOFTSTART, St.RELAY):
            self.state, self._t = St.STOPPING, 0.0

    def standby(self, on: bool):
        if on and self.state == St.STOP:
            self.state = St.STANDBY
        elif not on and self.state == St.STANDBY:
            self.state = St.STOP

    def trip(self):
        if self.state != St.EPO:
            self.state, self._t = St.FAULT, 0.0

    def clear_fault(self):
        if self.state == St.FAULT:
            self.state = St.STOP

    def tick(self, dt_s: float):
        self._t += dt_s
        if self.state in (St.PRECHARGE, St.SOFTSTART, St.RELAY) and self._t >= STAGE_S:
            self.state = _SEQ[_SEQ.index(self.state) + 1]
            self._t = 0.0
        elif self.state == St.STOPPING and self._t >= STAGE_S:
            self.state, self._t = St.STOP, 0.0

    def busy(self) -> bool:
        return self.state in (St.PRECHARGE, St.SOFTSTART, St.RELAY, St.STOPPING)

    def running(self) -> bool:
        return self.state == St.RUN

    def status_word(self, charging: bool) -> int:
        s, w = self.state, 0
        stage = {St.PRECHARGE: 0, St.SOFTSTART: 1, St.RELAY: 2}.get(s)
        closed = 4 if s in (St.RUN, St.STOPPING) else (stage or 0)
        if s in (St.RUN, St.STOPPING) or stage is not None:
            for b in range(min(closed, 4)):
                w |= 1 << b
        if s == St.RUN:
            w |= 0b1111 | (1 << 6)
        if charging and s == St.RUN:
            w |= 1 << 5
        if s == St.FAULT:
            w |= 1 << 7
        w |= (1 << 8) | (1 << 9)          # master machine + master cabinet
        if s == St.STANDBY:
            w |= 1 << 10
        if s in (St.STOP, St.STANDBY, St.FAULT):
            w |= 1 << 11                   # shutdown
        if s == St.EPO:
            w |= 1 << 12
        w |= 1 << 15                       # parameter initialization completed
        return w
