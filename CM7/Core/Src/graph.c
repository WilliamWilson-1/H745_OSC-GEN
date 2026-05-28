// graph.c
#include <stdlib.h>
#include <math.h>
#include "main.h"
#include "graph.h"

#include <string.h>

// 增加 M_PI 的防御性定义，防止 GCC 报错
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 1. 全局变量定义和初始化
SystemState current_sys_state = SYS_MAIN_MENU;
uint8_t main_menu_sel = 0;

WaveType current_wave = WAVE_SINE;
ControlMode current_ctrl = CTRL_FREQ;

float current_omega     = 0.05f;
float current_phase     = 0.0f;
float current_amplitude = 100.0f;
int   current_offset_y  = 240;

float wg_freq = 1000.0f; // 默认 1kHz
float wg_amp  = 3.3f;    // 默认 3.3V
uint8_t wg_ctrl = 0;
WaveType wg_wave = WAVE_SINE;
uint32_t my_palette[256] = {0};

// ==========================================================
// 内部函数声明区 (用 static 隐藏这些专用于 graph.c 内部的函数)
// ==========================================================
static void Icon_Sine(uint16_t x, uint16_t y, uint8_t c);
static void Icon_Square(uint16_t x, uint16_t y, uint8_t c);
static void Icon_Triangle(uint16_t x, uint16_t y, uint8_t c);
static void Icon_Freq(uint16_t x, uint16_t y, uint8_t c);
static void Icon_Phase(uint16_t x, uint16_t y, uint8_t c);
static void Icon_Amp(uint16_t x, uint16_t y, uint8_t c);
static void Icon_OffsetY(uint16_t x, uint16_t y, uint8_t c);

// ==========================================================
// 绘图引擎核心代码
// ==========================================================

void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color_index) {
    uint8_t *fb = (uint8_t *)0x24020000;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        if (x1 >= 0 && x1 < 800 && y1 >= 0 && y1 < 480) fb[y1 * 800 + x1] = color_index;
        if (x1 == x2 && y1 == y2) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x1 += sx; }
        if (e2 <  dy) { err += dx; y1 += sy; }
    }
}


void Draw_Grid_And_Axes(void) {
    uint8_t *fb = (uint8_t *)0x24020000;
    for(uint32_t i = 0; i < 800 * 480; i++) fb[i] = 0;
    for(int y = 0; y < 480; y += 50) for(int x = 0; x < 800; x++) fb[y * 800 + x] = 6;
    for(int x = 0; x < 800; x += 50) for(int y = 0; y < 480; y++) fb[y * 800 + x] = 6;
    for(int x = 0; x < 800; x++) fb[240 * 800 + x] = 4;
    for(int y = 0; y < 480; y++) fb[y * 800 + 400] = 4;
}

void Draw_UI_Button(uint16_t x, uint16_t y, uint8_t is_selected, void (*DrawIcon)(uint16_t, uint16_t, uint8_t)) {
    uint8_t color = is_selected ? 3 : 4;
    Draw_Line_L8(x, y, x + 60, y, color);
    Draw_Line_L8(x, y + 30, x + 60, y + 30, color);
    Draw_Line_L8(x, y, x, y + 30, color);
    Draw_Line_L8(x + 60, y, x + 60, y + 30, color);
    if(DrawIcon) DrawIcon(x, y, color);
}

// --- 以下所有图标绘制函数都加上 static ---
static void Icon_Sine(uint16_t x, uint16_t y, uint8_t c) {
    for (int i=5; i<55; i++) Draw_Line_L8(x+i, y+15-(int)(10*sin((i-5)*0.15f)), x+i, y+15-(int)(10*sin((i-5)*0.15f)), c);
}
static void Icon_Square(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+10, y+23, x+30, y+23, c); Draw_Line_L8(x+30, y+23, x+30, y+7, c); Draw_Line_L8(x+30, y+7, x+50, y+7, c);
}
static void Icon_Triangle(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+10, y+23, x+30, y+7, c); Draw_Line_L8(x+30, y+7, x+50, y+23, c);
}

