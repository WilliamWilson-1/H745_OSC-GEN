#include "graph.h"
#include "oscilloscope.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_ADDR      0x24020000U
#define SCREEN_W     800
#define SCREEN_H     480
#define PLOT_X       100
#define PLOT_Y        60
#define PLOT_W       600
#define PLOT_H       360
#define COLOR_BG       0U
#define COLOR_RED      1U
#define COLOR_GREEN    2U
#define COLOR_BLUE     3U
#define COLOR_WHITE    4U
#define COLOR_YELLOW   5U
#define COLOR_GRID     6U
#define COLOR_CYAN     7U

volatile SystemState current_sys_state = SYS_MAIN_MENU;
volatile uint8_t main_menu_sel = 0U;
volatile ControlMode current_ctrl = CTRL_TIMEBASE;

float wg_freq = 1000.0f;
float wg_amp = 3.3f;
volatile uint8_t wg_ctrl = 0U;
volatile WaveType wg_wave = WAVE_SINE;
uint32_t my_palette[256] = {0U};

static uint8_t *framebuffer(void)
{
    return (uint8_t *)FB_ADDR;
}

static void clear_screen(void)
{
    memset(framebuffer(), COLOR_BG, SCREEN_W * SCREEN_H);
}

static void draw_rect(int x, int y, int w, int h, uint8_t color)
{
    Draw_Line_L8(x, y, x + w, y, color);
    Draw_Line_L8(x, y + h, x + w, y + h, color);
    Draw_Line_L8(x, y, x, y + h, color);
    Draw_Line_L8(x + w, y, x + w, y + h, color);
}

static void fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; ++row) {
        memset(framebuffer() + (y + row) * SCREEN_W + x, color, (size_t)w);
    }
}

/* Compact 5x7 font. Each byte is one vertical column, least-significant bit
 * at the top. Unsupported characters are rendered as a space. */
static const uint8_t *glyph(char ch)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t dot[5]   = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t dash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t up[5]    = {0x08,0x04,0x02,0x04,0x08};
    static const uint8_t down[5]  = {0x08,0x10,0x20,0x10,0x08};

    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch == '.') return dot;
    if (ch == ':') return colon;
    if (ch == '/') return slash;
    if (ch == '-') return dash;
    if (ch == '^') return up;
    if (ch == 'v') return down;
    return blank;
}

static void draw_char(int x, int y, char ch, uint8_t color, uint8_t scale)
{
    const uint8_t *bitmap = glyph(ch);
    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            if ((bitmap[col] & (1U << row)) != 0U) {
                fill_rect(x + col * scale, y + row * scale,
                          scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint8_t color, uint8_t scale)
{
    while (*text != '\0') {
        draw_char(x, y, *text++, color, scale);
        x += 6 * scale;
    }
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        bool selected, uint8_t selected_color)
{
    const uint8_t color = selected ? selected_color : COLOR_WHITE;
    if (selected) fill_rect(x + 2, y + 2, w - 3, h - 3, COLOR_GRID);
    draw_rect(x, y, w, h, color);
    int label_width = (int)strlen(label) * 12 - 2;
    draw_text(x + (w - label_width) / 2, y + (h - 14) / 2,
              label, color, 2U);
}

static void format_fixed(char *out, size_t out_size, float value,
                         uint8_t decimals)
{
    uint32_t multiplier = decimals == 2U ? 100U : 10U;
    if (value < 0.0f) value = 0.0f;
    uint32_t scaled = (uint32_t)(value * (float)multiplier + 0.5f);
    if (decimals == 2U) {
        (void)snprintf(out, out_size, "%lu.%02lu",
                       (unsigned long)(scaled / 100U),
                       (unsigned long)(scaled % 100U));
    } else {
        (void)snprintf(out, out_size, "%lu.%01lu",
                       (unsigned long)(scaled / 10U),
                       (unsigned long)(scaled % 10U));
    }
}

void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        if (x1 >= 0 && x1 < SCREEN_W && y1 >= 0 && y1 < SCREEN_H) {
            framebuffer()[y1 * SCREEN_W + x1] = color;
        }
        if (x1 == x2 && y1 == y2) break;
        int twice_error = 2 * error;
        if (twice_error >= dy) { error += dy; x1 += sx; }
        if (twice_error <= dx) { error += dx; y1 += sy; }
    }
}

