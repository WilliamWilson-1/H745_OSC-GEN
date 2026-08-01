#ifndef GRAPH_H
#define GRAPH_H

#include "main.h"

typedef enum {
    SYS_MAIN_MENU = 0,
    SYS_OSC,
    SYS_GEN
} SystemState;

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
extern volatile WaveType wg_wave;
extern uint32_t my_palette[256];

void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color_index);
void Draw_Grid_And_Axes(void);
void Draw_UI_Button(uint16_t x, uint16_t y, uint8_t is_selected,
                    void (*DrawIcon)(uint16_t, uint16_t, uint8_t));
void Draw_Main_Menu(void);
void Draw_Oscilloscope_UI(void);
void Draw_Waveform(void);
void Draw_WaveGen_UI(void);
void Play_Cyber_Boot_Sequence(void);

#endif /* GRAPH_H */