static void Icon_Freq(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+20, y+8, x+20, y+22, c); Draw_Line_L8(x+30, y+8, x+30, y+22, c); Draw_Line_L8(x+40, y+8, x+40, y+22, c);
}
static void Icon_Phase(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+10, y+15, x+50, y+15, c); Draw_Line_L8(x+10, y+15, x+15, y+10, c); Draw_Line_L8(x+50, y+15, x+45, y+20, c);
}
static void Icon_Amp(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+30, y+5, x+30, y+25, c); Draw_Line_L8(x+30, y+5, x+25, y+10, c); Draw_Line_L8(x+30, y+25, x+35, y+20, c);
}
static void Icon_OffsetY(uint16_t x, uint16_t y, uint8_t c) {
    Draw_Line_L8(x+10, y+20, x+50, y+20, c); Draw_Line_L8(x+30, y+5, x+30, y+15, c); Draw_Line_L8(x+30, y+5, x+25, y+10, c);
}

// ----------------------------------------------------------

void Draw_Oscilloscope_UI(void) {
    Draw_Grid_And_Axes();
    // 右侧菜单
    Draw_UI_Button(720, 50,  (current_wave == WAVE_SINE), Icon_Sine);
    Draw_UI_Button(720, 100, (current_wave == WAVE_SQUARE), Icon_Square);
    Draw_UI_Button(720, 150, (current_wave == WAVE_TRIANGLE), Icon_Triangle);
    // 左侧菜单
    Draw_UI_Button(20, 50,  (current_ctrl == CTRL_FREQ), Icon_Freq);
    Draw_UI_Button(20, 100, (current_ctrl == CTRL_PHASE), Icon_Phase);
    Draw_UI_Button(20, 150, (current_ctrl == CTRL_AMPLITUDE), Icon_Amp);
    Draw_UI_Button(20, 200, (current_ctrl == CTRL_OFFSET_Y), Icon_OffsetY);
}

void Draw_Waveform(void) {
    int prev_x = 0;
    int prev_y = current_offset_y;

    for (int x = 0; x < 800; x++) {
        float math_x = (float)(x - 400);
        float math_y = 0;

        if (current_wave == WAVE_SINE) {
            math_y = current_amplitude * sin(math_x * current_omega + current_phase);
        }
        else if (current_wave == WAVE_SQUARE) {
            math_y = (sin(math_x * current_omega + current_phase) >= 0) ? current_amplitude : -current_amplitude;
        }
        else if (current_wave == WAVE_TRIANGLE) {
            math_y = (current_amplitude * 2.0f / M_PI) * asinf(sinf(math_x * current_omega + current_phase));
        }

        int y = current_offset_y - (int)math_y;
        if (x > 0) Draw_Line_L8(prev_x, prev_y, x, y, 5);
        prev_x = x;
        prev_y = y;
    }
}