void Draw_Grid_And_Axes(void)
{
    clear_screen();
    for (int x = PLOT_X; x <= PLOT_X + PLOT_W; x += PLOT_W / 10) {
        Draw_Line_L8(x, PLOT_Y, x, PLOT_Y + PLOT_H, COLOR_GRID);
    }
    for (int y = PLOT_Y; y <= PLOT_Y + PLOT_H; y += PLOT_H / 8) {
        Draw_Line_L8(PLOT_X, y, PLOT_X + PLOT_W, y, COLOR_GRID);
    }
    draw_rect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H, COLOR_WHITE);
}

void Draw_UI_Button(uint16_t x, uint16_t y, uint8_t selected,
                    void (*DrawIcon)(uint16_t, uint16_t, uint8_t))
{
    const uint8_t color = selected ? COLOR_BLUE : COLOR_WHITE;
    draw_rect(x, y, 60, 30, color);
    if (DrawIcon != NULL) DrawIcon(x, y, color);
}

void Draw_Oscilloscope_UI(void)
{
    Draw_Grid_And_Axes();
    const OscilloscopeState *osc = Oscilloscope_GetState();
    char text[40];
    char value[16];

    draw_text(8, 8, "OSC", COLOR_GREEN, 2U);
    draw_text(60, 8, "IN:A5 PF11", COLOR_CYAN, 2U);

    if (osc->timebase_us_per_div >= 1000U) {
        (void)snprintf(text, sizeof(text), "T:%lums/D",
                       (unsigned long)(osc->timebase_us_per_div / 1000U));
    } else {
        (void)snprintf(text, sizeof(text), "T:%luus/D",
                       (unsigned long)osc->timebase_us_per_div);
    }
    draw_text(210, 8, text, COLOR_WHITE, 2U);

    format_fixed(value, sizeof(value), osc->volts_per_div, 2U);
    (void)snprintf(text, sizeof(text), "V:%sV/D", value);
    draw_text(355, 8, text, COLOR_WHITE, 2U);

    format_fixed(value, sizeof(value), osc->trigger_level_v, 2U);
    (void)snprintf(text, sizeof(text), "TRG:%sV", value);
    draw_text(510, 8, text, COLOR_RED, 2U);

    draw_button(10, 60, 80, 45, "TIME", current_ctrl == CTRL_TIMEBASE, COLOR_BLUE);
    draw_button(10, 115, 80, 45, "V/D", current_ctrl == CTRL_VOLTS_DIV, COLOR_BLUE);
    draw_button(10, 170, 80, 45, "TRIG", current_ctrl == CTRL_TRIGGER_LEVEL, COLOR_BLUE);
    draw_button(10, 225, 80, 45, "POS", current_ctrl == CTRL_VERTICAL_POS, COLOR_BLUE);

    draw_button(710, 60, 80, 45, osc->running ? "RUN" : "HOLD",
                true, osc->running ? COLOR_GREEN : COLOR_YELLOW);
    draw_button(710, 115, 80, 45,
                osc->trigger_slope == OSC_TRIGGER_RISING ? "RISE" : "FALL",
                true, COLOR_RED);
    draw_button(710, 170, 80, 45, "SINGLE", false, COLOR_WHITE);
    draw_button(710, 5, 80, 40, "HOME", false, COLOR_WHITE);

    if (osc->frame_valid) {
        if (osc->frequency_hz >= 1000.0f) {
            format_fixed(value, sizeof(value), osc->frequency_hz / 1000.0f, 2U);
            (void)snprintf(text, sizeof(text), "F:%skHz", value);
        } else {
            (void)snprintf(text, sizeof(text), "F:%luHz",
                           (unsigned long)(osc->frequency_hz + 0.5f));
        }
        draw_text(110, 442, text, COLOR_GREEN, 2U);

        format_fixed(value, sizeof(value), osc->vpp_v, 2U);
        (void)snprintf(text, sizeof(text), "VPP:%sV", value);
        draw_text(300, 442, text, COLOR_YELLOW, 2U);

        format_fixed(value, sizeof(value), osc->average_v, 2U);
        (void)snprintf(text, sizeof(text), "AVG:%sV", value);
        draw_text(490, 442, text, COLOR_CYAN, 2U);
    } else {
        draw_text(280, 442, "WAITING FOR ADC", COLOR_YELLOW, 2U);
    }
}

