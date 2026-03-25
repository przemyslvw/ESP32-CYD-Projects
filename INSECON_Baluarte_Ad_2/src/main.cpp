/**
 * ═══════════════════════════════════════════════════════════════════
 *  BALUARTE – Haker Billboard  |  ESP32-CYD 2.2" (240×320)
 *  Silnik: LovyanGFX + LVGL v8  |  Orientacja: portrait
 * ═══════════════════════════════════════════════════════════════════
 *
 *  Jedno ogromne słowo na środku ekranu, podmieniane timerem.
 *  Stopka BALUARTE.pl zawsze widoczna.
 * ═══════════════════════════════════════════════════════════════════
 */

#include "LGFX_ESP32_2432S022.hpp"
#include <Arduino.h>
#include <lvgl.h>

// ─── Instancja wyświetlacza LovyanGFX ────────────────────────────
static LGFX tft;

// ─── Konfiguracja ekranu ─────────────────────────────────────────
#define SCR_W  240
#define SCR_H  320

// ─── Bufor LVGL ──────────────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCR_W * 20];   // 20-liniowy bufor

// ─── Obiekty LVGL ────────────────────────────────────────────────
static lv_obj_t  *scr_main;          // ekran główny
static lv_obj_t  *lbl_center;        // główny label na środku
static lv_obj_t  *pnl_footer;        // panel stopki
static lv_obj_t  *lbl_footer;        // tekst stopki

// ─── Kolory ──────────────────────────────────────────────────────
#define CLR_GREEN    lv_color_hex(0x00FF00)
#define CLR_WHITE    lv_color_hex(0xFFFFFF)
#define CLR_RED      lv_color_hex(0xFF0000)
#define CLR_BLACK    lv_color_hex(0x000000)
#define CLR_DARKRED  lv_color_hex(0x220000)

// ─── Wysokość stopki ─────────────────────────────────────────────
#define FOOTER_H  45

// ═══════════════════════════════════════════════════════════════════
//  Struktura jednego kroku animacji
// ═══════════════════════════════════════════════════════════════════
struct AnimStep {
    const char *text;       // tekst do wyświetlenia (nullptr = pusta klatka)
    lv_color_t  color;      // kolor tekstu
    bool        flash_bg;   // błysk tła na ciemnoczerwono?
    int8_t      blink_id;   // >0 → klatka migotania (1=biały, 2=zielony)
};

// Sekwencja kroków (indeksowane od 0)
// Krok "[ POGADAJMY ]" pojawia się dwukrotnie (migotanie biały/zielony)
static const AnimStep steps[] = {
    /* 0  */ { "EJ,",           CLR_GREEN,  false, 0 },
    /* 1  */ { "TY!",           CLR_GREEN,  false, 0 },
    /* 2  */ { "POTRZEBUJESZ",  CLR_WHITE,  false, 0 },
    /* 3  */ { "APKI?",         CLR_GREEN,  false, 0 },
    /* 4  */ { "ALBO",          CLR_WHITE,  false, 0 },
    /* 5  */ { "STRONY?",       CLR_GREEN,  false, 0 },
    /* 6  */ { nullptr,         CLR_BLACK,  false, 0 },   // pusta klatka
    /* 7  */ { "A MOZE...",     CLR_WHITE,  false, 0 },
    /* 8  */ { "BOISZ SIE",    CLR_RED,    false, 0 },
    /* 9  */ { "O DANE?",       CLR_RED,    true,  0 },   // błysk tła
    /* 10 */ { "ZROBIMY",       CLR_WHITE,  false, 0 },
    /* 11 */ { "SECURE",        CLR_GREEN,  false, 0 },
    /* 12 */ { "CODE",          CLR_GREEN,  false, 0 },
    /* 13 */ { "REVIEW.",       CLR_GREEN,  false, 0 },
    /* 14 */ { "POGADAJMY",     CLR_WHITE,  false, 1 },   // migotanie: biały
    /* 15 */ { "POGADAJMY",     CLR_GREEN,  false, 2 },   // migotanie: zielony
};

static const int STEP_COUNT = sizeof(steps) / sizeof(steps[0]);
static int       gStep = 0;

// ═══════════════════════════════════════════════════════════════════
//  Callback flush – kopia buforów LVGL → LovyanGFX
// ═══════════════════════════════════════════════════════════════════
static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                          lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t *)color_p, w * h);
    tft.endWrite();

    lv_disp_flush_ready(disp_drv);
}

