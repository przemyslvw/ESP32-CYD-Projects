/*  BALUARTE Billboard v6 – Pro Eye + Text
 *  ESP32-CYD 2.2" 240×320 | LovyanGFX + LVGL v8 | Portrait */
#include "LGFX_ESP32_2432S022.hpp"
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

static LGFX tft;
#define SCR_W 240
#define SCR_H 320
#define FOOTER_H 45

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCR_W*20];
static lv_color_t C_GRN,C_WHT,C_RED,C_BLK,C_DKRED,C_DKGRN;

#define FX_FADE 1
#define FX_STROBE 2
#define FX_THREAT_ON 4
#define FX_THREAT_OFF 8

struct AnimStep{const char*txt;uint32_t col;uint16_t dur;uint8_t fx;};
static const AnimStep SEQ[]={
  {"HEJ!",0x00FF00,700,FX_FADE},{nullptr,0,250,0},
  {"TWOJ KOD",0xFFFFFF,800,FX_FADE},{nullptr,0,200,0},
  {"JEST",0xFF0000,550,FX_FADE|FX_THREAT_ON},
  {"BEZPIECZNY?",0xFF0000,1100,FX_FADE|FX_STROBE},{nullptr,0,500,0},
  {"NA PEWNO?",0xFF0000,900,FX_FADE|FX_STROBE},{nullptr,0,600,FX_THREAT_OFF},
  {"SPRAWDZIMY.",0x00FF00,1000,FX_FADE},{nullptr,0,300,0},
  {"SECURE",0x00FF00,600,FX_FADE},{"CODE",0x00FF00,600,FX_FADE},
  {"REVIEW",0x00FF00,900,FX_FADE},{nullptr,0,500,0},
  {"POTRZEBUJESZ\nAPKI?",0xFFFFFF,1000,FX_FADE},
  {"ALBO\nSTRONY?",0x00FF00,1000,FX_FADE},
  {nullptr,0,300,0},
  {"A MOZE\nAUDYT?",0xFFFFFF,900,FX_FADE},
  {nullptr,0,400,0},
  {"POGADAJMY!",0xFFFFFF,400,0},{"POGADAJMY!",0x00FF00,400,0},
  {"POGADAJMY!",0xFFFFFF,400,0},{"POGADAJMY!",0x00FF00,400,0},
  {"POGADAJMY!",0xFFFFFF,400,0},{"POGADAJMY!",0x00FF00,400,0},
  {nullptr,0,1500,0},
};
static const int SEQ_LEN=sizeof(SEQ)/sizeof(SEQ[0]);

/* ── Eye geometry ──────────────────────────────────────────── */
#define EYE_CX 120
#define EYE_CY 62
#define EYE_A  72
#define EYE_BU 26
#define EYE_BD 20
#define EYE_N  25

static lv_point_t up_pts[EYE_N], lo_pts[EYE_N];
static lv_point_t gup_pts[EYE_N], glo_pts[EYE_N];  // glow outline
static lv_point_t tail_l[2], tail_r[2];

struct Gaze{int8_t dx,dy;uint16_t ms;};
static const Gaze gazes[]={
  {0,0,1400},{-16,0,900},{0,0,400},{16,0,900},{0,0,500},
  {-10,-5,700},{10,5,700},{0,-6,500},{0,0,1100},
  {14,-3,800},{0,0,600},{-14,3,800},{0,0,800},
};
static const int GAZE_N=sizeof(gazes)/sizeof(gazes[0]);

/* ── LVGL objects ──────────────────────────────────────────── */
static lv_obj_t *scr_main,*accent_bar,*scan_line;
static lv_obj_t *ln_up,*ln_lo,*ln_gup,*ln_glo;     // eye outlines
static lv_obj_t *ln_tl,*ln_tr;                       // corner tails
static lv_obj_t *iris_grp;                            // iris container
static lv_obj_t *iris_glow,*iris_ring,*iris_inner;    // iris layers
static lv_obj_t *pupil_obj,*lbl_excl,*hi_dot;        // pupil + highlight
static lv_obj_t *lbl_center,*lbl_cursor,*pnl_footer,*lbl_footer;

