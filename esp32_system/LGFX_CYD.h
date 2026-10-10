#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ESP32-2432S028R / "Cheap Yellow Display"
// Display: ILI9341 on HSPI, touch: XPT2046 on VSPI.
class LGFX_CYD : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI displayBus_;
  lgfx::Light_PWM backlight_;
  lgfx::Touch_XPT2046 touch_;

 public:
  LGFX_CYD() {
    {
      auto cfg = displayBus_.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      displayBus_.config(cfg);
      panel_.setBus(&displayBus_);
    }

    {
      auto cfg = panel_.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      panel_.config(cfg);
    }

    {
      auto cfg = backlight_.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 20000;
      cfg.pwm_channel = 7;
      backlight_.config(cfg);
      panel_.setLight(&backlight_);
    }

    {
      auto cfg = touch_.config();
      // XPT2046 reports 12-bit ADC coordinates, not screen pixels.
      // These safe raw defaults are replaced by the four-point calibration
      // stored in NVS on first boot.
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 400;
      cfg.y_max = 3900;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = VSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }

    setPanel(&panel_);
  }
};
