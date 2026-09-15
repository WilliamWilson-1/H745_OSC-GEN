# H745 OSC-GEN

基于 NUCLEO-H745ZI-Q 的双核数字示波器/波形发生器，外加 STM32F103C8T6 HMI 协处理器。STM32H745 Cortex-M7 负责 ADC 采样、触发与测量、LTDC 显示和 DAC 发生；F103 负责 GT9147 触摸与 VFD；Cortex-M4 固件作为已编译、可刷写的保留核，目前不参与实时采集。

## 功能

- 真实 ADC 示波器：`PF11 / ADC1_INP2 / Zio A5`，12 位，TIM2 定时触发，DMA1 Stream1 采集 2048 点。
- 最多 600 个真实点的触发波形显示，上升/下降沿触发，RUN/HOLD 和 SINGLE 单次采集。
- 自动测量频率、Vpp、平均电压、最高电压和最低电压；无有效触发时会自动居中显示，不会卡死。
- 时基 `2 us/div`～`10 ms/div`，快速档最高配置采样率 2 MS/s；垂直档位 `0.1/0.2/0.5/1.0 V/div`，可调触发电平和垂直位置。
- DAC1_OUT2 正弦、方波、三角波发生，50 Hz～100 kHz 设置范围，自适应每周期点数以限制 DAC 更新率；右侧触摸拨钮支持点击/拖动，上电默认关闭输出。
- 800×480 L8 帧缓冲 + LTDC CLUT 显示，编码器、按键和触摸均可操作。
- v3.2 About 页面：initUI 标志、开发者信息，以及覆盖全部页面的三套配色。
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

触摸 `Oscilloscope` 卡片进入示波器，触摸 `Wave generator` 卡片进入波形发生器。首页不再显示“01 / 02”编号。示波器卡片使用实际 ADC 采集数据，并复用当前时基、V/div 与垂直位置；`RUN` 时持续预览，`HOLD` 时保持最后一帧，尚无数据则明确显示等待/无采集提示，不绘制模拟正弦波。发生器卡片仍只是所选波形形状预览。

仪器页和 About 页右上角 `Home` 返回主菜单，不改变示波器 RUN/HOLD/SINGLE 状态；重新进入示波器也不会自动解除 HOLD。上电默认开启 ADC 采集，首页即可预览。返回菜单不会关闭已经开启的发生器，左下角始终标明 PA5 输出状态。

主菜单右下角 `Motion: on / Motion: reduced` 可关闭页面位移、按钮缩放和拨钮自动回位动效，直接拖动仍然跟手。此设置保留到本次断电，复位后恢复默认。完整设计、动效时序和可复现的预览说明见 [UI 说明](docs/UI_DESIGN.md)。

### About 与配色

点击首页右上角 `About` 进入关于页面：上方为原始 initUI 标志，随主题自动采用浅色或深色；下方显示开发者 **William Wilson / Vida David / Dkkk** 和版本 **v3.2**。

`Appearance` 提供 `Graphite`（石墨黑，默认）、`Midnight`（午夜蓝）、`Ivory`（暖白）。点击选项立即应用于首页、示波器、发生器和 About，选中项有边框与勾选标记。主题在本次开机期间保留，复位/断电后恢复默认，不写入 Flash。切换主题和进出 About 不改变采集状态、仪器参数或发生器输出；About 中编码器和复位键不调整仪器。

### 示波器

- 左侧 `Time / V / div / Trigger / Position` 选择时基、V/div、触发电平和垂直中心，旋转编码器修改当前项。
- 触摸左侧四个控制项后旋转编码器调节；编码器按键只执行一键复位，不再区分短按和长按。
- 右侧 `RUN/HOLD` 连续采集/保持，`RISE/FALL` 切换触发方向，`SINGLE` 采集一帧后自动 HOLD。
- 右侧新增 `Coarse`（粗调）/ `Fine`（微调），蓝色高亮表示旋钮当前步进模式，默认粗调；仅影响示波器旋钮，双指缩放仍使用标准档位。切换模式本身不改变参数或中断采集。
- 水平双指拉开会放大时间轴、捏合会缩小；垂直双指拉开会提高垂直灵敏度。
- 底部五张卡片显示 `FREQUENCY`、`PEAK TO PEAK`、`AVERAGE`、`HIGH` 和 `LOW`。图内灰蓝色虚线是触发电平；尚未取得数据时显示 `--`，不会展示模拟测量值。

| 旋钮选中项 | Coarse 粗调 | Fine 微调 | 范围 |
| --- | --- | --- | --- |
| Time | 相邻标准时基档位 | 当前值约 10%，四舍五入至整数 us，至少 1 us | 2 us/div～10 ms/div |
| V/div | 相邻 `0.1/0.2/0.5/1.0` 标准档位 | 当前值约 10%，四舍五入至 0.01 V，至少 0.01 V | 0.1～1.0 V/div |
| Trigger | 50 mV | 10 mV | 0～3.3 V |
| Position | 50 mV | 10 mV | 0～3.3 V |

