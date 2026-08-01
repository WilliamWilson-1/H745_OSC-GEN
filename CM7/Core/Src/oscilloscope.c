#include "oscilloscope.h"

#include <stddef.h>

/* DMA1 cannot access the CM7 DTCM at 0x20000000.  The linker places this
 * buffer in D2 SRAM and aligns it to a complete Cortex-M7 cache line. */
__attribute__((section(".dma_buffer"), aligned(32)))
static uint16_t adc_dma_buffer[OSC_RAW_SAMPLES];

static uint16_t trace_buffer[OSC_TRACE_POINTS];
static volatile bool dma_complete;
static volatile bool dma_active;
static volatile bool acquisition_error;
static bool single_pending;

static const uint32_t timebase_table_us[] = {
    50U, 100U, 200U, 500U, 1000U, 2000U, 5000U, 10000U
};

static const float volts_per_div_table[] = {
    0.1f, 0.2f, 0.5f, 1.0f
};

static uint8_t timebase_index = 3U;
static uint8_t volts_per_div_index = 2U;

static OscilloscopeState state = {
    .sample_rate_hz = 120000U,
    .timebase_us_per_div = 500U,
    .volts_per_div = 0.5f,
    .trigger_level_v = 1.65f,
    .vertical_center_v = 1.65f,
    .frequency_hz = 0.0f,
    .vpp_v = 0.0f,
    .average_v = 0.0f,
    .trigger_slope = OSC_TRIGGER_RISING,
    .running = false,
    .frame_valid = false
};

static uint32_t timer2_clock_hz(void)
{
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != RCC_APB1_DIV1) {
        clock *= 2U;
    }
    return clock;
}

static void configure_sample_timer(void)
{
    uint64_t requested = ((uint64_t)OSC_TRACE_POINTS * 1000000ULL) /
                         ((uint64_t)state.timebase_us_per_div * 10ULL);
    if (requested < 2000ULL) requested = 2000ULL;
    if (requested > 2000000ULL) requested = 2000000ULL;

    const uint32_t timer_clock = timer2_clock_hz();
    uint64_t total_div = ((uint64_t)timer_clock + requested / 2ULL) / requested;
    if (total_div < 1ULL) total_div = 1ULL;

    uint32_t prescaler = (uint32_t)((total_div - 1ULL) / 65536ULL);
    if (prescaler > 65535U) prescaler = 65535U;

    uint32_t period = (uint32_t)(total_div / (uint64_t)(prescaler + 1U));
    if (period < 1U) period = 1U;
    if (period > 0xFFFFFFFFU) period = 0xFFFFFFFFU;

    __HAL_TIM_DISABLE(&htim2);
    __HAL_TIM_SET_PRESCALER(&htim2, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim2, period - 1U);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    htim2.Instance->EGR = TIM_EGR_UG;

    state.sample_rate_hz = timer_clock / (prescaler + 1U) / period;
}

static void prepare_dma_buffer(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)adc_dma_buffer,
                                         sizeof(adc_dma_buffer));
    }
}

static void begin_capture(void)
{
    if (!state.running || dma_active) return;

    dma_complete = false;
    acquisition_error = false;
    prepare_dma_buffer();
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer,
                          OSC_RAW_SAMPLES) != HAL_OK) {
        state.running = false;
        return;
    }

    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
        (void)HAL_ADC_Stop_DMA(&hadc1);
        state.running = false;
        return;
    }
    dma_active = true;
}

static uint16_t voltage_to_adc(float voltage)
{
    if (voltage <= 0.0f) return 0U;
    if (voltage >= OSC_INPUT_FULL_V) return OSC_ADC_MAX;
    return (uint16_t)((voltage * (float)OSC_ADC_MAX / OSC_INPUT_FULL_V) + 0.5f);
}

