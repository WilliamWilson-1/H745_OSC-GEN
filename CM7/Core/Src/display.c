#include "display.h"
#include "graph.h"
#include <string.h>

extern LTDC_HandleTypeDef hltdc;
static MDMA_HandleTypeDef copy_dma;

/* H745 LTDC cannot read D2 SRAM (AN5557 Table 1). Only scanout lives in
 * AXI SRAM; MDMA can read the split software back buffer in AXI + D2. */
__attribute__((section(".lcd_front"), aligned(32)))
static uint8_t scanout[DISPLAY_WIDTH * DISPLAY_HEIGHT];
__attribute__((section(".lcd_top"), aligned(32)))
static uint8_t back_top[DISPLAY_TOP_BYTES];
__attribute__((section(".lcd_bottom"), aligned(32)))
static uint8_t back_bottom[DISPLAY_BOTTOM_BYTES];

volatile DisplayStats display_stats;
static volatile uint8_t copy_phase;
static volatile bool queued;
static bool rendering;
static uint32_t copy_start;

static void clean(uint8_t *address, int32_t bytes)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
        SCB_CleanDCache_by_Addr((uint32_t *)address, bytes);
    __DSB();
}

static void copy_error(MDMA_HandleTypeDef *dma)
{
    (void)dma;
    ++display_stats.copy_errors;
    copy_phase = 0U;
}

static void copy_complete(MDMA_HandleTypeDef *dma)
{
    (void)dma;
    if (copy_phase == 1U) {
        copy_phase = 2U;
        if (HAL_MDMA_Start_IT(&copy_dma, (uint32_t)(uintptr_t)back_bottom,
                (uint32_t)(uintptr_t)(scanout + DISPLAY_TOP_BYTES), 64000U, 4U) != HAL_OK)
            copy_error(&copy_dma);
    } else if (copy_phase == 2U) {
        uint32_t elapsed = DWT->CYCCNT - copy_start;
        uint32_t us = elapsed / (SystemCoreClock / 1000000U);
        display_stats.last_copy_us = us;
        if (us > display_stats.max_copy_us) display_stats.max_copy_us = us;
        /* The blanking interval is about 1.42 ms with current timings.
         * Count late copies explicitly so hardware testing can detect them. */
        uint32_t line = hltdc.Instance->CPSR & LTDC_CPSR_CYPOS;
        if (us >= display_stats.blank_budget_us ||
            (line > hltdc.Init.AccumulatedVBP && line <= hltdc.Init.AccumulatedActiveH))
            ++display_stats.late_copies;
        ++display_stats.presented;
        __DMB();
        copy_phase = 0U;
    }
}

void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *ltdc)
{
    if (queued && copy_phase == 0U) {
        uint32_t line = ltdc->Instance->CPSR & LTDC_CPSR_CYPOS;
        if (line == ltdc->Init.AccumulatedActiveH + 1U) {
            queued = false;
            copy_phase = 1U;
            copy_start = DWT->CYCCNT;
            if (HAL_MDMA_Start_IT(&copy_dma, (uint32_t)(uintptr_t)back_top,
                    (uint32_t)(uintptr_t)scanout, 64000U, 2U) != HAL_OK)
                copy_error(&copy_dma);
        }
        /* A delayed interrupt leaves the complete frame queued for next time. */
    }
    HAL_LTDC_ProgramLineEvent(ltdc, ltdc->Init.AccumulatedActiveH + 1U);
}

void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *ltdc)
{
    if ((ltdc->ErrorCode & HAL_LTDC_ERROR_FU) != 0U) ++display_stats.fifo_underruns;
    if ((ltdc->ErrorCode & HAL_LTDC_ERROR_TE) != 0U) ++display_stats.transfer_errors;
}

void LTDC_IRQHandler(void) { HAL_LTDC_IRQHandler(&hltdc); }
void MDMA_IRQHandler(void) { HAL_MDMA_IRQHandler(&copy_dma); }

