#ifndef DISPLAY_H
#define DISPLAY_H

#include "main.h"
#include <stdbool.h>

#define DISPLAY_WIDTH 800U
#define DISPLAY_HEIGHT 480U
#define DISPLAY_SPLIT_Y 160U
#define DISPLAY_BOTTOM_PITCH DISPLAY_WIDTH
#define DISPLAY_TOP_BYTES (DISPLAY_WIDTH * DISPLAY_SPLIT_Y)
#define DISPLAY_BOTTOM_BYTES (DISPLAY_BOTTOM_PITCH * (DISPLAY_HEIGHT - DISPLAY_SPLIT_Y))

typedef struct {
    uint32_t submitted;
    uint32_t presented;
    uint32_t fifo_underruns;
    uint32_t transfer_errors;
    uint32_t copy_errors;
    uint32_t late_copies;
    uint32_t last_copy_us;
    uint32_t max_copy_us;
    uint32_t blank_budget_us;
} DisplayStats;
extern volatile DisplayStats display_stats;

/* Initialize after MX_LTDC_Init, with the backlight still off. */
void Display_Init(void);
bool Display_BeginFrame(void);
void Display_Present(void);

#endif
