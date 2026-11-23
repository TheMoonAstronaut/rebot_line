#!/usr/bin/env python3
"""Observe real USB control timing; run with the motor's 24 V OFF."""
import argparse
import fcntl
import os
from pathlib import Path
import re
import select
import struct
import termios
import time
import tty

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="/dev/ttyACM0")
parser.add_argument("--duration", type=float, default=8)
parser.add_argument("--max-gap", type=int, default=50)
parser.add_argument("--log", type=Path, required=True)
args = parser.parse_args()
fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
original = termios.tcgetattr(fd)
tty.setraw(fd)
settings = termios.tcgetattr(fd)
settings[2] = (settings[2] | termios.CLOCAL | termios.CREAD) & ~termios.HUPCL
settings[4] = settings[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, settings)
try:
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("I", termios.TIOCM_DTR))
except OSError:
    pass
captured = bytearray()
try:
    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            data = os.read(fd, 8192)
            if data:
                captured.extend(data)
finally:
    original[2] &= ~termios.HUPCL
    termios.tcsetattr(fd, termios.TCSANOW, original)
    os.close(fd)
args.log.parent.mkdir(parents=True, exist_ok=True)
args.log.write_bytes(captured)
text = captured.decode(errors="replace")
gaps = [int(value) for value in re.findall(r"maxgap_ms=(\d+)", text)]
draws = [int(value) for value in re.findall(r"draw_ms=(\d+)", text)]
if not gaps:
    raise SystemExit("FAIL: no control timing samples received")
peak = max(gaps)
print(f"control_max_gap_ms={peak}, observed_draw_ms={max(draws, default=0)}, "
      f"samples={len(gaps)}, allowed_ms={args.max_gap}")
if peak > args.max_gap:
    raise SystemExit("FAIL: control loop blocked beyond the response budget")
print("PASS: control timing stayed within the response budget")
