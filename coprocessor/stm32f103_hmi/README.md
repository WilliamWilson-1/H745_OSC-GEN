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

当前固件按实际连接到 ST-Link 的 64 KiB STM32F103 目标配置为板载 8 MHz 晶振 + `RCC_HSE_ON`。若以后改用由 ST-Link MCO 提供时钟的 NUCLEO-F103RB，需要按板卡版本与焊桥状态切换为 `RCC_HSE_BYPASS`。

## 串口协议

所有帧使用 ASCII，以 `\n` 结束。

| 方向 | 格式 | 说明 |
|---|---|---|
| F103 → H745 | `T:x,y` | 单指新接触的按下坐标，一次接触仅发送一次 |
| F103 → H745 | `D:x,y` | 单指接触后续坐标，用于右侧输出拨钮拖动，不重复触发普通按钮 |
| F103 → H745 | `M:x1,y1,x2,y2` | 双指坐标/捏合手势 |
| F103 → H745 | `U:` | 所有触点已释放；结束本次点击/捏合 |
| F103 → H745 | `C:` | 取消单指操作：出现多指或 I2C 读取错误；不得提交输出切换 |
| H745 → F103 | `I:` | 进入主菜单，VFD 显示跑马灯 |
| H745 → F103 | `F:1000` | VFD 显示发生器频率 |
| H745 → F103 | `A:3.3` | VFD 显示发生器幅度 |

一次单指接触首先发送 `T:`，后续有效坐标报告发送 `D:`，全部松开后发送 `U:`。这样 RUN/HOLD、RISE/FALL、SINGLE 等普通按钮不会在按住期间被重复触发，发生器开关则可持续跟踪拖动，并在释放时提交。

出现多指时先发 `C:`，再持续发送 `M:`；即使双指坐标读取失败，也已取消待提交的拨钮操作。多指退回单指不重新发送 `T:`，必须全部松开后才能开始新单指操作。I2C 读取错误同样取消本次接触。H745 在串口错误、溢出或无效命令时取消待提交的拨动。协议没有 CRC/应答，不应当作安全联锁。

拖动功能必须同时更新 H745 与本 F103 固件；仅更新 H745 无法从旧 F103 获得移动坐标。本机旧 `screen` 工程副本已同步触摸修改；版本管理以本仓库子目录为准。

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
