from bess_sim.state_machine import StateMachine, St


def tick(sm, s):
    for _ in range(int(s * 10)):
        sm.tick(0.1)


def test_urutan_start():
    sm = StateMachine()
    sm.power_on()
    assert sm.state == St.PRECHARGE and sm.busy()
    tick(sm, 1.1); assert sm.state == St.SOFTSTART
    tick(sm, 1.1); assert sm.state == St.RELAY
    tick(sm, 1.1); assert sm.state == St.RUN
    assert sm.running() and not sm.busy()


def test_stop_lewat_stopping():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    sm.power_off()
    assert sm.state == St.STOPPING
    tick(sm, 1.1)
    assert sm.state == St.STOP


def test_status_word_run():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    w = sm.status_word(charging=False)
    for bit in (0, 1, 2, 3, 6, 8, 9, 15):   # precharge..relay, run, master, init done
        assert w & (1 << bit), bit
    assert not w & (1 << 7)
    assert sm.status_word(charging=True) & (1 << 5)


def test_status_word_stop():
    w = StateMachine().status_word(charging=False)
    assert w & (1 << 11) and w & (1 << 15)   # shutdown, param init
    assert not w & (1 << 6)


def test_trip_dari_run():
    sm = StateMachine(); sm.power_on(); tick(sm, 3.5)
    sm.trip()
    assert sm.state == St.FAULT
    assert sm.status_word(False) & (1 << 7)
    sm.power_on()                             # di FAULT: diabaikan
    assert sm.state == St.FAULT
    sm.clear_fault()
    assert sm.state == St.STOP


def test_standby():
    sm = StateMachine()
    sm.standby(True); assert sm.state == St.STANDBY
    assert sm.status_word(False) & (1 << 10)
    sm.power_on(); tick(sm, 3.5)
    assert sm.state == St.RUN
