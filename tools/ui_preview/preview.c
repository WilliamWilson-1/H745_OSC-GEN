/* Host-only fixtures: uses the firmware renderer, never linked into firmware. */
#include "graph.h"
#include "oscilloscope.h"
#include "display.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

uint8_t ui_preview_framebuffer[800*480];
static uint8_t saved[800*480];
static uint16_t trace[600];
static uint16_t fixture_count=600;
static uint32_t tick;
static struct { uint8_t before[32], pixels[DISPLAY_TOP_BYTES], after[32]; } split_top;
static struct { uint8_t before[32], pixels[DISPLAY_BOTTOM_BYTES], after[32]; } split_bottom;
static OscilloscopeState state = {
    .sample_rate_hz=120000, .timebase_us_per_div=500, .volts_per_div=0.5f,
    .trigger_level_v=1.65f, .vertical_center_v=1.65f, .frequency_hz=1000,
    .vpp_v=3.0f, .minimum_v=0.15f, .maximum_v=3.15f, .average_v=1.65f,
    .running=true, .frame_valid=true
};
uint32_t HAL_GetTick(void) { return tick; }
const OscilloscopeState *Oscilloscope_GetState(void) { return &state; }
const uint16_t *Oscilloscope_GetTrace(uint16_t *count) { *count=fixture_count; return trace; }

static void render(uint32_t now) {
    tick=now;
    UI_Tick(now);
    UI_SetRenderBuffers(ui_preview_framebuffer, ui_preview_framebuffer+DISPLAY_TOP_BYTES,800);
    UI_Render();
    /* Render every fixture into physically separate strips. Verify exact
     * pixel equivalence and that primitives crossing the split stay in bounds. */
    memset(&split_top,0xa5,sizeof(split_top));
    memset(&split_bottom,0xa5,sizeof(split_bottom));
    UI_SetRenderBuffers(split_top.pixels,split_bottom.pixels,DISPLAY_BOTTOM_PITCH);
    UI_Render();
    assert(memcmp(ui_preview_framebuffer,split_top.pixels,DISPLAY_TOP_BYTES)==0);
    for(unsigned y=0;y<DISPLAY_HEIGHT-DISPLAY_SPLIT_Y;++y) {
        assert(memcmp(ui_preview_framebuffer+DISPLAY_TOP_BYTES+y*800,
                      split_bottom.pixels+y*DISPLAY_BOTTOM_PITCH,800)==0);
        for(unsigned x=800;x<DISPLAY_BOTTOM_PITCH;++x)
            assert(split_bottom.pixels[y*DISPLAY_BOTTOM_PITCH+x]==0);
    }
    for(unsigned i=0;i<32;++i) {
        assert(split_top.before[i]==0xa5 && split_top.after[i]==0xa5);
        assert(split_bottom.before[i]==0xa5 && split_bottom.after[i]==0xa5);
    }
    UI_SetRenderBuffers(ui_preview_framebuffer,ui_preview_framebuffer+DISPLAY_TOP_BYTES,800);
}
static void snapshot(const char *name) {
    char path[128]; snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n800 480\n255\n");
    for(unsigned i=0;i<sizeof(ui_preview_framebuffer);++i) {
        uint32_t c=my_palette[ui_preview_framebuffer[i]];
        fputc((c>>16)&255,f); fputc((c>>8)&255,f); fputc(c&255,f);
    }
    assert(fclose(f)==0);
}
static bool enabled(SystemState s, UiAction a) {
    return (s!=SYS_MAIN_MENU && a==UI_HOME) ||
        (s==SYS_MAIN_MENU && (a==UI_OSC||a==UI_GEN||a==UI_MOTION||a==UI_ABOUT)) ||
        (s==SYS_OSC && a>=UI_TIME && a<=UI_FINE) ||
        (s==SYS_GEN && a>=UI_SINE && a<=UI_AMPLITUDE) ||
        (s==SYS_ABOUT && a>=UI_THEME_DARK && a<=UI_THEME_LIGHT);
}
static void test_hitboxes(void) {
    for(SystemState s=SYS_MAIN_MENU;s<=SYS_ABOUT;++s) {
        for(int y=0;y<480;++y) for(int x=0;x<800;++x) {
            UiAction expected=UI_NONE;
            for(UiAction a=UI_HOME;a<UI_ACTION_COUNT;++a) {
                UiRect r=UI_GetRect(a);
                assert(r.h>=48 && r.x>=0 && r.y>=0 && r.x+r.w<=800 && r.y+r.h<=480);
                if(enabled(s,a) && x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                    assert(expected==UI_NONE); /* no overlapping active targets */
                    expected=a;
                }
            }
            assert(UI_HitTest(s,(uint16_t)x,(uint16_t)y)==expected);
        }
        assert(UI_HitTest(s,800,0)==UI_NONE);
        assert(UI_HitTest(s,0,480)==UI_NONE);
        assert(UI_HitTest(s,65535,65535)==UI_NONE);
    }
    UiRect invalid=UI_GetRect(UI_ACTION_COUNT);
    assert(invalid.w==0 && invalid.h==0);
    assert(UI_HitTest(SYS_GEN,140,400)==UI_NONE);
    assert(UI_HitTest(SYS_GEN,650,400)==UI_NONE);
    assert(UI_HitTest(SYS_GEN,700,400)==UI_OUTPUT);
    assert(UI_HitTest(SYS_OSC,740,250)==UI_COARSE);
    assert(UI_HitTest(SYS_OSC,740,310)==UI_FINE);
}

