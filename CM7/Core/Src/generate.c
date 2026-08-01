#include "generate.h"
#include "main.h"
#include <math.h>

/* Keep the DAC lookup table in DMA1-accessible D2 SRAM. */
__attribute__((section(".dma_buffer"), aligned(32)))
uint16_t wave_table[WAVE_POINTS];

void DAC_WaveGen_Init(void) {
    // 启动 DMA 传输，对准通道 2 (PA5)
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t*)wave_table,
                          WAVE_POINTS, DAC_ALIGN_12B_R) != HAL_OK) {
        Error_Handler();
    }
    /* Circular playback needs no half/full-transfer callbacks. Disabling
     * them avoids one DMA interrupt per generated waveform period. */
    __HAL_DMA_DISABLE_IT(hdac1.DMA_Handle2, DMA_IT_HT | DMA_IT_TC);
    // 启动触发用的 TIM6
    if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
        Error_Handler();
    }
}

void DAC_Generate_Wave(WaveType type, float dac_amplitude) {
    // 防呆：防止幅值越界导致 DAC 溢出
    if (dac_amplitude > 2047.0f) dac_amplitude = 2047.0f;
    if (dac_amplitude < 0.0f) dac_amplitude = 0.0f;

    for(int i = 0; i < WAVE_POINTS; i++) {
        switch(type) {
            case WAVE_SINE: {
                float value = sinf(2 * 3.1415926f * i / WAVE_POINTS);
                wave_table[i] = (uint16_t)(value * dac_amplitude + 2048);
                break;
            }
            case WAVE_SQUARE: {
                if(i < WAVE_POINTS/2)
                    wave_table[i] = 2048 + (uint16_t)dac_amplitude;
                else
                    wave_table[i] = 2048 - (uint16_t)dac_amplitude;
                break;
            }
            case WAVE_TRIANGLE: {
                if(i < WAVE_POINTS/2) {
                    wave_table[i] = (uint16_t)(2048 - dac_amplitude + (2 * dac_amplitude * i)/(WAVE_POINTS/2));
                } else {
                    wave_table[i] = (uint16_t)(2048 + dac_amplitude - (2 * dac_amplitude * (i - WAVE_POINTS/2))/(WAVE_POINTS/2));
                }
                break;
            }
        }
    }

    // 💥 3. 核心救命代码：强制将 CPU 高速缓存中的数据刷入 0x30000000 的物理内存中！
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache_by_Addr((uint32_t*)wave_table, WAVE_POINTS * sizeof(uint16_t));
    }
}

void DAC_Set_Frequency(uint32_t freq) {
    if (freq == 0) freq = 1;

    // 假设 APB1 定时器时钟为 240MHz (请以你的 CubeMX 真实时钟树为准)
    uint32_t timer_clock = 240000000;

    // 目标：每秒需要触发的总次数
    uint32_t target_trigger_rate = freq * WAVE_POINTS;

    // 计算总的分频系数
    uint32_t total_div = timer_clock / target_trigger_rate;

    uint32_t psc = 0;
    uint32_t arr = total_div - 1;

    // 如果 ARR 超出了 16 位极限，增加预分频器 PSC
    while (arr > 65535) {
        psc++;
        arr = (total_div / (psc + 1)) - 1;
    }

    __HAL_TIM_SET_PRESCALER(&htim6, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim6, arr);
}
