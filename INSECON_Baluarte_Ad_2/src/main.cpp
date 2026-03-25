/**
 * ═══════════════════════════════════════════════════════════════════
 *  BALUARTE – Hacker Billboard v4  |  ESP32-CYD 2.2" (240×320)
 *  LovyanGFX + LVGL v8  |  Portrait
 * ═══════════════════════════════════════════════════════════════════
 *  Engaging animated billboard with:
 *  - Fade-in + slide-up per word (lv_anim_t)
 *  - Continuous scan-line sweep
 *  - Pulsing accent bar
 *  - Background strobe during threat phase
 *  - Dynamic footer (green → red ALERT → green)
 *  - Blinking terminal cursor
 *  - Variable timing per step
 * ═══════════════════════════════════════════════════════════════════
 */

#include "LGFX_ESP32_2432S022.hpp"
#include <Arduino.h>
#include <lvgl.h>

// ─── LovyanGFX instance ─────────────────────────────────────────
static LGFX tft;

// ─── Screen geometry ─────────────────────────────────────────────
#define SCR_W     240
#define SCR_H     320
#define FOOTER_H   45

// ─── LVGL draw buffer ───────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf1[SCR_W * 20];

// ─── Colours ─────────────────────────────────────────────────────
static const lv_color_t C_GREEN   = lv_color_hex(0x00FF00);
static const lv_color_t C_WHITE   = lv_color_hex(0xFFFFFF);
static const lv_color_t C_RED     = lv_color_hex(0xFF0000);
static const lv_color_t C_BLACK   = lv_color_hex(0x000000);
static const lv_color_t C_DKRED   = lv_color_hex(0x330000);
static const lv_color_t C_DKGREEN = lv_color_hex(0x003300);

// ─── Effect bit-flags ────────────────────────────────────────────
#define FX_FADE       (1 << 0)
#define FX_STROBE     (1 << 1)
#define FX_THREAT_ON  (1 << 2)
#define FX_THREAT_OFF (1 << 3)

// ─── Animation step ──────────────────────────────────────────────
struct AnimStep {
    const char *text;       // nullptr = blank frame
    uint32_t    colHex;
    uint16_t    dur;        // ms this step lasts
    uint8_t     fx;
};

static const AnimStep SEQ[] = {
    /* ── Opener ─────────────────────────────────────── */
    { "HEJ!",          0x00FF00,  700, FX_FADE },
    { nullptr,          0,         250, 0 },
    { "TWOJ KOD",      0xFFFFFF,  800, FX_FADE },
    { nullptr,          0,         200, 0 },
    /* ── Threat ─────────────────────────────────────── */
    { "JEST",           0xFF0000,  550, FX_FADE | FX_THREAT_ON },
    { "BEZPIECZNY?",    0xFF0000, 1100, FX_FADE | FX_STROBE },
    { nullptr,          0,         500, 0 },
    { "NA PEWNO?",      0xFF0000,  900, FX_FADE | FX_STROBE },
    { nullptr,          0,         600, FX_THREAT_OFF },
    /* ── Solution ───────────────────────────────────── */
    { "SPRAWDZIMY.",    0x00FF00, 1000, FX_FADE },
    { nullptr,          0,         300, 0 },
    { "SECURE",         0x00FF00,  600, FX_FADE },
    { "CODE",           0x00FF00,  600, FX_FADE },
    { "REVIEW",         0x00FF00,  900, FX_FADE },
    { nullptr,          0,         400, 0 },
    /* ── CTA blink ──────────────────────────────────── */
    { "POGADAJMY!",     0xFFFFFF,  400, 0 },
    { "POGADAJMY!",     0x00FF00,  400, 0 },
    { "POGADAJMY!",     0xFFFFFF,  400, 0 },
    { "POGADAJMY!",     0x00FF00,  400, 0 },
    { "POGADAJMY!",     0xFFFFFF,  400, 0 },
    { "POGADAJMY!",     0x00FF00,  400, 0 },
    /* ── Breath pause ───────────────────────────────── */
    { nullptr,          0,        1500, 0 },
};
static const int SEQ_LEN = sizeof(SEQ) / sizeof(SEQ[0]);

// ─── LVGL objects ────────────────────────────────────────────────
static lv_obj_t *scr_main   = nullptr;
static lv_obj_t *accent_bar = nullptr;
static lv_obj_t *scan_line  = nullptr;
static lv_obj_t *lbl_center = nullptr;
static lv_obj_t *lbl_cursor = nullptr;
static lv_obj_t *pnl_footer = nullptr;
static lv_obj_t *lbl_footer = nullptr;

