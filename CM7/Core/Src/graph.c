#include "graph.h"
#include "oscilloscope.h"
#include "ui_font_data.h"
#include "display.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 800
#define SCREEN_H 480
#define PLOT_X 100
#define PLOT_Y 60
#define PLOT_W 600
#define PLOT_H 360
enum { BG, RED, GREEN, BLUE, WHITE, YELLOW, GRID, CYAN,
       PANEL, BORDER, MUTED, SELECTED, RAISED, TINT, PLOT, DIM };
enum { SMALL, BODY, TITLE, NUMBER };

volatile SystemState current_sys_state = SYS_MAIN_MENU;
volatile uint8_t main_menu_sel = 0U;
volatile ControlMode current_ctrl = CTRL_TIMEBASE;
float wg_freq = 1000.0f;
float wg_amp = 3.3f;
volatile uint8_t wg_ctrl = 0U;
volatile uint8_t wg_enabled = 0U;
volatile WaveType wg_wave = WAVE_SINE;
uint32_t my_palette[256];
static uint8_t text_blend[16][16][4];

/* One geometry source for both rendering and touch, including 48px targets. */
static const UiRect rects[UI_ACTION_COUNT] = {
    [UI_HOME] = {708, 6, 84, 48},
    [UI_OSC] = {32, 126, 360, 264}, [UI_GEN] = {408, 126, 360, 264},
    [UI_TIME] = {8, 64, 84, 48}, [UI_VOLTS] = {8, 120, 84, 48},
    [UI_TRIGGER] = {8, 176, 84, 48}, [UI_POSITION] = {8, 232, 84, 48},
    [UI_RUN] = {708, 64, 84, 48}, [UI_SLOPE] = {708, 120, 84, 48},
    [UI_SINGLE] = {708, 176, 84, 48},
    [UI_COARSE] = {708, 232, 84, 48}, [UI_FINE] = {708, 288, 84, 48},
    [UI_SINE] = {8, 72, 88, 56}, [UI_SQUARE] = {8, 136, 88, 56},
    [UI_TRIANGLE] = {8, 200, 88, 56}, [UI_OUTPUT] = {688, 381, 72, 48},
    [UI_FREQUENCY] = {112, 72, 328, 144}, [UI_AMPLITUDE] = {456, 72, 328, 144},
    [UI_MOTION] = {584, 420, 184, 48}
};

static SystemState motion_screen = SYS_MAIN_MENU;
static UiAction pressed = UI_NONE;
static uint32_t clock_ms, press_start, page_start, toggle_start, reset_start;
static bool page_active, press_active, toggle_active, reset_visible;
static bool motion_enabled = true;
static uint8_t output_target;
static float toggle_from, toggle_position;
static bool output_captured, output_dragged, toggle_spring;
static int output_down_x, output_down_y;
static float output_grab_position, output_velocity, toggle_velocity;
static uint32_t output_move_time;

#ifdef UI_HOST_PREVIEW
extern uint8_t ui_preview_framebuffer[800 * 480];
static uint8_t *render_top = ui_preview_framebuffer;
static uint8_t *render_bottom = ui_preview_framebuffer + DISPLAY_TOP_BYTES;
#else
static uint8_t *render_top, *render_bottom;
#endif
static uint16_t render_bottom_pitch = SCREEN_W;

void UI_SetRenderBuffers(uint8_t *top, uint8_t *bottom, uint16_t bottom_pitch)
{
    render_top = top;
    render_bottom = bottom;
    render_bottom_pitch = bottom_pitch;
}

static uint8_t *row_pixels(unsigned y)
{
    return y < DISPLAY_SPLIT_Y ? render_top + y*SCREEN_W :
           render_bottom + (y-DISPLAY_SPLIT_Y)*render_bottom_pitch;
}

