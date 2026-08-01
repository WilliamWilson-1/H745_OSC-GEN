# STM32F103 HMI 协处理器

该子工程是整机的人机界面协处理器固件，使用 STM32F103C8T6 驱动 8 位 VFD 和 GT9147 电容触摸屏，通过 USART1 与 STM32H745 CM7 交换数据。它保留了 STM32CubeIDE 工程文件，同时增加独立 CMake 构建，不依赖上层 H745 工程。

## 引脚

| F103 引脚 | 方向 | 用途 |
|---|---|---|
| PA0 | 输出 | VFD DATA |
| PA1 | 输出 | VFD CLK |
| PA2 | 输出 | VFD CS |
| PA3 | 输出 | VFD RESET |
| PA4 | 输出 | VFD EN/高压使能 |
| PB6 | 输出 | GT9147 I2C1 SCL |
| PB7 | 双向 | GT9147 I2C1 SDA |
| PB8 | 输出 | GT9147 RESET |
| PB9 | 输入 | GT9147 INT（启动阶段临时输出） |
| PA9 | 输出 | USART1 TX → H745 PB15/RX |
| PA10 | 输入 | USART1 RX ← H745 PB14/TX |

F103 和 H745 必须共地，两侧 UART 均为 `115200 8N1` 的 3.3 V TTL 电平，不可直接连接 RS-232 电平。

## 串口协议

所有帧使用 ASCII，以 `\n` 结束。

| 方向 | 格式 | 说明 |
|---|---|---|
| F103 → H745 | `T:x,y` | 单指触摸坐标 |
| F103 → H745 | `M:x1,y1,x2,y2` | 双指坐标/捏合手势 |
| H745 → F103 | `I:` | 进入主菜单，VFD 显示跑马灯 |
| H745 → F103 | `F:1000` | VFD 显示发生器频率 |
| H745 → F103 | `A:3.3` | VFD 显示发生器幅度 |

## 构建与刷写

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel
```

产物位于 `build/Debug/`：`stm32f103_hmi.elf`、`.hex` 和 `.bin`。在专用 ST-Link 连接 F103 SWDIO/SWCLK/NRST/GND 后，可执行：

```powershell
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -w build/Debug/stm32f103_hmi.elf -v -rst
```

刷写前请确认当前 ST-Link 连接的是 F103，而不是 NUCLEO-H745ZI-Q 的板载 ST-Link。
