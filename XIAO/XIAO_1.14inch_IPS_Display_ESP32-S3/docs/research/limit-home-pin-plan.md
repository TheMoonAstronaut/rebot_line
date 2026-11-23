# 两端限位与独立原点输入：引脚核查

核查日期：2026-11-19。对象仅为本目录的 XIAO 1.14'' IPS Display（ESP32-S3），主控为 XIAO ESP32-S3 Plus。本次仅研究接线资源，没有修改或烧录固件。

## 结论

可以扩展成“两端限位 + 一个独立原点”三路输入，但目前不能承诺整板上有三个空闲、方便接线的 GPIO 接口。要区分 MCU 尚可复用的引脚和屏幕扩展板实际引出的焊盘。保留屏幕、三个按键和现有电机接线时，优先考虑 Grove I2C 输入扩展；直接 GPIO 方案需要确认 XIAO 本体焊盘可触达，并焊接引线。[屏幕板引脚表](https://wiki.seeedstudio.com/getting_started_1.14_inch_display_esp32s3/#pin-map)、[官方硬件照片](https://files.seeedstudio.com/wiki/Display_Gadgets/imgs/114_ESP32S3Plus_display_hardware_overviewNEW.png)

## 当前占用

- EN：D11 / GPIO38；STEP：D12 / GPIO39；DIR：D13 / GPIO40。
- USR1、USR2、USR3：D6 / GPIO43、D7 / GPIO44、D19 / GPIO11，均保留现有用途。
- LCD 使用 D2、D3、D8、D10、D17、D18，不能拿来接限位。

电机与按键依据：[当前主程序](../../xiao_pulse_trial/xiao_pulse_trial.ino)；LCD 与 Plus GPIO 对应依据：[屏幕板引脚表](https://wiki.seeedstudio.com/getting_started_1.14_inch_display_esp32s3/#pin-map)、[Plus 引脚表](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#xiao-esp32-s3-plus-back)。

## 直接 GPIO：有条件成立

| 候选 | MCU GPIO | 实际硬件情况 | 接入条件 |
| --- | --- | --- | --- |
| D15 | GPIO42 | 无功能外设，接 TP15；官方照片背面右侧标 D15 | 可作为一路候选，核对实物焊盘 |
| D9 | GPIO8 | 屏幕扩展板标 NC、没有物理连接；原理图未引出到测试点 | 只能考虑从 XIAO 模块自己的 D9 焊盘引线，不能当作扩展板已有插口 |
| D0 | GPIO1 | 接板载麦克风 CLOCK 输入 | 停用麦克风/PDM时钟功能，从 XIAO D0 焊盘引线；仍有麦克风输入负载 |

这些连接可在[官方原理图第 4 页](https://files.seeedstudio.com/wiki/Display_Gadgets/resources/schematic/XIAO%201.14%27%27%20IPS%20Display%20%28ESP32-S3%29%20Schematic.pdf#page=4)核对；GPIO 编号来自[Plus 引脚表](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#xiao-esp32-s3-plus-back)。D0 是麦克风的输入端，因此在不再产生 PDM 时钟时，将其作为开关输入是电路层面的候选方案，尚未做实物验证。

因此，D15 + D9 + D0 可作为待检查的三路直连路线，不能现在直接下发最终接线表。需先确认实物版本、焊点可达性与连通关系，再确认电气输入方案。D1 接麦克风 DATA **输出**，不能因程序没使用麦克风就当作空闲输入；D14 接 IMU 中断输出；D16 接电池分压，都不列为空闲输入。[官方原理图第 4 页](https://files.seeedstudio.com/wiki/Display_Gadgets/resources/schematic/XIAO%201.14%27%27%20IPS%20Display%20%28ESP32-S3%29%20Schematic.pdf#page=4)

## Grove I2C 扩展：接口更明确

D4 / GPIO5 与 D5 / GPIO6 已通过 Grove 插座引出，同时连接板载 IMU；适合继续作为 I2C 总线增加输入扩展器。以 TCA9534 为例，它提供 8 路可配置 IO，并支持输入变化中断。可用其中三路接两端限位与原点；D15 可选接扩展器 INT，也可由固件轮询。扩展器应以 3.3 V 供电并核对模块上拉电压、地址与实际电路。[官方屏幕板原理图第 4 页](https://files.seeedstudio.com/wiki/Display_Gadgets/resources/schematic/XIAO%201.14%27%27%20IPS%20Display%20%28ESP32-S3%29%20Schematic.pdf#page=4)、[TI TCA9534 官方资料](https://www.ti.com/product/TCA9534)

这是本次工程建议：扩展器可以保留 IMU，且避免从模块边缘细焊盘找三路信号。不能把 D4/D5 直接各接一个对地开关，同时还保留正常 I2C 通信；开关闭合会拉住总线。I2C 读取还引入通信时间及总线失败路径，后续固件需要定义停止延迟、超时停机与回零逻辑；这些尚未实现或测试。[板上连接依据](https://files.seeedstudio.com/wiki/Display_Gadgets/resources/schematic/XIAO%201.14%27%27%20IPS%20Display%20%28ESP32-S3%29%20Schematic.pdf#page=4)、[扩展器通信与中断能力依据](https://www.ti.com/product/TCA9534)

## 接线前仍需确认

用户已说明准备使用 **3.3 V 供电的光电开关**，但未提供具体型号、输出形式、触发极性和耗电量。后续选带数字 OUT 的型号；若为推挽输出，核对输出为 3.3 V 逻辑；若为开漏/开集电极输出，按数据手册将输出上拉到 3.3 V。三个传感器的总供电电流也需按型号核对，不能仅凭供电电压确定整套电气兼容性。

XIAO 为 3.3 V IO；ESP32-S3 数据手册的输入高电平上限为对应电源域 VDD + 0.3 V，不能把工业 24 V 开关输出直接接 GPIO 或上述扩展器。带其他电压输出的传感器需先确定电平转换或隔离输入电路。[Espressif 官方数据手册，表 5-4，第 65 页](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf#page=65)、[TI TCA9534 电气范围](https://www.ti.com/product/TCA9534)

另一个可选器件是 MCP23017，官方支持 1.8–5.5 V 供电、16 路 IO 和中断输出，因此也可按 3.3 V 使用。可以将 GPA0、GPA1、GPA2 分配给负向限位、正向限位、原点，INTA 接 D15；这是待实现的分配建议，不是现有固件功能。其地址须与板载 IMU 的 0x6A 区分。[Microchip MCP23017 官方规格](https://www.microchip.com/en-us/product/mcp23017)、[Seeed IMU 地址](https://wiki.seeedstudio.com/function_1.14_inch_display_esp32s3/#imu)

此文不更新 UI 验收记录，也不把无电机测试解释为带电运动或限位功能验收。