static void clear_frame(void)
{
    /* Only the back buffer is cleared. The visible frame remains untouched. */
    memset(render_top, BG, DISPLAY_TOP_BYTES);
    memset(render_bottom, BG, render_bottom_pitch*(SCREEN_H-DISPLAY_SPLIT_Y));
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, unsigned t, unsigned total)
{
    uint32_t result = 0xff000000U;
    for (unsigned shift = 0; shift <= 16; shift += 8) {
        unsigned c = (((a >> shift) & 255U) * (total-t) +
                      ((b >> shift) & 255U) * t) / total;
        result |= c << shift;
    }
    return result;
}

void UI_InitPalette(void)
{
    static const uint32_t base[16] = {
        0xff101419, 0xfff18b85, 0xff80dbb0, 0xff88baff,
        0xffedf1f5, 0xffeed28e, 0xff232e38, 0xff8fd4df,
        0xff1a2028, 0xff35414e, 0xffa0acba, 0xff283b50,
        0xff29323e, 0xff22392f, 0xff121920, 0xff607284
    };
    memcpy(my_palette, base, sizeof(base));
    /* 240 additional shades keep small type antialiased in the L8 buffer. */
    for (unsigned bg = 0; bg < 16; ++bg)
        for (unsigned t = 1; t <= 15; ++t)
            my_palette[16 + bg*15 + t-1] = mix_rgb(base[bg], base[WHITE], t, 16);
    for (unsigned fg = 0; fg < 16; ++fg) {
        for (unsigned bg = 0; bg < 16; ++bg) {
            text_blend[fg][bg][0] = bg;
            text_blend[fg][bg][3] = fg;
            for (unsigned a = 1; a < 3; ++a) {
                uint32_t color = mix_rgb(base[bg], base[fg], a, 3);
                unsigned best = 0, distance = 0xffffffffU;
                for (unsigned p = 0; p < 256; ++p) {
                    unsigned d = 0;
                    for (unsigned s = 0; s <= 16; s += 8) {
                        int delta = (int)((color >> s)&255) - (int)((my_palette[p] >> s)&255);
                        d += (unsigned)(delta*delta);
                    }
                    if (d < distance) { distance = d; best = p; }
                }
                text_blend[fg][bg][a] = (uint8_t)best;
            }
        }
    }
}

static void fill(int x, int y, int w, int h, uint8_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x+w > SCREEN_W) w = SCREEN_W-x;
    if (y+h > SCREEN_H) h = SCREEN_H-y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; ++r)
        memset(row_pixels((unsigned)(y+r))+x, color, (size_t)w);
}

static void rounded(int x, int y, int w, int h, int radius, uint8_t color)
{
    fill(x, y+radius, w, h-2*radius, color);
    for (int row = 0; row < radius; ++row) {
        int dy = radius-row-1, inset = radius;
        while (inset > 0 && (radius-inset)*(radius-inset)+dy*dy < radius*radius) --inset;
        fill(x+inset, y+row, w-2*inset, 1, color);
        fill(x+inset, y+h-row-1, w-2*inset, 1, color);
    }
}

static void panel(UiRect r, uint8_t bg, uint8_t edge)
{
    rounded(r.x,r.y,r.w,r.h,10,edge);
    rounded(r.x+1,r.y+1,r.w-2,r.h-2,9,bg);
}

void Draw_Line_L8(int x1, int y1, int x2, int y2, uint8_t color)
{
    int dx=abs(x2-x1), sx=x1<x2?1:-1;
    int dy=-abs(y2-y1), sy=y1<y2?1:-1, error=dx+dy;
    for (;;) {
        if (x1>=0 && x1<SCREEN_W && y1>=0 && y1<SCREEN_H)
            row_pixels((unsigned)y1)[x1]=color;
        if (x1==x2 && y1==y2) break;
        int twice=2*error;
        if (twice>=dy) { error+=dy; x1+=sx; }
        if (twice<=dx) { error+=dx; y1+=sy; }
    }
}

