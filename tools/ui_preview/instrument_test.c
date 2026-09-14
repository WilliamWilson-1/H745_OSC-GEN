/* Compile actual acquisition and generation code against a recording HAL.
 * Inclusion here permits fixtures to populate the private DMA buffer. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../CM7/Core/Src/oscilloscope.c"
#include "../../CM7/Core/Src/generate.c"

RCC_TypeDef fake_rcc={1U};
SCB_TypeDef fake_scb={0U};
uint32_t fake_pclk=120000000U, dac_dma_length, dac_starts, dac_stops;
uint8_t dac_dma_active;
static TIM_TypeDef tim2, tim6;
ADC_HandleTypeDef hadc1={ADC1};
DMA_HandleTypeDef hdma_adc1, dac_dma;
TIM_HandleTypeDef htim2={&tim2}, htim6={&tim6};
DAC_HandleTypeDef hdac1={&dac_dma};
void Error_Handler(void) { abort(); }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return fake_pclk; }
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *h) { h->Instance->running=1; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *h) { h->Instance->running=0; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *h,uint32_t *p,uint32_t n) {
    assert(h==&hadc1 && p==(uint32_t *)adc_dma_buffer && n==2048); return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h,uint32_t a,uint32_t b) {
    (void)h; (void)a; (void)b; return HAL_OK;
}
HAL_StatusTypeDef HAL_DAC_Start_DMA(DAC_HandleTypeDef *h,uint32_t c,uint32_t *p,uint32_t n,uint32_t a) {
    (void)h; (void)c; (void)a;
    assert(p==(uint32_t *)wave_table && !dac_dma_active && !tim6.running);
    assert(n>=10 && n<=128 && n%2==0);
    dac_dma_length=n; dac_dma_active=1; ++dac_starts; return HAL_OK;
}
HAL_StatusTypeDef HAL_DAC_Stop_DMA(DAC_HandleTypeDef *h,uint32_t c) {
    (void)h; (void)c; assert(!tim6.running); dac_dma_active=0; ++dac_stops; return HAL_OK;
}
HAL_StatusTypeDef HAL_DAC_Start(DAC_HandleTypeDef *h,uint32_t c) { (void)h; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_DAC_Stop(DAC_HandleTypeDef *h,uint32_t c) { (void)h; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_DAC_SetValue(DAC_HandleTypeDef *h,uint32_t c,uint32_t a,uint32_t v) {
    (void)h; (void)c; (void)a; assert(v==0); return HAL_OK;
}
void SCB_CleanDCache_by_Addr(uint32_t *p,int32_t n) {
    assert(!dac_dma_active && p==(uint32_t *)wave_table && n==256);
}
void SCB_InvalidateDCache_by_Addr(uint32_t *p,int32_t n) { assert(p==(uint32_t *)adc_dma_buffer && n==4096); }
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *p,int32_t n) { SCB_InvalidateDCache_by_Addr(p,n); }

static void capture_sine(float frequency) {
    for (unsigned i=0;i<OSC_RAW_SAMPLES;++i)
        adc_dma_buffer[i]=(uint16_t)(2048+1800*sinf(6.283185307f*frequency*i/state.sample_rate_hz));
    Oscilloscope_OnConversionComplete(&hadc1);
    assert(Oscilloscope_Poll());
}
static void test_scope(void) {
    const uint16_t counts[]={40,100,200,400,600,600,600,600,600,600,600,600};
    assert(Oscilloscope_Init()==HAL_OK);
    Oscilloscope_AdjustTimebase(-100);
    for (unsigned k=0;k<sizeof(counts)/sizeof(counts[0]);++k) {
        assert(!state.frame_valid);
        assert(state.timebase_us_per_div==timebase_table_us[k]);
        assert(state.sample_rate_hz<=2000000U);
        for (unsigned slope=0;slope<2;++slope) {
            Oscilloscope_SetTriggerSlope((OscTriggerSlope)slope);
            capture_sine(k<4 ? 100000.0f : state.sample_rate_hz/20.0f);
            uint16_t n=0;
            const uint16_t *trace=Oscilloscope_GetTrace(&n);
            assert(n==counts[k] && trace==trace_buffer && state.frame_valid);
            if (k<4) assert(fabsf(state.frequency_hz-100000.0f)<100.0f);
            uint16_t start=find_trigger_start();
            assert(start+n<=OSC_RAW_SAMPLES);
            for (unsigned i=0;i<n;++i) assert(trace[i]==adc_dma_buffer[start+i]);
            /* Consecutive displayed x coordinates preserve physical dt. */
            double span=(double)state.sample_rate_hz*state.timebase_us_per_div*10/1e6;
            assert((n-1)*600/span<600 && n*600/span>=599.0);
        }
        if (k+1<sizeof(counts)/sizeof(counts[0])) Oscilloscope_AdjustTimebase(1);
    }
    Oscilloscope_Stop();
    Oscilloscope_AdjustTimebase(-100);
    assert(!state.frame_valid && !state.running);
    Oscilloscope_Single();
    capture_sine(100000);
    assert(state.frame_valid && !state.running && !state.single_active);
    assert(!Oscilloscope_Poll());
    Oscilloscope_ResetControls();
    assert(state.timebase_us_per_div==500 && !state.running && !state.frame_valid);
    puts("PASS: 12 timebases, 40/100/200/400 real fast samples, 100 kHz measurement, both slopes, DMA trace bounds, held-frame invalidation and SINGLE.");
}
static void test_generator(void) {
    fake_scb.CCR=SCB_CCR_DC_Msk;
    DAC_WaveGen_Init();
    assert(!DAC_WaveGen_IsEnabled() && !dac_dma_active && !tim6.running);
    for (unsigned freq=50;freq<=100000;++freq) {
        DAC_WaveGen_Configure(WAVE_SINE,2047,freq);
        double rate=240000000.0/(tim6.PSC+1)/(tim6.ARR+1);
        assert(rate<=1000000 && rate/wave_points<=freq);
        assert(wave_points>=10 && wave_points<=128 && wave_points%2==0);
        assert(!dac_dma_active && !tim6.running);
        for(unsigned i=0;i<wave_points;++i) assert(wave_table[i]<=4095);
    }
    assert(wave_points==10 && tim6.ARR==239 && tim6.PSC==0);
    DAC_WaveGen_SetEnabled(1);
    assert(dac_dma_active && tim6.running && dac_dma_length==10);
    unsigned starts=dac_starts;
    DAC_WaveGen_SetEnabled(1); assert(dac_starts==starts);
    for (unsigned type=0;type<3;++type) {
        DAC_WaveGen_Configure((WaveType)type,2047,100000);
        assert(dac_dma_active && tim6.running && dac_dma_length==10);
        uint16_t lo=4095,hi=0;
        for(unsigned i=0;i<10;++i) {
            if(wave_table[i]<lo) lo=wave_table[i];
            if(wave_table[i]>hi) hi=wave_table[i];
        }
        assert(lo<=2 && hi>=4094);
    }
    DAC_WaveGen_Configure(WAVE_TRIANGLE,0,1000);
    assert(dac_dma_length==128 && dac_stops==4);
    for(unsigned i=0;i<128;++i) assert(wave_table[i]==2048);
    DAC_WaveGen_SetEnabled(0);
    assert(!dac_dma_active && !tim6.running);
    DAC_WaveGen_Configure(WAVE_SQUARE,2047,UINT32_MAX);
    assert(wave_points==10 && tim6.ARR==239);
    DAC_WaveGen_Configure(WAVE_SINE,2047,0);
    assert(wave_points==128);
    fake_rcc.D2CFGR=RCC_APB1_DIV1;
    DAC_WaveGen_Configure(WAVE_SINE,2047,100000);
    assert(tim6.ARR==119); /* No hardcoded APB doubling. */
    puts("PASS: all 99,951 integer frequencies 50..100000 Hz respect DAC 1 MHz ceiling; 100 kHz exact timer division, adaptive DMA length, waveform peaks, atomic reconfiguration and output-off safety.");
}

