#!/usr/bin/env python3
"""Motor-24V-OFF bench test using PC-generated hold-to-run key frames, not panel keys."""

import argparse
from contextlib import ExitStack, suppress
from datetime import datetime, timezone
import json
from pathlib import Path
import select
import subprocess
import time

from check_panel_axis_usb import open_serial
from run_usb_jog import identify


def test(args):
    ports = sorted(Path("/dev/serial/by-id").glob("usb-Espressif_*"))
    if not ports:
        raise RuntimeError("No XIAO USB devices")
    with ExitStack() as stack:
        axes = []
        for port in ports:
            check = subprocess.run(["fuser", str(port)], capture_output=True)
            if check.returncode != 1:
                raise RuntimeError(f"Port busy or occupancy check failed: {port}")
            link = stack.enter_context(open_serial(port))
            role, identity = identify(link)
            if role == "AXIS_C3":
                axes.append((link, identity))
        if len(axes) != 1:
            raise RuntimeError("Expected exactly one AXIS_C3")
        axis, identity = axes[0]
        samples = []
        last = None
        sequence = 0
        require_raw = False
        last_at = time.monotonic()

        def pump(seconds, mask=0, send=True):
            nonlocal last, sequence, last_at
            deadline = time.monotonic() + seconds
            next_send = 0
            while time.monotonic() < deadline:
                now = time.monotonic()
                if send and now >= next_send:
                    sequence += 1
                    axis.send({"cmd": "KEYS", "seq": sequence, "mask": mask})
                    next_send = now + 0.02
                if select.select([axis.fd], [], [], min(0.01, max(0, deadline-now)))[0]:
                    axis.read()
                    for status in axis.messages():
                        if status.get("type") != "AXIS":
                            continue
                        for field, maximum in (("raw", 7), ("recovery", 3), ("state", 8),
                                               ("target", 6827 if args.normal_jog else 200), ("homed", 1),
                                               ("home", 9), ("fault", 11)):
                            if type(status.get(field)) is not int or not 0 <= status[field] <= maximum:
                                raise RuntimeError(f"Invalid commissioning telemetry: {status}")
                        if status["state"] == 8:
                            raise RuntimeError(f"Axis FAULT; stop and inspect: {status}")
                        if require_raw and status["raw"] != args.raw:
                            raise RuntimeError(f"Sensor changed during fixed-input test: {status}")
                        last = status
                        last_at = time.monotonic()
                        samples.append({"elapsed": last_at - started, **status})
                if time.monotonic() - last_at > 0.5:
                    raise RuntimeError("Axis telemetry timeout")

        started = time.monotonic()
        axis.send({"cmd": "STOP"})
        try:
            pump(2.3)
            if last is None or last["raw"] != args.raw:
                raise RuntimeError(f"Expected raw mask {args.raw}, actual {last}")
            require_raw = True
            pump(1.2, 4)
            pump(0.2)
            if last["state"] != 3:
                raise RuntimeError(f"Failed to arm with a separate release: {last}")
            before = dict(last)
            pump(args.seconds, args.mask)
            if args.normal_jog and (last["state"] != (5 if args.mask == 1 else 6) or last["target"] != 6827):
                raise RuntimeError(f"Normal held jog stopped early or did not reach 6827 Hz: {last}")
            if args.timeout_test:
                pump(0.4, send=False)
                if last["linked"] != 0 or last["target"] != 0:
                    raise RuntimeError(f"No local lock after missing key heartbeat: {last}")
                plateau = last["total"]
                pump(0.2, args.mask)  # Held key on reconnect must not re-start.
                if last["total"] != plateau or last["linked"] != 0:
                    raise RuntimeError(f"Held-key reconnect re-started: {last}")
            else:
                pump(0.2)
                if last["target"] != 0:
                    raise RuntimeError(f"Raw button release did not stop pulses: {last}")
            delta = (last["total"] - before["total"]) % (1 << 32)
            if not args.normal_jog and not 0 <= delta <= 400:
                raise RuntimeError(f"Jog pulse budget exceeded: {delta}")
            if args.normal_jog and delta <= 400:
                raise RuntimeError(f"Normal jog still appears pulse-limited: {delta}")
            if args.expect == "blocked" and delta != 0:
                raise RuntimeError(f"Blocked direction generated {delta} pulses")
            if args.expect == "move" and delta == 0:
                raise RuntimeError("Allowed direction generated no pulses")
            if args.expect == "move":
                directional = "fwd" if args.mask == 1 else "back"
                opposite = "back" if args.mask == 1 else "fwd"
                if last[directional] - before[directional] != delta or last[opposite] != before[opposite]:
                    raise RuntimeError("Direction accounting mismatch")
            plateau = last["total"]
            axis.send({"cmd": "STOP"})
            pump(0.25, send=False)
            if last["total"] != plateau or last["target"] != 0 or last["linked"] != 0:
                raise RuntimeError(f"Stop failed: {last}")
            summary = {"device_id": identity, "raw_required": args.raw, "mask": args.mask,
                       "expect": args.expect, "seconds": args.seconds,
                       "timeout_test": args.timeout_test, "pulses": delta,
                       "normal_jog": args.normal_jog,
                       "max_target_hz": max(s["target"] for s in samples), "last": last}
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_text(json.dumps({"result": "PASS", "summary": summary,
                                            "samples": samples}, indent=2) + "\n")
            print(json.dumps(summary), flush=True)
            print(f"PASS (motor power OFF, PC key frames). Log: {args.log}", flush=True)
        except BaseException as error:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_text(json.dumps({"result": "FAIL", "error": str(error),
                                            "samples": samples}, indent=2) + "\n")
            raise
        finally:
            with suppress(OSError, RuntimeError):
                axis.send({"cmd": "STOP"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", type=int, required=True, choices=range(8),
                        help="Expected raw bits: negative=1, home=2, positive=4")
    parser.add_argument("--mask", type=int, required=True, choices=(1, 2), help="1 forward, 2 reverse")
    parser.add_argument("--expect", required=True, choices=("move", "blocked"))
    parser.add_argument("--seconds", type=float, default=1.5)
    parser.add_argument("--timeout-test", action="store_true")
    parser.add_argument("--normal-jog", action="store_true",
                        help="Motor power OFF only: verify normal sustained jog at 6827Hz")
    parser.add_argument("--log", type=Path, default=Path(__file__).resolve().parents[1] /
                        "XIAO/XIAO_ESP32C3/.build/limit-checks" /
                        (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".json"))
    args = parser.parse_args()
    if not 0 < args.seconds <= 5:
        parser.error("--seconds must be between 0 and 5")
    if args.normal_jog and (args.raw != 7 or args.expect != "move" or args.seconds < 4):
        parser.error("--normal-jog requires --raw 7 --expect move and --seconds >= 4")
    try:
        test(args)
    except KeyboardInterrupt:
        parser.exit(130, "Bench test interrupted; STOP sent.\n")
    except (OSError, RuntimeError, TimeoutError) as error:
        parser.exit(1, f"FAIL: {error}\n")