void Display_Init(void)
{
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    __HAL_RCC_MDMA_CLK_ENABLE();
    copy_phase = 0U;
    queued = rendering = false;
    memset((void *)&display_stats, 0, sizeof(display_stats));
    /* H745 LTDC uses PLL3R. This HAL version's generic peripheral-clock
     * getter does NOT support LTDC and returns zero for that identifier. */
    PLL3_ClocksTypeDef pll3 = {0};
    HAL_RCCEx_GetPLL3ClockFreq(&pll3);
    uint32_t pixel_clock = pll3.PLL3_R_Frequency;
    if (pixel_clock == 0U) Error_Handler();
    uint32_t blank_lines = hltdc.Init.TotalHeigh - hltdc.Init.AccumulatedActiveH +
                           hltdc.Init.AccumulatedVBP + 1U;
    display_stats.blank_budget_us = (uint32_t)((uint64_t)(hltdc.Init.TotalWidth + 1U) *
                                              blank_lines * 1000000ULL / pixel_clock);
    UI_SetRenderBuffers(back_top, back_bottom, DISPLAY_WIDTH);
    UI_Render();
    memcpy(scanout, back_top, DISPLAY_TOP_BYTES);
    memcpy(scanout + DISPLAY_TOP_BYTES, back_bottom, DISPLAY_BOTTOM_BYTES);
    clean(scanout, sizeof(scanout));

    LTDC_LayerCfgTypeDef cfg = {0};
    cfg.WindowX1 = cfg.ImageWidth = DISPLAY_WIDTH;
    cfg.WindowY1 = cfg.ImageHeight = DISPLAY_HEIGHT;
    cfg.PixelFormat = LTDC_PIXEL_FORMAT_L8;
    cfg.Alpha = 255;
    cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    cfg.FBStartAdress = (uint32_t)(uintptr_t)scanout;
    if (HAL_LTDC_ConfigLayer(&hltdc, &cfg, 0) != HAL_OK ||
        HAL_LTDC_ConfigCLUT(&hltdc, my_palette, 256, 0) != HAL_OK ||
        HAL_LTDC_EnableCLUT(&hltdc, 0) != HAL_OK) Error_Handler();

    copy_dma.Instance = MDMA_Channel0;
    copy_dma.Init.Request = MDMA_REQUEST_SW;
    copy_dma.Init.TransferTriggerMode = MDMA_FULL_TRANSFER;
    copy_dma.Init.Priority = MDMA_PRIORITY_VERY_HIGH;
    copy_dma.Init.Endianness = MDMA_LITTLE_ENDIANNESS_PRESERVE;
    copy_dma.Init.SourceInc = MDMA_SRC_INC_WORD;
    copy_dma.Init.DestinationInc = MDMA_DEST_INC_WORD;
    copy_dma.Init.SourceDataSize = MDMA_SRC_DATASIZE_WORD;
    copy_dma.Init.DestDataSize = MDMA_DEST_DATASIZE_WORD;
    copy_dma.Init.DataAlignment = MDMA_DATAALIGN_PACKENABLE;
    copy_dma.Init.BufferTransferLength = 128;
    copy_dma.Init.SourceBurst = MDMA_SOURCE_BURST_16BEATS;
    copy_dma.Init.DestBurst = MDMA_DEST_BURST_16BEATS;
    if (HAL_MDMA_Init(&copy_dma) != HAL_OK) Error_Handler();
    copy_dma.XferCpltCallback = copy_complete;
    copy_dma.XferErrorCallback = copy_error;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_RR | LTDC_FLAG_FU | LTDC_FLAG_TE | LTDC_FLAG_LI);
    HAL_NVIC_SetPriority(MDMA_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(MDMA_IRQn);
    HAL_NVIC_SetPriority(LTDC_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_LTDC_ProgramLineEvent(&hltdc, hltdc.Init.AccumulatedActiveH + 1U);
}

bool Display_BeginFrame(void)
{
    if (rendering || queued || copy_phase != 0U) return false;
    __DMB();
    rendering = true;
    UI_SetRenderBuffers(back_top, back_bottom, DISPLAY_WIDTH);
    return true;
}

void Display_Present(void)
{
    if (!rendering) return;
    clean(back_top, sizeof(back_top));
    clean(back_bottom, sizeof(back_bottom));
    rendering = false;
    ++display_stats.submitted;
    __DMB();
    queued = true;
}
