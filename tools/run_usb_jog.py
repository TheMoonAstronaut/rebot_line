#!/usr/bin/env python3
"""Relay panel buttons and axis telemetry between the two XIAO USB ports."""

import argparse
from contextlib import ExitStack, suppress
from pathlib import Path
import re
import select
import time

from check_panel_axis_usb import open_serial


FIRMWARE = {"PANEL_S3": "panel_s3_jog", "AXIS_C3": "axis_c3_jog"}
STATE_NAMES = ("BOOT", "LOCKED", "ARM_RELEASE", "READY", "START_WAIT",
               "FORWARD", "REVERSE", "RELEASE", "FAULT")


def identify(link):
    deadline = time.monotonic() + 3
    while True:
        link.send({"cmd": "HELLO"})
        try:
            message = link.receive("HELLO", timeout=0.4)
            break
        except TimeoutError:
            if time.monotonic() >= deadline:
                raise
    role = message.get("role")
    identity = message.get("device_id")
    if (role not in FIRMWARE or message.get("firmware") != FIRMWARE[role] or
            message.get("protocol") != 4 or not isinstance(identity, str) or
            not re.fullmatch(r"[0-9A-F]{12}", identity)):
        raise RuntimeError(f"Unexpected jog firmware on {link.port}: {message}")
    return role, identity


def relay(panel, axis):
    axis.send({"cmd": "STOP"})
    panel.buffer.clear()
    last_panel_seq = None
    now = time.monotonic()
    previous_loop = last_panel = last_axis = now
    bridge_seq = 0
    last_state = None
    print("USB jog active. Release all keys, then hold USR3 until RELEASE USR3.", flush=True)
    try:
        while True:
            ready, _, _ = select.select([panel.fd, axis.fd], [], [], 0.02)
            now = time.monotonic()
            if now - previous_loop >= 0.15 or now - last_panel >= 0.15:
                raise RuntimeError("Panel heartbeat lost. Axis locking; restart the bridge to resume.")
            if now - last_axis >= 0.5:
                raise RuntimeError("Axis status lost. Axis locking; restart the bridge to resume.")
            previous_loop = now
            if panel.fd in ready:
                panel.read()
                for message in panel.messages():
                    if message.get("type") != "KEYS":
                        continue
                    seq, mask = message.get("seq"), message.get("mask")
                    if type(seq) is not int or not 0 <= seq <= 0xFFFFFFFF or \
                       type(mask) is not int or not 0 <= mask <= 7:
                        raise RuntimeError("Invalid panel key heartbeat")
                    if last_panel_seq is not None and not 0 < (seq - last_panel_seq) % (1 << 32) < (1 << 31):
                        raise RuntimeError("Panel restarted or repeated a heartbeat")
                    last_panel_seq = seq
                    last_panel = now
                    bridge_seq = (bridge_seq + 1) % (1 << 32)
                    axis.send({"cmd": "KEYS", "seq": bridge_seq, "mask": mask})
            if axis.fd in ready:
                axis.read()
                for message in axis.messages():
                    if message.get("type") != "AXIS":
                        continue
                    fields = ("state", "rev", "linked", "hz", "fwd", "back", "raw", "recovery", "homed", "home", "fault", "zeroed", "zero", "pos")
                    maxima = (len(STATE_NAMES) - 1, 1, 1, 8000, (1 << 63) - 1, (1 << 63) - 1, 7, 3, 1, 9, 11, 1, (1 << 32)-1, (1 << 63) - 1)
                    for field, maximum in zip(fields, maxima):
                        value = message.get(field)
                        minimum = -maximum if field == "pos" else 0
                        if type(value) is not int or not minimum <= value <= maximum:
                            raise RuntimeError(f"Invalid axis {field}: {message}")
                    if bool(message["homed"]) != (message["home"] == 5) or (message["homed"] and (message["fault"] or not message["zeroed"])):
                        raise RuntimeError(f"Inconsistent axis home state: {message}")
                    last_axis = now
                    panel.send({"cmd": "AXIS", **{key: message[key] for key in fields}})
                    if message["state"] != last_state:
                        last_state = message["state"]
                        print(f"Axis: {STATE_NAMES[last_state]}", flush=True)
    finally:
        with suppress(OSError, RuntimeError):
            axis.send({"cmd": "STOP"})


def main(ports):
    ports = ports or sorted(Path("/dev/serial/by-id").glob("usb-Espressif_*"))
    if len(ports) != 2 or len({Path(port).resolve() for port in ports}) != 2:
        raise RuntimeError("Provide two different XIAO USB device paths")
    with ExitStack() as stack:
        links, identities = {}, set()
        for port in ports:
            link = stack.enter_context(open_serial(port))
            role, identity = identify(link)
            if role in links or identity in identities:
                raise RuntimeError("Duplicate board role or device ID")
            links[role] = link
            identities.add(identity)
            print(f"{role} at {Path(port).resolve()}", flush=True)
        relay(links["PANEL_S3"], links["AXIS_C3"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", nargs="*", help="Two USB serial paths, in either order")
    try:
        main(parser.parse_args().ports)
    except KeyboardInterrupt:
        print("USB jog stopped.")
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"USB jog stopped: {error}\n")
