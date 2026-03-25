/*  BALUARTE Billboard v5 – Animated Eye + Text
 *  ESP32-CYD 2.2" (240×320) | LovyanGFX + LVGL v8 | Portrait
 */
#include "LGFX_ESP32_2432S022.hpp"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

static LGFX tft;
#define SCR_W   240
#define SCR_H   320
#define FOOTER_H 45

/* ── LVGL buffer ─────────────────────────────────────────────── */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCR_W * 20];

/* ── Colours (runtime-init via lv_color_hex) ─────────────────── */
static lv_color_t C_GRN, C_WHT, C_RED, C_BLK, C_DKRED, C_DKGRN;

/* ── Effect flags ────────────────────────────────────────────── */
#define FX_FADE      1
#define FX_STROBE    2
#define FX_THREAT_ON 4
#define FX_THREAT_OFF 8

struct AnimStep { const char *txt; uint32_t col; uint16_t dur; uint8_t fx; };
static const AnimStep SEQ[] = {
    {"HEJ!",        0x00FF00, 700, FX_FADE},
    {nullptr,       0,        250, 0},
    {"TWOJ KOD",    0xFFFFFF, 800, FX_FADE},
    {nullptr,       0,        200, 0},
    {"JEST",        0xFF0000, 550, FX_FADE|FX_THREAT_ON},
    {"BEZPIECZNY?", 0xFF0000,1100, FX_FADE|FX_STROBE},
    {nullptr,       0,        500, 0},
    {"NA PEWNO?",   0xFF0000, 900, FX_FADE|FX_STROBE},
    {nullptr,       0,        600, FX_THREAT_OFF},
    {"SPRAWDZIMY.", 0x00FF00,1000, FX_FADE},
    {nullptr,       0,        300, 0},
    {"SECURE",      0x00FF00, 600, FX_FADE},
    {"CODE",        0x00FF00, 600, FX_FADE},
    {"REVIEW",      0x00FF00, 900, FX_FADE},
    {nullptr,       0,        400, 0},
    {"POGADAJMY!",  0xFFFFFF, 400, 0},
    {"POGADAJMY!",  0x00FF00, 400, 0},
    {"POGADAJMY!",  0xFFFFFF, 400, 0},
    {"POGADAJMY!",  0x00FF00, 400, 0},
    {"POGADAJMY!",  0xFFFFFF, 400, 0},
    {"POGADAJMY!",  0x00FF00, 400, 0},
    {nullptr,       0,       1500, 0},
};
static const int SEQ_LEN = sizeof(SEQ)/sizeof(SEQ[0]);

/* ── Eye geometry ────────────────────────────────────────────── */
#define EYE_CX    120
#define EYE_CY     62
#define EYE_A      72
#define EYE_BU     26
#define EYE_BD     20
#define EYE_N      25
#define IRIS_SZ    46
#define PUPIL_SZ   26

static lv_point_t up_pts[EYE_N];
static lv_point_t lo_pts[EYE_N];

/* Iris gaze sequence */
struct Gaze { int8_t dx,dy; uint16_t ms; };
static const Gaze gazes[] = {
    { 0, 0,1400}, {-16, 0, 900}, { 0, 0, 400}, { 16, 0, 900},
    { 0, 0, 500}, {-10,-5, 700}, { 10, 5, 700}, { 0,-6, 500},
    { 0, 0,1100}, { 14,-3, 800}, { 0, 0, 600}, {-14, 3, 800},
    { 0, 0, 800},
};
static const int GAZE_N = sizeof(gazes)/sizeof(gazes[0]);

/* ── LVGL objects ────────────────────────────────────────────── */
static lv_obj_t *scr_main, *accent_bar, *scan_line;
static lv_obj_t *line_up, *line_lo;               // eye outline
static lv_obj_t *iris_obj, *pupil_obj, *lbl_excl;  // eye iris
static lv_obj_t *lbl_center, *lbl_cursor;
static lv_obj_t *pnl_footer, *lbl_footer;

/* ── State ───────────────────────────────────────────────────── */
static int      cur_step = -1;
static uint32_t step_t   = 0;
static bool     threat   = false;

static int      gaze_i   = 0;
static uint32_t gaze_t   = 0;
static int8_t   gaze_cx  = 0, gaze_cy = 0;

static lv_timer_t *strobe_tmr = nullptr;
static int         strobe_n   = 0;

/* ═══════════════════════════════════════════════════════════════
 *  Flush: LVGL → LovyanGFX
 * ═══════════════════════════════════════════════════════════════ */
