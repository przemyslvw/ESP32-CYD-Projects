#include "LGFX_ESP32_2432S022.hpp"
#include <Arduino.h>

static LGFX tft;

// ─── Kolory palety hakerskiej ─────────────────────────────────────
#define C_BG        0x0000   // czarne tło
#define C_GREEN     0x07E0   // klasyczny matrix green
#define C_DIMGREEN  0x03A0   // przyciemniony green
#define C_CYAN      0x07FF
#define C_DIMCYAN   0x0410
#define C_ORANGE    0xFBE0
#define C_WHITE     0xFFFF
#define C_GREY      0x4208
#define C_DARKGREY  0x2104
#define C_MAGENTA   0xF81F
#define C_RED       0xF800

// ─── Konfiguracja ekranu ──────────────────────────────────────────
#define SCR_W  240
#define SCR_H  320

// ─── Matrix Rain ──────────────────────────────────────────────────
#define RAIN_COLS    20
#define RAIN_SPEED   35   // ms między ramkami deszczu
#define CHAR_W       12
#define CHAR_H       16

static int rainY[RAIN_COLS];
static int rainLen[RAIN_COLS];
static int rainSpeed[RAIN_COLS];

// ─── Stany animacji (maszyna stanów pętli) ────────────────────────
enum AnimState {
  BOOT_SEQUENCE,
  MATRIX_RAIN,
  NETWORK_SCAN,
  LOGO_REVEAL,
  SERVICES_LIST,
  CTA_PULSE,
  FADE_OUT
};

static AnimState  gState         = BOOT_SEQUENCE;
static uint32_t   gStateStart    = 0;
static int        gSubStep       = 0;
static uint32_t   gLastFrame     = 0;

// ─── Pomocniczy tekst bootowania ──────────────────────────────────
static const char* bootLines[] = {
  "> init secure_kernel v3.7.2",
  "> loading crypto_modules...",
  "> sha256_verify: [OK]",
  "> ssl_handshake: TLS1.3 [OK]",
  "> firewall: 24 rules loaded",
  "> intrusion_detect: active",
  "> pen_test_engine: armed",
  "> INSECON_NODE: online",
  "",
  "  [ BALUARTE SYSTEMS ]",
  "  >> ALL MODULES READY <<"
};
static const int bootLineCount = sizeof(bootLines) / sizeof(bootLines[0]);

// ─── Dane skanowania sieci ────────────────────────────────────────
static const char* scanLines[] = {
  "SCAN> 192.168.1.0/24",
  "  [+] host 192.168.1.1  OPEN",
  "  [+] port 443 (https)  OPEN",
  "  [+] port  80 (http)   FILTERED",
  "  [!] vuln CVE-2024-31402 detect",
  "  [*] threat level: HIGH",
  "SCAN> reverse DNS lookup...",
  "  [+] FQDN: gw.target.local",
  "SCAN> running exploit check...",
  "  [+] patched: 0  unpatched: 3",
  "",
  "  >> BALUARTE SHIELD: ACTIVE <<"
};
static const int scanLineCount = sizeof(scanLines) / sizeof(scanLines[0]);

// ─── Usługi BALUARTE ─────────────────────────────────────────────
struct ServiceLine {
  const char* icon;
  const char* text;
  uint16_t    color;
};

static const ServiceLine services[] = {
  {"[>]", " Secure Code Review",    C_GREEN},
  {"[>]", " Pentesty & Red Team",   C_GREEN},
  {"[>]", " Techniczne SEO",        C_CYAN},
  {"[>]", " Core Web Vitals",       C_CYAN},
  {"[>]", " Dedykowane Aplikacje",  C_ORANGE},
  {"[>]", " Automatyzacja QA",      C_ORANGE},
  {"[>]", " Audyt Bezpieczenstwa",  C_MAGENTA},
  {"[>]", " Ochrona Infrastruktury",C_MAGENTA},
};
static const int serviceCount = sizeof(services) / sizeof(services[0]);

// ─── Losowy znak ASCII do Matrix Rain ─────────────────────────────
static char randomMatrixChar() {
  // Katakana-style + ASCII symbole
  const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*<>{}[]|/\\";
  return chars[random(0, sizeof(chars) - 1)];
}

// ─── Inicjalizacja deszczu ────────────────────────────────────────
static void initRain() {
  for (int i = 0; i < RAIN_COLS; i++) {
    rainY[i]     = random(-20, 0) * CHAR_H;
    rainLen[i]   = random(4, 14);
    rainSpeed[i] = random(1, 4);
  }
}

