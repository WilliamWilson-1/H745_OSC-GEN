#include "display.h"
#include "graph.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FakeLTDC registers;
LTDC_HandleTypeDef hltdc={&registers,{32,512,525,927},0};
FakeSCB fake_scb;
FakeDWT fake_dwt;
FakeCoreDebug fake_debug;
uint32_t SystemCoreClock=480000000;
uint32_t my_palette[256];
static uint8_t *render_top,*render_bottom;
static uint32_t scanout_address,starts,clean_count,clut_count;
static MDMA_HandleTypeDef *active_dma;
static bool busy;
void Error_Handler(void) { abort(); }
void UI_SetRenderBuffers(uint8_t *a,uint8_t *b,uint16_t pitch) {
    assert(pitch==800); render_top=a; render_bottom=b;
}
void UI_Render(void) {
    memset(render_top,0x5a,DISPLAY_TOP_BYTES);
    memset(render_bottom,0x6b,DISPLAY_BOTTOM_BYTES);
}
void SCB_CleanDCache_by_Addr(uint32_t *address,int32_t size) {
    assert(((uintptr_t)address&31U)==0U);
    assert(size==DISPLAY_TOP_BYTES || size==DISPLAY_BOTTOM_BYTES || size==384000);
    ++clean_count;
}
HAL_StatusTypeDef HAL_LTDC_ConfigLayer(LTDC_HandleTypeDef *h,LTDC_LayerCfgTypeDef *c,uint32_t layer) {
    (void)h; assert(layer==0);
    assert(c->WindowX0==0 && c->WindowX1==800 && c->ImageWidth==800);
    assert(c->WindowY0==0 && c->WindowY1==480 && c->ImageHeight==480);
    scanout_address=c->FBStartAdress;
    assert(scanout_address!=(uint32_t)(uintptr_t)render_top);
    assert(scanout_address!=(uint32_t)(uintptr_t)render_bottom);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_LTDC_ConfigCLUT(LTDC_HandleTypeDef *h,uint32_t *p,uint32_t n,uint32_t layer) {
    (void)h; assert(p==my_palette && n==256 && layer==0); ++clut_count; return HAL_OK;
}
HAL_StatusTypeDef HAL_LTDC_EnableCLUT(LTDC_HandleTypeDef *h,uint32_t layer) {
    (void)h; assert(layer==0); return HAL_OK;
}
void HAL_RCCEx_GetPLL3ClockFreq(PLL3_ClocksTypeDef *clocks) { clocks->PLL3_R_Frequency=30000000; }
void HAL_NVIC_SetPriority(uint32_t irq,uint32_t p,uint32_t sub) { (void)irq;(void)p;(void)sub; }
void HAL_NVIC_EnableIRQ(uint32_t irq) { (void)irq; }
void HAL_LTDC_IRQHandler(LTDC_HandleTypeDef *h) { (void)h; }
void HAL_MDMA_IRQHandler(MDMA_HandleTypeDef *h) { (void)h; }
HAL_StatusTypeDef HAL_LTDC_ProgramLineEvent(LTDC_HandleTypeDef *h,uint32_t line) {
    (void)h; assert(line==513); return HAL_OK;
}
HAL_StatusTypeDef HAL_MDMA_Init(MDMA_HandleTypeDef *h) {
    assert(h->Init.TransferTriggerMode==MDMA_FULL_TRANSFER);
    assert(h->Init.BufferTransferLength==128); return HAL_OK;
}
HAL_StatusTypeDef HAL_MDMA_Start_IT(MDMA_HandleTypeDef *h,uint32_t src,uint32_t dst,uint32_t bytes,uint32_t count) {
    assert(!busy && bytes==64000);
    if((starts&1U)==0) {
        assert(registers.CPSR==513 && count==2);
        assert(src==(uint32_t)(uintptr_t)render_top && dst==scanout_address);
    } else {
        assert(count==4);
        assert(src==(uint32_t)(uintptr_t)render_bottom && dst==scanout_address+DISPLAY_TOP_BYTES);
    }
    busy=true; active_dma=h; ++starts; return HAL_OK;
}
static void complete(uint32_t line,uint32_t cycles) {
    assert(busy); busy=false;
    registers.CPSR=line; fake_dwt.CYCCNT+=cycles;
    active_dma->XferCpltCallback(active_dma);
}
int main(void) {
    fake_scb.CCR=SCB_CCR_DC_Msk;
    Display_Init(); assert(clut_count==1 && clean_count==1);
    assert(display_stats.blank_budget_us==1422);
    Display_Present(); assert(starts==0);
    assert(Display_BeginFrame()); assert(!Display_BeginFrame());
    registers.CPSR=513; HAL_LTDC_LineEventCallback(&hltdc); assert(starts==0);
    for(unsigned i=0;i<1000;++i) {
        UI_Render(); Display_Present(); Display_Present();
        assert(display_stats.submitted==i+1 && starts==i*2);
        assert(!Display_BeginFrame());
        registers.CPSR=100; HAL_LTDC_LineEventCallback(&hltdc); assert(starts==i*2);
        registers.CPSR=513; HAL_LTDC_LineEventCallback(&hltdc); assert(starts==i*2+1);
        assert(!Display_BeginFrame());
        complete(520,160000); assert(starts==i*2+2 && !Display_BeginFrame());
        complete(5,160000); assert(display_stats.presented==i+1);
        assert(Display_BeginFrame());
    }
    assert(display_stats.late_copies==0 && display_stats.max_copy_us==666);
    Display_Present(); registers.CPSR=513; HAL_LTDC_LineEventCallback(&hltdc);
    complete(520,160000); complete(40,600000);
    assert(display_stats.late_copies==1 && Display_BeginFrame());
    hltdc.ErrorCode=HAL_LTDC_ERROR_FU|HAL_LTDC_ERROR_TE; HAL_LTDC_ErrorCallback(&hltdc);
    assert(display_stats.fifo_underruns==1 && display_stats.transfer_errors==1);
    assert(clean_count==2003);
    puts("PASS: 1000 blanking-triggered 384000-byte MDMA frame copies; pending/partial/duplicate protection, delayed IRQ deferral, cache ranges and late/error counters.");
    return 0;
}
