#ifndef __GENERATE_H
#define __GENERATE_H

#include "graph.h"
#include "main.h"

#define WAVE_POINTS 128

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;

extern uint16_t wave_table[WAVE_POINTS];

void DAC_WaveGen_Init(void);
void DAC_Generate_Wave(WaveType type, float dac_amplitude);
void DAC_Set_Frequency(uint32_t freq);

#endif /* __GENERATE_H */