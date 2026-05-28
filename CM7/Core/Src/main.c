/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>  // 提供 sin() 函数支持
#include <stdlib.h> // 提供 abs() 函数支持
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "graph.h"
#include "generate.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* DUAL_CORE_BOOT_SYNC_SEQUENCE: Define for dual core boot synchronization    */
/*                             demonstration code based on hardware semaphore */
/* This define is present in both CM7/CM4 projects                            */
/* To comment when developping/debugging on a single core                     */
// #define DUAL_CORE_BOOT_SYNC_SEQUENCE
//
// #if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
// #ifndef HSEM_ID_0
// #define HSEM_ID_0 (0U) /* HW semaphore 0*/
// #endif
// #endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef hdma_dac1_ch2;

LTDC_HandleTypeDef hltdc;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_LTDC_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_DAC1_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// 💥 封装一个硬件同步中心：把 0~3.3V 转化为 0~2047 写入底层
void Sync_Hardware_WaveGen(void) {
    float hw_amp = (wg_amp / 3.3f) * 2047.0f;
    DAC_Generate_Wave(wg_wave, hw_amp);
    DAC_Set_Frequency((uint32_t)wg_freq);
}

// 双指手势引擎内部变量
static uint16_t last_pinch_dist_x = 0;
static uint16_t last_pinch_dist_y = 0;
static uint8_t is_pinching = 0;

// 极客专属：格式化串口发送函数
void UART_SendToF103(const char *format, ...) {
    char tx_buf[64];
    va_list args;
    va_start(args, format);
    vsprintf(tx_buf, format, args);
    va_end(args);
    HAL_UART_Transmit(&huart1, (uint8_t*)tx_buf, strlen(tx_buf), 100);
}

// 触摸与重绘全局标志
volatile uint8_t global_needs_redraw = 0;
uint8_t h7_rx_byte;
char h7_cmd_buf[32];
uint8_t h7_cmd_idx = 0;

// =========================================================
// 核心：带有“防抖与差分拦截”的触摸碰撞引擎
// =========================================================
void Process_Touch_Interaction(uint16_t x, uint16_t y) {
    is_pinching = 0;
    uint8_t old_state = current_sys_state;
    uint8_t old_sel   = main_menu_sel;
    uint8_t old_wave  = current_wave;
    uint8_t old_ctrl  = current_ctrl;
    uint8_t old_wg_w  = wg_wave;
    uint8_t old_wg_c  = wg_ctrl;

    if (current_sys_state != SYS_MAIN_MENU && x > 700 && y < 60) {
        current_sys_state = SYS_MAIN_MENU;
    }
    else if (current_sys_state == SYS_MAIN_MENU) {
        if (x > 130 && x < 370 && y > 120 && y < 360) {
            main_menu_sel = 0;
            current_sys_state = SYS_OSC;
        }
        else if (x > 430 && x < 670 && y > 120 && y < 360) {
            main_menu_sel = 1;
            current_sys_state = SYS_GEN;
            UART_SendToF103("F:%d\n", (int)wg_freq);
        }
    }
    else if (current_sys_state == SYS_OSC) {
        if (x > 700) {
            if (y > 40 && y < 90)       current_wave = WAVE_SINE;
            else if (y > 90 && y < 140) current_wave = WAVE_SQUARE;
            else if (y > 140 && y < 190)current_wave = WAVE_TRIANGLE;
        }
        else if (x < 100) {
            if (y > 40 && y < 90)       current_ctrl = CTRL_FREQ;
            else if (y > 90 && y < 140) current_ctrl = CTRL_PHASE;
            else if (y > 140 && y < 190)current_ctrl = CTRL_AMPLITUDE;
            else if (y > 190 && y < 240)current_ctrl = CTRL_OFFSET_Y;
        }
    }
    else if (current_sys_state == SYS_GEN) {
        if (x < 100) {
            if (y > 40 && y < 90)       wg_wave = WAVE_SINE;
            else if (y > 90 && y < 140) wg_wave = WAVE_SQUARE;
            else if (y > 140 && y < 190)wg_wave = WAVE_TRIANGLE;

            // 💥 如果波形种类被触摸改变了，立刻同步给底层 DAC
            if (old_wg_w != wg_wave) {
                Sync_Hardware_WaveGen();
            }
        }
        else if (x > 280 && x < 720 && y > 80 && y < 220) {
            wg_ctrl = 0;
        }
        else if (x > 280 && x < 720 && y > 230 && y < 370) {
            wg_ctrl = 1;
        }
    }

    if (old_state != current_sys_state ||
        old_sel   != main_menu_sel   ||
        old_wave  != current_wave    ||
        old_ctrl  != current_ctrl    ||
        old_wg_w  != wg_wave         ||
        old_wg_c  != wg_ctrl) {

        global_needs_redraw = 1;
    }
}


