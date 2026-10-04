#!/usr/bin/env python3
"""Create a virtual gamepad with uinput and play a scripted sequence through it.

Lets the whole chain be exercised without anyone's thumbs: point snake-pad.py
at the node this prints and watch the M4 trace react.

    ./fake-pad.py                 # prints /dev/input/eventN, then emits
"""
import fcntl, os, struct, sys, time

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
ABS_HAT0X, ABS_HAT0Y = 0x10, 0x11
BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH = 0x130, 0x131, 0x134, 0x133
BTN_TR, BTN_SELECT, BTN_START = 0x137, 0x13a, 0x13b

UI_DEV_CREATE, UI_DEV_DESTROY = 0x5501, 0x5502
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045567

ABS_CNT = 64
NAME = b"GameSir-K1 Virtual Test"


def create():
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    for ev in (EV_KEY, EV_ABS):
        fcntl.ioctl(fd, UI_SET_EVBIT, ev)
    for btn in (BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
                BTN_TR, BTN_SELECT, BTN_START):
        fcntl.ioctl(fd, UI_SET_KEYBIT, btn)
    for axis in (ABS_HAT0X, ABS_HAT0Y):
        fcntl.ioctl(fd, UI_SET_ABSBIT, axis)

    absmax = [0] * ABS_CNT
    absmin = [0] * ABS_CNT
    for axis in (ABS_HAT0X, ABS_HAT0Y):
        absmax[axis], absmin[axis] = 1, -1

    dev = NAME.ljust(80, b"\0")
    dev += struct.pack("4HI", 0x03, 0x3537, 0x1082, 0x0110, 0)
    dev += struct.pack(f"{ABS_CNT}i", *absmax)
    dev += struct.pack(f"{ABS_CNT}i", *absmin)
    dev += struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT))      # fuzz
    dev += struct.pack(f"{ABS_CNT}i", *([0] * ABS_CNT))      # flat
    os.write(fd, dev)
    fcntl.ioctl(fd, UI_DEV_CREATE)
    return fd


def node():
    """Find the event node udev just made for our virtual device."""
    blocks = open("/proc/bus/input/devices").read().split("\n\n")
    for b in blocks:
        if NAME.decode() not in b:
            continue
        for line in b.splitlines():
            if line.startswith("H: Handlers="):
                for h in line.split("=", 1)[1].split():
                    if h.startswith("event"):
                        return "/dev/input/" + h
    return None


def emit(fd, etype, code, value):
    for t, c, v in ((etype, code, value), (EV_SYN, 0, 0)):
        os.write(fd, struct.pack("llHHi", 0, 0, t, c, v))


def main():
    fd = create()
    time.sleep(0.5)
    print(node() or "no node", flush=True)

    time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 3.0)

    # Enough of a stickman run to cover every command the firmware takes.
    script = [
        ("hold-right", EV_ABS, ABS_HAT0X, 1),
        ("jump",       EV_KEY, BTN_SOUTH, 1), ("release", EV_KEY, BTN_SOUTH, 0),
        ("fire",       EV_KEY, BTN_WEST, 1),  ("release", EV_KEY, BTN_WEST, 0),
        ("drop-right", EV_ABS, ABS_HAT0X, 0),
        ("hold-left",  EV_ABS, ABS_HAT0X, -1),
        ("fire",       EV_KEY, BTN_TR, 1),    ("release", EV_KEY, BTN_TR, 0),
        ("drop-left",  EV_ABS, ABS_HAT0X, 0),
        ("jump-dpad",  EV_ABS, ABS_HAT0Y, -1), ("centre", EV_ABS, ABS_HAT0Y, 0),
        ("pause",      EV_KEY, BTN_START, 1), ("release", EV_KEY, BTN_START, 0),
        ("resume",     EV_KEY, BTN_START, 1), ("release", EV_KEY, BTN_START, 0),
        ("newgame",    EV_KEY, BTN_SELECT, 1), ("release", EV_KEY, BTN_SELECT, 0),
    ]
    for label, etype, code, value in script:
        emit(fd, etype, code, value)
        print(label, flush=True)
        time.sleep(0.6)

    time.sleep(1)
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    os.close(fd)


if __name__ == "__main__":
    main()
