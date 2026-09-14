#ifndef UI_PREVIEW_HAL_H
#define UI_PREVIEW_HAL_H
#include <stdint.h>
typedef struct { unsigned unused; } ADC_HandleTypeDef;
typedef struct { unsigned unused; } DMA_HandleTypeDef;
typedef struct { unsigned unused; } TIM_HandleTypeDef;
typedef int HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
#endif