例如 `500 us/div` 微调增加为 `550 us/div`；切回粗调后向下到 `500`、向上到 `1000`。微调是按当前值步进，往返一个刻度不保证精确回原值；在最快档 `2 us/div` 受 1 us 分辨率限制，下一步为 `3 us/div`。毫秒中间档位保留三位小数显示。编码器复位恢复粗调和默认参数。

### 波形发生器

- 左侧触摸选择正弦/方波/三角波。
- 仅底部卡片**右侧拨钮**可控制 PA5：点击拨钮后松手切换，向右拖动开启、向左拖动关闭。文字及大框其他区域不响应；上电和一键复位后默认关闭并输出 0 V 码。
- 拖动时只预览位置，松手才提交输出状态；中点附近保留原状态。明显纵向拖动、双指、触摸链路错误或复位会取消当前拨动，不以甩动速度决定是否开启。
- 触摸 `FREQUENCY` 或 `AMPLITUDE` 选中参数，再旋转编码器调节。
- 按下编码器恢复正弦波、1 kHz、3.3 Vpp，并关闭输出。输出从 PA5/DAC1_OUT2 取得。
- 中间的 `WAVE SHAPE / Preview only` 仅用于说明所选波形形状，不是 PA5 的实测反馈。幅度为峰峰值，波形围绕约 1.65 V 偏置，不输出负电压。

### 100 kHz 与快速时基

| 时基 | 十格时间跨度 | 配置采样率 | 屏内真实 ADC 点数 | 100 kHz 周期数 |
| --- | --- | --- | --- | --- |
| 20 us/div | 200 us | 2 MS/s | 400 | 20 |
| 10 us/div | 100 us | 2 MS/s | 200 | 10 |
| 5 us/div | 50 us | 2 MS/s | 100 | 5 |
| 2 us/div | 20 us | 2 MS/s | 40 | 2 |

完整档位为 `2/5/10/20/50/100/200/500 us/div` 和 `1/2/5/10 ms/div`，默认仍为 `500 us/div`。波形按真实采样时间映射到 600 像素宽的绘图区，连线仅帮助阅读，不产生额外独立采样点。快速档名义采样间隔 0.5 us，100 kHz 每周期约 20 个 ADC 点；更小的 us/div 是横向放大，不等于提高 ADC 位数或模拟带宽。修改时基会清除旧帧，HOLD 下需要 RUN 或 SINGLE 重新采集，避免用新时间刻度错误标注旧数据。

DAC 每周期使用 10～128 个点，更新率限制在 1 MHz 内；在当前 240 MHz 定时器时钟下，100 kHz 使用 10 点、分频 240，名义周期准确为 10 us。其他设置存在整数分频误差，实际频率不超过设定值，仍受时钟误差影响。调整频率/幅度/波形时会短暂停止输出并重建表，不能保证相位连续。

**100 kHz 是设置上限，不是低失真或方波边沿的保证。** DAC 最高档仅 10 点/周期，满幅建立时间、负载及外接滤波都会影响实际输出；方波/三角波的高次谐波会受限。官方 [STM32H745 数据手册](https://www.st.com/resource/en/datasheet/stm32h745zi.pdf) 列明 DAC 为 1 MHz，并提供模拟电气条件。需要高质量高频波形时应使用外置高速 DAC/DDS 与合适的模拟前端。当前自动化检查验证代码、时序计算和模拟输入，不替代实机带宽/失真校准。

本版增加了 `D:` 移动和 `C:` 取消触摸消息，**H745 CM4/CM7 与 F103 需要配套刷写**；旧 F103 只能提供点击，不能提供拨钮拖动。

本次构建、刷写及实机运行检查结果见 [验证记录](docs/VALIDATION.md)。

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

ADC 采样缓冲和 DAC 查找表通过 `.dma_buffer` 链接段放在 `0x30004000` 起始的独立 16 KiB D2 SRAM 区域。D2 的前 16 KiB 留给 CM4，后 256 KiB 留给 LCD 后台下段，避免双核、DMA 和显存重叠。

LCD 使用一张 AXI 前台扫描帧和上下分段的后台绘制帧，在垂直消隐期由 MDMA 将完整后台复制到前台；LTDC 不直接读取 D2 SRAM。无需外接 SDRAM，详见 [显示内存与换帧说明](docs/UI_DESIGN.md#显示内存与同步换帧)。此次内存布局变更必须成套刷写 CM4 和 CM7（现有 `flash_h745.ps1` 已按此顺序执行）。
