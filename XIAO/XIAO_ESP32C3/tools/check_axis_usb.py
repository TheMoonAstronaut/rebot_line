#!/usr/bin/env python3
"""Verify C3 diagnostics over Linux USB CDC with the drive supply off."""

import argparse
import fcntl
import json
import os
import re
import select
import struct
import termios
import time
import tty


def exchange(fd, command):
    data = json.dumps({"cmd": command}, separators=(",", ":")).encode() + b"\n"
    if os.write(fd, data) != len(data):
        raise RuntimeError("Incomplete serial write")
    pending = bytearray()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if not ready:
            break
        chunk = os.read(fd, 512)
        if not chunk:
            raise RuntimeError("USB serial disconnected")
        pending += chunk
        while b"\n" in pending:
            line, _, pending = pending.partition(b"\n")
            try:
                value = json.loads(line)
            except (UnicodeError, ValueError):
                continue
            if isinstance(value, dict) and "type" in value:
                return value
        if len(pending) > 1024:
            raise RuntimeError("Serial response exceeds the expected length")
    raise RuntimeError(f"No serial reply for {command}")


def verify(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        original = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            settings = termios.tcgetattr(fd)
            settings[2] = (settings[2] | termios.CLOCAL) & ~termios.HUPCL
            settings[4] = settings[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, settings)
            fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack("I", termios.TIOCM_DTR))
            termios.tcflush(fd, termios.TCIFLUSH)

            hello = exchange(fd, "HELLO")
            if (hello.get("type"), hello.get("role"), hello.get("protocol"),
                hello.get("firmware")) != ("HELLO", "AXIS_C3", 1, "axis_c3_diagnostic"):
                raise RuntimeError(f"Unexpected device identity: {hello}")
            if not re.fullmatch(r"[0-9A-F]{12}", hello.get("device_id", "")):
                raise RuntimeError("Device ID is missing or malformed")

            rejected = exchange(fd, "MOVE_ABS")
            if rejected != {"type": "ERROR", "code": "UNSUPPORTED_REQUEST"}:
                raise RuntimeError(f"Diagnostic command should be rejected: {rejected}")

            status = exchange(fd, "STATUS")
            if (status.get("type"), status.get("state"),
                status.get("motion_available"), status.get("pulses")) != ("STATUS", "LOCKED", False, 0):
                raise RuntimeError(f"Unexpected diagnostic state: {status}")
            if any(status.get(key) not in (0, 1) for key in
                   ("limit_neg_raw", "limit_pos_raw", "home_raw")):
                raise RuntimeError(f"Missing raw input: {status}")

            print(json.dumps(hello))
            print(json.dumps(status))
            print("PASS: AXIS_C3 identity, motion rejection, locked state and raw inputs")
        finally:
            original[2] &= ~termios.HUPCL
            termios.tcsetattr(fd, termios.TCSANOW, original)
    finally:
        os.close(fd)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="C3 USB serial device path")
    args = parser.parse_args()
    verify(args.port)
