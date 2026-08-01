/* USER CODE BEGIN Header */
/**
  * @author         : William's HMI Coprocessor Core (VFD Edition)
  * @brief          : VFD Display & GT9147 Touch Controller
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// =========================================================
// VFD 硬件引脚映射 (增加 EN 升压使能引脚)
// =========================================================
#define VFD_PROT 		GPIOA
#define	VFD_DATA_PIN	GPIO_PIN_0
#define VFD_CLK_PIN		GPIO_PIN_1
#define VFD_CS_PIN		GPIO_PIN_2
#define VFD_RST_PIN		GPIO_PIN_3
#define VFD_EN_PIN      GPIO_PIN_4  // 💥 新增：模块上的 EN 引脚 (对应原代码的 HON)

#define SET_CS 		HAL_GPIO_WritePin(VFD_PROT, VFD_CS_PIN, GPIO_PIN_SET)
#define CLR_CS		HAL_GPIO_WritePin(VFD_PROT, VFD_CS_PIN, GPIO_PIN_RESET)
#define SET_CLK 	HAL_GPIO_WritePin(VFD_PROT, VFD_CLK_PIN, GPIO_PIN_SET)
#define CLR_CLK		HAL_GPIO_WritePin(VFD_PROT, VFD_CLK_PIN, GPIO_PIN_RESET)
#define SET_DATA 	HAL_GPIO_WritePin(VFD_PROT, VFD_DATA_PIN, GPIO_PIN_SET)
#define CLR_DATA	HAL_GPIO_WritePin(VFD_PROT, VFD_DATA_PIN, GPIO_PIN_RESET)
#define SET_RST 	HAL_GPIO_WritePin(VFD_PROT, VFD_RST_PIN, GPIO_PIN_SET)
#define CLR_RST		HAL_GPIO_WritePin(VFD_PROT, VFD_RST_PIN, GPIO_PIN_RESET)
#define SET_EN      HAL_GPIO_WritePin(VFD_PROT, VFD_EN_PIN, GPIO_PIN_SET)   // 💥 开启高压
#define CLR_EN      HAL_GPIO_WritePin(VFD_PROT, VFD_EN_PIN, GPIO_PIN_RESET) // 💥 关闭高压

// VFD 命令集
#define Write_DCRAM_CMD     0x20
#define Write_CGRAM_CMD     0x40
#define Set_Timing_CMD      0xE0
#define Set_Dimming_CMD     0xE4
#define Light_Normal_CMD    0xE8
#define Brightness          230 // 亮度 0~240

// 💥 修复1：加入 __IO (volatile) 防止被编译器过度优化吃掉延时！
void delay_us(uint32_t us) {
    __IO uint32_t delay = (SystemCoreClock / 1000000) * us / 4;
    while (delay--) { __NOP(); }
}

// =========================================================
// VFD 底层驱动
// =========================================================
void vfd_send_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        CLR_CLK;
        if (data & 0x01) SET_DATA; else CLR_DATA;
        data >>= 1;
        delay_us(2); // 稍微放宽一点时序，保证波形完美
        SET_CLK;
        delay_us(2);
    }
}

void vfd_display_char(uint8_t position, uint8_t data) {
    CLR_CS;
    vfd_send_byte(Write_DCRAM_CMD | position);
    vfd_send_byte(data);
    SET_CS;
}

// 💥 修复2：带空格补齐的字符串输出 (防止残留残影)
void vfd_display_string(const char* str) {
    for(int i = 0; i < 8; i++) {
        if(str[i] == '\0') {
            // 如果字符串长度不够 8 位，用空格填满剩下的屏幕！
            while(i < 8) { vfd_display_char(i, ' '); i++; }
            break;
        } else {
            vfd_display_char(i, (uint8_t)str[i]);
        }
    }
}

// =========================================================
// 自定义字模：底部绝对对齐的 k (0x01) 和 z (0x02)
// 取模规则：5列，从左到右，低位在上(Bit0=Row1)，高位在下(Bit6=Row7)
// =========================================================
const uint8_t custom_k[5] = {0x7F, 0x20, 0x10, 0x28, 0x44}; // k
const uint8_t custom_z[5] = {0x44, 0x64, 0x54, 0x4C, 0x44}; // z
const uint8_t custom_ht[5] = {0x18, 0x3C, 0x78, 0x3C, 0x18}; // heart
const uint8_t custom_dian[5] = {0x3E, 0x2A, 0x7F, 0x6A, 0x7E}; // 电
const uint8_t custom_wo[5] = {0x25, 0x7F, 0x14, 0x7E, 0x55}; // 我

void vfd_load_custom_chars(void) {
    // 注入自定义 k 到地址 0x01
    CLR_CS;
    vfd_send_byte(Write_CGRAM_CMD | 0x01);
    for(int i=0; i<5; i++) vfd_send_byte(custom_k[i]);
    SET_CS;
    delay_us(10);

    // 注入自定义 z 到地址 0x02
    CLR_CS;
    vfd_send_byte(Write_CGRAM_CMD | 0x02);
    for(int i=0; i<5; i++) vfd_send_byte(custom_z[i]);
    SET_CS;

    CLR_CS;
    vfd_send_byte(Write_CGRAM_CMD | 0x03);
    for(int i=0; i<5; i++) vfd_send_byte(custom_ht[i]);
    SET_CS;

    CLR_CS;
    vfd_send_byte(Write_CGRAM_CMD | 0x04);
    for(int i=0; i<5; i++) vfd_send_byte(custom_dian[i]);
    SET_CS;

    CLR_CS;
    vfd_send_byte(Write_CGRAM_CMD | 0x05);
    for(int i=0; i<5; i++) vfd_send_byte(custom_wo[i]);
    SET_CS;
}

void vfd_init(void) {
	SET_EN;
	HAL_Delay(50); // 给高压电容一点充电时间，让电压稳定

    SET_CS;
    HAL_Delay(100);
    CLR_RST;
    HAL_Delay(5);
    SET_RST;

    CLR_CS;
    vfd_send_byte(Set_Timing_CMD);
    HAL_Delay(1); // 严格对标原厂的毫秒级间隔
    vfd_send_byte(0x07);
    SET_CS;
    HAL_Delay(1);

    CLR_CS;
    vfd_send_byte(Set_Dimming_CMD);
    HAL_Delay(1);
    vfd_send_byte(Brightness);
    SET_CS;
    HAL_Delay(1);

    CLR_CS;
    vfd_send_byte(Light_Normal_CMD);
    SET_CS;

    HAL_Delay(1);
        // 💥 挂载自定义底齐字模
    vfd_load_custom_chars();
}

// =========================================================
// H7 交互与动画引擎
// =========================================================
uint8_t anim_mode = 0;     // 💥 修改：开机默认不进跑马灯，方便看开机画面
const char marquee_str[] = "        UNIVERSITY OF ELECTRONIC SCIENCE AND TECHNOLOGY OF CHINA (UESTC)        ";
uint16_t marquee_len = sizeof(marquee_str) - 1;

uint8_t rx_byte;
char rx_str[32];
uint8_t rx_idx = 0;

void Parse_Command(char* cmd) {
    char vfd_buf[16] = {0};

    if (cmd[0] == 'I' && cmd[1] == ':') {
        anim_mode = 1; // 触发跑马灯
    }
    else if (cmd[0] == 'F' && cmd[1] == ':') {
            anim_mode = 0;
            int val = atoi(&cmd[2]);

            if (val >= 1000) {
                int total_tenths = (val + 50) / 100;
                int k_int = total_tenths / 10;
                int k_frac = total_tenths % 10;
                // 💥 使用 \x01(k), H, \x02(z) 组合
                snprintf(vfd_buf, sizeof(vfd_buf), "%3d.%1d\x01H\x02", k_int, k_frac);
            } else {
                // 💥 使用 H, \x02(z) 组合
                snprintf(vfd_buf, sizeof(vfd_buf), "%5d H\x02 ", val);
            }

            vfd_display_string(vfd_buf);
        }
    else if (cmd[0] == 'A' && cmd[1] == ':') {
        anim_mode = 0;
        snprintf(vfd_buf, sizeof(vfd_buf), "   %.8s V", &cmd[2]);
        vfd_display_string(vfd_buf);
    }
    memset(rx_str, 0, sizeof(rx_str));
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

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  // 1. 初始化 VFD 屏幕
  /* USER CODE BEGIN 2 */
    // 1. 初始化 VFD 屏幕
    vfd_init();
    vfd_display_string("  \x04\x03\x05");
    HAL_Delay(1000);
    vfd_display_string("STARTING");
    HAL_Delay(500);

  // 2. 暴力唤醒 GT9147 (复用之前的逻辑)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_Delay(10);
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_Delay(50);

  // 3. 开启串口接收中断
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_marquee_tick = HAL_GetTick();
  uint16_t marquee_offset = 0;

  while (1)
  {
      // ==========================================
      // 1. 跑马灯动画驱动 (非阻塞)
      // ==========================================
      if (anim_mode == 1) {
          if (HAL_GetTick() - last_marquee_tick > 250) { // 250ms 滚一格
              last_marquee_tick = HAL_GetTick();
              char view[9] = {0};
              strncpy(view, &marquee_str[marquee_offset], 8);
              vfd_display_string(view);

              marquee_offset++;
              if (marquee_offset > marquee_len - 8) {
                  marquee_offset = 0;
              }
          }
      }

      // ==========================================
      // 2. 触摸屏 GT9147 处理逻辑 (完全保留)
      // ==========================================
      uint8_t status = 0;
      HAL_StatusTypeDef res = HAL_I2C_Mem_Read(&hi2c1, 0x28, 0x814E, I2C_MEMADD_SIZE_16BIT, &status, 1, 100);

      if (res != HAL_OK) {
          if (!anim_mode) vfd_display_string(" ERR-I2C");
      }
      else {
          if (status & 0x80) {
              uint8_t touch_count = status & 0x0F;
              if (touch_count == 1) {
                  uint8_t coord_buf[4] = {0};
                  HAL_I2C_Mem_Read(&hi2c1, 0x28, 0x8150, I2C_MEMADD_SIZE_16BIT, coord_buf, 4, 100);
                  uint16_t touch_x = ((uint16_t)coord_buf[1] << 8) | coord_buf[0];
                  uint16_t touch_y = ((uint16_t)coord_buf[3] << 8) | coord_buf[2];
                  char t_buf[32];
                  sprintf(t_buf, "T:%d,%d\n", touch_x, touch_y);
                  HAL_UART_Transmit(&huart1, (uint8_t*)t_buf, strlen(t_buf), 10);
              }
              else if (touch_count >= 2) {
                  uint8_t multi_buf[12] = {0};
                  HAL_I2C_Mem_Read(&hi2c1, 0x28, 0x8150, I2C_MEMADD_SIZE_16BIT, multi_buf, 12, 100);
                  uint16_t x1 = ((uint16_t)multi_buf[1] << 8) | multi_buf[0];
                  uint16_t y1 = ((uint16_t)multi_buf[3] << 8) | multi_buf[2];
                  uint16_t x2 = ((uint16_t)multi_buf[9] << 8) | multi_buf[8];
                  uint16_t y2 = ((uint16_t)multi_buf[11] << 8) | multi_buf[10];
                  char m_buf[64];
                  sprintf(m_buf, "M:%d,%d,%d,%d\n", x1, y1, x2, y2);
                  HAL_UART_Transmit(&huart1, (uint8_t*)m_buf, strlen(m_buf), 10);
              }
              uint8_t clear_cmd = 0x00;
              HAL_I2C_Mem_Write(&hi2c1, 0x28, 0x814E, I2C_MEMADD_SIZE_16BIT, &clear_cmd, 1, 100);
          }
      }
      HAL_Delay(10);
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

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
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        if (rx_byte == '\n') {
            rx_str[rx_idx] = '\0';
            Parse_Command(rx_str);
            rx_idx = 0;
        } else {
            if (rx_idx < 30) rx_str[rx_idx++] = rx_byte;
        }
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}
/* USER CODE END 4 */

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
