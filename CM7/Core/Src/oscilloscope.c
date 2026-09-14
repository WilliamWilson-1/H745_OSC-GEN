#include "oscilloscope.h"

#include <stddef.h>

/* DMA1 cannot access the CM7 DTCM at 0x20000000.  The linker places this
 * buffer in D2 SRAM and aligns it to a complete Cortex-M7 cache line. */
__attribute__((section(".dma_buffer"), aligned(32)))
static uint16_t adc_dma_buffer[OSC_RAW_SAMPLES];

static uint16_t trace_buffer[OSC_TRACE_POINTS];
static uint16_t trace_count = OSC_TRACE_POINTS;
static volatile bool dma_complete;
static volatile bool dma_active;
static volatile bool acquisition_error;
volatile OscilloscopeDiagnostics osc_diagnostics;

static const uint32_t timebase_table_us[] = {
    2U, 5U, 10U, 20U, 50U, 100U, 200U, 500U, 1000U, 2000U, 5000U, 10000U
};

static const float volts_per_div_table[] = {
    0.1f, 0.2f, 0.5f, 1.0f
};

static OscilloscopeState state = {
    .sample_rate_hz = 120000U,
    .timebase_us_per_div = 500U,
    .volts_per_div = 0.5f,
    .trigger_level_v = 1.65f,
    .vertical_center_v = 1.65f,
    .frequency_hz = 0.0f,
    .vpp_v = 0.0f,
    .minimum_v = 0.0f,
    .maximum_v = 0.0f,
    .average_v = 0.0f,
    .trigger_slope = OSC_TRIGGER_RISING,
    .running = false,
    .single_active = false,
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
        ++osc_diagnostics.start_errors;
        state.running = false;
        state.single_active = false;
        return;
    }

    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
        ++osc_diagnostics.start_errors;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        state.running = false;
        state.single_active = false;
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
    const uint16_t pretrigger = trace_count / 4U;
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
            if ((uint32_t)start + trace_count > OSC_RAW_SAMPLES) {
                start = OSC_RAW_SAMPLES - trace_count;
            }
            return start;
        }
    }

    return (OSC_RAW_SAMPLES - trace_count) / 2U;
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
    state.minimum_v = (float)minimum * adc_to_volts;
    state.maximum_v = (float)maximum * adc_to_volts;
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
    /* Keep real samples only. Fast timebases expand their actual timestamps
     * across the plot; they do not claim 600 independent ADC measurements. */
    uint64_t span = (uint64_t)state.sample_rate_hz * state.timebase_us_per_div * 10U;
    uint32_t count = (uint32_t)((span + 999999U) / 1000000U);
    if (count < 2U) count = 2U;
    if (count > OSC_TRACE_POINTS) count = OSC_TRACE_POINTS;
    trace_count = (uint16_t)count;
    const uint16_t start = find_trigger_start();
    for (uint16_t i = 0U; i < trace_count; ++i) {
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
    state.single_active = false;
    begin_capture();
}

void Oscilloscope_Stop(void)
{
    state.running = false;
    state.single_active = false;
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
    if (state.single_active) return;
    Oscilloscope_Stop();
    state.single_active = true;
    state.running = true;
    begin_capture();
}

void Oscilloscope_ResetControls(void)
{
    const bool resume_continuous = state.running && !state.single_active;
    Oscilloscope_Stop();
    state.timebase_us_per_div = 500U;
    state.volts_per_div = 0.5f;
    state.fine_adjustment = false;
    state.trigger_level_v = 1.65f;
    state.vertical_center_v = 1.65f;
    state.trigger_slope = OSC_TRIGGER_RISING;
    state.frame_valid = false;
    configure_sample_timer();
    if (resume_continuous) Oscilloscope_Start();
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
    ++osc_diagnostics.completed_frames;

    if (state.single_active) {
        state.single_active = false;
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
    ++osc_diagnostics.adc_errors;
}

static void set_timebase(uint32_t value)
{
    if (value == state.timebase_us_per_div) return;
    const bool resume_continuous = state.running && !state.single_active;
    Oscilloscope_Stop();
    state.timebase_us_per_div = value;
    /* Never relabel a held capture with a newly configured sampling clock. */
    state.frame_valid = false;
    configure_sample_timer();
    if (resume_continuous) Oscilloscope_Start();
}

/* From a fine-adjusted value, coarse motion goes to the next standard
 * setting in the requested direction, not a stale table index. */
void Oscilloscope_AdjustTimebase(int steps)
{
    uint32_t value = state.timebase_us_per_div;
    const int count = sizeof(timebase_table_us) / sizeof(timebase_table_us[0]);
    if (steps > count) steps = count;
    if (steps < -count) steps = -count;
    while (steps > 0) {
        for (int i = 0; i < count; ++i) {
            if (timebase_table_us[i] > value) { value = timebase_table_us[i]; break; }
        }
        --steps;
    }
    while (steps < 0) {
        for (int i = count-1; i >= 0; --i) {
            if (timebase_table_us[i] < value) { value = timebase_table_us[i]; break; }
        }
        ++steps;
    }
    set_timebase(value);
}

void Oscilloscope_AdjustVoltsPerDiv(int steps)
{
    const int count = sizeof(volts_per_div_table) / sizeof(volts_per_div_table[0]);
    if (steps > count) steps = count;
    if (steps < -count) steps = -count;
    while (steps > 0) {
        for (int i = 0; i < count; ++i) {
            if (volts_per_div_table[i] > state.volts_per_div + 0.001f) {
                state.volts_per_div = volts_per_div_table[i]; break;
            }
        }
        --steps;
    }
    while (steps < 0) {
        for (int i = count-1; i >= 0; --i) {
            if (volts_per_div_table[i] < state.volts_per_div - 0.001f) {
                state.volts_per_div = volts_per_div_table[i]; break;
            }
        }
        ++steps;
    }
}

void Oscilloscope_SetFineAdjustment(bool fine) { state.fine_adjustment = fine; }

static uint32_t fine_value(uint32_t value, uint32_t minimum, uint32_t maximum, int steps)
{
    if (steps > 128) steps = 128;
    if (steps < -128) steps = -128;
    while (steps != 0) {
        uint32_t increment = (value + 5U) / 10U;
        if (!increment) increment = 1U;
        value = steps > 0 ? value + increment : value - increment;
        if (value < minimum) value = minimum;
        if (value > maximum) value = maximum;
        steps += steps > 0 ? -1 : 1;
    }
    return value;
}

void Oscilloscope_AdjustTimebaseFine(int steps)
{
    set_timebase(fine_value(state.timebase_us_per_div, 2U, 10000U, steps));
}

void Oscilloscope_AdjustVoltsPerDivFine(int steps)
{
    uint32_t centivolts = (uint32_t)(state.volts_per_div * 100.0f + 0.5f);
    state.volts_per_div = (float)fine_value(centivolts, 10U, 100U, steps) * 0.01f;
}

static float clamp_voltage(float v)
{
    return v < 0.0f ? 0.0f : v > OSC_INPUT_FULL_V ? OSC_INPUT_FULL_V : v;
}

void Oscilloscope_AdjustTriggerFine(int steps)
{
    state.trigger_level_v = clamp_voltage(state.trigger_level_v + (float)steps * 0.01f);
}

void Oscilloscope_AdjustVerticalPositionFine(int steps)
{
    state.vertical_center_v = clamp_voltage(state.vertical_center_v + (float)steps * 0.01f);
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
    if (point_count != NULL) *point_count = trace_count;
    return trace_buffer;
}