static void test_output_gestures(void) {
    current_sys_state=SYS_GEN; wg_enabled=0; render(5000); render(5600);
    UI_OutputTouchDown(200,400,5601); /* Entire card no longer toggles. */
    assert(!UI_OutputTouchRelease(5610) && !wg_enabled);
    UI_OutputTouchDown(704,405,5620);
    assert(!wg_enabled); /* A touch-down must never energize the output. */
    UI_OutputTouchMove(708,407,5630); /* Tap jitter, below drag threshold. */
    assert(UI_OutputTouchRelease(5640) && wg_enabled);
    assert(!UI_OutputTouchRelease(5641)); /* Duplicate release. */
    render(5900);
    UI_OutputTouchDown(744,405,6000);
    UI_OutputTouchMove(724,405,6033); render(6033); snapshot("switch_drag_half");
    assert(wg_enabled); /* Preview does not change hardware state. */
    UI_OutputTouchMove(700,405,6066);
    assert(UI_OutputTouchRelease(6067) && !wg_enabled);
    render(6667); assert(!UI_Tick(6668));
    UI_OutputTouchDown(704,405,6700);
    UI_OutputTouchMove(748,405,6733);
    assert(UI_OutputTouchRelease(6734) && wg_enabled); /* Swipe right. */
    render(7300);
    UI_OutputTouchDown(744,405,7310);
    UI_OutputTouchMove(724,405,7340); /* Midpoint dead band retains ON. */
    assert(!UI_OutputTouchRelease(7350) && wg_enabled);
    render(7900);
    UI_OutputTouchDown(744,405,7910);
    UI_OutputTouchMove(700,405,7940);
    UI_OutputTouchCancel(7941); /* Multitouch, overflow or reset. */
    assert(!UI_OutputTouchRelease(7942) && wg_enabled);
    render(8500);
    UI_OutputTouchDown(744,405,8510);
    UI_OutputTouchMove(742,420,8540); /* Vertical intent cancels. */
    assert(!UI_OutputTouchRelease(8541) && wg_enabled);
    UI_OutputTouchDown(744,405,8600);
    UI_OutputTouchMove(780,405,8630); /* Captured beyond horizontal bounds. */
    assert(!UI_OutputTouchRelease(8631) && wg_enabled);
    render(9200);
    UI_OutputTouchDown(744,405,9210);
    current_sys_state=SYS_MAIN_MENU; render(9220);
    assert(!UI_OutputTouchRelease(9230) && wg_enabled);
    current_sys_state=SYS_GEN; render(9800);
    UI_ToggleMotion(); /* Reduced motion still allows direct manipulation. */
    UI_OutputTouchDown(744,405,9900);
    UI_OutputTouchMove(704,405,9933);
    assert(UI_OutputTouchRelease(9934) && !wg_enabled);
    render(9934); assert(!UI_Tick(9935));
    UI_ToggleMotion();
    puts("PASS: switch tap/drag, release-only commit, card exclusion, jitter, dead band, capture, cancellation, duplicate release and reduced motion.");
}

