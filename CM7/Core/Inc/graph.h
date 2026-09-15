#ifndef GRAPH_H
#define GRAPH_H

#include "main.h"
#include <stdbool.h>

typedef enum {
    SYS_MAIN_MENU = 0,
    SYS_OSC,
    SYS_GEN,
    SYS_ABOUT
} SystemState;

#define UI_VERSION "v3.2"
typedef enum { UI_THEME_GRAPHITE, UI_THEME_MIDNIGHT, UI_THEME_IVORY, UI_THEME_COUNT } UiTheme;
UiTheme UI_GetTheme(void);
void UI_SelectTheme(UiTheme theme);

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_TRIANGLE
} WaveType;

typedef enum {
    CTRL_TIMEBASE = 0,
    CTRL_VOLTS_DIV,
    CTRL_TRIGGER_LEVEL,
    CTRL_VERTICAL_POS
} ControlMode;

extern volatile SystemState current_sys_state;
extern volatile uint8_t main_menu_sel;
extern volatile ControlMode current_ctrl;

extern float wg_freq;
extern float wg_amp;
extern volatile uint8_t wg_ctrl;
extern volatile uint8_t wg_enabled;
extern volatile WaveType wg_wave;
extern uint32_t my_palette[256];

typedef enum {
    UI_NONE, UI_HOME, UI_OSC, UI_GEN, UI_TIME, UI_VOLTS, UI_TRIGGER,
    UI_POSITION, UI_RUN, UI_SLOPE, UI_SINGLE, UI_COARSE, UI_FINE, UI_SINE, UI_SQUARE,
    UI_TRIANGLE, UI_OUTPUT, UI_FREQUENCY, UI_AMPLITUDE, UI_MOTION,
    UI_ABOUT, UI_THEME_DARK, UI_THEME_BLUE, UI_THEME_LIGHT, UI_ACTION_COUNT
} UiAction;

typedef struct { int16_t x, y, w, h; } UiRect;
UiRect UI_GetRect(UiAction action);
UiAction UI_HitTest(SystemState screen, uint16_t x, uint16_t y);
void UI_InitPalette(void);
void UI_NotifyTouch(UiAction action, uint32_t now);
void UI_NotifyReset(uint32_t now);
void UI_ToggleMotion(void);
/* Output gestures only preview position until a valid release commits. */
void UI_OutputTouchDown(uint16_t x, uint16_t y, uint32_t now);
bool UI_OutputTouchMove(uint16_t x, uint16_t y, uint32_t now);
bool UI_OutputTouchRelease(uint32_t now);
void UI_OutputTouchCancel(uint32_t now);
bool UI_Tick(uint32_t now);
void UI_Render(void);
void UI_SetRenderBuffers(uint8_t *top, uint8_t *bottom, uint16_t bottom_pitch);

void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color_index);
void Draw_Grid_And_Axes(void);
void Draw_UI_Button(uint16_t x, uint16_t y, uint8_t is_selected,
                    void (*DrawIcon)(uint16_t, uint16_t, uint8_t));
void Draw_Main_Menu(void);
void Draw_About(void);
void Draw_Oscilloscope_UI(void);
void Draw_Waveform(void);
void Draw_WaveGen_UI(void);
void Play_Cyber_Boot_Sequence(void);

#endif /* GRAPH_H */
