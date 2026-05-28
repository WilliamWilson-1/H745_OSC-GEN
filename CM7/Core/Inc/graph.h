#ifndef NUCLEO_1_CM7_GRAPH_H
#define NUCLEO_1_CM7_GRAPH_H

#include "main.h"

// --- 系统三大核心状态 ---
typedef enum { SYS_MAIN_MENU = 0, SYS_OSC, SYS_GEN } SystemState;

// --- 枚举与控制模式 ---
typedef enum { WAVE_SINE = 0, WAVE_SQUARE, WAVE_TRIANGLE } WaveType;
typedef enum { CTRL_FREQ = 0, CTRL_PHASE, CTRL_AMPLITUDE, CTRL_OFFSET_Y } ControlMode;

// --- 全局状态机变量 ---
extern SystemState current_sys_state;
extern uint8_t main_menu_sel; // 0: 示波器, 1: 波形发生器

// --- 示波器参数 ---
extern WaveType current_wave;
extern ControlMode current_ctrl;
extern float current_omega;
extern float current_phase;
extern float current_amplitude;
extern int   current_offset_y;

// --- 波形发生器参数 ---
extern float wg_freq;   // 频率 (50 - 10000 Hz)
extern float wg_amp;    // 幅值 (0.0 - 3.3 V)
extern uint8_t wg_ctrl; // 0: 控制频率, 1: 控制幅值
extern WaveType wg_wave;// 发生的波形类型

extern uint32_t my_palette[256];

// --- 绘图 API ---
void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color_index);
void Draw_Grid_And_Axes(void);
void Draw_UI_Button(uint16_t x, uint16_t y, uint8_t is_selected, void (*DrawIcon)(uint16_t, uint16_t, uint8_t));

void Draw_Main_Menu(void);
void Draw_Oscilloscope_UI(void);
void Draw_Waveform(void);
void Draw_WaveGen_UI(void);

void Play_Cyber_Boot_Sequence(void);

#endif //NUCLEO_1_CM7_GRAPH_H