// ─── Rysuj jedną ramkę deszczu ────────────────────────────────────
static void drawRainFrame() {
  for (int col = 0; col < RAIN_COLS; col++) {
    int x = col * CHAR_W;

    // Wyczyść stary pixel na górze smugi
    int topY = rainY[col] - rainLen[col] * CHAR_H;
    if (topY >= 0 && topY < SCR_H) {
      tft.fillRect(x, topY, CHAR_W, CHAR_H, C_BG);
    }

    // Rysuj głowicę (jasny biały)
    if (rainY[col] >= 0 && rainY[col] < SCR_H) {
      tft.setTextColor(C_WHITE);
      tft.setTextSize(1);
      tft.setCursor(x, rainY[col]);
      char c[2] = {randomMatrixChar(), 0};
      tft.print(c);
    }

    // Rysuj ogon (zielony, zanikający)
    for (int t = 1; t < rainLen[col]; t++) {
      int ty = rainY[col] - t * CHAR_H;
      if (ty >= 0 && ty < SCR_H) {
        uint16_t c_col = (t < rainLen[col] / 3) ? C_GREEN : C_DIMGREEN;
        tft.setTextColor(c_col);
        tft.setCursor(x, ty);
        char ch[2] = {randomMatrixChar(), 0};
        tft.print(ch);
      }
    }

    rainY[col] += rainSpeed[col] * (CHAR_H / 2);
    if (rainY[col] - rainLen[col] * CHAR_H > SCR_H) {
      rainY[col]     = random(-8, 0) * CHAR_H;
      rainLen[col]   = random(4, 14);
      rainSpeed[col] = random(1, 4);
    }
  }
}

// ─── Rysowanie ramki terminala ────────────────────────────────────
static void drawTerminalFrame() {
  tft.drawRect(2, 2, SCR_W - 4, SCR_H - 4, C_DIMGREEN);
  tft.drawRect(3, 3, SCR_W - 6, SCR_H - 6, C_DARKGREY);

  // Pasek tytułowy
  tft.fillRect(4, 4, SCR_W - 8, 14, C_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN);
  tft.setCursor(8, 6);
  tft.print("[ BALUARTE TERMINAL ]");

  // Kropki okna
  tft.fillCircle(SCR_W - 14, 11, 3, C_RED);
  tft.fillCircle(SCR_W - 24, 11, 3, C_ORANGE);
  tft.fillCircle(SCR_W - 34, 11, 3, C_GREEN);
}

// ─── Rysowanie paska postępu ──────────────────────────────────────
static void drawProgressBar(int x, int y, int w, int h, int percent, uint16_t color) {
  tft.drawRect(x, y, w, h, C_GREY);
  int fillW = (w - 2) * percent / 100;
  tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

// ─── Rysowanie linii skanowania ───────────────────────────────────
static void drawScanLine(int lineIdx) {
  int y = 22 + lineIdx * 14;
  tft.setTextSize(1);

  const char* line = scanLines[lineIdx];
  uint16_t color = C_GREEN;

  if (strstr(line, "[!]") || strstr(line, "HIGH")) color = C_RED;
  else if (strstr(line, "[*]")) color = C_ORANGE;
  else if (strstr(line, "SHIELD")) color = C_CYAN;
  else if (strstr(line, "SCAN>")) color = C_DIMCYAN;

  tft.setTextColor(color);
  tft.setCursor(8, y);
  tft.print(line);
}

// ─── Efekt glitch na logo ─────────────────────────────────────────
static void drawLogo(bool glitch) {
  int cx = SCR_W / 2;

  if (glitch) {
    // Glitch offset
    int offX = random(-4, 5);
    int offY = random(-2, 3);
    tft.setTextSize(3);
    tft.setTextColor(C_RED);
    tft.setCursor(cx - 96 + offX + 2, 80 + offY + 1);
    tft.print("BALUARTE");
    tft.setTextColor(C_CYAN);
    tft.setCursor(cx - 96 + offX - 2, 80 + offY - 1);
    tft.print("BALUARTE");
  }

  tft.setTextSize(3);
  tft.setTextColor(C_GREEN);
  tft.setCursor(cx - 96, 80);
  tft.print("BALUARTE");

  // Podtytuł
  tft.setTextSize(1);
  tft.setTextColor(C_DIMGREEN);
  tft.setCursor(cx - 60, 115);
  tft.print("SECURE  \xF7  SEO  \xF7  CODE");
}

// ─── Hexdump dekoracyjny ──────────────────────────────────────────
static void drawHexDecor(int y, int count) {
  tft.setTextSize(1);
  tft.setTextColor(C_DARKGREY);
  for (int i = 0; i < count; i++) {
    tft.setCursor(8, y + i * 10);
    char buf[42];
    snprintf(buf, sizeof(buf), "%04X: %02X %02X %02X %02X %02X %02X %02X %02X",
             random(0x1000, 0xFFFF),
             random(0, 256), random(0, 256), random(0, 256), random(0, 256),
             random(0, 256), random(0, 256), random(0, 256), random(0, 256));
    tft.print(buf);
  }
}

// ─── Przejście między stanami ─────────────────────────────────────
static void changeState(AnimState next) {
  gState      = next;
  gStateStart = millis();
  gSubStep    = 0;
  if (next == MATRIX_RAIN) {
    tft.fillScreen(C_BG);
    initRain();
  }
}

// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000);

  // Podświetlenie i zasilanie
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  Serial.println("BALUARTE INSECON Display v2.0");

  if (!tft.init()) {
    Serial.println("BLAD: Ekran nie zainicjalizowany!");
    while (1) delay(100);
  }

  tft.setRotation(0);
  tft.fillScreen(C_BG);
  tft.setTextWrap(false);

  randomSeed(analogRead(0) ^ millis());

  changeState(BOOT_SEQUENCE);
  Serial.println("Animacja INSECON uruchomiona.");
}

