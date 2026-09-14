#ifndef __GENERATE_H
#define __GENERATE_H

#include "graph.h"
#include "main.h"

#define WAVE_POINTS 128
#define WAVE_MAX_HZ 100000U
#define DAC_MAX_SAMPLE_HZ 1000000U

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;

extern uint16_t wave_table[WAVE_POINTS];

void DAC_WaveGen_Init(void);
void DAC_WaveGen_SetEnabled(uint8_t enabled);
uint8_t DAC_WaveGen_IsEnabled(void);
void DAC_WaveGen_Configure(WaveType type, float dac_amplitude, uint32_t freq);

#endif /* __GENERATE_H */
