#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define OSC_RAW_SAMPLES    2048U
#define OSC_TRACE_POINTS    600U
#define OSC_ADC_MAX        4095U
#define OSC_INPUT_FULL_V  3.3f

typedef enum {
    OSC_TRIGGER_RISING = 0,
    OSC_TRIGGER_FALLING
} OscTriggerSlope;

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t timebase_us_per_div;
    float volts_per_div;
    float trigger_level_v;
    float vertical_center_v;
    float frequency_hz;
    float vpp_v;
    float minimum_v;
    float maximum_v;
    float average_v;
    OscTriggerSlope trigger_slope;
    bool running;
    bool single_active;
    bool frame_valid;
    bool fine_adjustment;
} OscilloscopeState;

typedef struct {
    uint32_t completed_frames;
    uint32_t adc_errors;
    uint32_t start_errors;
} OscilloscopeDiagnostics;
extern volatile OscilloscopeDiagnostics osc_diagnostics;

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

HAL_StatusTypeDef Oscilloscope_Init(void);
void Oscilloscope_Start(void);
void Oscilloscope_Stop(void);
void Oscilloscope_ToggleRun(void);
void Oscilloscope_Single(void);
void Oscilloscope_ResetControls(void);
bool Oscilloscope_Poll(void);
void Oscilloscope_OnConversionComplete(ADC_HandleTypeDef *hadc);
void Oscilloscope_OnError(ADC_HandleTypeDef *hadc);

void Oscilloscope_AdjustTimebase(int steps);
void Oscilloscope_AdjustVoltsPerDiv(int steps);
void Oscilloscope_AdjustTrigger(int steps);
void Oscilloscope_AdjustVerticalPosition(int steps);
void Oscilloscope_SetFineAdjustment(bool fine);
void Oscilloscope_AdjustTimebaseFine(int steps);
void Oscilloscope_AdjustVoltsPerDivFine(int steps);
void Oscilloscope_AdjustTriggerFine(int steps);
void Oscilloscope_AdjustVerticalPositionFine(int steps);
void Oscilloscope_SetTriggerSlope(OscTriggerSlope slope);

const OscilloscopeState *Oscilloscope_GetState(void);
const uint16_t *Oscilloscope_GetTrace(uint16_t *point_count);

#endif /* OSCILLOSCOPE_H */
