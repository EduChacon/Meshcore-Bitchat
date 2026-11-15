#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <helpers/ui/CardColor.h>
#include <helpers/ui/DisplayDriver.h>

// The keyboard UI draws a 240x135 frame, as on the Cardputer. Here the frame is drawn in memory and
// shown enlarged to 320x180 in the middle of the T-Deck's 320x240 panel.
class TDeckCardDisplay : public DisplayDriver {
  SPIClass displaySPI;
  Adafruit_ST7789 panel;
  GFXcanvas16 *canvas = nullptr;
  bool _isOn = false, started = false;
  uint16_t _color = 0xFFFF, _light_color = 0xFFFF, _dark_color = 0x0000;
  uint8_t text_size = 1, backlight = 0, brightness = 16;
  uint16_t line[320], scratch[640];

  uint16_t themed(ColorVal c, uint16_t fallback) const;
  void show();
  void setBacklightLevel(uint8_t level);

public:
  TDeckCardDisplay();
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = CardColor::DARK) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
  void endFrame() override;

  void setLightColor(uint16_t color) { _light_color = color; }
  void setDarkColor(uint16_t color) { _dark_color = color; }
  // Where the frame is drawn: the memory frame, or the panel itself when there was no memory for it.
  Adafruit_GFX &surface();
  // 0..255, as the Cardputer's library takes it.
  void setBrightness(uint8_t value);
};