void Draw_Waveform(void)
{
    const OscilloscopeState *osc = Oscilloscope_GetState();
    if (!osc->frame_valid) return;

    uint16_t count = 0U;
    const uint16_t *trace = Oscilloscope_GetTrace(&count);
    const float pixels_per_volt = ((float)PLOT_H / 8.0f) / osc->volts_per_div;
    int previous_y = PLOT_Y + PLOT_H / 2;

    int trigger_y = PLOT_Y + PLOT_H / 2 -
                    (int)((osc->trigger_level_v - osc->vertical_center_v) *
                          pixels_per_volt);
    if (trigger_y >= PLOT_Y && trigger_y <= PLOT_Y + PLOT_H) {
        for (int x = PLOT_X; x < PLOT_X + PLOT_W; x += 8) {
            Draw_Line_L8(x, trigger_y, x + 3, trigger_y, COLOR_RED);
        }
    }

    for (uint16_t i = 0U; i < count; ++i) {
        const float volts = (float)trace[i] * OSC_INPUT_FULL_V / (float)OSC_ADC_MAX;
        int y = PLOT_Y + PLOT_H / 2 -
                (int)((volts - osc->vertical_center_v) * pixels_per_volt);
        if (y < PLOT_Y) y = PLOT_Y;
        if (y > PLOT_Y + PLOT_H) y = PLOT_Y + PLOT_H;
        if (i > 0U) {
            Draw_Line_L8(PLOT_X + (int)i - 1, previous_y,
                         PLOT_X + (int)i, y, COLOR_YELLOW);
        }
        previous_y = y;
    }
}

static void icon_sine(uint16_t x, uint16_t y, uint8_t color)
{
    for (int i = 5; i < 55; ++i) {
        int py = y + 15 - (int)(10.0f * sinf((float)(i - 5) * 0.15f));
        Draw_Line_L8(x + i, py, x + i, py, color);
    }
}

static void icon_square(uint16_t x, uint16_t y, uint8_t color)
{
    Draw_Line_L8(x + 8, y + 22, x + 28, y + 22, color);
    Draw_Line_L8(x + 28, y + 22, x + 28, y + 8, color);
    Draw_Line_L8(x + 28, y + 8, x + 52, y + 8, color);
}

static void icon_triangle(uint16_t x, uint16_t y, uint8_t color)
{
    Draw_Line_L8(x + 8, y + 23, x + 30, y + 7, color);
    Draw_Line_L8(x + 30, y + 7, x + 52, y + 23, color);
}

void Draw_Main_Menu(void)
{
    clear_screen();
    draw_text(182, 45, "H745 OSCILLOSCOPE + GENERATOR", COLOR_CYAN, 2U);
    draw_button(130, 130, 250, 220, "OSC", main_menu_sel == 0U, COLOR_GREEN);
    draw_button(420, 130, 250, 220, "GEN", main_menu_sel == 1U, COLOR_BLUE);
    draw_text(210, 375, "TOUCH A MODE", COLOR_WHITE, 3U);
}

void Draw_WaveGen_UI(void)
{
    clear_screen();
    draw_text(8, 8, "WAVE GENERATOR", COLOR_BLUE, 2U);
    draw_button(710, 5, 80, 40, "HOME", false, COLOR_WHITE);

    Draw_UI_Button(20, 70, wg_wave == WAVE_SINE, icon_sine);
    Draw_UI_Button(20, 120, wg_wave == WAVE_SQUARE, icon_square);
    Draw_UI_Button(20, 170, wg_wave == WAVE_TRIANGLE, icon_triangle);

    draw_button(180, 90, 500, 120, "FREQUENCY", wg_ctrl == 0U, COLOR_BLUE);
    draw_button(180, 260, 500, 120, "AMPLITUDE", wg_ctrl == 1U, COLOR_RED);

    char text[32];
    char value[16];
    (void)snprintf(text, sizeof(text), "%lu Hz", (unsigned long)wg_freq);
    draw_text(335, 165, text, wg_ctrl == 0U ? COLOR_BLUE : COLOR_WHITE, 3U);
    format_fixed(value, sizeof(value), wg_amp, 2U);
    (void)snprintf(text, sizeof(text), "%s V", value);
    draw_text(360, 335, text, wg_ctrl == 1U ? COLOR_RED : COLOR_WHITE, 3U);
    draw_text(215, 425, "DAC1 OUT2: PA5", COLOR_YELLOW, 2U);
}

void Play_Cyber_Boot_Sequence(void)
{
    clear_screen();
    draw_text(245, 170, "H745 OSC-GEN", COLOR_CYAN, 4U);
    draw_rect(180, 260, 440, 24, COLOR_WHITE);
    for (int width = 0; width <= 436; width += 12) {
        fill_rect(182, 262, width, 20, COLOR_BLUE);
        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
            SCB_CleanDCache_by_Addr((uint32_t *)FB_ADDR, SCREEN_W * SCREEN_H);
        }
        HAL_Delay(5U);
    }
}