static double luminance(uint32_t rgb) {
    double value=0;
    const double weight[3]={0.0722,0.7152,0.2126};
    for(unsigned i=0;i<3;++i) {
        double c=(double)((rgb>>(i*8))&255)/255;
        value+=weight[i]*(c<=0.04045?c/12.92:pow((c+0.055)/1.055,2.4));
    }
    return value;
}
static void test_about_themes(void) {
    assert(UI_HitTest(SYS_MAIN_MENU,710,45)==UI_ABOUT);
    assert(UI_HitTest(SYS_ABOUT,710,25)==UI_HOME);
    assert(UI_HitTest(SYS_ABOUT,100,400)==UI_THEME_DARK);
    assert(UI_HitTest(SYS_ABOUT,350,400)==UI_THEME_BLUE);
    assert(UI_HitTest(SYS_ABOUT,600,400)==UI_THEME_LIGHT);
    uint32_t palette_before[256]; memcpy(palette_before,my_palette,sizeof(my_palette));
    OscilloscopeState before=state;
    uint8_t output=wg_enabled;
    unsigned sequence=0;
    for(UiTheme theme=UI_THEME_GRAPHITE;theme<UI_THEME_COUNT;++theme) {
        UI_SelectTheme(theme); assert(UI_GetTheme()==theme);
        for(SystemState screen=SYS_MAIN_MENU;screen<=SYS_ABOUT;++screen) {
            current_sys_state=screen;
            uint32_t now=12000+(sequence++)*1000;
            render(now); render(now+600);
            char name[40]; snprintf(name,sizeof(name),"theme_%u_page_%u",theme,screen);
            snapshot(name);
        }
        /* Normal text and selected labels retain readable contrast. */
        const unsigned pairs[][2]={{4,0},{4,8},{10,0},{10,8},{3,11},{2,13}};
        for(unsigned p=0;p<sizeof(pairs)/sizeof(pairs[0]);++p) {
            double a=luminance(my_palette[theme*16+pairs[p][0]]);
            double b=luminance(my_palette[theme*16+pairs[p][1]]);
            double contrast=a>b?(a+0.05)/(b+0.05):(b+0.05)/(a+0.05);
            assert(contrast>=4.5);
        }
    }
    assert(memcmp(palette_before,my_palette,sizeof(my_palette))==0);
    assert(memcmp(&before,&state,sizeof(state))==0 && wg_enabled==output);
    memcpy(saved,ui_preview_framebuffer,sizeof(saved));
    UI_SelectTheme(UI_THEME_GRAPHITE);
    /* Selecting cannot mutate an already-rendered/queued framebuffer. */
    assert(memcmp(saved,ui_preview_framebuffer,sizeof(saved))==0);
    UI_SelectTheme((UiTheme)99); assert(UI_GetTheme()==UI_THEME_GRAPHITE);
    UI_SelectTheme((UiTheme)-1); assert(UI_GetTheme()==UI_THEME_GRAPHITE);
    for(unsigned i=0;i<90;++i) {
        UI_SelectTheme((UiTheme)(i%UI_THEME_COUNT));
        render(25000+i*33);
        assert(memcmp(palette_before,my_palette,sizeof(my_palette))==0);
    }
    UI_SelectTheme(UI_THEME_GRAPHITE);
    puts("PASS: About navigation; 3 themes x 4 pages; text contrast >=4.5; immutable CLUT/queued pixels; no instrument state changes; 90 rapid theme switches.");
}

