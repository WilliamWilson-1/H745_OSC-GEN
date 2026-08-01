# H745 OSC-GEN

基于 NUCLEO-H745ZI-Q 的双核数字示波器/波形发生器，外加 STM32F103C8T6 HMI 协处理器。STM32H745 Cortex-M7 负责 ADC 采样、触发与测量、LTDC 显示和 DAC 发生；F103 负责 GT9147 触摸与 VFD；Cortex-M4 固件作为已编译、可刷写的保留核，目前不参与实时采集。

## 功能

- 真实 ADC 示波器：`PF11 / ADC1_INP2 / Zio A5`，12 位，TIM2 定时触发，DMA1 Stream1 采集 2048 点。
- 600 点触发波形显示，上升/下降沿触发，RUN/HOLD 和 SINGLE 单次采集。
- 自动测量频率、Vpp 和平均电压；无有效触发时会自动居中显示，不会卡死。
- 时基 `50 us/div`～`10 ms/div`，垂直档位 `0.1/0.2/0.5/1.0 V/div`，可调触发电平和垂直位置。
- DAC1_OUT2 正弦、方波、三角波发生，频率和幅度可调，DMA 循环输出。
- 800×480 L8 帧缓冲 + LTDC CLUT 显示，编码器、按键和触摸均可操作。
- H745 CM7、CM4 和 F103 三个目标都有 CMake Debug/Release 构建，自动产生 ELF/HEX/BIN。

## 安全提示

> 示波器输入是 MCU 裸 ADC 引脚，不是商用示波器前端。`PF11/A5` 只允许 **0～3.3 V**，禁止负压、超过 VDDA 的电压或市电直连。测量更高/双极性信号必须外接限流、分压、偏置和钳位保护电路，所有板卡必须共地。

## H745 引脚表

### 采集、发生和交互

| H745 引脚 | 方向 | 作用 | 说明 |
|---|---|---|---|
| PF11 | 模拟输入 | ADC1_INP2，示波器 CH1 | NUCLEO Zio/Arduino A5，0～3.3 V |
| PA5 | 模拟输出 | DAC1_OUT2，波形发生器 | 直流中点约 1.65 V，设置幅度为 Vpp |
| PB4 | 输入 | TIM3_CH1 | 增量编码器 A，内部上拉 |
| PB5 | 输入 | TIM3_CH2 | 增量编码器 B，内部上拉 |
| PE1 | 输入 | 编码器按键 | 低有效，内部上拉 |
| PB14 | 输出 | USART1_TX | 连 F103 PA10/RX |
| PB15 | 输入 | USART1_RX | 连 F103 PA9/TX |
| PA7 | 输出 | LCD 背光使能 | 高电平点亮 |
| PC4、PC5 | 输出 | LCD 辅助控制预留 | 当前启动时置低，依实际转接板网标使用 |
| PA13、PA14 | 双向/输入 | SWDIO、SWCLK | 板载 ST-Link 调试/刷写 |
| PD8、PD9 | 输出/输入 | ST-Link VCP TX/RX 预留 | 分配给 CM4，当前未开启 USART3 业务 |

### 800×480 RGB LCD / LTDC

| 信号 | H745 引脚 |
|---|---|
| R0～R7 | PG13, PA2, PA1, PB0, PA11, PC0, PB1, PE15 |
| G0～G7 | PE5, PE6, PA6, PE11, PB10, PB11, PC7, PG8 |
| B0～B7 | PE4, PA10, PC9, PD10, PE12, PA3, PB8, PB9 |
| LTDC_CLK | PE14 |
| LTDC_HSYNC | PC6 |
| LTDC_VSYNC | PA4 |
| LTDC_DE | PF10 |