// ==========================================================
// 极客专属：算法生成七段数码管数字和单位字符
// ==========================================================
void Draw_7Seg(int x, int y, int val, uint8_t c, int s) {
    uint8_t segs[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    if(val<0 || val>9) return;
    uint8_t mask = segs[val];
    // 粗犷线条，用两条线并排增加厚度
    if(mask & 1) { Draw_Line_L8(x, y, x+s, y, c); Draw_Line_L8(x, y+1, x+s, y+1, c); }
    if(mask & 2) { Draw_Line_L8(x+s, y, x+s, y+s, c); Draw_Line_L8(x+s-1, y, x+s-1, y+s, c); }
    if(mask & 4) { Draw_Line_L8(x+s, y+s, x+s, y+2*s, c); Draw_Line_L8(x+s-1, y+s, x+s-1, y+2*s, c); }
    if(mask & 8) { Draw_Line_L8(x, y+2*s, x+s, y+2*s, c); Draw_Line_L8(x, y+2*s-1, x+s, y+2*s-1, c); }
    if(mask & 16){ Draw_Line_L8(x, y+s, x, y+2*s, c); Draw_Line_L8(x+1, y+s, x+1, y+2*s, c); }
    if(mask & 32){ Draw_Line_L8(x, y, x, y+s, c); Draw_Line_L8(x+1, y, x+1, y+s, c); }
    if(mask & 64){ Draw_Line_L8(x, y+s, x+s, y+s, c); Draw_Line_L8(x, y+s-1, x+s, y+s-1, c); }
}

void Draw_Number(int x, int y, int num, uint8_t c, int s) {
    if(num == 0) { Draw_7Seg(x, y, 0, c, s); return; }
    int digits[10], cnt=0;

    while(num > 0) { digits[cnt++] = num%10; num/=10; }

    for(int i = 0; i < cnt; i++) {
        // i=0 时，画 digits[cnt-1] (最高位)，偏移 0*(s+8)
        // i 越大，画的位数越低，向右的偏移量越大
        Draw_7Seg(x + i * (s + 8), y, digits[cnt - 1 - i], c, s);
    }
}

void Draw_Text_Hz(int x, int y, uint8_t c, int s) {
    Draw_Line_L8(x, y, x, y+2*s, c); Draw_Line_L8(x+s, y, x+s, y+2*s, c); Draw_Line_L8(x, y+s, x+s, y+s, c); // H
    Draw_Line_L8(x+s+6, y+s, x+2*s+6, y+s, c); Draw_Line_L8(x+2*s+6, y+s, x+s+6, y+2*s, c); Draw_Line_L8(x+s+6, y+2*s, x+2*s+6, y+2*s, c); // z
}

void Draw_Text_V(int x, int y, uint8_t c, int s) {
    Draw_Line_L8(x, y, x+s/2, y+2*s, c); Draw_Line_L8(x+s/2, y+2*s, x+s, y, c);
}

void Draw_Dot(int x, int y, uint8_t c, int s) {
    Draw_Line_L8(x, y+2*s, x+2, y+2*s, c); Draw_Line_L8(x, y+2*s-1, x+2, y+2*s-1, c);
}

void Draw_Rect(int x, int y, int w, int h, uint8_t c) {
    Draw_Line_L8(x, y, x+w, y, c); Draw_Line_L8(x, y+h, x+w, y+h, c);
    Draw_Line_L8(x, y, x, y+h, c); Draw_Line_L8(x+w, y, x+w, y+h, c);
}

// ==========================================================
// 菜单与 UI 绘制
// ==========================================================
// --- 内部函数：绘制字母 O-S-C ---
static void Draw_Text_OSC(uint16_t x, uint16_t y, uint8_t c, int s) {
    // O
    Draw_Rect(x, y, s, 2 * s, c);
    // S
    //Draw_7Seg(x + s + 10, y, 5, c, s);
    Draw_Line_L8(x + s + 10, y, x + 2 * s + 10, y, c);                  // 上
    Draw_Line_L8(x + s + 10, y, x + s + 10, y + s, c);                  // 左
    Draw_Line_L8(x + s + 10, y + s, x + 2 * s + 10, y + s, c);          // 中
    Draw_Line_L8(x + 2 * s + 10, y + s, x + 2 * s + 10, y + 2 * s, c);  // 右
    Draw_Line_L8(x + s + 10, y + 2 * s, x + 2 * s + 10, y + 2 * s, c);  // 下
    // C
    Draw_Line_L8(x + 2 * s + 20, y, x + 3 * s + 20, y, c);         // 上
    Draw_Line_L8(x + 2 * s + 20, y, x + 2 * s + 20, y + 2 * s, c); // 左
    Draw_Line_L8(x + 2 * s + 20, y + 2 * s, x + 3 * s + 20, y + 2 * s, c); // 下
}

// --- 内部函数：绘制字母 G-E-N ---
static void Draw_Text_GEN(uint16_t x, uint16_t y, uint8_t c, int s) {
    // G (在 C 的基础上加一横)
    Draw_Line_L8(x, y, x + s, y, c);         // 上
    Draw_Line_L8(x, y, x, y + 2 * s, c);     // 左
    Draw_Line_L8(x, y + 2 * s, x + s, y + 2 * s, c); // 下
    Draw_Line_L8(x + s, y + s, x + s, y + 2 * s, c); // 右下
    Draw_Line_L8(x + s/2, y + s, x + s, y + s, c);   // 中间一横

    // E (三横一竖)
    int x_e = x + s + 10;
    Draw_Line_L8(x_e, y, x_e, y + 2 * s, c);
    Draw_Line_L8(x_e, y, x_e + s, y, c);
    Draw_Line_L8(x_e, y + s, x_e + s, y + s, c);
    Draw_Line_L8(x_e, y + 2 * s, x_e + s, y + 2 * s, c);

    // N (两竖一斜)
    int x_n = x + 2 * s + 20;
    Draw_Line_L8(x_n, y, x_n, y + 2 * s, c);
    Draw_Line_L8(x_n + s, y, x_n + s, y + 2 * s, c);
    Draw_Line_L8(x_n, y, x_n + s, y + 2 * s, c);
}

// 1. 顶层主菜单
// graph.c

void Draw_Main_Menu(void) {
    uint8_t *fb = (uint8_t *)0x24020000;
    for(uint32_t i = 0; i < 800 * 480; i++) fb[i] = 0; // 清屏

    // --- 左侧：OSCILLOSCOPE 模式 ---
    uint8_t c_osc = (main_menu_sel == 0) ? 1 : 4;
    Draw_Rect(150, 140, 200, 200, c_osc);
    // 在框内绘制 "OSC"，s=40 为字母大小，位置居中微调
    Draw_Text_OSC(185, 200, c_osc, 40);

    // --- 右侧：GENERATOR 模式 ---
    uint8_t c_gen = (main_menu_sel == 1) ? 3 : 4;
    Draw_Rect(450, 140, 200, 200, c_gen);
    // 在框内绘制 "GEN"
    Draw_Text_GEN(485, 200, c_gen, 40);
}

// 2. 示波器 UI (保持不变)


// 3. 波形发生器 UI
void Draw_WaveGen_UI(void) {
    uint8_t *fb = (uint8_t *)0x24020000;
    for(uint32_t i = 0; i < 800 * 480; i++) fb[i] = 0; // 波形发生界面保持纯黑背景，更专业

    // 左侧：复用小矩形显示当前输出的波形
    Draw_UI_Button(20, 50,  (wg_wave == WAVE_SINE), Icon_Sine);
    Draw_UI_Button(20, 100, (wg_wave == WAVE_SQUARE), Icon_Square);
    Draw_UI_Button(20, 150, (wg_wave == WAVE_TRIANGLE), Icon_Triangle);

    // 右侧：大方框控制频率和幅值
    uint8_t c_freq = (wg_ctrl == 0) ? 3 : 4; // 频率框颜色
    uint8_t c_amp  = (wg_ctrl == 1) ? 1 : 4; // 幅值框颜色

    Draw_Rect(300, 100, 400, 100, c_freq);
    Draw_Number(350, 130, (int)wg_freq, c_freq, 20);
    Draw_Text_Hz(600, 130, c_freq, 20);

    Draw_Rect(300, 250, 400, 100, c_amp);
    int a_int = (int)wg_amp;
    int a_frac = (int)((wg_amp - a_int) * 10);
    Draw_Number(350, 280, a_int, c_amp, 20);
    Draw_Dot(400, 280, c_amp, 20);
    Draw_Number(430, 280, a_frac, c_amp, 20);
    Draw_Text_V(500, 280, c_amp, 20);
}

#define FB_ADDR 0x24020000
#define WIDTH 800
#define HEIGHT 480

void Draw_Block(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    uint8_t* fb = (uint8_t*)FB_ADDR;
    for(int i = 0; i < h; i++) {
        if (y + i >= HEIGHT) break;
        memset(fb + (y + i) * WIDTH + x, color, w);
    }
}

// 辅助函数：绘制单像素点
void Draw_Pixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x < WIDTH && y < HEIGHT) {
        *((uint8_t*)(FB_ADDR + y * WIDTH + x)) = color;
    }
}

