# reBot Line

滑台双板控制程序：XIAO ESP32-C3 负责 STEP / DIR / EN、两端限位和原点输入；XIAO ESP32-S3 Plus 屏幕负责显示和 USR1 / USR2 / USR3 按键；PC 通过两路 USB 串口转发按键与状态。

## 当前版本

2026-11-23 现场修复版，C3、S3 和 PC 配套使用协议 **4**。自动寻零入口暂时关闭；D5 经过原点校零功能启用。

- USR1 正向、USR2 反向，按住运行、松开停止；普通点动从 20 Hz 在 2 秒内升到 6827 Hz，按现有传动参数估算约 80 mm/s。
- 锁定时长按 USR3 约 1 秒，松开全部按键后进入 READY；READY / 点动时按 USR3 立即停止并锁定。
- 任一端部限位新触发后，停止脉冲、禁用驱动并锁定两个方向。长按 USR3 一次确认，松开后即可低速反向退出。
- D4 负端触发后用 **USR1** 退出；D10 正端触发后用 **USR2** 退出。屏幕显示 `NEG END STOP` / `POS END STOP` 和对应退离按键。
- 限位仍有效时退离最高 200 Hz，累计最多 400 脉冲或 3 秒有效运行时间。已有退离脉冲且限位连续解除 20 ms 后，结束退离限制与旧定时器预算，重新加速；方向键不必先松开。
- D5 有效边沿建立零点参考，运动不中断，累计脉冲不清除。停机、故障确认和链路锁定保留软件坐标；C3 重启后重新建立参考。
- 双端同时触发仍禁止解锁；定时器故障要求排查后重启。按键心跳丢失会停止并锁定，重连不自动续跑。

位置和速度来自软件 STEP 计数，未使用编码器。当前记录不能代替机械速度、停止距离或回零重复性验收。

## 接线

| C3 信号 | 板上引脚 | GPIO |
| --- | --- | --- |
| STEP | D2 | 4 |
| DIR | D1 | 3 |
| EN（低电平有效） | D3 | 5 |
| 负向限位 NEG | D4 | 6 |
| 原点 HOME | D5 | 7 |
| 正向限位 POS | D10 | 10 |

当前光电开关采用 NPN 白线输出，低电平表示遮挡，输入启用内部上拉。传感器棕线接 5 V，蓝线接 GND，白线分别接上表对应输入，未用黑线单独绝缘。GPIO 输入使用 3.3 V 逻辑。

## 目录

| 路径 | 用途 |
| --- | --- |
| `XIAO/common/` | 协议和公共定义 |
| `XIAO/XIAO_ESP32C3/axis_c3_jog/` | 当前 C3 控制固件 |
| `XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog/` | 当前 S3 屏幕固件 |
| `XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/xiao_pulse_trial/` | 共享控制 / 显示代码及保留的旧单板程序 |
| `tools/run_usb_jog.py` | PC USB 转发上位机 |
| `tools/run_control_tests.py` | 不连接 USB 的回归测试 |
| `docs/validation/2026-11-23.md` | 本次修复与验证范围 |

两块板目录内还有输入诊断程序、早期试验和历史说明；当前运行入口以上表为准。

## 编译与烧录

已验证构建环境：Arduino CLI、Espressif Arduino ESP32 core **3.3.12**，以及 S3 使用的 Seeed_GFX2 显示库。Linux 下如遇显示库文件名大小写问题，可使用仓库中的 `fix_seeed_gfx_case.py`，用实际安装路径替换参数。

在仓库根目录编译：

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 \
  --board-options CDCOnBoot=default \
  --build-path XIAO/XIAO_ESP32C3/.build/axis_c3_jog \
  XIAO/XIAO_ESP32C3/axis_c3_jog

arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3_Plus \
  --board-options USBMode=hwcdc,CDCOnBoot=default,PSRAM=opi \
  --build-path XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/.build/panel_s3_jog \
  XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog
```

烧录及首次输入测试保持驱动器 **24 V 关闭**，先关闭占用串口的上位机。选择实际 USB 设备路径分别上传：

```bash
arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32C3 \
  --board-options CDCOnBoot=default --port /dev/serial/by-id/C3设备链接 \
  --input-dir XIAO/XIAO_ESP32C3/.build/axis_c3_jog \
  XIAO/XIAO_ESP32C3/axis_c3_jog

arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32S3_Plus \
  --board-options USBMode=hwcdc,CDCOnBoot=default,PSRAM=opi \
  --port /dev/serial/by-id/S3设备链接 \
  --input-dir XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/.build/panel_s3_jog \
  XIAO/XIAO_1.14inch_IPS_Display_ESP32-S3/panel_s3_jog
```

## 启动上位机与测试

PC 转发器使用 Python 3 标准库和 Linux 串口接口，自动识别两块板的身份及协议版本：

```bash
python3 tools/run_usb_jog.py
# 也可显式传入 C3、S3 的两个 /dev/serial/by-id/ 路径，顺序不限。
```

只运行一个串口控制程序。退出上位机时会发送 STOP；再次启动须松开全部按键并重新解锁。

离线测试需要 Python 3 和支持 C++17 的 g++，不会打开串口：

```bash
python3 tools/run_control_tests.py
```

当前通过 **11 组 C++ 测试和 4 组 PC 协议测试**。构建产物、临时备份和现场 USB 日志保存在本地，不纳入版本控制。