static uint16_t find_trigger_start(void)
{
    const uint16_t level = voltage_to_adc(state.trigger_level_v);
    const uint16_t hysteresis = 8U;
    const uint16_t pretrigger = OSC_TRACE_POINTS / 4U;
    bool armed = false;

    for (uint16_t i = 32U; i < OSC_RAW_SAMPLES - 32U; ++i) {
        const uint16_t sample = adc_dma_buffer[i];
        bool triggered = false;

        if (state.trigger_slope == OSC_TRIGGER_RISING) {
            if (sample + hysteresis < level) armed = true;
            triggered = armed && sample >= level + hysteresis;
        } else {
            if (sample > level + hysteresis) armed = true;
            triggered = armed && sample + hysteresis <= level;
        }

        if (triggered) {
            if (i <= pretrigger) return 0U;
            uint16_t start = i - pretrigger;
            if ((uint32_t)start + OSC_TRACE_POINTS > OSC_RAW_SAMPLES) {
                start = OSC_RAW_SAMPLES - OSC_TRACE_POINTS;
            }
            return start;
        }
    }

    return (OSC_RAW_SAMPLES - OSC_TRACE_POINTS) / 2U;
}

static void calculate_measurements(void)
{
    uint16_t minimum = OSC_ADC_MAX;
    uint16_t maximum = 0U;
    uint64_t sum = 0ULL;

    for (uint32_t i = 0U; i < OSC_RAW_SAMPLES; ++i) {
        const uint16_t sample = adc_dma_buffer[i];
        if (sample < minimum) minimum = sample;
        if (sample > maximum) maximum = sample;
        sum += sample;
    }

    const float adc_to_volts = OSC_INPUT_FULL_V / (float)OSC_ADC_MAX;
    state.vpp_v = (float)(maximum - minimum) * adc_to_volts;
    state.average_v = ((float)sum / (float)OSC_RAW_SAMPLES) * adc_to_volts;
    state.frequency_hz = 0.0f;

    const uint16_t span = maximum - minimum;
    if (span < 16U) return;

    const uint16_t midpoint = minimum + span / 2U;
    uint16_t hysteresis = span / 20U;
    if (hysteresis < 4U) hysteresis = 4U;

    bool armed = false;
    uint32_t first_crossing = 0U;
    uint32_t last_crossing = 0U;
    uint32_t crossing_count = 0U;

    for (uint32_t i = 0U; i < OSC_RAW_SAMPLES; ++i) {
        const uint16_t sample = adc_dma_buffer[i];
        if (sample + hysteresis < midpoint) armed = true;
        if (armed && sample >= midpoint + hysteresis) {
            if (crossing_count == 0U) first_crossing = i;
            last_crossing = i;
            crossing_count++;
            armed = false;
        }
    }

    if (crossing_count >= 2U && last_crossing > first_crossing) {
        state.frequency_hz = ((float)(crossing_count - 1U) *
                              (float)state.sample_rate_hz) /
                             (float)(last_crossing - first_crossing);
    }
}

static void process_frame(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)adc_dma_buffer,
                                    sizeof(adc_dma_buffer));
    }

    calculate_measurements();
    const uint16_t start = find_trigger_start();
    for (uint16_t i = 0U; i < OSC_TRACE_POINTS; ++i) {
        trace_buffer[i] = adc_dma_buffer[start + i];
    }
    state.frame_valid = true;
}

HAL_StatusTypeDef Oscilloscope_Init(void)
{
    configure_sample_timer();
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET,
                                    ADC_SINGLE_ENDED) != HAL_OK) {
        return HAL_ERROR;
    }
    Oscilloscope_Start();
    return state.running ? HAL_OK : HAL_ERROR;
}

void Oscilloscope_Start(void)
{
    state.running = true;
    single_pending = false;
    begin_capture();
}

void Oscilloscope_Stop(void)
{
    state.running = false;
    single_pending = false;
    acquisition_error = false;
    dma_complete = false;
    (void)HAL_TIM_Base_Stop(&htim2);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    dma_active = false;
}