static int cur_step=-1; static uint32_t step_t=0;
static bool threat=false;
static int gaze_i=0; static uint32_t gaze_t=0;
static int8_t gcx=0,gcy=0;
static lv_timer_t *strobe_tmr=nullptr; static int strobe_n=0;

/* ── Flush ─────────────────────────────────────────────────── */
static void my_disp_flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*p){
  uint32_t w=a->x2-a->x1+1,h=a->y2-a->y1+1;
  tft.startWrite();tft.setAddrWindow(a->x1,a->y1,w,h);
  tft.writePixels((uint16_t*)p,w*h);tft.endWrite();
  lv_disp_flush_ready(d);
}

/* ── Anim callbacks ────────────────────────────────────────── */
static void cb_opa(void*o,int32_t v){lv_obj_set_style_opa((lv_obj_t*)o,v,0);}
static void cb_ty(void*o,int32_t v){lv_obj_set_style_translate_y((lv_obj_t*)o,v,0);}
static void cb_tx(void*o,int32_t v){lv_obj_set_style_translate_x((lv_obj_t*)o,v,0);}
static void cb_y(void*o,int32_t v){lv_obj_set_y((lv_obj_t*)o,v);}

static void animate_word_in(){
  lv_anim_t a;
  lv_anim_init(&a);lv_anim_set_var(&a,lbl_center);
  lv_anim_set_values(&a,0,255);lv_anim_set_time(&a,200);
  lv_anim_set_exec_cb(&a,cb_opa);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
  lv_anim_start(&a);
  lv_anim_init(&a);lv_anim_set_var(&a,lbl_center);
  lv_anim_set_values(&a,18,0);lv_anim_set_time(&a,280);
  lv_anim_set_exec_cb(&a,cb_ty);lv_anim_set_path_cb(&a,lv_anim_path_ease_out);
  lv_anim_start(&a);
}

/* ── Strobe ────────────────────────────────────────────────── */
static void strobe_cb(lv_timer_t*t){
  strobe_n++;
  lv_obj_set_style_bg_color(scr_main,(strobe_n%2)?C_DKRED:C_BLK,0);
  if(strobe_n>=6){lv_obj_set_style_bg_color(scr_main,C_BLK,0);
    lv_timer_del(t);strobe_tmr=nullptr;}
}
static void start_strobe(){
  if(strobe_tmr)lv_timer_del(strobe_tmr);
  strobe_n=0;strobe_tmr=lv_timer_create(strobe_cb,80,nullptr);
}

/* ── Eye color (threat/normal) ─────────────────────────────── */
static void set_eye_color(lv_color_t c){
  lv_obj_set_style_line_color(ln_up,c,0);
  lv_obj_set_style_line_color(ln_lo,c,0);
  lv_obj_set_style_line_color(ln_tl,c,0);
  lv_obj_set_style_line_color(ln_tr,c,0);
  lv_obj_set_style_line_color(ln_gup,c,0);
  lv_obj_set_style_line_color(ln_glo,c,0);
  lv_obj_set_style_border_color(iris_ring,c,0);
  lv_obj_set_style_border_color(iris_inner,c,0);
  lv_obj_set_style_bg_color(iris_glow,c,0);
  lv_obj_set_style_bg_color(pupil_obj,c,0);
  lv_obj_set_style_text_color(lbl_excl,c,0);
  lv_obj_set_style_bg_color(accent_bar,c,0);
  lv_obj_set_style_bg_color(scan_line,c,0);
}
static void enter_threat(){
  if(threat)return;threat=true;set_eye_color(C_RED);
  lv_obj_set_style_bg_color(pnl_footer,C_RED,0);
  lv_label_set_text(lbl_footer,"! ALERT !");
  lv_obj_set_style_border_color(scr_main,C_RED,0);
  lv_obj_set_style_border_width(scr_main,2,0);
}
static void exit_threat(){
  if(!threat)return;threat=false;set_eye_color(C_GRN);
  lv_obj_set_style_bg_color(scr_main,C_BLK,0);
  lv_obj_set_style_bg_color(pnl_footer,C_GRN,0);
  lv_label_set_text(lbl_footer,"> BALUARTE.pl <");
  lv_obj_set_style_border_color(scr_main,C_DKGRN,0);
  lv_obj_set_style_border_width(scr_main,1,0);
}

