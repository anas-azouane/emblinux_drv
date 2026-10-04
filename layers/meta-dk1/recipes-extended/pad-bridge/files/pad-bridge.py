#!/usr/bin/env python3
"""Forward GameSir-K1 input to the game running on the Cortex-M4.

Writes single-letter commands to the rpmsg tty the M4 announces. The stickman
needs held inputs, so left/right are press/release pairs:

    stickman:  l L  walk left, stop     r R  walk right, stop
               j    jump (d-pad up or A)     f  fireball (X or RB)
               p    pause (Start)            n  new game (Select)
    snake:     u d l r steer, a back to the AI, p pause, n new game

A keepalive goes out every second either way: the firmware drops held keys if
the bridge stops talking, so a crash here cannot leave the stickman running
into a wall forever.

This is the stopgap userspace bridge; the mapping belongs in the gamesir-k1
kernel driver once the board runs a kernel we can build modules for.

    ./pad-bridge.py [--game stickman|snake] [--device /dev/input/eventN]
"""
import argparse, errno, fcntl, os, select, stat, struct, sys, time

EV_KEY, EV_ABS = 0x01, 0x03
ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y = 0x00, 0x01, 0x10, 0x11
BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH = 0x130, 0x131, 0x134, 0x133
BTN_TL, BTN_TR, BTN_SELECT, BTN_START = 0x136, 0x137, 0x13a, 0x13b

EVENT_FMT = "llHHi"                      # timeval, type, code, value
EVENT_SIZE = struct.calcsize(EVENT_FMT)
ABSINFO_FMT = "6i"
KEEPALIVE_S = 1.0
STICK_EDGE = 0.35                        # fraction of travel from centre


def eviocgabs(fd, axis):
    """EVIOCGABS(axis) = _IOR('E', 0x40 + axis, struct input_absinfo)"""
    size = struct.calcsize(ABSINFO_FMT)
    req = (2 << 30) | (size << 16) | (ord('E') << 8) | (0x40 + axis)
    _, lo, hi, _, _, _ = struct.unpack(ABSINFO_FMT,
                                       fcntl.ioctl(fd, req, b"\0" * size))
    return lo, hi


def find_pad():
    """The pad exposes several interfaces; we want the one with a d-pad."""
    for b in open("/proc/bus/input/devices").read().split("\n\n"):
        if "gamesir" not in b.lower():
            continue
        bits = 0
        for line in b.splitlines():
            if line.startswith("B: ABS="):
                bits = int(line.split("=", 1)[1].strip() or "0", 16)
        if not bits & (1 << ABS_HAT0X):
            continue
        for line in b.splitlines():
            if line.startswith("H: Handlers="):
                for h in line.split("=", 1)[1].split():
                    if h.startswith("event"):
                        return "/dev/input/" + h
    return None


def wait_for_pad():
    """Survive an unplug: udev gives the pad a new event node when it returns."""
    while True:
        dev = find_pad()
        if dev:
            try:
                print(f"pad back on {dev}", flush=True)
                return os.open(dev, os.O_RDONLY), dev
            except OSError:
                pass
        time.sleep(1)


class Link:
    """The tty disappears whenever the M4 is restarted, so reopen lazily.

    Opened with os.open and no O_CREAT on purpose: open(path, "wb") would
    create a regular file in place of the absent device node, and that file
    then shadows the real one when rpmsg_tty comes back.
    """

    def __init__(self, path):
        self.path = path
        self.fd = None

    def _open(self):
        if not stat.S_ISCHR(os.stat(self.path).st_mode):
            raise OSError(errno.ENOTTY, f"{self.path} is not a character device")
        return os.open(self.path, os.O_WRONLY)

    def send(self, cmd):
        try:
            if self.fd is None:
                self.fd = self._open()
            os.write(self.fd, cmd.encode())
            return True
        except OSError as e:
            if self.fd is not None:
                os.close(self.fd)
            self.fd = None
            if e.errno == errno.ENOTTY:
                sys.exit(str(e))
            if e.errno not in (errno.ENOENT, errno.EIO, errno.ENODEV):
                raise
            return False


class Stickman:
    """Axes are press/release; jump and fire are edge triggered."""

    buttons = {BTN_SOUTH: "j", BTN_NORTH: "j",
               BTN_WEST: "f", BTN_TR: "f", BTN_TL: "f",
               BTN_START: "p", BTN_SELECT: "n", BTN_EAST: "n"}

    def axis_x(self, step, prev):
        if step:
            return "l" if step < 0 else "r"
        return "L" if prev < 0 else "R"    # release the one that was held

    def axis_y(self, step, prev):
        return "j" if step < 0 else None   # up jumps, down does nothing


class Snake:
    buttons = {BTN_SOUTH: "n", BTN_EAST: "a", BTN_START: "p"}

    def axis_x(self, step, prev):
        return {-1: "l", 1: "r"}.get(step)

    def axis_y(self, step, prev):
        return {-1: "u", 1: "d"}.get(step)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", choices=("stickman", "snake"), default="stickman")
    ap.add_argument("--device")
    ap.add_argument("--tty", default="/dev/ttyRPMSG0")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    game = Stickman() if args.game == "stickman" else Snake()
    dev = args.device or find_pad()
    if not dev:
        sys.exit("no GameSir pad with a d-pad found in /proc/bus/input/devices")

    fd = os.open(dev, os.O_RDONLY)
    link = Link(args.tty)
    print(f"{args.game}: pad {dev} -> {args.tty}", flush=True)

    centre = {}
    for axis in (ABS_X, ABS_Y):
        try:
            lo, hi = eviocgabs(fd, axis)
            centre[axis] = ((lo + hi) / 2.0, (hi - lo) / 2.0)
        except OSError:
            pass

    # Last quantised direction per axis, so a held stick is not re-sent and a
    # release can name the direction it is releasing.
    state = {ABS_X: 0, ABS_Y: 0, ABS_HAT0X: 0, ABS_HAT0Y: 0}
    last_keepalive = 0.0

    while True:
        r, _, _ = select.select([fd], [], [], KEEPALIVE_S)
        now = time.time()

        if now - last_keepalive >= KEEPALIVE_S:
            link.send(".")
            last_keepalive = now
        if not r:
            continue

        try:
            data = os.read(fd, EVENT_SIZE * 64)
        except OSError as e:
            if e.errno not in (errno.ENODEV, errno.EIO):
                raise
            os.close(fd)
            if args.device:
                sys.exit(f"{dev} went away")
            print("pad disconnected, waiting", flush=True)
            fd, dev = wait_for_pad()
            continue

        for i in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
            _, _, etype, code, value = struct.unpack_from(EVENT_FMT, data, i)
            cmds = []

            if etype == EV_ABS and code in (ABS_HAT0X, ABS_HAT0Y):
                step = max(-1, min(1, value))
                prev, state[code] = state[code], step
                if step != prev:
                    f = game.axis_x if code == ABS_HAT0X else game.axis_y
                    cmds = [f(step, prev)]
            elif etype == EV_ABS and code in centre:
                mid, span = centre[code]
                pos = (value - mid) / span if span else 0
                step = -1 if pos < -STICK_EDGE else (1 if pos > STICK_EDGE else 0)
                prev, state[code] = state[code], step
                if step != prev:
                    f = game.axis_x if code == ABS_X else game.axis_y
                    cmds = [f(step, prev)]
            elif etype == EV_KEY and value == 1:
                cmds = [game.buttons.get(code)]

            for cmd in cmds:
                if cmd and link.send(cmd):
                    last_keepalive = now
                    if args.verbose:
                        print(cmd, flush=True)


if __name__ == "__main__":
    main()