void Oscilloscope_ToggleRun(void)
{
    if (state.running) Oscilloscope_Stop();
    else Oscilloscope_Start();
}

void Oscilloscope_Single(void)
{
    Oscilloscope_Stop();
    single_pending = true;
    state.running = true;
    begin_capture();
}

void Oscilloscope_ResetControls(void)
{
    const bool was_running = state.running;
    Oscilloscope_Stop();
    timebase_index = 3U;
    volts_per_div_index = 2U;
    state.timebase_us_per_div = timebase_table_us[timebase_index];
    state.volts_per_div = volts_per_div_table[volts_per_div_index];
    state.trigger_level_v = 1.65f;
    state.vertical_center_v = 1.65f;
    state.trigger_slope = OSC_TRIGGER_RISING;
    configure_sample_timer();
    if (was_running) Oscilloscope_Start();
}

bool Oscilloscope_Poll(void)
{
    if (acquisition_error) {
        acquisition_error = false;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        dma_active = false;
        if (state.running) begin_capture();
        return false;
    }
    if (!dma_complete) return false;

    dma_complete = false;
    (void)HAL_ADC_Stop_DMA(&hadc1);
    process_frame();

    if (single_pending) {
        single_pending = false;
        state.running = false;
    }
    if (state.running) begin_capture();
    return true;
}

void Oscilloscope_OnConversionComplete(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;
    (void)HAL_TIM_Base_Stop(&htim2);
    dma_active = false;
    dma_complete = true;
}

void Oscilloscope_OnError(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;
    (void)HAL_TIM_Base_Stop(&htim2);
    dma_active = false;
    acquisition_error = true;
}

void Oscilloscope_AdjustTimebase(int steps)
{
    int next = (int)timebase_index + steps;
    if (next < 0) next = 0;
    if (next >= (int)(sizeof(timebase_table_us) / sizeof(timebase_table_us[0]))) {
        next = (int)(sizeof(timebase_table_us) / sizeof(timebase_table_us[0])) - 1;
    }
    if (next == (int)timebase_index) return;

    const bool was_running = state.running;
    Oscilloscope_Stop();
    timebase_index = (uint8_t)next;
    state.timebase_us_per_div = timebase_table_us[timebase_index];
    configure_sample_timer();
    if (was_running) Oscilloscope_Start();
}

void Oscilloscope_AdjustVoltsPerDiv(int steps)
{
    int next = (int)volts_per_div_index + steps;
    if (next < 0) next = 0;
    if (next >= (int)(sizeof(volts_per_div_table) / sizeof(volts_per_div_table[0]))) {
        next = (int)(sizeof(volts_per_div_table) / sizeof(volts_per_div_table[0])) - 1;
    }
    volts_per_div_index = (uint8_t)next;
    state.volts_per_div = volts_per_div_table[volts_per_div_index];
}

void Oscilloscope_AdjustTrigger(int steps)
{
    state.trigger_level_v += (float)steps * 0.05f;
    if (state.trigger_level_v < 0.0f) state.trigger_level_v = 0.0f;
    if (state.trigger_level_v > OSC_INPUT_FULL_V) {
        state.trigger_level_v = OSC_INPUT_FULL_V;
    }
}

void Oscilloscope_AdjustVerticalPosition(int steps)
{
    state.vertical_center_v += (float)steps * 0.05f;
    if (state.vertical_center_v < 0.0f) state.vertical_center_v = 0.0f;
    if (state.vertical_center_v > OSC_INPUT_FULL_V) {
        state.vertical_center_v = OSC_INPUT_FULL_V;
    }
}

void Oscilloscope_SetTriggerSlope(OscTriggerSlope slope)
{
    state.trigger_slope = slope;
}

const OscilloscopeState *Oscilloscope_GetState(void)
{
    return &state;
}

const uint16_t *Oscilloscope_GetTrace(uint16_t *point_count)
{
    if (point_count != NULL) *point_count = OSC_TRACE_POINTS;
    return trace_buffer;
}