// ─── State ───────────────────────────────────────────────────────
static int      cur_step   = -1;
static uint32_t step_start = 0;
static bool     in_threat  = false;

// ─── Strobe sub-timer ────────────────────────────────────────────
static lv_timer_t *strobe_tmr = nullptr;
static int         strobe_cnt = 0;

// ═══════════════════════════════════════════════════════════════════
//  Flush callback: LVGL buffer → LovyanGFX
// ═══════════════════════════════════════════════════════════════════
static void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t *)px, w * h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// ═══════════════════════════════════════════════════════════════════
//  Animation helpers (lv_anim exec callbacks)
// ═══════════════════════════════════════════════════════════════════
static void cb_opa(void *o, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}
static void cb_ty(void *o, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)o, (lv_coord_t)v, 0);
}
static void cb_y(void *o, int32_t v) {
    lv_obj_set_y((lv_obj_t *)o, (lv_coord_t)v);
}

// Fade-in + slide-up on main label
static void animate_word_in() {
    lv_anim_t a;
    // opacity 0 → 255
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_center);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, cb_opa);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    // translate_y +18 → 0
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_center);
    lv_anim_set_values(&a, 18, 0);
    lv_anim_set_time(&a, 280);
    lv_anim_set_exec_cb(&a, cb_ty);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ═══════════════════════════════════════════════════════════════════
//  Strobe effect (rapid bg flashes)
// ═══════════════════════════════════════════════════════════════════
static void strobe_cb(lv_timer_t *t) {
    strobe_cnt++;
    lv_obj_set_style_bg_color(scr_main,
        (strobe_cnt % 2) ? C_DKRED : C_BLACK, 0);
    if (strobe_cnt >= 6) {
        lv_obj_set_style_bg_color(scr_main, C_BLACK, 0);
        lv_timer_del(t);
        strobe_tmr = nullptr;
    }
}
static void start_strobe() {
    if (strobe_tmr) { lv_timer_del(strobe_tmr); }
    strobe_cnt = 0;
    strobe_tmr = lv_timer_create(strobe_cb, 80, nullptr);
}

// ═══════════════════════════════════════════════════════════════════
//  Threat mode on/off (footer + border change)
// ═══════════════════════════════════════════════════════════════════
static void enter_threat() {
    if (in_threat) return;
    in_threat = true;
    lv_obj_set_style_bg_color(pnl_footer, C_RED, 0);
    lv_label_set_text(lbl_footer, "! ALERT !");
    lv_obj_set_style_border_color(scr_main, C_RED, 0);
    lv_obj_set_style_border_width(scr_main, 2, 0);
    lv_obj_set_style_bg_color(accent_bar, C_RED, 0);
}
static void exit_threat() {
    if (!in_threat) return;
    in_threat = false;
    lv_obj_set_style_bg_color(scr_main, C_BLACK, 0);
    lv_obj_set_style_bg_color(pnl_footer, C_GREEN, 0);
    lv_label_set_text(lbl_footer, "> BALUARTE.pl <");
    lv_obj_set_style_border_color(scr_main, C_DKGREEN, 0);
    lv_obj_set_style_border_width(scr_main, 1, 0);
    lv_obj_set_style_bg_color(accent_bar, C_GREEN, 0);
}

// ═══════════════════════════════════════════════════════════════════
//  Apply a single animation step
// ═══════════════════════════════════════════════════════════════════
static void apply_step(int idx) {
    const AnimStep &s = SEQ[idx];

    // Text
    if (s.text) {
        lv_label_set_text(lbl_center, s.text);
        lv_obj_set_style_text_color(lbl_center, lv_color_hex(s.colHex), 0);
        lv_obj_clear_flag(lbl_center, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lbl_center, "");
    }
    lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, -(FOOTER_H / 2));

    // Effects
    if (s.fx & FX_THREAT_ON)  enter_threat();
    if (s.fx & FX_THREAT_OFF) exit_threat();
    if (s.fx & FX_FADE)       animate_word_in();
    if (s.fx & FX_STROBE)     start_strobe();
}