int main(void) {
    test_hitboxes();
    UI_InitPalette();
    for(unsigned i=0;i<600;++i)
        trace[i]=(uint16_t)((1.65+1.5*sin(i*6.28318530718/120.0))*4095/3.3);
    render(0); Play_Cyber_Boot_Sequence();
    for(unsigned i=0;i<=6;++i) {
        char name[32]; snprintf(name,sizeof(name),"menu_motion_%02u",i);
        render(i*33); snapshot(name);
    }
    snapshot("menu");
    assert(!UI_Tick(300));
    memcpy(saved,ui_preview_framebuffer,sizeof(saved));
    for(unsigned i=0;i<600;++i) trace[i]=2048;
    render(301); snapshot("menu_dc");
    assert(memcmp(saved,ui_preview_framebuffer,sizeof(saved))!=0); /* Real input, not decorative sine. */
    state.running=false; render(302); snapshot("menu_hold");
    memcpy(saved,ui_preview_framebuffer,sizeof(saved));
    render(400); assert(memcmp(saved,ui_preview_framebuffer,sizeof(saved))==0);
    state.frame_valid=false; render(401); snapshot("menu_empty");
    assert(memcmp(saved,ui_preview_framebuffer,sizeof(saved))!=0);
    state.frame_valid=true; state.running=true;
    for(unsigned i=0;i<600;++i)
        trace[i]=(uint16_t)((1.65+1.5*sin(i*6.28318530718/120.0))*4095/3.3);
    current_sys_state=SYS_OSC;
    render(500); snapshot("scope");
    state.fine_adjustment=true; state.timebase_us_per_div=1100;
    state.sample_rate_hz=54545;
    for(unsigned i=0;i<600;++i)
        trace[i]=(uint16_t)((1.65+1.5*sin(i*6.28318530718*1000/54545))*4095/3.3);
    render(501); snapshot("scope_fine");
    state.timebase_us_per_div=500; state.fine_adjustment=false;
    state.sample_rate_hz=120000;
    for(unsigned i=0;i<600;++i)
        trace[i]=(uint16_t)((1.65+1.5*sin(i*6.28318530718/120.0))*4095/3.3);
    state.running=false; state.trigger_slope=OSC_TRIGGER_FALLING;
    render(600); snapshot("scope_hold");
    state.single_active=true; state.running=true; state.frame_valid=false;
    render(700); snapshot("scope_wait");
    state.single_active=false; state.frame_valid=true;
    state.volts_per_div=0.05f; state.minimum_v=0; state.maximum_v=3.3f;
    state.frequency_hz=100000; state.timebase_us_per_div=500000;
    render(800); snapshot("scope_limits");
    state.sample_rate_hz=2000000; state.volts_per_div=0.5f;
    state.minimum_v=0.15f; state.maximum_v=3.15f;
    for(unsigned i=0;i<600;++i)
        trace[i]=(uint16_t)((1.65+1.5*sin(i*6.28318530718/20.0))*4095/3.3);
    const uint32_t fast_us[]={2,5,10,20};
    for(unsigned k=0;k<4;++k) {
        state.timebase_us_per_div=fast_us[k];
        fixture_count=(uint16_t)(fast_us[k]*20U);
        char name[32]; snprintf(name,sizeof(name),"scope_100khz_%luus",(unsigned long)fast_us[k]);
        render(900+k); snapshot(name);
    }
    current_sys_state=SYS_GEN;
    render(1000); render(1200); snapshot("generator_off");
    wg_enabled=1; UI_NotifyTouch(UI_OUTPUT,1300);
    for(unsigned i=0;i<=6;++i) {
        char name[32]; snprintf(name,sizeof(name),"output_motion_%02u",i);
        render(1300+i*33); snapshot(name);
    }
    snapshot("generator_on");
    assert(!UI_Tick(1600));
    wg_enabled=0; render(1700); render(1766);
    wg_enabled=1; render(1767); render(2000);
    assert(!UI_Tick(2001)); /* rapid retarget settles */
    wg_freq=100000; wg_amp=0; wg_wave=WAVE_TRIANGLE; wg_ctrl=1;
    render(2100); snapshot("generator_limits");
    UI_NotifyReset(2200); render(2200); snapshot("reset");
    assert(!UI_Tick(3199));
    assert(UI_Tick(3200)); /* toast expires once */
    assert(!UI_Tick(3201));
    UI_ToggleMotion(); wg_enabled=0; render(3300);
    memcpy(saved,ui_preview_framebuffer,sizeof(saved));
    UI_NotifyTouch(UI_OUTPUT,3301); render(3301);
    assert(memcmp(saved,ui_preview_framebuffer,sizeof(saved))==0);
    assert(!UI_Tick(3302));
    current_sys_state=SYS_MAIN_MENU; render(3400); snapshot("reduced_motion");
    assert(!UI_Tick(3401));
    UI_ToggleMotion();
    UI_NotifyTouch(UI_OSC,UINT32_MAX-50U); render(UINT32_MAX-50U);
    assert(UI_Tick(100)); assert(!UI_Tick(101)); /* tick wraparound */
    test_output_gestures();
    test_about_themes();
    puts("PASS: split-buffer pixel equivalence and guards; 1,536,000 hit-test pixels, target bounds/non-overlap, animation settling, rapid reversal, reduced motion, toast expiry, tick wraparound.");
    return 0;
}