// ═══════════════════════════════════════════════════════════════════
//  Timer callback – podmiana słów na ekranie (maszyna stanów)
// ═══════════════════════════════════════════════════════════════════
static void anim_timer_cb(lv_timer_t *timer) {
    (void)timer;

    const AnimStep &s = steps[gStep];

    // ── Tło sceny ────────────────────────────────────────────────
    if (s.flash_bg) {
        // Błysk ciemnoczerwony na 1 takt
        lv_obj_set_style_bg_color(scr_main, CLR_DARKRED, 0);
    } else {
        lv_obj_set_style_bg_color(scr_main, CLR_BLACK, 0);
    }

    // ── Tekst główny ────────────────────────────────────────────
    if (s.text == nullptr) {
        // Pusta klatka
        lv_label_set_text(lbl_center, "");
    } else {
        lv_label_set_text(lbl_center, s.text);
        lv_obj_set_style_text_color(lbl_center, s.color, 0);
    }

    // Wycentruj ponownie po zmianie tekstu
    lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, -(FOOTER_H / 2));

    // ── Następny krok ───────────────────────────────────────────
    gStep++;
    if (gStep >= STEP_COUNT) {
        gStep = 0;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Inicjalizacja interfejsu LVGL (ekran + stopka + label)
// ═══════════════════════════════════════════════════════════════════
static void ui_init(void) {
    // ── Ekran główny ─────────────────────────────────────────────
    scr_main = lv_scr_act();
    lv_obj_set_style_bg_color(scr_main, CLR_BLACK, 0);
    lv_obj_set_style_bg_opa(scr_main, LV_OPA_COVER, 0);

    // ── Główny label (centrum) ───────────────────────────────────
    lbl_center = lv_label_create(scr_main);
    lv_label_set_text(lbl_center, "");
    lv_obj_set_style_text_font(lbl_center, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_center, CLR_GREEN, 0);
    lv_obj_set_style_text_align(lbl_center, LV_TEXT_ALIGN_CENTER, 0);
    // Przesunięty lekko w górę, żeby nie nachodzić na stopkę
    lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, -(FOOTER_H / 2));

    // ── Stopka – panel ───────────────────────────────────────────
    pnl_footer = lv_obj_create(scr_main);
    lv_obj_remove_style_all(pnl_footer);
    lv_obj_set_size(pnl_footer, SCR_W, FOOTER_H);
    lv_obj_align(pnl_footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(pnl_footer, CLR_GREEN, 0);
    lv_obj_set_style_bg_opa(pnl_footer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pnl_footer, 0, 0);
    lv_obj_set_style_border_width(pnl_footer, 0, 0);
    lv_obj_set_style_pad_all(pnl_footer, 0, 0);

    // ── Stopka – tekst ──────────────────────────────────────────
    lbl_footer = lv_label_create(pnl_footer);
    lv_label_set_text(lbl_footer, "> BALUARTE.pl <");
    lv_obj_set_style_text_font(lbl_footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_footer, CLR_BLACK, 0);
    lv_obj_set_style_text_letter_space(lbl_footer, 2, 0);
    lv_obj_center(lbl_footer);

    // ── Timer animacji: ~620 ms między krokami ───────────────────
    lv_timer_create(anim_timer_cb, 620, NULL);
}

// ═══════════════════════════════════════════════════════════════════
//  setup()
// ═══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(200);

    // Podświetlenie i zasilanie
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);

    Serial.println("BALUARTE Billboard v3.0 — LVGL");

    // ── Inicjalizacja wyświetlacza ──────────────────────────────
    if (!tft.init()) {
        Serial.println("BLAD: Ekran nie zainicjalizowany!");
        while (1) delay(100);
    }
    tft.setRotation(0);      // portrait 240×320
    tft.fillScreen(0x0000);

    // ── Inicjalizacja LVGL ──────────────────────────────────────
    lv_init();

    // Bufor rysowania
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCR_W * 20);

    // Sterownik wyświetlacza
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCR_W;
    disp_drv.ver_res  = SCR_H;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // ── Budowa interfejsu ───────────────────────────────────────
    ui_init();

    Serial.println("Billboard uruchomiony.");
}

// ═══════════════════════════════════════════════════════════════════
//  loop() — wystarczy karmić LVGL handlerem
// ═══════════════════════════════════════════════════════════════════
void loop() {
    lv_timer_handler();
    delay(5);
}