// ═══════════════════════════════════════════════════════════════════
//  Master timer (50 ms tick) — drives the state machine
// ═══════════════════════════════════════════════════════════════════
static void master_cb(lv_timer_t *t) {
    uint32_t now = lv_tick_get();

    if (cur_step < 0 || (now - step_start >= SEQ[cur_step].dur)) {
        cur_step++;
        if (cur_step >= SEQ_LEN) cur_step = 0;
        step_start = now;
        apply_step(cur_step);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Cursor blink timer (400 ms)
// ═══════════════════════════════════════════════════════════════════
static void cursor_blink_cb(lv_timer_t *t) {
    static bool vis = true;
    vis = !vis;
    if (vis) lv_obj_clear_flag(lbl_cursor, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(lbl_cursor, LV_OBJ_FLAG_HIDDEN);
}

// ═══════════════════════════════════════════════════════════════════
//  Build the UI
// ═══════════════════════════════════════════════════════════════════
static void ui_init() {
    // ── Screen ───────────────────────────────────────────────────
    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, C_BLACK, 0);
    lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(scr_main, C_DKGREEN, 0);
    lv_obj_set_style_border_width(scr_main, 1, 0);
    lv_obj_set_style_border_opa(scr_main, LV_OPA_COVER, 0);

    // ── Pulsing accent bar (top 3 px) ────────────────────────────
    accent_bar = lv_obj_create(scr_main);
    lv_obj_remove_style_all(accent_bar);
    lv_obj_set_size(accent_bar, SCR_W, 3);
    lv_obj_align(accent_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(accent_bar, C_GREEN, 0);
    lv_obj_set_style_bg_opa(accent_bar, LV_OPA_COVER, 0);
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, accent_bar);
        lv_anim_set_values(&a, 60, 255);
        lv_anim_set_time(&a, 1200);
        lv_anim_set_playback_time(&a, 1200);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, cb_opa);
        lv_anim_start(&a);
    }

    // ── Scanning line (2 px, sweeps top→bottom, 40% opa) ─────────
    scan_line = lv_obj_create(scr_main);
    lv_obj_remove_style_all(scan_line);
    lv_obj_set_size(scan_line, SCR_W, 2);
    lv_obj_set_style_bg_color(scan_line, C_GREEN, 0);
    lv_obj_set_style_bg_opa(scan_line, (lv_opa_t)100, 0);
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, scan_line);
        lv_anim_set_values(&a, 0, SCR_H - FOOTER_H);
        lv_anim_set_time(&a, 2500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, cb_y);
        lv_anim_start(&a);
    }

    // ── Main label (center) ──────────────────────────────────────
    lbl_center = lv_label_create(scr_main);
    lv_label_set_text(lbl_center, "");
    lv_obj_set_style_text_font(lbl_center, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_center, C_GREEN, 0);
    lv_obj_set_style_text_align(lbl_center, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, -(FOOTER_H / 2));

    // ── Blinking cursor (bottom-left area) ───────────────────────
    lbl_cursor = lv_label_create(scr_main);
    lv_label_set_text(lbl_cursor, "_");
    lv_obj_set_style_text_font(lbl_cursor, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_cursor, C_GREEN, 0);
    lv_obj_align(lbl_cursor, LV_ALIGN_BOTTOM_LEFT, 8, -(FOOTER_H + 6));

    // ── Footer panel ─────────────────────────────────────────────
    pnl_footer = lv_obj_create(scr_main);
    lv_obj_remove_style_all(pnl_footer);
    lv_obj_set_size(pnl_footer, SCR_W, FOOTER_H);
    lv_obj_align(pnl_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(pnl_footer, C_GREEN, 0);
    lv_obj_set_style_bg_opa(pnl_footer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pnl_footer, 0, 0);
    lv_obj_set_style_border_width(pnl_footer, 0, 0);
    lv_obj_set_style_pad_all(pnl_footer, 0, 0);

    lbl_footer = lv_label_create(pnl_footer);
    lv_label_set_text(lbl_footer, "> BALUARTE.pl <");
    lv_obj_set_style_text_font(lbl_footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_footer, C_BLACK, 0);
    lv_obj_set_style_text_letter_space(lbl_footer, 2, 0);
    lv_obj_center(lbl_footer);

    // ── Timers ───────────────────────────────────────────────────
    lv_timer_create(master_cb, 50, nullptr);       // main state machine
    lv_timer_create(cursor_blink_cb, 400, nullptr); // cursor blink
}

// ═══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(21, OUTPUT); digitalWrite(21, HIGH);
    pinMode(27, OUTPUT); digitalWrite(27, HIGH);

    Serial.println("BALUARTE Billboard v4.0");

    if (!tft.init()) {
        Serial.println("BLAD: Ekran!");
        while (1) delay(100);
    }
    tft.setRotation(0);
    tft.fillScreen(0x0000);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, SCR_W * 20);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res  = SCR_W;
    drv.ver_res  = SCR_H;
    drv.flush_cb = my_disp_flush;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);

    ui_init();
    Serial.println("Billboard uruchomiony.");
}

// ═══════════════════════════════════════════════════════════════════
void loop() {
    lv_timer_handler();
    delay(5);
}