// ═══════════════════════════════════════════════════════════════════
void loop() {
  uint32_t now     = millis();
  uint32_t elapsed = now - gStateStart;

  switch (gState) {

    // ──── FAZA 1: Sekwencja bootowania terminala ──────────────────
    case BOOT_SEQUENCE: {
      if (gSubStep == 0) {
        tft.fillScreen(C_BG);
        drawTerminalFrame();
        gSubStep = 1;
        gLastFrame = now;
      }

      int targetLine = min(bootLineCount, (int)(elapsed / 280));
      if (targetLine > gSubStep - 1) {
        int idx = targetLine - 1;
        if (idx < bootLineCount) {
          int y = 22 + idx * 14;
          tft.setTextSize(1);
          uint16_t color = C_GREEN;
          if (strstr(bootLines[idx], "[OK]")) color = C_CYAN;
          else if (strstr(bootLines[idx], "BALUARTE")) color = C_WHITE;
          else if (strstr(bootLines[idx], "READY")) color = C_ORANGE;
          tft.setTextColor(color);
          tft.setCursor(8, y);
          tft.print(bootLines[idx]);

          // Dźwięk kursora – migający underscore
          tft.setTextColor(C_GREEN);
          tft.setCursor(8, y + 14);
          tft.print("_");
        }
        gSubStep = targetLine + 1;
      }

      // Pasek postępu na dole
      int pct = min(100, (int)(elapsed * 100 / (bootLineCount * 280)));
      drawProgressBar(20, SCR_H - 30, SCR_W - 40, 10, pct, C_GREEN);
      tft.setTextSize(1);
      tft.setTextColor(C_DIMGREEN);
      tft.setCursor(20, SCR_H - 18);
      char pctBuf[16];
      snprintf(pctBuf, sizeof(pctBuf), "LOADING %3d%%", pct);
      tft.print(pctBuf);

      if (elapsed > (uint32_t)(bootLineCount * 280 + 800)) {
        changeState(MATRIX_RAIN);
      }
      delay(30);
      break;
    }

    // ──── FAZA 2: Matrix Rain ─────────────────────────────────────
    case MATRIX_RAIN: {
      if (now - gLastFrame >= RAIN_SPEED) {
        drawRainFrame();
        gLastFrame = now;
      }

      // Po 4s przejście do skanowania
      if (elapsed > 4000) {
        changeState(NETWORK_SCAN);
      }
      break;
    }

    // ──── FAZA 3: Skan sieci ──────────────────────────────────────
    case NETWORK_SCAN: {
      if (gSubStep == 0) {
        tft.fillScreen(C_BG);
        drawTerminalFrame();

        // Tytuł
        tft.setTextColor(C_CYAN);
        tft.setTextSize(1);
        tft.setCursor(50, 6);
        tft.print("[ NETWORK SCAN ]");

        gSubStep = 1;
        gLastFrame = now;
      }

      int targetLine = min(scanLineCount, (int)(elapsed / 350));
      if (targetLine > gSubStep - 1) {
        int idx = targetLine - 1;
        if (idx < scanLineCount) {
          drawScanLine(idx);
          // Migający kursor
          tft.setTextColor(C_GREEN);
          tft.setCursor(8, 22 + (idx + 1) * 14);
          tft.print("_");
        }
        gSubStep = targetLine + 1;
      }

      // Hexdump dekoracyjny na dole
      if ((elapsed / 200) % 3 == 0) {
        drawHexDecor(SCR_H - 70, 5);
      }

      // Pasek postępu skanowania
      int scanPct = min(100, (int)(elapsed * 100 / (scanLineCount * 350)));
      drawProgressBar(20, SCR_H - 16, SCR_W - 40, 8, scanPct, C_RED);

      if (elapsed > (uint32_t)(scanLineCount * 350 + 1000)) {
        changeState(LOGO_REVEAL);
      }
      delay(30);
      break;
    }

    // ──── FAZA 4: Ujawnienie logo z efektem glitch ────────────────
    case LOGO_REVEAL: {
      if (gSubStep == 0) {
        tft.fillScreen(C_BG);
        gSubStep = 1;
      }

      // Glitch przez pierwsze 1.5s
      bool doGlitch = elapsed < 1500 && (elapsed / 80) % 3 != 0;

      if (doGlitch || (elapsed >= 1500 && gSubStep < 10)) {
        if (doGlitch) {
          // Losowe linie glitch
          for (int g = 0; g < 3; g++) {
            int gy = random(0, SCR_H);
            tft.drawFastHLine(0, gy, SCR_W, random(2) ? C_GREEN : C_CYAN);
          }
        }
        tft.fillRect(0, 70, SCR_W, 60, C_BG);
        drawLogo(doGlitch);
        if (!doGlitch) gSubStep = 10;
      }

      // Po ustabilizowaniu – rysuj hexdump w tle
      if (elapsed > 1800 && gSubStep < 20) {
        drawHexDecor(140, 8);
        gSubStep = 20;
      }

      // Ramka ozdobna wokół logo
      if (elapsed > 2000 && gSubStep < 30) {
        tft.drawRect(10, 65, SCR_W - 20, 70, C_DIMGREEN);
        tft.drawRect(12, 67, SCR_W - 24, 66, C_DARKGREY);
        gSubStep = 30;
      }

      // Info o targach
      if (elapsed > 2500 && gSubStep < 40) {
        tft.setTextSize(2);
        tft.setTextColor(C_WHITE);
        tft.setCursor(40, SCR_H - 50);
        tft.print("INSECON 2026");
        tft.setTextSize(1);
        tft.setTextColor(C_GREY);
        tft.setCursor(30, SCR_H - 28);
        tft.print("Targi Bezpieczenstwa IT");
        gSubStep = 40;
      }

      if (elapsed > 4500) {
        changeState(SERVICES_LIST);
      }
      delay(40);
      break;
    }

    // ──── FAZA 5: Lista usług z animacją wpisywania ──────────────
    case SERVICES_LIST: {
      if (gSubStep == 0) {
        tft.fillScreen(C_BG);
        drawTerminalFrame();

        // Nagłówek
        tft.setTextSize(2);
        tft.setTextColor(C_GREEN);
        tft.setCursor(20, 24);
        tft.print("USLUGI:");

        // Linia pod nagłówkiem
        tft.drawFastHLine(8, 44, SCR_W - 16, C_DIMGREEN);

        gSubStep = 1;
      }

      int targetSvc = min(serviceCount, (int)(elapsed / 500));
      if (targetSvc > gSubStep - 1) {
        int idx = targetSvc - 1;
        if (idx < serviceCount) {
          int y = 52 + idx * 22;
          tft.setTextSize(1);
          tft.setTextColor(C_DIMGREEN);
          tft.setCursor(8, y);
          tft.print(services[idx].icon);
          tft.setTextColor(services[idx].color);
          tft.setCursor(34, y);
          tft.print(services[idx].text);

          // Kursor
          tft.setTextColor(C_GREEN);
          tft.setCursor(8, y + 22);
          tft.print("_");
        }
        gSubStep = targetSvc + 1;
      }

      // Dolny pasek – pulsujący tekst
      if (elapsed > serviceCount * 500 + 500) {
        bool blink = (elapsed / 400) % 2 == 0;
        tft.fillRect(0, SCR_H - 40, SCR_W, 40, C_BG);
        if (blink) {
          tft.setTextSize(1);
          tft.setTextColor(C_CYAN);
          tft.setCursor(20, SCR_H - 35);
          tft.print(">> SECURE YOUR BUSINESS <<");
          tft.setTextColor(C_GREEN);
          tft.setCursor(40, SCR_H - 20);
          tft.print("www.baluarte.pl");
        }
      }

      if (elapsed > (uint32_t)(serviceCount * 500 + 3000)) {
        changeState(CTA_PULSE);
      }
      delay(40);
      break;
    }

    // ──── FAZA 6: CTA pulsujące ──────────────────────────────────
    case CTA_PULSE: {
      if (gSubStep == 0) {
        tft.fillScreen(C_BG);

        // Ramka główna
        tft.drawRect(5, 5, SCR_W - 10, SCR_H - 10, C_GREEN);
        tft.drawRect(7, 7, SCR_W - 14, SCR_H - 14, C_DIMGREEN);

        // Hexdump dekoracyjny góra
        drawHexDecor(12, 3);

        // Logo
        tft.setTextSize(3);
        tft.setTextColor(C_GREEN);
        tft.setCursor(24, 60);
        tft.print("BALUARTE");

        // Separator
        tft.drawFastHLine(20, 95, SCR_W - 40, C_CYAN);
        tft.drawFastHLine(20, 97, SCR_W - 40, C_DIMCYAN);

        // Hasło
        tft.setTextSize(1);
        tft.setTextColor(C_WHITE);
        tft.setCursor(15, 108);
        tft.print("Bezpieczenstwo Kodu");
        tft.setCursor(15, 122);
        tft.print("Techniczne SEO | Web Vitals");
        tft.setCursor(15, 136);
        tft.print("Pentesty i Audyty");

        // URL
        tft.setTextSize(2);
        tft.setTextColor(C_CYAN);
        tft.setCursor(8, 170);
        tft.print("baluarte.pl");

        // Dolna linia dekoracyjna
        tft.drawFastHLine(20, 200, SCR_W - 40, C_DIMGREEN);

        // Hexdump dekoracyjny dół
        drawHexDecor(SCR_H - 60, 4);

        gSubStep = 1;
      }

      // Pulsujący CTA
      bool pulse = (elapsed / 600) % 2 == 0;
      tft.fillRect(10, 210, SCR_W - 20, 50, C_BG);
      if (pulse) {
        tft.setTextSize(2);
        tft.setTextColor(C_MAGENTA);
        tft.setCursor(12, 215);
        tft.print("Zbuduj bezpieczny");
        tft.setCursor(12, 237);
        tft.print("biznes z nami!");
      } else {
        tft.setTextSize(2);
        tft.setTextColor(C_ORANGE);
        tft.setCursor(18, 215);
        tft.print(" INSECON 2026 ");
        tft.setTextSize(1);
        tft.setTextColor(C_GREY);
        tft.setCursor(30, 240);
        tft.print("Targi Bezpieczenstwa IT");
      }

      // Animowany border pulse
      uint16_t borderCol = pulse ? C_GREEN : C_DIMGREEN;
      tft.drawRect(5, 5, SCR_W - 10, SCR_H - 10, borderCol);

      if (elapsed > 6000) {
        changeState(FADE_OUT);
      }
      delay(50);
      break;
    }

    // ──── FAZA 7: Fade out i restart pętli ────────────────────────
    case FADE_OUT: {
      if (gSubStep == 0) {
        gSubStep = 1;
      }

      // Symulacja fade – rysuj ciemne prostokąty narastająco
      int bars = min(16, (int)(elapsed / 80));
      for (int i = 0; i < bars; i++) {
        int y = i * (SCR_H / 16);
        tft.fillRect(0, y, SCR_W, SCR_H / 16, C_BG);
      }

      // Tekst "reboot" na środku pod koniec
      if (elapsed > 800 && elapsed < 1600) {
        tft.setTextSize(1);
        tft.setTextColor(C_DIMGREEN);
        tft.setCursor(70, SCR_H / 2 - 4);
        tft.print("> rebooting...");
      }

      // Restart pętli
      if (elapsed > 1800) {
        changeState(BOOT_SEQUENCE);
      }
      delay(30);
      break;
    }
  }
}
