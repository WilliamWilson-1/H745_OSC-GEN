#ifndef INSTRUMENT_HOST_HAL_H
#define INSTRUMENT_HOST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef int HAL_StatusTypeDef;
enum { HAL_OK, HAL_ERROR };
typedef struct { uint32_t EGR, PSC, ARR, CNT, running; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef struct { void *Instance; } ADC_HandleTypeDef;
typedef struct { unsigned unused; } DMA_HandleTypeDef;
typedef struct { DMA_HandleTypeDef *DMA_Handle2; } DAC_HandleTypeDef;
typedef struct { uint32_t D2CFGR; } RCC_TypeDef;
typedef struct { uint32_t CCR; } SCB_TypeDef;
extern RCC_TypeDef fake_rcc;
extern SCB_TypeDef fake_scb;
extern uint32_t fake_pclk, dac_dma_length, dac_starts, dac_stops;
extern uint8_t dac_dma_active;
#define RCC (&fake_rcc)
#define SCB (&fake_scb)
#define RCC_D2CFGR_D2PPRE1 7U
#define RCC_APB1_DIV1 0U
#define SCB_CCR_DC_Msk 1U
#define ADC1 ((void *)(uintptr_t)1U)
#define ADC_CALIB_OFFSET 0
#define ADC_SINGLE_ENDED 0
#define TIM_EGR_UG 1U
#define DAC_CHANNEL_2 2U
#define DAC_ALIGN_12B_R 0U
#define DMA_IT_HT 1U
#define DMA_IT_TC 2U
#define __HAL_TIM_DISABLE(h) ((h)->Instance->running=0U)
#define __HAL_TIM_SET_PRESCALER(h,v) ((h)->Instance->PSC=(v))
#define __HAL_TIM_SET_AUTORELOAD(h,v) ((h)->Instance->ARR=(v))
#define __HAL_TIM_SET_COUNTER(h,v) ((h)->Instance->CNT=(v))
#define __HAL_DMA_DISABLE_IT(h,v) ((void)(h),(void)(v))
void Error_Handler(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h,uint32_t *p,uint32_t n);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h,uint32_t a,uint32_t b);
HAL_StatusTypeDef HAL_DAC_Start_DMA(DAC_HandleTypeDef *h,uint32_t c,uint32_t *p,uint32_t n,uint32_t a);
HAL_StatusTypeDef HAL_DAC_Stop_DMA(DAC_HandleTypeDef *h,uint32_t c);
HAL_StatusTypeDef HAL_DAC_Start(DAC_HandleTypeDef *h,uint32_t c);
HAL_StatusTypeDef HAL_DAC_Stop(DAC_HandleTypeDef *h,uint32_t c);
HAL_StatusTypeDef HAL_DAC_SetValue(DAC_HandleTypeDef *h,uint32_t c,uint32_t a,uint32_t v);
void SCB_CleanDCache_by_Addr(uint32_t *p,int32_t n);
void SCB_InvalidateDCache_by_Addr(uint32_t *p,int32_t n);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *p,int32_t n);
#endif