static void test_fine_controls(void) {
    Oscilloscope_ResetControls();
    assert(!state.fine_adjustment);
    Oscilloscope_SetFineAdjustment(true);
    assert(state.fine_adjustment && !state.running);
    Oscilloscope_AdjustTimebaseFine(1);
    assert(state.timebase_us_per_div==550 && !state.running);
    Oscilloscope_AdjustTimebase(-1); assert(state.timebase_us_per_div==500);
    Oscilloscope_AdjustTimebaseFine(-1); assert(state.timebase_us_per_div==450);
    Oscilloscope_AdjustTimebase(1); assert(state.timebase_us_per_div==500);
    Oscilloscope_AdjustTimebaseFine(128); assert(state.timebase_us_per_div==10000);
    Oscilloscope_AdjustTimebaseFine(-128); assert(state.timebase_us_per_div==2);
    Oscilloscope_AdjustTimebaseFine(-1); assert(state.timebase_us_per_div==2);
    Oscilloscope_AdjustTimebaseFine(1); assert(state.timebase_us_per_div==3);
    Oscilloscope_Single(); capture_sine(100000);
    uint16_t n; Oscilloscope_GetTrace(&n); assert(n==60);
    Oscilloscope_AdjustTimebase(1); assert(state.timebase_us_per_div==5);

    Oscilloscope_AdjustVoltsPerDivFine(1); assert(fabsf(state.volts_per_div-0.55f)<0.001f);
    Oscilloscope_AdjustVoltsPerDiv(-1); assert(fabsf(state.volts_per_div-0.5f)<0.001f);
    Oscilloscope_AdjustVoltsPerDivFine(-1); assert(fabsf(state.volts_per_div-0.45f)<0.001f);
    Oscilloscope_AdjustVoltsPerDiv(1); assert(fabsf(state.volts_per_div-0.5f)<0.001f);
    Oscilloscope_AdjustVoltsPerDivFine(128); assert(fabsf(state.volts_per_div-1.0f)<0.001f);
    Oscilloscope_AdjustVoltsPerDivFine(-128); assert(fabsf(state.volts_per_div-0.1f)<0.001f);
    Oscilloscope_AdjustTriggerFine(1); assert(fabsf(state.trigger_level_v-1.66f)<0.001f);
    Oscilloscope_AdjustTrigger(-1); assert(fabsf(state.trigger_level_v-1.61f)<0.001f);
    Oscilloscope_AdjustVerticalPositionFine(-1); assert(fabsf(state.vertical_center_v-1.64f)<0.001f);
    Oscilloscope_AdjustVerticalPosition(1); assert(fabsf(state.vertical_center_v-1.69f)<0.001f);
    Oscilloscope_AdjustTriggerFine(1000); assert(state.trigger_level_v==3.3f);
    Oscilloscope_AdjustVerticalPositionFine(-1000); assert(state.vertical_center_v==0);
    Oscilloscope_ResetControls(); assert(!state.fine_adjustment && state.timebase_us_per_div==500);
    /* Fine settings use actual timer rates, including fractional divisors. */
    Oscilloscope_Start();
    Oscilloscope_AdjustTimebaseFine(1); capture_sine(1000);
    assert(state.running && state.frame_valid && state.timebase_us_per_div==550);
    Oscilloscope_GetTrace(&n); assert(n==600);
    Oscilloscope_Stop();
    puts("PASS: coarse/fine directional transitions, 10% quantization, time/voltage limits, 10 mV versus 50 mV, RUN/HOLD/SINGLE, and reset defaults.");
}
int main(void) { test_scope(); test_fine_controls(); test_generator(); return 0; }