// =========================================================
// 双指操作支持
// =========================================================


void Process_Pinch_Gesture(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    // 只有在示波器模式下才允许缩放波形
    if (current_sys_state != SYS_OSC) return;

    // 计算当前双指在 X 轴和 Y 轴上的绝对距离
    uint16_t dist_x = abs((int)x1 - (int)x2);
    uint16_t dist_y = abs((int)y1 - (int)y2);

    if (!is_pinching) {
        // 第一帧双指按下，只记录初始距离，不触发形变
        last_pinch_dist_x = dist_x;
        last_pinch_dist_y = dist_y;
        is_pinching = 1;
        return;
    }

    // 计算相对于上一帧的位移增量 (这就是虚拟旋转编码器的 diff！)
    int16_t diff_x = (int16_t)dist_x - (int16_t)last_pinch_dist_x;
    int16_t diff_y = (int16_t)dist_y - (int16_t)last_pinch_dist_y;

    // 更新距离历史
    last_pinch_dist_x = dist_x;
    last_pinch_dist_y = dist_y;

    uint8_t state_changed = 0;

    // 💥 轴向手势分离逻辑
    // 判断用户的意图是水平捏合（调时基/频率）还是垂直捏合（调幅值）

    if (abs(diff_x) > abs(diff_y) && abs(diff_x) > 2) {
        // 阈值设为 2 防止手抖抖动
        // 动作：水平双指滑动 -> 调节 current_omega (相当于调节 X 轴缩放)

        // 双指拉开 (diff_x > 0)，频率降低/波形拉宽；捏合则反之。
        // 你可以根据视觉习惯修改前面的正负号
        current_omega -= (float)diff_x * 0.0005f;

        if(current_omega < 0.005f) current_omega = 0.005f;
        if(current_omega > 0.5f) current_omega = 0.5f;
        state_changed = 1;
    }
    else if (abs(diff_y) > abs(diff_x) && abs(diff_y) > 2) {
        // 动作：垂直双指滑动 -> 调节 current_amplitude (相当于调节 Y 轴缩放)

        // 双指拉开 (diff_y > 0)，幅值变大；捏合变小
        current_amplitude += (float)diff_y * 0.5f;

        if(current_amplitude < 1.0f) current_amplitude = 1.0f;
        if(current_amplitude > 480.0f) current_amplitude = 480.0f;
        state_changed = 1;
    }

    if (state_changed) {
        global_needs_redraw = 1;
    }
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_0 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0));
  if ( timeout < 0 )
  {
  Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
/* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
HSEM notification */
/*HW semaphore Clock enable*/
__HAL_RCC_HSEM_CLK_ENABLE();
/*Take HSEM */
HAL_HSEM_FastTake(HSEM_ID_0);
/*Release HSEM in order to notify the CPU2(CM4)*/
HAL_HSEM_Release(HSEM_ID_0,0);
/* wait until CPU2 wakes up from stop mode */
timeout = 0xFFFF;
while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0));
if ( timeout < 0 )
{
Error_Handler();
}
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_LTDC_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  MX_DAC1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

    UART_SendToF103("I:\n");

    // 💥 启动底层波形发生器，并刷入第一次默认参数
    DAC_WaveGen_Init();
    Sync_Hardware_WaveGen();

    // 1. 配置示波器专用调色板 (加入 3 号蓝色用于菜单高亮)
    my_palette[0] = 0xFF000000; // 0号: 纯黑 (背景)
    my_palette[1] = 0xFFFF0000; // 1号: 纯红
    my_palette[2] = 0xFF00FF00; // 2号: 纯绿
    my_palette[3] = 0xFF0000FF; // 3号: 纯蓝 (选中菜单高亮)
    my_palette[4] = 0xFFFFFFFF; // 4号: 纯白 (未选中/坐标轴)
    my_palette[5] = 0xFFFFFF00; // 5号: 纯黄 (波形)
    my_palette[6] = 0xFF404040; // 6号: 暗灰 (网格)

    HAL_LTDC_ConfigCLUT(&hltdc, my_palette, 256, 0);
    HAL_LTDC_EnableCLUT(&hltdc, 0);
    HAL_LTDC_SetAddress(&hltdc, 0x24020000, 0);
    LTDC->SRCR = LTDC_SRCR_IMR;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET); // 点亮屏幕背光

    // 2. 启动编码器硬件解码！(TIM3)
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);

    // 3. 画出第一帧初始画面
    // (注意：这里调用的已经是全面升级后的无参数渲染函数)
    Draw_Oscilloscope_UI();
    Draw_Waveform();

    // 4. 将第一帧画面推入物理显存
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache_by_Addr((uint32_t*)0x24020000, 800 * 480);
    }


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  // 别忘了在进入主循环前，开启串口接收中断，准备接收 F103 传来的触摸和手势数据
  HAL_UART_Receive_IT(&huart1, &h7_rx_byte, 1);

  int16_t last_encoder_cnt = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);

  // 刷出主菜单第一帧
  Play_Cyber_Boot_Sequence();
  Draw_Main_Menu();
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) SCB_CleanDCache_by_Addr((uint32_t*)0x24020000, 800 * 480);

  while (1)
  {
      // =========================================================
      // 进阶版：编码器按键 (PE1, 短按切换焦点，长按重置波形)
      // =========================================================
      static uint8_t encoder_btn_last = 1;
      static uint32_t btn_press_time = 0;      // 记录按下的时刻
      static uint8_t long_press_triggered = 0; // 长按触发标志位

      uint8_t encoder_btn_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_1);

      // 只要处于低电平，就锁定旋钮防误触
      uint8_t is_button_pressing = (encoder_btn_now == GPIO_PIN_RESET);

      // 1. 按下瞬间：记录时间，清除长按标记
      if (encoder_btn_last == 1 && encoder_btn_now == 0) {
          btn_press_time = HAL_GetTick();
          long_press_triggered = 0;
      }

      // 2. 按住过程：检测是否触发长按 (阈值设为 800ms)
      if (encoder_btn_now == 0 && !long_press_triggered) {
          if (HAL_GetTick() - btn_press_time > 800) {
              long_press_triggered = 1; // 标记已触发长按，防止重复触发

              // 💥 执行重置波形参数逻辑！
              if (current_sys_state == SYS_OSC) {
                  current_omega = 0.05f;
                  current_phase = 0.0f;
                  current_amplitude = 100.0f;
                  current_offset_y = 240;
                  global_needs_redraw = 1;
              }
              else if (current_sys_state == SYS_GEN) {
                  wg_freq = 1000.0f;
                  wg_amp = 3.3f;
                  UART_SendToF103("F:%d\n", (int)wg_freq); // 同步数码管
                  UART_SendToF103("A:3.3\n");

                  Sync_Hardware_WaveGen(); // 💥 长按复位，同步底层 DAC！

                  global_needs_redraw = 1;
              }
          }
      }

      // 3. 松开瞬间：如果没有触发长按，且经过了 20ms 的消抖，则视为【短按】
      if (encoder_btn_last == 0 && encoder_btn_now == 1) {
          if (!long_press_triggered && (HAL_GetTick() - btn_press_time > 20)) {

              // 💥 执行短按切换焦点逻辑
              if (current_sys_state == SYS_OSC) {
                  UART_SendToF103("I:\n"); // 打断 F103 跑马灯
                  if (current_ctrl < 3) current_ctrl++; else current_ctrl = 0;
                  global_needs_redraw = 1;
              }
              else if (current_sys_state == SYS_GEN) {
                  wg_ctrl = !wg_ctrl; // 频率和幅值焦点切换
                  global_needs_redraw = 1;
              }
          }
      }

      encoder_btn_last = encoder_btn_now;

      // =========================================================
      // 唯一保留的物理输入：旋转编码器 (专职精细调参)
      // =========================================================
      int16_t current_cnt = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
      if (current_cnt != last_encoder_cnt) {
          int16_t diff = current_cnt - last_encoder_cnt;
          last_encoder_cnt = current_cnt;

          // 💥 只有在按键没被按下时，才允许响应旋转！(硬件与软件双重防误触)
          if (!is_button_pressing) {
              if (current_sys_state == SYS_OSC) {
                  UART_SendToF103("I:\n"); // 打断 F103 跑马灯
                  switch (current_ctrl) {
                      case CTRL_FREQ: current_omega += (float)diff * 0.002f; if(current_omega < 0.005f) current_omega = 0.005f; if(current_omega > 0.5f) current_omega = 0.5f; break;
                      case CTRL_PHASE: current_phase += (float)diff * 0.2f; break;
                      case CTRL_AMPLITUDE: current_amplitude += (float)diff * 5.0f; if(current_amplitude < 1.0f) current_amplitude = 1.0f; if(current_amplitude > 480.0f) current_amplitude = 480.0f; break;
                      case CTRL_OFFSET_Y: current_offset_y -= diff * 5; if(current_offset_y < 20) current_offset_y = 20; if(current_offset_y > 460) current_offset_y = 460; break;
                  }
                  global_needs_redraw = 1;
              }
              else if (current_sys_state == SYS_GEN) {
                  if (wg_ctrl == 0) {
                      float step;

                      // 💥 核心逻辑：根据当前频率所在的区间，动态分配旋钮步长
                      if (wg_freq >= 10000.0f) {
                          step = 500.0f; // 大于等于 10k 时，步进 1kHz
                      } else if (wg_freq >= 1000.0f) {
                          step = 50.0f;  // 大于等于 1k 且小于 10k 时，步进 0.1kHz (100Hz)
                      } else {
                          step = 5.0f;    // 小于 1k 时，精细步进 5Hz
                      }

                      // 应用动态步长 (注意这里保留了你原来的 -= 逻辑，如果旋钮方向反了改成 += 即可)
                      wg_freq -= diff * step;

                      // 硬件物理极限防呆保护
                      if(wg_freq < 50.0f) wg_freq = 50.0f;
                      if(wg_freq > 100000.0f) wg_freq = 100000.0f;

                      UART_SendToF103("F:%d\n", (int)wg_freq); // 同步给 F103 VFD 屏幕
                  } else {
                      wg_amp -= diff * 0.05f;
                      if(wg_amp < 0.0f) wg_amp = 0.0f;
                      if(wg_amp > 3.3f) wg_amp = 3.3f;
                      int a_int = (int)wg_amp;
                      int a_frac = (int)((wg_amp - a_int) * 10);
                      UART_SendToF103("A:%d.%d\n", a_int, a_frac); // 同步给 F103
                  }

                  Sync_Hardware_WaveGen(); // 💥 旋钮调参完成，即时同步底层 DAC！

                  global_needs_redraw = 1;
              }
          }
      }

      // =========================================================
      // 统一渲染调度 (被触摸、手势或编码器触发)
      // =========================================================
      if (global_needs_redraw) {
          global_needs_redraw = 0; // 清除标志

          if (current_sys_state == SYS_MAIN_MENU) {
              UART_SendToF103("I:\n"); // 主菜单默认让 F103 亮起 UESTC 跑马灯
              Draw_Main_Menu();
          }
          else if (current_sys_state == SYS_OSC) {
              Draw_Oscilloscope_UI();
              Draw_Waveform();
          }
          else if (current_sys_state == SYS_GEN) {
              Draw_WaveGen_UI();
          }

          // 刷入显存 (M7 必备的 D-Cache 清理操作)
          if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
              SCB_CleanDCache_by_Addr((uint32_t*)0x24020000, 800 * 480);
          }
      }

      HAL_Delay(10); // 彻底释放 CPU 资源，留给中断处理触摸和串口
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 60;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_DISABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 39;
  hltdc.Init.VerticalSync = 0;
  hltdc.Init.AccumulatedHBP = 79;
  hltdc.Init.AccumulatedVBP = 32;
  hltdc.Init.AccumulatedActiveW = 879;
  hltdc.Init.AccumulatedActiveH = 512;
  hltdc.Init.TotalWidth = 927;
  hltdc.Init.TotalHeigh = 525;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 800;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 480;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_L8;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = 0x24020000;
  pLayerCfg.ImageWidth = 800;
  pLayerCfg.ImageHeight = 480;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 10;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 10;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 100;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PC4 PC5 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PE1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        if (h7_rx_byte == '\n') {
            h7_cmd_buf[h7_cmd_idx] = '\0';

            // 👆 解析来自 F103 的单指触摸数据 (T:X,Y)
            if (h7_cmd_buf[0] == 'T' && h7_cmd_buf[1] == ':') {
                int tx = 0, ty = 0;
                if (sscanf(h7_cmd_buf, "T:%d,%d", &tx, &ty) == 2) {
                    Process_Touch_Interaction((uint16_t)tx, (uint16_t)ty);
                }
            }
            // ✌️ 💥新增：解析来自 F103 的双指捏合数据 (M:X1,Y1,X2,Y2)
            else if (h7_cmd_buf[0] == 'M' && h7_cmd_buf[1] == ':') {
                int x1, y1, x2, y2;
                if (sscanf(h7_cmd_buf, "M:%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) {
                    // 将提取出的 4 个坐标喂给双指缩放引擎
                    Process_Pinch_Gesture((uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2);
                }
            }

            h7_cmd_idx = 0; // 解析完清零缓冲索引
        } else {
            if (h7_cmd_idx < 30) h7_cmd_buf[h7_cmd_idx++] = h7_rx_byte;
        }

        // 重新开启中断接收下一个字节
        HAL_UART_Receive_IT(&huart1, &h7_rx_byte, 1);
    }
}


/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