当前 LCD 总线会占用 NUCLEO 的多个 Arduino/Zio 引脚，尤其是 A0/A1/A3，请勿再把它们当作 ADC 输入。本工程特意选择未冲突的 A5/PF11。板卡官方引脚参考：[ST UM2408](https://www.st.com/resource/en/user_manual/um2408-stm32h7-nucleo144-boards-mb1363-stmicroelectronics.pdf)。

## F103 HMI 协处理器

F103 完整工程放在 [`coprocessor/stm32f103_hmi`](coprocessor/stm32f103_hmi)，不包含 Eclipse `Debug/` 产物，但保留 CubeIDE 项目文件、`.ioc`、HAL/CMSIS 和独立 CMake 构建。其 VFD/GT9147 引脚和 H745↔F103 串口协议详见 [F103 README](coprocessor/stm32f103_hmi/README.md)。

H745 与 F103 之间使用 3.3 V TTL `115200 8N1`，接线必须交叉：

- H745 PB14/TX → F103 PA10/RX
- H745 PB15/RX ← F103 PA9/TX
- H745 GND ↔ F103 GND

## 界面操作

### 主菜单

触摸 `OSC` 进入示波器，触摸 `GEN` 进入波形发生器。右上角 `HOME` 返回主菜单。

### 示波器

- 左侧 `TIME / V/D / TRIG / POS` 选择时基、V/div、触发电平和垂直中心，旋转编码器修改当前项。
- 编码器短按依次切换四个控制项；长按 0.8 s 恢复默认时基、垂直档位、触发和位置。
- 右侧 `RUN/HOLD` 连续采集/保持，`RISE/FALL` 切换触发方向，`SINGLE` 采集一帧后自动 HOLD。
- 水平双指捏合改变时基，垂直双指捏合改变 V/div。
- 底部显示 `F`、`VPP`、`AVG`。屏幕红色虚线是触发电平。

### 波形发生器

- 左侧触摸选择正弦/方波/三角波。
- 触摸 `FREQUENCY` 或 `AMPLITUDE` 选中参数，旋转编码器调节；编码器短按也可切换。
- 长按编码器恢复 1 kHz、3.3 Vpp。输出从 PA5/DAC1_OUT2 取得。

## 构建

需要 CMake 3.22+、Ninja 和 Arm GNU Toolchain (`arm-none-eabi-gcc`)。已安装 STM32CubeCLT 时可直接在 PowerShell 执行：

```powershell
.\tools\build_all.ps1 -Configuration Debug
```

或分别构建：

```powershell
# H745 CM7 + CM4
cmake --preset Debug
cmake --build --preset Debug --parallel

# F103 HMI
cd coprocessor\stm32f103_hmi
cmake --preset Debug
cmake --build --preset Debug --parallel
```

主要产物：

- `CM7/build/Debug/NUCLEO_1_CM7.elf|hex|bin`
- `CM4/build/Debug/NUCLEO_1_CM4.elf|hex|bin`
- `coprocessor/stm32f103_hmi/build/Debug/stm32f103_hmi.elf|hex|bin`

## 刷写

### H745

连接 NUCLEO 板载 ST-Link，先构建，再执行：

```powershell
.\tools\flash_h745.ps1 -Configuration Debug
```

脚本先写入并校验 CM4 (`0x08100000`)，再写入并校验 CM7 (`0x08000000`)，最后复位。多个 ST-Link 同时连接时传入序列号：

```powershell
.\tools\flash_h745.ps1 -Configuration Debug -ProbeSerial 00112233445566778899AABB
```

### F103

使用独立 ST-Link 连接 F103 的 SWDIO、SWCLK、NRST 和 GND，确认目标无误后执行：

```powershell
.\tools\flash_f103.ps1 -Configuration Debug
```

H745 和 F103 是两个独立目标，不能用同一次 SWD 会话同时刷写。两者都刷写完成后断电重启整机。

## 工程结构

```text
CM7/                         H745 主核：ADC、LTDC、DAC、UI
CM4/                         H745 保留核固件
Common/                      H745 双核公共启动代码
Drivers/                     STM32H7 HAL/CMSIS
coprocessor/stm32f103_hmi/   F103 VFD + GT9147 独立工程
tools/                       构建和刷写脚本
```

ADC 采样缓冲和 DAC 查找表通过 `.dma_buffer` 链接段放在 `0x30000000` 起始的 D2 SRAM，避免 DMA1 不能访问 CM7 DTCM 以及 D-Cache 不一致导致的死机/花屏。
