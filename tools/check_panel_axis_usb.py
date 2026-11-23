#!/usr/bin/env python3
"""Check PANEL_S3 -> PC -> AXIS_C3 -> PC -> PANEL_S3 with motor power off."""

import argparse
from contextlib import ExitStack, contextmanager
import fcntl
import json
import os
from pathlib import Path
import re
import select
import struct
import termios
import time
import tty


class UsbSerial:
    def __init__(self, fd, port):
        self.fd = fd
        self.port = port
        self.buffer = bytearray()

    def send(self, message):
        data = json.dumps(message, separators=(",", ":")).encode() + b"\n"
        if os.write(self.fd, data) != len(data):
            raise RuntimeError(f"{self.port}: incomplete write")

    def read(self):
        chunk = os.read(self.fd, 512)
        if not chunk:
            raise RuntimeError(f"{self.port}: USB disconnected")
        self.buffer += chunk
        if len(self.buffer) > 4096:
            raise RuntimeError(f"{self.port}: serial frame too long")

    def messages(self):
        while b"\n" in self.buffer:
            line, _, self.buffer = self.buffer.partition(b"\n")
            try:
                message = json.loads(line)
            except (UnicodeError, ValueError):
                continue
            if isinstance(message, dict):
                yield message

    def receive(self, message_type, timeout=4):
        deadline = time.monotonic() + timeout
        while True:
            for message in self.messages():
                if message.get("type") == message_type:
                    return message
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise TimeoutError(f"{self.port}: no {message_type} reply")
            self.read()


@contextmanager
def open_serial(port):
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
            yield UsbSerial(fd, port)
        finally:
            original[2] &= ~termios.HUPCL
            termios.tcsetattr(fd, termios.TCSANOW, original)
    finally:
        os.close(fd)


def identify(link):
    link.send({"cmd": "HELLO"})
    message = link.receive("HELLO")
    roles = {"PANEL_S3": "panel_s3_diagnostic", "AXIS_C3": "axis_c3_diagnostic"}
    role = message.get("role")
    if (role not in roles or message.get("firmware") != roles[role] or
            message.get("protocol") != 1 or
            not re.fullmatch(r"[0-9A-F]{12}", message.get("device_id", ""))):
        raise RuntimeError(f"{link.port}: unexpected diagnostic identity {message}")
    if role == "PANEL_S3" and message.get("display_ready") is not True:
        raise RuntimeError(f"{link.port}: S3 display initialization failed")
    print(f"{role} at {Path(link.port).resolve()}", flush=True)
    return role, message["device_id"]


def relay(panel, axis, request):
    seq = request.get("seq")
    if type(seq) is not int or not 1 <= seq <= 0xFFFFFFFF:
        raise RuntimeError(f"Invalid panel sequence: {request}")
    axis.send({"cmd": "STATUS"})
    status = axis.receive("STATUS")
    if (status.get("state") != "LOCKED" or status.get("motion_available") is not False or
            status.get("pulses") != 0):
        raise RuntimeError(f"C3 is not in the diagnostic locked state: {status}")
    values = [status.get(key) for key in ("limit_neg_raw", "limit_pos_raw", "home_raw")]
    if any(type(value) is not int or value not in (0, 1) for value in values):
        raise RuntimeError(f"Invalid C3 inputs: {status}")
    negative, positive, home = values
    panel.send({"cmd": "RESULT", "seq": seq, "negative": negative,
                "positive": positive, "home": home})
    result = panel.receive("RESULT_SHOWN")
    if result.get("seq") != seq or result.get("lcd_drawn") is not True:
        raise RuntimeError(f"Panel did not confirm LCD submission: {result}")
    print(f"PASS: {request['source']} seq={seq}, AXIS LOCKED, L/R/H={values}", flush=True)


def main(args):
    ports = args.ports or sorted(Path("/dev/serial/by-id").glob("usb-Espressif_*"))
    expected = 1 if args.panel_only else 2
    if len(ports) != expected or len({Path(port).resolve() for port in ports}) != expected:
        raise RuntimeError(f"Expected {expected} different USB device paths, found {ports}")
    with ExitStack() as stack:
        links = {}
        identities = set()
        for port in ports:
            link = stack.enter_context(open_serial(port))
            role, identity = identify(link)
            if role in links or identity in identities:
                raise RuntimeError("Duplicate diagnostic role or device ID")
            links[role] = link
            identities.add(identity)
        panel = links.get("PANEL_S3")
        if not panel:
            raise RuntimeError("PANEL_S3 is missing")
        if args.panel_only:
            print("PASS: PANEL_S3 identity and display initialization")
            return
        axis = links.get("AXIS_C3")
        if not axis:
            raise RuntimeError("AXIS_C3 is missing")

        panel.send({"cmd": "TEST"})
        request = panel.receive("TEST_REQUEST")
        if request.get("source") != "PC":
            raise RuntimeError(f"Unexpected test request source: {request}")
        relay(panel, axis, request)
        if args.listen > 0:
            print(f"Listening for USR1 presses for {args.listen:g} seconds", flush=True)
            deadline = time.monotonic() + args.listen
            while time.monotonic() < deadline:
                try:
                    request = panel.receive("TEST_REQUEST", deadline - time.monotonic())
                except TimeoutError:
                    break
                if request.get("source") == "USR1":
                    relay(panel, axis, request)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", nargs="*", help="USB serial paths in either order")
    parser.add_argument("--panel-only", action="store_true", help="Verify only the S3 board")
    parser.add_argument("--listen", type=float, default=0, help="Seconds to listen for USR1")
    try:
        main(parser.parse_args())
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"FAIL: {error}\n")
