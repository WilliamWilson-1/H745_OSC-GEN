#include "generate.h"
#include "main.h"
#include <math.h>

/* Keep the DAC lookup table in DMA1-accessible D2 SRAM. */
__attribute__((section(".dma_buffer"), aligned(32)))
uint16_t wave_table[WAVE_POINTS];

static uint8_t wavegen_running = 0U;
static uint16_t wave_points = WAVE_POINTS;

void DAC_WaveGen_Init(void) {
    /* Power-up is intentionally quiet. The UI OUTPUT button starts DMA. */
    DAC_WaveGen_SetEnabled(0U);
}

void DAC_WaveGen_SetEnabled(uint8_t enabled) {
    if (enabled != 0U) {
        if (wavegen_running != 0U) return;

        (void)HAL_TIM_Base_Stop(&htim6);
        (void)HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);
        if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t*)wave_table,
                              wave_points, DAC_ALIGN_12B_R) != HAL_OK) {
            Error_Handler();
        }
        /* Circular playback needs no half/full-transfer callbacks. */
        __HAL_DMA_DISABLE_IT(hdac1.DMA_Handle2, DMA_IT_HT | DMA_IT_TC);
        if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
            (void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
            Error_Handler();
        }
        wavegen_running = 1U;
    } else {
        (void)HAL_TIM_Base_Stop(&htim6);
        if (wavegen_running != 0U) {
            (void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
        } else {
            (void)HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);
        }
        /* Keep PA5 at a defined 0 V code instead of holding an arbitrary
         * last DMA sample when output is disabled. */
        (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, 0U);
        if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK) {
            Error_Handler();
        }
        wavegen_running = 0U;
    }
}

uint8_t DAC_WaveGen_IsEnabled(void) {
    return wavegen_running;
}

static void generate_wave(WaveType type, float dac_amplitude) {
    // 防呆：防止幅值越界导致 DAC 溢出
    if (dac_amplitude > 2047.0f) dac_amplitude = 2047.0f;
    if (dac_amplitude < 0.0f) dac_amplitude = 0.0f;

    for(int i = 0; i < wave_points; i++) {
        switch(type) {
            case WAVE_SINE: {
                /* Even lengths 2 mod 4 use a half-sample phase offset so
                 * both peaks remain represented (including 10 points at 100 kHz). */
                float phase = (wave_points % 4U == 2U) ? 0.5f : 0.0f;
                float value = sinf(2 * 3.1415926f * (i + phase) / wave_points);
                wave_table[i] = (uint16_t)(value * dac_amplitude + 2048);
                break;
            }
            case WAVE_SQUARE: {
                if(i < wave_points/2)
                    wave_table[i] = 2048 + (uint16_t)dac_amplitude;
                else
                    wave_table[i] = 2048 - (uint16_t)dac_amplitude;
                break;
            }
            case WAVE_TRIANGLE: {
                if(i < wave_points/2) {
                    wave_table[i] = (uint16_t)(2048 - dac_amplitude + (2 * dac_amplitude * i)/(wave_points/2));
                } else {
                    wave_table[i] = (uint16_t)(2048 + dac_amplitude - (2 * dac_amplitude * (i - wave_points/2))/(wave_points/2));
                }
                break;
            }
        }
    }

    // Make the D2 SRAM lookup table visible to DMA when D-cache is enabled.
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache_by_Addr((uint32_t*)wave_table, WAVE_POINTS * sizeof(uint16_t));
    }
}

void DAC_WaveGen_Configure(WaveType type, float dac_amplitude, uint32_t freq) {
    if (freq < 50U) freq = 50U;
    if (freq > WAVE_MAX_HZ) freq = WAVE_MAX_HZ;
    if (type > WAVE_TRIANGLE) type = WAVE_SINE;
    const uint8_t resume = wavegen_running;
    /* Never rewrite a table while circular DMA is still reading it. */
    if (resume) DAC_WaveGen_SetEnabled(0U);
    else (void)HAL_TIM_Base_Stop(&htim6);

    uint32_t points = DAC_MAX_SAMPLE_HZ / freq;
    if (points > WAVE_POINTS) points = WAVE_POINTS;
    points &= ~1U; /* Equal square-wave halves and triangle symmetry. */
    wave_points = (uint16_t)points;
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != RCC_APB1_DIV1) timer_clock *= 2U;
    uint32_t rate = freq * points;
    /* Round the divisor UP: neither the requested frequency nor DAC's
     * 1 MHz update ceiling may be exceeded by timer quantization. */
    uint32_t total_div = (timer_clock + rate - 1U) / rate;
    uint32_t psc = (total_div - 1U) / 65536U;
    uint32_t period = (total_div + psc) / (psc + 1U);
    __HAL_TIM_SET_PRESCALER(&htim6, psc);
    __HAL_TIM_SET_AUTORELOAD(&htim6, period - 1U);
    __HAL_TIM_SET_COUNTER(&htim6, 0U);
    htim6.Instance->EGR = TIM_EGR_UG;
    generate_wave(type, dac_amplitude);
    if (resume) DAC_WaveGen_SetEnabled(1U);
}
