#ifndef DISPLAY_HOST_HAL_H
#define DISPLAY_HOST_HAL_H
#include "host_hal.h"
#define HAL_OK 0
#define LTDC_PIXEL_FORMAT_L8 5
#define LTDC_BLENDING_FACTOR1_CA 4
#define LTDC_BLENDING_FACTOR2_CA 5
#define LTDC_FLAG_RR 8U
#define LTDC_FLAG_FU 2U
#define LTDC_FLAG_TE 4U
#define LTDC_FLAG_LI 1U
#define LTDC_CPSR_CYPOS 65535U
#define HAL_LTDC_ERROR_FU 1U
#define HAL_LTDC_ERROR_TE 2U
#define SCB_CCR_DC_Msk (1U<<16)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define MDMA_Channel0 0U
#define MDMA_REQUEST_SW 1U
#define MDMA_FULL_TRANSFER 2U
#define MDMA_PRIORITY_VERY_HIGH 3U
#define MDMA_LITTLE_ENDIANNESS_PRESERVE 0U
#define MDMA_SRC_INC_WORD 2U
#define MDMA_DEST_INC_WORD 2U
#define MDMA_SRC_DATASIZE_WORD 2U
#define MDMA_DEST_DATASIZE_WORD 2U
#define MDMA_DATAALIGN_PACKENABLE 1U
#define MDMA_SOURCE_BURST_16BEATS 16U
#define MDMA_DEST_BURST_16BEATS 16U
#define RCC_PERIPHCLK_LTDC 1U
#define MDMA_IRQn 1U
#define LTDC_IRQn 2U
typedef struct { uint32_t ISR,CPSR; } FakeLTDC;
typedef struct { uint32_t AccumulatedVBP,AccumulatedActiveH,TotalHeigh,TotalWidth; } FakeTiming;
typedef struct { FakeLTDC *Instance; FakeTiming Init; uint32_t ErrorCode; } LTDC_HandleTypeDef;
typedef struct {
    uint32_t WindowX0,WindowX1,WindowY0,WindowY1,PixelFormat,Alpha,Alpha0;
    uint32_t BlendingFactor1,BlendingFactor2,FBStartAdress,ImageWidth,ImageHeight;
} LTDC_LayerCfgTypeDef;
typedef struct { uint32_t CCR; } FakeSCB;
typedef struct { uint32_t CYCCNT,CTRL; } FakeDWT;
typedef struct { uint32_t DEMCR; } FakeCoreDebug;
extern FakeSCB fake_scb;
extern FakeDWT fake_dwt;
extern FakeCoreDebug fake_debug;
extern uint32_t SystemCoreClock;
#define SCB (&fake_scb)
#define DWT (&fake_dwt)
#define CoreDebug (&fake_debug)
typedef struct {
    uint32_t Request,TransferTriggerMode,Priority,Endianness,SourceInc,DestinationInc;
    uint32_t SourceDataSize,DestDataSize,DataAlignment,BufferTransferLength,SourceBurst,DestBurst;
} FakeMDMAInit;
typedef struct MDMA_HandleTypeDef {
    uint32_t Instance;
    FakeMDMAInit Init;
    void (*XferCpltCallback)(struct MDMA_HandleTypeDef *);
    void (*XferErrorCallback)(struct MDMA_HandleTypeDef *);
} MDMA_HandleTypeDef;
#define __DSB() ((void)0)
#define __DMB() ((void)0)
#define __HAL_RCC_D2SRAM1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_D2SRAM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_D2SRAM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_MDMA_CLK_ENABLE() ((void)0)
#define __HAL_LTDC_CLEAR_FLAG(h,mask) ((h)->Instance->ISR &= ~(mask))
void SCB_CleanDCache_by_Addr(uint32_t *address,int32_t size);
void Error_Handler(void);
HAL_StatusTypeDef HAL_LTDC_ConfigLayer(LTDC_HandleTypeDef *,LTDC_LayerCfgTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_LTDC_ConfigCLUT(LTDC_HandleTypeDef *,uint32_t *,uint32_t,uint32_t);
HAL_StatusTypeDef HAL_LTDC_EnableCLUT(LTDC_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_LTDC_ProgramLineEvent(LTDC_HandleTypeDef *,uint32_t);
HAL_StatusTypeDef HAL_MDMA_Init(MDMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_MDMA_Start_IT(MDMA_HandleTypeDef *,uint32_t,uint32_t,uint32_t,uint32_t);
typedef struct { uint32_t PLL3_P_Frequency, PLL3_Q_Frequency, PLL3_R_Frequency; } PLL3_ClocksTypeDef;
void HAL_RCCEx_GetPLL3ClockFreq(PLL3_ClocksTypeDef *);
void HAL_NVIC_SetPriority(uint32_t,uint32_t,uint32_t);
void HAL_NVIC_EnableIRQ(uint32_t);
void HAL_LTDC_IRQHandler(LTDC_HandleTypeDef *);
void HAL_MDMA_IRQHandler(MDMA_HandleTypeDef *);
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *);
void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *);
#endif