static int text_width(const char *text, unsigned font)
{
    int width=0;
    while (*text) {
        unsigned ch=(unsigned char)*text++;
        if (ch<32 || ch>126) ch='?';
        width+=ui_glyphs[ui_fonts[font].first+ch-32].advance;
    }
    return width;
}

static void text_at(int x, int y, const char *text, uint8_t fg, uint8_t bg, unsigned font)
{
    while (*text) {
        unsigned ch=(unsigned char)*text++;
        if (ch<32 || ch>126) ch='?';
        const UiGlyph *g=&ui_glyphs[ui_fonts[font].first+ch-32];
        for (unsigned row=0; row<ui_fonts[font].height; ++row) {
            for (unsigned col=0; col<g->width; ++col) {
                unsigned index=row*g->width+col;
                uint8_t coverage=(ui_font_pixels[g->offset+index/4] >> ((index%4)*2))&3;
                if (coverage) fill(x+(int)col,y+(int)row,1,1,text_blend[fg][bg][coverage]);
            }
        }
        x+=g->advance;
    }
}

static void fixed(char *out, size_t size, float value)
{
    bool negative=value<0;
    uint32_t v=(uint32_t)(fabsf(value)*100.0f+0.5f);
    snprintf(out,size,"%s%lu.%02lu",negative?"-":"",(unsigned long)(v/100),(unsigned long)(v%100));
}

static float ease_out(uint32_t elapsed, uint32_t duration)
{
    if (elapsed>=duration) return 1.0f;
    float t=1.0f-(float)elapsed/(float)duration;
    return 1.0f-t*t*t;
}

UiRect UI_GetRect(UiAction action)
{
    if (action<=UI_NONE || action>=UI_ACTION_COUNT) return rects[UI_NONE];
    return rects[action];
}

static bool contains(UiRect r, unsigned x, unsigned y)
{
    return x>=(unsigned)r.x && x<(unsigned)(r.x+r.w) &&
           y>=(unsigned)r.y && y<(unsigned)(r.y+r.h);
}

UiAction UI_HitTest(SystemState screen, uint16_t x, uint16_t y)
{
    if (x>=SCREEN_W || y>=SCREEN_H) return UI_NONE;
    for (UiAction a=UI_HOME; a<UI_ACTION_COUNT; ++a) {
        bool enabled=(screen!=SYS_MAIN_MENU && a==UI_HOME) ||
            (screen==SYS_MAIN_MENU && (a==UI_OSC || a==UI_GEN || a==UI_MOTION)) ||
            (screen==SYS_OSC && a>=UI_TIME && a<=UI_FINE) ||
            (screen==SYS_GEN && a>=UI_SINE && a<=UI_AMPLITUDE);
        if (enabled && contains(rects[a],x,y)) return a;
    }
    return UI_NONE;
}

void UI_NotifyTouch(UiAction action, uint32_t now)
{
    pressed=action; press_start=now;
    press_active=motion_enabled && action!=UI_NONE;
}

