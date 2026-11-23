#!/usr/bin/env python3
"""Read three C3 diagnostic inputs with motor 24 V off; never send motion commands."""

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import fcntl
import json
from pathlib import Path
import re
import subprocess
import termios
import time

from check_panel_axis_usb import open_serial


# Display order follows the physical assignment: negative, home, positive.
INPUTS = ("limit_neg_raw", "home_raw", "limit_pos_raw")
PINS = {"limit_neg_gpio": 6, "home_gpio": 7, "limit_pos_gpio": 10}
LABELS = ("负向 D4", "原点 D5", "正向 D10")


def validate(status):
    if (status.get("type") != "STATUS" or status.get("state") != "LOCKED" or
            status.get("motion_available") is not False or
            type(status.get("pulses")) is not int or status["pulses"] != 0):
        raise RuntimeError(f"诊断板未保持 LOCKED / 零脉冲: {status}")
    if any(type(status.get(key)) is not int or status[key] != value
           for key, value in PINS.items()):
        raise RuntimeError(f"固件引脚分配不匹配，请烧录新版诊断固件: {status}")
    values = tuple(status.get(key) for key in INPUTS)
    if any(type(value) is not int or value not in (0, 1) for value in values):
        raise RuntimeError(f"无效输入电平: {status}")
    return values


def connect(stack, port):
    # Refuse to share a port with the jog bridge or another serial monitor.
    occupied = subprocess.run(["fuser", str(port)], capture_output=True)
    if occupied.returncode == 0:
        raise RuntimeError(f"串口已被进程占用: {port} PID={occupied.stdout.decode().strip()}")
    if occupied.returncode != 1:
        raise RuntimeError(f"无法检查串口占用: {occupied.stderr.decode().strip()}")
    link = stack.enter_context(open_serial(port))
    fcntl.ioctl(link.fd, termios.TIOCEXCL)
    stack.callback(fcntl.ioctl, link.fd, termios.TIOCNXCL)
    deadline = time.monotonic() + 3
    while True:
        link.send({"cmd": "HELLO"})
        try:
            hello = link.receive("HELLO", timeout=0.4)
            break
        except TimeoutError:
            if time.monotonic() >= deadline:
                raise
    if hello.get("role") != "AXIS_C3":
        return None
    if (hello.get("firmware") != "axis_c3_diagnostic" or hello.get("protocol") != 1 or
            not isinstance(hello.get("device_id"), str) or
            not re.fullmatch(r"[0-9A-F]{12}", hello["device_id"])):
        raise RuntimeError(f"需要新版 AXIS_C3 诊断固件: {hello}")
    return link, hello


def monitor(args):
    ports = [Path(args.port)] if args.port else sorted(Path("/dev/serial/by-id").glob("usb-Espressif_*"))
    if not ports:
        raise RuntimeError("未发现 XIAO USB 串口，请连接 C3 数据线")
    with ExitStack() as stack:
        candidates = [result for port in ports if (result := connect(stack, port)) is not None]
        if len(candidates) != 1:
            raise RuntimeError(f"需要唯一 AXIS_C3，实际找到 {len(candidates)} 个")
        link, hello = candidates[0]
        args.log.parent.mkdir(parents=True, exist_ok=True)
        log = stack.enter_context(args.log.open("x", encoding="utf-8"))

        def record(kind, **data):
            log.write(json.dumps({"at": datetime.now(timezone.utc).isoformat(),
                                  "kind": kind, **data}, ensure_ascii=False) + "\n")
            log.flush()

        record("identity", port=str(link.port), hello=hello, pins=PINS)
        print(f"AXIS_C3 {hello['device_id']}；仅输入诊断；日志 {args.log}", flush=True)
        print("显示顺序：负向D4 / 原点D5 / 正向D10。白线方案：遮挡=0，解除=1。", flush=True)
        previous = None
        deadline = time.monotonic() + args.seconds
        samples = 0
        try:
            while time.monotonic() < deadline:
                link.send({"cmd": "STATUS"})
                status = link.receive("STATUS", timeout=1)
                values = validate(status)
                record("sample", status=status)
                samples += 1
                if values != previous:
                    print(" / ".join(f"{label}={value}" for label, value in zip(LABELS, values)),
                          flush=True)
                    previous = values
                time.sleep(min(0.05, max(0, deadline - time.monotonic())))
        except BaseException as error:
            record("interrupted", error=str(error), samples=samples)
            raise
        record("finished", samples=samples)
        print(f"监测结束，共 {samples} 次采样；此记录需结合人工遮挡顺序判读。", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="可选串口路径；仍通过 HELLO 核对角色")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--log", type=Path, default=Path(__file__).resolve().parents[1] /
                        "XIAO/XIAO_ESP32C3/.build/input-checks" /
                        (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".jsonl"))
    args = parser.parse_args()
    if not 0 < args.seconds <= 600:
        parser.error("--seconds 必须在 0 到 600 秒之间")
    try:
        monitor(args)
    except KeyboardInterrupt:
        parser.exit(130, "输入监测已停止。\n")
    except (OSError, RuntimeError, ValueError, TimeoutError) as error:
        parser.exit(1, f"输入监测失败: {error}\n")
