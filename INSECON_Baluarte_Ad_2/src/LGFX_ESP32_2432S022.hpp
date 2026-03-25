#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  // Zmieniamy na ILI9341 - to najczęstszy sterownik w 2.2" SPI
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = HSPI_HOST; // Zmiana na HSPI (piny 14, 13, 12)
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 14; // SCK
      cfg.pin_mosi = 13; // MOSI
      cfg.pin_miso = 12; // MISO
      cfg.pin_dc = 2;    // DC
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 15; // CS
      cfg.pin_rst = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;

      // W ILI9341 zazwyczaj invert to false, ale jeśli kolory
      // będą dziwne (np. tło białe zamiast niebieskiego), zmień na true.
      cfg.invert = false;

      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};
