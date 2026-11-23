#!/usr/bin/env python3
"""Build and run all offline axis/panel control regressions. Never opens USB."""
from pathlib import Path
import os
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
build=root/"work/control-tests"
build.mkdir(parents=True,exist_ok=True)
axis=root/"XIAO/XIAO_ESP32C3/tests"
panel=root/"XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/tests"
sources=[axis/name for name in ("axis_limits_test.cpp","axis_jog_profile_test.cpp",
                               "axis_homing_test.cpp","axis_controller_test.cpp","panel_control_test.cpp","homing_disabled_test.cpp",
                               "manual_origin_test.cpp","panel_manual_zero_test.cpp","limit_feedback_test.cpp")]
sources += [panel/name for name in ("jog_trial_test.cpp","motion_telemetry_test.cpp")]
for source in sources:
    binary=build/source.stem
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",f"-I{axis/'mock'}",
                    str(source),"-o",str(binary)],check=True,cwd=root)
    subprocess.run([str(binary)],check=True,cwd=root)
env={**os.environ,"PYTHONDONTWRITEBYTECODE":"1"}
subprocess.run([sys.executable,str(root/"tools/test_usb_jog_protocol.py")],check=True,cwd=root,env=env)
print("PASS: 11 C++ suites and 4 offline PC protocol tests; no hardware used.",flush=True)