/* ── Apply step ────────────────────────────────────────────── */
static void apply_step(int i){
  auto&s=SEQ[i];
  if(s.txt){lv_label_set_text(lbl_center,s.txt);
    lv_obj_set_style_text_color(lbl_center,lv_color_hex(s.col),0);}
  else lv_label_set_text(lbl_center,"");
  lv_obj_align(lbl_center,LV_ALIGN_CENTER,0,30);
  if(s.fx&FX_THREAT_ON)enter_threat();
  if(s.fx&FX_THREAT_OFF)exit_threat();
  if(s.fx&FX_FADE)animate_word_in();
  if(s.fx&FX_STROBE)start_strobe();
}

/* ── Timers ────────────────────────────────────────────────── */
static void master_cb(lv_timer_t*){
  uint32_t now=lv_tick_get();
  if(cur_step<0||now-step_t>=SEQ[cur_step].dur){
    cur_step=(cur_step+1)%SEQ_LEN;step_t=now;apply_step(cur_step);}
}
static void eye_gaze_cb(lv_timer_t*){
  uint32_t now=lv_tick_get();
  if(now-gaze_t<gazes[gaze_i].ms)return;
  gaze_i=(gaze_i+1)%GAZE_N;gaze_t=now;
  int8_t nx=gazes[gaze_i].dx,ny=gazes[gaze_i].dy;
  lv_anim_t a;
  lv_anim_init(&a);lv_anim_set_var(&a,iris_grp);
  lv_anim_set_values(&a,gcx,nx);lv_anim_set_time(&a,300);
  lv_anim_set_exec_cb(&a,cb_tx);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
  lv_anim_start(&a);
  lv_anim_init(&a);lv_anim_set_var(&a,iris_grp);
  lv_anim_set_values(&a,gcy,ny);lv_anim_set_time(&a,300);
  lv_anim_set_exec_cb(&a,cb_ty);lv_anim_set_path_cb(&a,lv_anim_path_ease_in_out);
  lv_anim_start(&a);
  gcx=nx;gcy=ny;
}
static void cursor_blink_cb(lv_timer_t*){
  static bool v=true;v=!v;
  if(v)lv_obj_clear_flag(lbl_cursor,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(lbl_cursor,LV_OBJ_FLAG_HIDDEN);
}

/* ── Compute eye outline points ────────────────────────────── */
static void compute_eye_pts(){
  for(int i=0;i<EYE_N;i++){
    float t=3.14159265f*(float)i/(float)(EYE_N-1);
    float cv=cosf(3.14159265f-t),sv=sinf(t);
    up_pts[i].x=EYE_CX+(lv_coord_t)(EYE_A*cv);
    up_pts[i].y=EYE_CY-(lv_coord_t)(EYE_BU*sv);
    lo_pts[i].x=EYE_CX+(lv_coord_t)(EYE_A*cv);
    lo_pts[i].y=EYE_CY+(lv_coord_t)(EYE_BD*sv);
    // glow outline (slightly larger)
    gup_pts[i].x=EYE_CX+(lv_coord_t)((EYE_A+5)*cv);
    gup_pts[i].y=EYE_CY-(lv_coord_t)((EYE_BU+4)*sv);
    glo_pts[i].x=EYE_CX+(lv_coord_t)((EYE_A+5)*cv);
    glo_pts[i].y=EYE_CY+(lv_coord_t)((EYE_BD+3)*sv);
  }
  // corner tails
  tail_l[0]={EYE_CX-EYE_A-18,EYE_CY-5};
  tail_l[1]={EYE_CX-EYE_A+4,EYE_CY};
  tail_r[0]={EYE_CX+EYE_A-4,EYE_CY};
  tail_r[1]={EYE_CX+EYE_A+18,EYE_CY-5};
}

/* helper: create styled line */
static lv_obj_t*mk_line(lv_obj_t*par,lv_point_t*pts,int n,int w,lv_opa_t op){
  lv_obj_t*l=lv_line_create(par);
  lv_line_set_points(l,pts,n);
  lv_obj_set_style_line_color(l,C_GRN,0);
  lv_obj_set_style_line_width(l,w,0);
  lv_obj_set_style_line_rounded(l,true,0);
  lv_obj_set_style_line_opa(l,op,0);
  return l;
}

/* helper: create circle obj */
static lv_obj_t*mk_circle(lv_obj_t*par,int sz,lv_color_t bg,lv_opa_t bgop,
    lv_color_t brd,int bw){
  lv_obj_t*o=lv_obj_create(par);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o,sz,sz);
  lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_color(o,bg,0);
  lv_obj_set_style_bg_opa(o,bgop,0);
  if(bw>0){lv_obj_set_style_border_color(o,brd,0);
    lv_obj_set_style_border_width(o,bw,0);
    lv_obj_set_style_border_opa(o,LV_OPA_COVER,0);}
  lv_obj_set_style_pad_all(o,0,0);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

/* ── Build UI ──────────────────────────────────────────────── */
static void ui_init(){
  C_GRN=lv_color_hex(0x00FF00);C_WHT=lv_color_hex(0xFFFFFF);
  C_RED=lv_color_hex(0xFF0000);C_BLK=lv_color_hex(0x000000);
  C_DKRED=lv_color_hex(0x330000);C_DKGRN=lv_color_hex(0x003300);

  scr_main=lv_scr_act();
  lv_obj_set_style_bg_color(scr_main,C_BLK,0);
  lv_obj_set_style_bg_opa(scr_main,LV_OPA_COVER,0);
  lv_obj_set_style_border_color(scr_main,C_DKGRN,0);
  lv_obj_set_style_border_width(scr_main,1,0);

  /* Accent bar */
  accent_bar=mk_circle(scr_main,0,C_GRN,LV_OPA_COVER,C_BLK,0);
  lv_obj_set_size(accent_bar,SCR_W,3);
  lv_obj_set_style_radius(accent_bar,0,0);
  lv_obj_align(accent_bar,LV_ALIGN_TOP_MID,0,0);
  {lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,accent_bar);
   lv_anim_set_values(&a,60,255);lv_anim_set_time(&a,1200);
   lv_anim_set_playback_time(&a,1200);
   lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);
   lv_anim_set_exec_cb(&a,cb_opa);lv_anim_start(&a);}

  /* Scan line */
  scan_line=lv_obj_create(scr_main);lv_obj_remove_style_all(scan_line);
  lv_obj_set_size(scan_line,SCR_W,2);
  lv_obj_set_style_bg_color(scan_line,C_GRN,0);
  lv_obj_set_style_bg_opa(scan_line,80,0);
  {lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,scan_line);
   lv_anim_set_values(&a,0,SCR_H-FOOTER_H);lv_anim_set_time(&a,2500);
   lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);
   lv_anim_set_exec_cb(&a,cb_y);lv_anim_start(&a);}

  /* ── EYE ─────────────────────────────────────────────────── */
  compute_eye_pts();

  // Glow outline (larger, dim)
  ln_gup=mk_line(scr_main,gup_pts,EYE_N,2,80);
  ln_glo=mk_line(scr_main,glo_pts,EYE_N,2,80);
  // Main outline
  ln_up=mk_line(scr_main,up_pts,EYE_N,3,LV_OPA_COVER);
  ln_lo=mk_line(scr_main,lo_pts,EYE_N,3,LV_OPA_COVER);
  // Corner tails
  ln_tl=mk_line(scr_main,tail_l,2,3,LV_OPA_COVER);
  ln_tr=mk_line(scr_main,tail_r,2,3,LV_OPA_COVER);

  // Iris group (transparent container for all iris layers)
  iris_grp=lv_obj_create(scr_main);lv_obj_remove_style_all(iris_grp);
  lv_obj_set_size(iris_grp,62,62);
  lv_obj_set_pos(iris_grp,EYE_CX-31,EYE_CY-31);
  lv_obj_clear_flag(iris_grp,LV_OBJ_FLAG_SCROLLABLE);

  // Outer glow (soft green halo)
  iris_glow=mk_circle(iris_grp,58,C_GRN,30,C_BLK,0);
  lv_obj_center(iris_glow);

  // Iris ring (main circle with border)
  iris_ring=mk_circle(iris_grp,46,C_BLK,LV_OPA_COVER,C_GRN,2);
  lv_obj_center(iris_ring);

  // Inner decorative ring
  iris_inner=mk_circle(iris_ring,34,C_BLK,0,C_GRN,1);
  lv_obj_center(iris_inner);

  // Pupil (green filled)
  pupil_obj=mk_circle(iris_ring,22,C_GRN,LV_OPA_COVER,C_BLK,0);
  lv_obj_center(pupil_obj);

  // "!" inside pupil
  lbl_excl=lv_label_create(iris_ring);
  lv_label_set_text(lbl_excl,"!");
  lv_obj_set_style_text_color(lbl_excl,C_BLK,0);
  lv_obj_set_style_text_font(lbl_excl,&lv_font_montserrat_14,0);
  lv_obj_center(lbl_excl);

  // Specular highlight (white dot, upper-left offset)
  hi_dot=mk_circle(iris_ring,8,C_WHT,LV_OPA_COVER,C_BLK,0);
  lv_obj_align(hi_dot,LV_ALIGN_CENTER,-9,-9);

  /* ── Text label (lower center) ───────────────────────────── */
  lbl_center=lv_label_create(scr_main);
  lv_label_set_text(lbl_center,"");
  lv_obj_set_style_text_font(lbl_center,&lv_font_montserrat_32,0);
  lv_obj_set_style_text_color(lbl_center,C_GRN,0);
  lv_obj_set_style_text_align(lbl_center,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_align(lbl_center,LV_ALIGN_CENTER,0,30);

  /* Cursor */
  lbl_cursor=lv_label_create(scr_main);
  lv_label_set_text(lbl_cursor,"_");
  lv_obj_set_style_text_font(lbl_cursor,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(lbl_cursor,C_GRN,0);
  lv_obj_align(lbl_cursor,LV_ALIGN_BOTTOM_LEFT,8,-(FOOTER_H+6));

  /* Footer */
  pnl_footer=lv_obj_create(scr_main);lv_obj_remove_style_all(pnl_footer);
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

  /* Timers */
  lv_timer_create(master_cb,50,nullptr);
  lv_timer_create(eye_gaze_cb,50,nullptr);
  lv_timer_create(cursor_blink_cb,400,nullptr);
}

void setup(){
  Serial.begin(115200);delay(200);
  pinMode(21,OUTPUT);digitalWrite(21,HIGH);
  pinMode(27,OUTPUT);digitalWrite(27,HIGH);
  if(!tft.init()){Serial.println("ERR");while(1)delay(100);}
  tft.setRotation(0);tft.fillScreen(0);
  lv_init();
  lv_disp_draw_buf_init(&draw_buf,buf1,nullptr,SCR_W*20);
  static lv_disp_drv_t drv;lv_disp_drv_init(&drv);
  drv.hor_res=SCR_W;drv.ver_res=SCR_H;
  drv.flush_cb=my_disp_flush;drv.draw_buf=&draw_buf;
  lv_disp_drv_register(&drv);
  ui_init();
  Serial.println("Billboard v6 OK");
}

void loop(){lv_timer_handler();delay(5);}