// 💥 终极炫酷开机动画：Cyber Boot Sequence
void Play_Cyber_Boot_Sequence(void) {
    uint8_t* fb = (uint8_t*)FB_ADDR;

    // 清空屏幕为纯黑
    memset(fb, 0, WIDTH * HEIGHT);
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
    }
    HAL_Delay(200);

    // ==========================================
    // Phase 1: Grid Initialization (底层网格扫描)
    // ==========================================
    for (int y = 0; y < HEIGHT; y += 8) {
        memset(fb, 0, WIDTH * HEIGHT); // 刷黑

        // 绘制静态背景网格点
        for (int gy = 0; gy < y; gy += 40) {
            for (int gx = 0; gx < WIDTH; gx += 40) {
                Draw_Pixel(gx, gy, 2); // 假设 2 是暗绿色
            }
        }

        // 绘制正在扫描的高亮横线
        memset(fb + y * WIDTH, 3, WIDTH); // 假设 3 是亮蓝色
        memset(fb + (y+1) * WIDTH, 3, WIDTH);

        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
            SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
        }
        HAL_Delay(10); // 速度控制
    }

    // ==========================================
    // Phase 2: Cluster Diagnostics (集群算力并发检测)
    // ==========================================
    // 屏幕中央出现高速随机跳动的色块，模拟内核加载
    int center_x = WIDTH / 2;
    int center_y = HEIGHT / 2;

    for (int frames = 0; frames < 40; frames++) {
        memset(fb, 0, WIDTH * HEIGHT); // 刷黑

        for(int block = 0; block < 15; block++) {
            // 利用简单的线性同余生成伪随机数，极速运算
            uint16_t rw = (frames * block * 17) % 100 + 10;
            uint16_t rh = (frames * block * 23) % 20 + 5;
            uint16_t rx = center_x - 200 + ((frames * block * 31) % 400);
            uint16_t ry = center_y - 50 + ((frames * block * 37) % 100);
            uint8_t rcol = (frames * block % 3) + 3; // 随机取颜色索引 3,4,5

            Draw_Block(rx, ry, rw, rh, rcol);
        }

        // 中央主干进度条
        Draw_Block(center_x - (frames * 5), center_y + 100, frames * 10, 4, 1); // 假设 1 是纯白

        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
            SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
        }
        HAL_Delay(15);
    }

    // ==========================================
    // Phase 3: Geometric Sonar Sweep (雷达波纹演算)
    // 动用 M7 的 FPU 进行极速浮点画圆演算
    // ==========================================
    for (int radius = 10; radius < 450; radius += 15) {
        memset(fb, 0, WIDTH * HEIGHT);

        // 画多个几何圆环
        for (int r = radius; r > 0; r -= 40) {
            for (float angle = 0; angle < 6.28f; angle += 0.05f) { // 极速弧度遍历
                uint16_t x = center_x + (uint16_t)(r * cosf(angle));
                uint16_t y = center_y + (uint16_t)(r * sinf(angle));
                Draw_Pixel(x, y, 4); // 假设 4 是青色

                // 加粗外圈
                if (r == radius) {
                    Draw_Pixel(x+1, y, 4);
                    Draw_Pixel(x, y+1, 4);
                }
            }
        }

        // 核心十字准星
        Draw_Block(center_x - 50, center_y, 100, 2, 1);
        Draw_Block(center_x, center_y - 50, 2, 100, 1);

        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
            SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
        }
        HAL_Delay(15);
    }

    // ==========================================
    // Phase 4: Hyperspace Clear (超空间跃迁)
    // ==========================================
    // 屏幕从中间向上下拉开，完美过渡到你的正常界面
    for (int gap = 0; gap <= HEIGHT / 2; gap += 10) {
        memset(fb, 0, WIDTH * HEIGHT);

        // 留下一丝极具速度感的残影横线
        memset(fb + (center_y - gap) * WIDTH, 1, WIDTH);
        memset(fb + (center_y + gap) * WIDTH, 1, WIDTH);

        if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
            SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
        }
        HAL_Delay(10);
    }

    // 确保最后交给 UI 渲染前，显存是纯净的
    memset(fb, 0, WIDTH * HEIGHT);
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_CleanDCache_by_Addr((uint32_t*)fb, WIDTH * HEIGHT);
    }
}