static void my_disp_flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *px){
    uint32_t w=a->x2-a->x1+1, h=a->y2-a->y1+1;
    tft.startWrite();
    tft.setAddrWindow(a->x1,a->y1,w,h);
    tft.writePixels((uint16_t*)px,w*h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

/* ═══════════════════════════════════════════════════════════════
 *  Anim exec callbacks
 * ═══════════════════════════════════════════════════════════════ */
static void cb_opa(void*o,int32_t v){lv_obj_set_style_opa((lv_obj_t*)o,v,0);}
static void cb_ty (void*o,int32_t v){lv_obj_set_style_translate_y((lv_obj_t*)o,v,0);}
static void cb_tx (void*o,int32_t v){lv_obj_set_style_translate_x((lv_obj_t*)o,v,0);}
static void cb_y  (void*o,int32_t v){lv_obj_set_y((lv_obj_t*)o,v);}

static void animate_word_in(){
    lv_anim_t a;
    lv_anim_init(&a); lv_anim_set_var(&a,lbl_center);
    lv_anim_set_values(&a,0,255); lv_anim_set_time(&a,200);
    lv_anim_set_exec_cb(&a,cb_opa); lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_start(&a);
    lv_anim_init(&a); lv_anim_set_var(&a,lbl_center);
    lv_anim_set_values(&a,18,0); lv_anim_set_time(&a,280);
    lv_anim_set_exec_cb(&a,cb_ty); lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ═══════════════════════════════════════════════════════════════
 *  Strobe (rapid bg flashes)
 * ═══════════════════════════════════════════════════════════════ */
static void strobe_cb(lv_timer_t *t){
    strobe_n++;
    lv_obj_set_style_bg_color(scr_main,(strobe_n%2)?C_DKRED:C_BLK,0);
    if(strobe_n>=6){lv_obj_set_style_bg_color(scr_main,C_BLK,0);lv_timer_del(t);strobe_tmr=nullptr;}
}
static void start_strobe(){
    if(strobe_tmr)lv_timer_del(strobe_tmr);
    strobe_n=0; strobe_tmr=lv_timer_create(strobe_cb,80,nullptr);
}

/* ═══════════════════════════════════════════════════════════════
 *  Threat mode (eye+footer+border turn red / green)
 * ═══════════════════════════════════════════════════════════════ */
static void set_eye_color(lv_color_t c){
    lv_obj_set_style_line_color(line_up,c,0);
    lv_obj_set_style_line_color(line_lo,c,0);
    lv_obj_set_style_border_color(iris_obj,c,0);
    lv_obj_set_style_bg_color(pupil_obj,c,0);
    lv_obj_set_style_bg_color(accent_bar,c,0);
    lv_obj_set_style_bg_color(scan_line,c,0);
}
static void enter_threat(){
    if(threat)return; threat=true;
    set_eye_color(C_RED);
    lv_obj_set_style_bg_color(pnl_footer,C_RED,0);
    lv_label_set_text(lbl_footer,"! ALERT !");
    lv_obj_set_style_border_color(scr_main,C_RED,0);
    lv_obj_set_style_border_width(scr_main,2,0);
}
static void exit_threat(){
    if(!threat)return; threat=false;
    set_eye_color(C_GRN);
    lv_obj_set_style_bg_color(scr_main,C_BLK,0);
    lv_obj_set_style_bg_color(pnl_footer,C_GRN,0);
    lv_label_set_text(lbl_footer,"> BALUARTE.pl <");
    lv_obj_set_style_border_color(scr_main,C_DKGRN,0);
    lv_obj_set_style_border_width(scr_main,1,0);
}

/* ═══════════════════════════════════════════════════════════════
 *  Apply animation step
 * ═══════════════════════════════════════════════════════════════ */
static void apply_step(int i){
    auto &s=SEQ[i];
    if(s.txt){lv_label_set_text(lbl_center,s.txt);lv_obj_set_style_text_color(lbl_center,lv_color_hex(s.col),0);}
    else lv_label_set_text(lbl_center,"");
    lv_obj_align(lbl_center,LV_ALIGN_CENTER,0,30);
    if(s.fx&FX_THREAT_ON)enter_threat();
    if(s.fx&FX_THREAT_OFF)exit_threat();
    if(s.fx&FX_FADE)animate_word_in();
    if(s.fx&FX_STROBE)start_strobe();
}

/* ═══════════════════════════════════════════════════════════════
 *  Master timer – word sequencer (50 ms poll)
 * ═══════════════════════════════════════════════════════════════ */
static void master_cb(lv_timer_t*){
    uint32_t now=lv_tick_get();
    if(cur_step<0||now-step_t>=SEQ[cur_step].dur){
        cur_step=(cur_step+1)%SEQ_LEN;
        step_t=now; apply_step(cur_step);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Eye gaze timer (50 ms poll)
 * ═══════════════════════════════════════════════════════════════ */
static void eye_gaze_cb(lv_timer_t*){
    uint32_t now=lv_tick_get();
    if(now-gaze_t<gazes[gaze_i].ms)return;
    gaze_i=(gaze_i+1)%GAZE_N; gaze_t=now;
    int8_t nx=gazes[gaze_i].dx, ny=gazes[gaze_i].dy;
    lv_anim_t a;
    lv_anim_init(&a); lv_anim_set_var(&a,iris_obj);
    lv_anim_set_values(&a,gaze_cx,nx); lv_anim_set_time(&a,300);
    lv_anim_set_exec_cb(&a,cb_tx); lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    lv_anim_init(&a); lv_anim_set_var(&a,iris_obj);
    lv_anim_set_values(&a,gaze_cy,ny); lv_anim_set_time(&a,cb_ty==cb_ty?300:300);
    lv_anim_set_exec_cb(&a,cb_ty); lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    gaze_cx=nx; gaze_cy=ny;
}

/* ═══════════════════════════════════════════════════════════════
 *  Cursor blink (400 ms)
 * ═══════════════════════════════════════════════════════════════ */
static void cursor_blink_cb(lv_timer_t*){
    static bool v=true; v=!v;
    if(v)lv_obj_clear_flag(lbl_cursor,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lbl_cursor,LV_OBJ_FLAG_HIDDEN);
}

/* ═══════════════════════════════════════════════════════════════
 *  Compute eye outline points
 * ═══════════════════════════════════════════════════════════════ */
static void compute_eye_pts(){
    for(int i=0;i<EYE_N;i++){
        float t=3.14159265f*(float)i/(float)(EYE_N-1);
        float cv=cosf(3.14159265f-t), sv=sinf(t);
        up_pts[i].x=EYE_CX+(lv_coord_t)(EYE_A*cv);
        up_pts[i].y=EYE_CY-(lv_coord_t)(EYE_BU*sv);
        lo_pts[i].x=EYE_CX+(lv_coord_t)(EYE_A*cv);
        lo_pts[i].y=EYE_CY+(lv_coord_t)(EYE_BD*sv);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Build the UI
 * ═══════════════════════════════════════════════════════════════ */
static void ui_init(){
    C_GRN=lv_color_hex(0x00FF00); C_WHT=lv_color_hex(0xFFFFFF);
    C_RED=lv_color_hex(0xFF0000); C_BLK=lv_color_hex(0x000000);
    C_DKRED=lv_color_hex(0x330000); C_DKGRN=lv_color_hex(0x003300);

    scr_main=lv_scr_act();
    lv_obj_set_style_bg_color(scr_main,C_BLK,0);
    lv_obj_set_style_bg_opa(scr_main,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(scr_main,C_DKGRN,0);
    lv_obj_set_style_border_width(scr_main,1,0);

    /* ── Accent bar (top 3px, pulsing) ─────────────────────────── */
    accent_bar=lv_obj_create(scr_main);
    lv_obj_remove_style_all(accent_bar);
    lv_obj_set_size(accent_bar,SCR_W,3);
    lv_obj_align(accent_bar,LV_ALIGN_TOP_MID,0,0);
    lv_obj_set_style_bg_color(accent_bar,C_GRN,0);
    lv_obj_set_style_bg_opa(accent_bar,LV_OPA_COVER,0);
    {lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,accent_bar);
     lv_anim_set_values(&a,60,255); lv_anim_set_time(&a,1200);
     lv_anim_set_playback_time(&a,1200); lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);
     lv_anim_set_exec_cb(&a,cb_opa); lv_anim_start(&a);}

    /* ── Scan line (2px, sweeps top→bottom) ────────────────────── */
    scan_line=lv_obj_create(scr_main);
    lv_obj_remove_style_all(scan_line);
    lv_obj_set_size(scan_line,SCR_W,2);
    lv_obj_set_style_bg_color(scan_line,C_GRN,0);
    lv_obj_set_style_bg_opa(scan_line,80,0);
    {lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,scan_line);
     lv_anim_set_values(&a,0,SCR_H-FOOTER_H); lv_anim_set_time(&a,2500);
     lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);
     lv_anim_set_exec_cb(&a,cb_y); lv_anim_start(&a);}

    /* ── Eye outline ───────────────────────────────────────────── */
    compute_eye_pts();
    line_up=lv_line_create(scr_main);
    lv_line_set_points(line_up,up_pts,EYE_N);
    lv_obj_set_style_line_color(line_up,C_GRN,0);
    lv_obj_set_style_line_width(line_up,3,0);
    lv_obj_set_style_line_rounded(line_up,true,0);

    line_lo=lv_line_create(scr_main);
    lv_line_set_points(line_lo,lo_pts,EYE_N);
    lv_obj_set_style_line_color(line_lo,C_GRN,0);
    lv_obj_set_style_line_width(line_lo,3,0);
    lv_obj_set_style_line_rounded(line_lo,true,0);

    /* ── Iris (bordered circle, animated position) ─────────────── */
    iris_obj=lv_obj_create(scr_main);
    lv_obj_remove_style_all(iris_obj);
    lv_obj_set_size(iris_obj,IRIS_SZ,IRIS_SZ);
    lv_obj_set_style_radius(iris_obj,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_color(iris_obj,C_GRN,0);
    lv_obj_set_style_border_width(iris_obj,2,0);
    lv_obj_set_style_border_opa(iris_obj,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(iris_obj,C_BLK,0);
    lv_obj_set_style_bg_opa(iris_obj,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(iris_obj,0,0);
    lv_obj_set_pos(iris_obj,EYE_CX-IRIS_SZ/2, EYE_CY-IRIS_SZ/2);

    /* ── Pupil (inner green circle) ────────────────────────────── */
    pupil_obj=lv_obj_create(iris_obj);
    lv_obj_remove_style_all(pupil_obj);
    lv_obj_set_size(pupil_obj,PUPIL_SZ,PUPIL_SZ);
    lv_obj_set_style_radius(pupil_obj,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(pupil_obj,C_GRN,0);
    lv_obj_set_style_bg_opa(pupil_obj,LV_OPA_COVER,0);
    lv_obj_center(pupil_obj);

    /* ── "!" inside pupil ──────────────────────────────────────── */
    lbl_excl=lv_label_create(pupil_obj);
    lv_label_set_text(lbl_excl,"!");
    lv_obj_set_style_text_color(lbl_excl,C_BLK,0);
    lv_obj_set_style_text_font(lbl_excl,&lv_font_montserrat_14,0);
    lv_obj_center(lbl_excl);

    /* ── Main text label (lower center) ────────────────────────── */
    lbl_center=lv_label_create(scr_main);
    lv_label_set_text(lbl_center,"");
    lv_obj_set_style_text_font(lbl_center,&lv_font_montserrat_32,0);
    lv_obj_set_style_text_color(lbl_center,C_GRN,0);
    lv_obj_set_style_text_align(lbl_center,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(lbl_center,LV_ALIGN_CENTER,0,30);

    /* ── Blinking cursor ───────────────────────────────────────── */
    lbl_cursor=lv_label_create(scr_main);
    lv_label_set_text(lbl_cursor,"_");
    lv_obj_set_style_text_font(lbl_cursor,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(lbl_cursor,C_GRN,0);
    lv_obj_align(lbl_cursor,LV_ALIGN_BOTTOM_LEFT,8,-(FOOTER_H+6));

    /* ── Footer ────────────────────────────────────────────────── */
    pnl_footer=lv_obj_create(scr_main);
    lv_obj_remove_style_all(pnl_footer);
    lv_obj_set_size(pnl_footer,SCR_W,FOOTER_H);
    lv_obj_align(pnl_footer,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_set_style_bg_color(pnl_footer,C_GRN,0);
    lv_obj_set_style_bg_opa(pnl_footer,LV_OPA_COVER,0);
    lv_obj_set_style_radius(pnl_footer,0,0);
    lv_obj_set_style_border_width(pnl_footer,0,0);
    lv_obj_set_style_pad_all(pnl_footer,0,0);

    lbl_footer=lv_label_create(pnl_footer);
    lv_label_set_text(lbl_footer,"> BALUARTE.pl <");
    lv_obj_set_style_text_font(lbl_footer,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(lbl_footer,C_BLK,0);
    lv_obj_set_style_text_letter_space(lbl_footer,2,0);
    lv_obj_center(lbl_footer);

    /* ── Timers ────────────────────────────────────────────────── */
    lv_timer_create(master_cb,50,nullptr);
    lv_timer_create(eye_gaze_cb,50,nullptr);
    lv_timer_create(cursor_blink_cb,400,nullptr);
}

/* ═══════════════════════════════════════════════════════════════ */
void setup(){
    Serial.begin(115200); delay(200);
    pinMode(21,OUTPUT); digitalWrite(21,HIGH);
    pinMode(27,OUTPUT); digitalWrite(27,HIGH);
    if(!tft.init()){Serial.println("BLAD: Ekran!");while(1)delay(100);}
    tft.setRotation(0); tft.fillScreen(0);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf,buf1,nullptr,SCR_W*20);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res=SCR_W; drv.ver_res=SCR_H;
    drv.flush_cb=my_disp_flush; drv.draw_buf=&draw_buf;
    lv_disp_drv_register(&drv);

    ui_init();
    Serial.println("Billboard v5 – Eye + Text OK");
}

void loop(){
    lv_timer_handler();
    delay(5);
}