static float unit_clamp(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

static void output_settle(uint32_t now, bool spring, float velocity)
{
    toggle_from=toggle_position;
    toggle_start=now;
    output_target=wg_enabled;
    toggle_spring=spring;
    toggle_velocity=velocity;
    toggle_active=motion_enabled;
    if (!motion_enabled) toggle_position=(float)output_target;
}

void UI_OutputTouchCancel(uint32_t now)
{
    if (!output_captured) return;
    output_captured=false;
    output_settle(now,true,0.0f);
}

void UI_OutputTouchDown(uint16_t x, uint16_t y, uint32_t now)
{
    UI_OutputTouchCancel(now);
    if (current_sys_state!=SYS_GEN || !contains(rects[UI_OUTPUT],x,y)) return;
    output_captured=true;
    output_dragged=false;
    output_down_x=x; output_down_y=y;
    output_grab_position=toggle_position;
    output_velocity=0.0f; output_move_time=now;
    toggle_active=false;
}

bool UI_OutputTouchMove(uint16_t x, uint16_t y, uint32_t now)
{
    if (!output_captured) return false;
    float previous=toggle_position;
    int dx=(int)x-output_down_x, dy=(int)y-output_down_y;
    /* Vertical escape cancels, even after horizontal capture. */
    if (current_sys_state!=SYS_GEN || abs(dy)>24 ||
        (!output_dragged && abs(dy)>=10 && abs(dy)>abs(dx))) {
        UI_OutputTouchCancel(now);
        return true;
    }
    if (abs(dx)>=10) output_dragged=true;
    if (output_dragged) {
        float next=unit_clamp(output_grab_position+(float)dx/40.0f);
        uint32_t elapsed=now-output_move_time;
        if (elapsed) output_velocity=(next-toggle_position)*1000.0f/(float)elapsed;
        toggle_position=next; /* Direct manipulation: no easing or delay. */
    }
    output_move_time=now;
    return toggle_position!=previous;
}

bool UI_OutputTouchRelease(uint32_t now)
{
    if (!output_captured) return false;
    output_captured=false;
    if (current_sys_state!=SYS_GEN) {
        output_settle(now,true,0.0f);
        return false;
    }
    uint8_t old=wg_enabled;
    /* Position decides equipment state; a flick alone cannot energize PA5. */
    if (output_dragged) {
        if (toggle_position>0.55f) wg_enabled=1U;
        else if (toggle_position<0.45f) wg_enabled=0U;
    } else wg_enabled=!wg_enabled;
    float velocity=now-output_move_time>100U ? 0.0f : output_velocity;
    output_settle(now,output_dragged,velocity);
    return old!=wg_enabled;
}

void UI_NotifyReset(uint32_t now) { reset_start=now; reset_visible=true; }
void UI_ToggleMotion(void)
{
    motion_enabled=!motion_enabled;
    if (!motion_enabled) {
        press_active=page_active=toggle_active=false;
        toggle_position=(float)wg_enabled;
    }
}

bool UI_Tick(uint32_t now)
{
    bool changed=false;
    clock_ms=now;
    if (motion_screen!=current_sys_state) {
        UI_OutputTouchCancel(now);
        motion_screen=current_sys_state;
        page_start=now;
        page_active=motion_enabled && motion_screen!=SYS_OSC;
        press_active=false;
        changed=true;
    }
    if (press_active) { changed=true; if (now-press_start>=140U) press_active=false; }
    if (page_active) { changed=true; if (now-page_start>=180U) page_active=false; }
    if (output_target!=wg_enabled) {
        UI_OutputTouchCancel(now);
        /* Retarget from the current position, so rapid reversals never jump. */
        toggle_from=toggle_position; toggle_start=now; output_target=wg_enabled;
        toggle_active=motion_enabled; changed=true;
        toggle_spring=false;
        if (!motion_enabled) toggle_position=(float)output_target;
    }
    if (toggle_active && !output_captured) {
        if (toggle_spring) {
            /* Critically damped spring, response 0.3 s, no overshoot outside
             * the switch track. Carry release velocity through interruption. */
            const float omega=20.943951f;
            float t=(float)(now-toggle_start)*0.001f;
            float d=toggle_from-(float)output_target;
            float b=toggle_velocity+omega*d;
            toggle_position=unit_clamp((float)output_target+(d+b*t)*expf(-omega*t));
            if (now-toggle_start>=500U) {
                toggle_position=(float)output_target; toggle_active=false;
            }
        } else {
            toggle_position=toggle_from+((float)output_target-toggle_from)*ease_out(now-toggle_start,160);
            if (now-toggle_start>=160U) toggle_active=false;
        }
        changed=true;
    }
    if (reset_visible && now-reset_start>=1000U) { reset_visible=false; changed=true; }
    return changed;
}

static UiRect visual_rect(UiAction action)
{
    UiRect r=rects[action];
    if (page_active) r.y+=(int16_t)(8.0f*(1.0f-ease_out(clock_ms-page_start,180)));
    if (press_active && pressed==action) {
        int inset=(int)(2.5f*(1.0f-ease_out(clock_ms-press_start,140)));
        r.x+=inset; r.y+=inset; r.w-=2*inset; r.h-=2*inset;
    }
    return r;
}

static void button(UiAction action, const char *label, bool selected, uint8_t accent)
{
    UiRect r=visual_rect(action);
    uint8_t bg=selected?SELECTED:PANEL;
    if (press_active && pressed==action) bg=RAISED;
    panel(r,bg,selected?accent:BORDER);
    if (selected) rounded(r.x+5,r.y+12,3,r.h-24,1,accent);
    text_at(r.x+(r.w-text_width(label,BODY))/2,r.y+(r.h-20)/2,
            label,selected?accent:WHITE,bg,BODY);
}

static void topbar(const char *title, const char *subtitle, uint8_t accent)
{
    fill(0,0,800,56,PANEL);
    rounded(16,18,4,20,2,accent);
    text_at(28,8,title,WHITE,PANEL,TITLE);
    if (subtitle) text_at(210,22,subtitle,MUTED,PANEL,SMALL);
    button(UI_HOME,"Home",false,WHITE);
}

static void mini_wave(int x,int y,int w,int h,WaveType type,uint8_t color)
{
    int prev=y+h/2;
    for (int i=0;i<w;++i) {
        float phase=(float)i/(float)(w-1)*2.0f;
        float p=phase-floorf(phase);
        float v=type==WAVE_SINE?sinf(phase*6.2831853f):
                type==WAVE_SQUARE?(p<0.5f?1.0f:-1.0f):(1.0f-4.0f*fabsf(p-0.5f));
        int py=y+h/2-(int)(v*(float)(h-4)/2.0f);
        if(i) Draw_Line_L8(x+i-1,prev,x+i,py,color);
        prev=py;
    }
}

static void draw_scope_trace(UiRect plot, uint8_t color)
{
    const OscilloscopeState *s=Oscilloscope_GetState();
    if (!s->frame_valid || s->volts_per_div<=0.0f) return;
    uint16_t count;
    const uint16_t *trace=Oscilloscope_GetTrace(&count);
    if (count>OSC_TRACE_POINTS) count=OSC_TRACE_POINTS;
    uint64_t span=(uint64_t)s->sample_rate_hz*s->timebase_us_per_div*10U;
    if (!span) return;
    float ppv=(float)plot.h/(8.0f*s->volts_per_div);
    int previous_y=plot.y+plot.h/2, previous_x=plot.x;
    for (uint16_t i=0;i<count;++i) {
        int x=plot.x+(int)((uint64_t)i*(uint16_t)plot.w*1000000U/span);
        if (x>=plot.x+plot.w) break;
        int y=plot.y+plot.h/2-(int)(((float)trace[i]*OSC_INPUT_FULL_V/OSC_ADC_MAX-
                                   s->vertical_center_v)*ppv);
        if (y<plot.y) y=plot.y;
        if (y>plot.y+plot.h) y=plot.y+plot.h;
        if (i) Draw_Line_L8(previous_x,previous_y,x,y,color);
        previous_y=y; previous_x=x;
    }
}

void Draw_Main_Menu(void)
{
    clear_frame();
    text_at(32,24,"OSC / GEN",WHITE,BG,NUMBER);
    text_at(34,78,"A small bench. Two precise tools.",MUTED,BG,BODY);
    UiRect l=visual_rect(UI_OSC), r=visual_rect(UI_GEN);
    panel(l,PANEL,BORDER); panel(r,PANEL,BORDER);
    const OscilloscopeState *s=Oscilloscope_GetState();
    text_at(l.x+24,l.y+20,"ACQUIRE",GREEN,PANEL,SMALL);
    text_at(r.x+24,r.y+20,"GENERATE",BLUE,PANEL,SMALL);
    text_at(l.x+278,l.y+20,s->single_active?"WAIT":s->running?"RUN":"HOLD",
            s->running?GREEN:YELLOW,PANEL,SMALL);
    for (int x=0;x<6;++x) {
        fill(l.x+24+x*60,l.y+68,1,76,GRID);
        fill(r.x+24+x*60,r.y+68,1,76,GRID);
    }
    fill(l.x+24,l.y+105,312,1,GRID);
    if (s->frame_valid) draw_scope_trace((UiRect){l.x+24,l.y+70,312,70},GREEN);
    else text_at(l.x+100,l.y+96,s->running?"Waiting for ADC":"No capture",MUTED,PANEL,BODY);
    mini_wave(r.x+24,r.y+70,312,70,wg_wave,BLUE);
    text_at(l.x+24,l.y+156,"Oscilloscope",WHITE,PANEL,TITLE);
    text_at(r.x+24,r.y+156,"Wave generator",WHITE,PANEL,TITLE);
    text_at(l.x+24,l.y+202,s->running?"Live input, trigger and measurements":
            "Held capture, trigger and measurements",MUTED,PANEL,BODY);
    text_at(r.x+24,r.y+202,"Sine, square and triangle",MUTED,PANEL,BODY);
    text_at(l.x+24,l.y+234,"Open instrument  >",GREEN,PANEL,SMALL);
    text_at(r.x+24,r.y+234,"Set up output  >",BLUE,PANEL,SMALL);
    text_at(32,437,wg_enabled?"PA5 output is ON":"PA5 output is OFF",wg_enabled?GREEN:MUTED,BG,BODY);
    button(UI_MOTION,motion_enabled?"Motion: on":"Motion: reduced",false,MUTED);
}

void Draw_Grid_And_Axes(void)
{
    fill(PLOT_X,PLOT_Y,PLOT_W+1,PLOT_H+1,PLOT);
    for(int x=PLOT_X;x<=PLOT_X+PLOT_W;x+=60)
        Draw_Line_L8(x,PLOT_Y,x,PLOT_Y+PLOT_H,GRID);
    for(int y=PLOT_Y;y<=PLOT_Y+PLOT_H;y+=45)
        Draw_Line_L8(PLOT_X,y,PLOT_X+PLOT_W,y,GRID);
    Draw_Line_L8(PLOT_X,240,700,240,BORDER);
    Draw_Line_L8(400,PLOT_Y,400,420,BORDER);
    for(int x=PLOT_X;x<=700;x+=12) Draw_Line_L8(x,238,x,242,BORDER);
}

static void metric(int x,const char *label,const char *value,uint8_t color)
{
    rounded(x,428,115,46,6,PANEL);
    text_at(x+9,430,label,MUTED,PANEL,SMALL);
    unsigned font=text_width(value,BODY)<=97?BODY:SMALL;
    text_at(x+9,449,value,color,PANEL,font);
}

void Draw_Oscilloscope_UI(void)
{
    clear_frame();
    const OscilloscopeState *s=Oscilloscope_GetState();
    topbar("Scope",NULL,GREEN);
    char t[40],v[16];
    if (s->timebase_us_per_div<1000U)
        snprintf(t,sizeof(t),"%lu us/div",(unsigned long)s->timebase_us_per_div);
    else if (s->timebase_us_per_div%1000U)
        snprintf(t,sizeof(t),"%lu.%03lu ms/div",(unsigned long)(s->timebase_us_per_div/1000U),
                 (unsigned long)(s->timebase_us_per_div%1000U));
    else snprintf(t,sizeof(t),"%lu ms/div",(unsigned long)(s->timebase_us_per_div/1000U));
    text_at(152,18,t,WHITE,PANEL,BODY);
    fixed(v,sizeof(v),s->volts_per_div); snprintf(t,sizeof(t),"%s V/div",v);
    text_at(292,18,t,WHITE,PANEL,BODY);
    fixed(v,sizeof(v),s->trigger_level_v); snprintf(t,sizeof(t),"Trig  %s V",v);
    text_at(440,18,t,YELLOW,PANEL,BODY);
    text_at(610,21,"CH1",GREEN,PANEL,SMALL);
    button(UI_TIME,"Time",current_ctrl==CTRL_TIMEBASE,BLUE);
    button(UI_VOLTS,"V / div",current_ctrl==CTRL_VOLTS_DIV,BLUE);
    button(UI_TRIGGER,"Trigger",current_ctrl==CTRL_TRIGGER_LEVEL,BLUE);
    button(UI_POSITION,"Position",current_ctrl==CTRL_VERTICAL_POS,BLUE);
    button(UI_RUN,s->single_active?"Wait":s->running?"Run":"Hold",true,s->running?GREEN:YELLOW);
    button(UI_SLOPE,s->trigger_slope==OSC_TRIGGER_RISING?"Rise":"Fall",false,YELLOW);
    button(UI_SINGLE,"Single",s->single_active,GREEN);
    button(UI_COARSE,"Coarse",!s->fine_adjustment,BLUE);
    button(UI_FINE,"Fine",s->fine_adjustment,BLUE);
    text_at(16,312,"Touch",MUTED,BG,SMALL);
    text_at(16,330,"to select.",MUTED,BG,SMALL);
    text_at(16,360,"Turn knob",MUTED,BG,SMALL);
    text_at(16,378,"to adjust.",MUTED,BG,SMALL);
    text_at(714,350,wg_enabled?"GEN ON":"GEN OFF",wg_enabled?GREEN:MUTED,BG,SMALL);
    text_at(714,382,"0 - 3.3 V",MUTED,BG,SMALL);
    text_at(714,400,"PF11",MUTED,BG,SMALL);
    Draw_Grid_And_Axes();
    if(s->frame_valid) {
        if(s->frequency_hz>=1000) {
            fixed(v,sizeof(v),s->frequency_hz/1000); snprintf(t,sizeof(t),"%s kHz",v);
        } else snprintf(t,sizeof(t),"%lu Hz",(unsigned long)(s->frequency_hz+0.5f));
        metric(100,"FREQUENCY",t,GREEN);
        fixed(v,sizeof(v),s->vpp_v); snprintf(t,sizeof(t),"%s V",v); metric(221,"PEAK TO PEAK",t,YELLOW);
        fixed(v,sizeof(v),s->average_v); snprintf(t,sizeof(t),"%s V",v); metric(342,"AVERAGE",t,WHITE);
        fixed(v,sizeof(v),s->maximum_v); snprintf(t,sizeof(t),"%s V",v); metric(463,"HIGH",t,RED);
        fixed(v,sizeof(v),s->minimum_v); snprintf(t,sizeof(t),"%s V",v); metric(584,"LOW",t,CYAN);
    } else {
        metric(100,"FREQUENCY","--",MUTED); metric(221,"PEAK TO PEAK","--",MUTED);
        metric(342,"AVERAGE","--",MUTED); metric(463,"HIGH","--",MUTED); metric(584,"LOW","--",MUTED);
        const char *hint=s->running?"Waiting for ADC":"Tap Run or Single";
        fill(290,223,220,28,PLOT);
        text_at(400-text_width(hint,BODY)/2,225,hint,MUTED,PLOT,BODY);
    }
}

void Draw_Waveform(void)
{
    const OscilloscopeState *s=Oscilloscope_GetState();
    if(!s->frame_valid) return;
    float ppv=45.0f/s->volts_per_div;
    int ty=240-(int)((s->trigger_level_v-s->vertical_center_v)*ppv);
    if(ty>=60 && ty<=420)
        for(int x=100;x<700;x+=10) Draw_Line_L8(x,ty,x+3,ty,DIM);
    draw_scope_trace((UiRect){100,60,600,360},YELLOW);
}

static void value_card(UiAction action,const char *label,const char *value,
                       const char *unit,bool selected,uint8_t accent)
{
    UiRect r=visual_rect(action);
    uint8_t bg=selected?SELECTED:PANEL;
    panel(r,bg,selected?accent:BORDER);
    text_at(r.x+20,r.y+16,label,selected?accent:MUTED,bg,BODY);
    text_at(r.x+20,r.y+48,value,WHITE,bg,NUMBER);
    text_at(r.x+32+text_width(value,NUMBER),r.y+68,unit,MUTED,bg,BODY);
    text_at(r.x+20,r.y+113,selected?"Turn knob to adjust":"Tap to select",MUTED,bg,SMALL);
}

void Draw_WaveGen_UI(void)
{
    clear_frame();
    topbar("Generator","DAC  /  PA5  /  Max 100 kHz",BLUE);
    button(UI_SINE,"Sine",wg_wave==WAVE_SINE,BLUE);
    button(UI_SQUARE,"Square",wg_wave==WAVE_SQUARE,BLUE);
    button(UI_TRIANGLE,"Triangle",wg_wave==WAVE_TRIANGLE,BLUE);
    char t[24],v[16];
    snprintf(t,sizeof(t),"%lu",(unsigned long)wg_freq);
    value_card(UI_FREQUENCY,"Frequency",t,"Hz",wg_ctrl==0,BLUE);
    fixed(v,sizeof(v),wg_amp);
    value_card(UI_AMPLITUDE,"Amplitude",v,"Vpp",wg_ctrl==1,BLUE);
    panel((UiRect){112,232,672,120},PLOT,BORDER);
    text_at(130,240,"WAVE SHAPE",MUTED,PLOT,SMALL);
    text_at(627,240,"Preview only",MUTED,PLOT,SMALL);
    fill(132,300,632,1,GRID);
    /* This is explicitly a shape preview, never a fabricated measurement. */
    mini_wave(132,270,632,60,wg_wave,BLUE);
    UiRect r={112,368,672,72}; /* Informational card, not a touch target. */
    uint8_t bg=wg_enabled?TINT:PANEL;
    panel(r,bg,wg_enabled?GREEN:BORDER);
    text_at(r.x+20,r.y+9,wg_enabled?"Output is on":"Output is off",wg_enabled?GREEN:WHITE,bg,TITLE);
    text_at(r.x+20,r.y+43,wg_enabled?"PA5 waveform active":"Tap or slide the switch",MUTED,bg,SMALL);
    if(output_captured) rounded(684,385,80,40,20,BLUE);
    rounded(r.x+r.w-96,r.y+21,72,32,16,wg_enabled?GREEN:BORDER);
    rounded(r.x+r.w-92+(int)(toggle_position*40.0f),r.y+25,24,24,12,WHITE);
    text_at(114,450,"Press knob to reset parameters and turn output off.",MUTED,BG,SMALL);
}

void UI_Render(void)
{
    if(current_sys_state==SYS_OSC) { Draw_Oscilloscope_UI(); Draw_Waveform(); }
    else if(current_sys_state==SYS_GEN) Draw_WaveGen_UI();
    else Draw_Main_Menu();
    if(reset_visible) {
        /* Fixed, short acknowledgement; never delays input or acquisition. */
        rounded(300,6,200,44,8,RAISED);
        text_at(325,18,"Defaults restored",WHITE,RAISED,BODY);
    }
}

void Play_Cyber_Boot_Sequence(void)
{
    /* Compatibility entry point: no fake progress bar or blocking delays. */
    page_start=HAL_GetTick(); page_active=motion_enabled;
}

void Draw_UI_Button(uint16_t x,uint16_t y,uint8_t selected,
                    void (*icon)(uint16_t,uint16_t,uint8_t))
{
    panel((UiRect){(int16_t)x,(int16_t)y,84,48},PANEL,selected?BLUE:BORDER);
    if(icon)icon(x+10,y+8,selected?BLUE:WHITE);
}
