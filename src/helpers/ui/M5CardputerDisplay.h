#pragma once

#include <M5Cardputer.h>
#include <new>
#include "DisplayDriver.h"

// M5Cardputer uses standard color constants
#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_ORANGE 0xFD20
#endif

// The keyboard UI names colours by role, as older MeshCore did; this display maps them to the theme.
struct CardColor {
  enum : ColorVal { DARK = 0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE };
};

class M5CardputerDisplay : public DisplayDriver {
private:
  bool _isOn = false;
  uint16_t cursor_x = 0;
  uint16_t cursor_y = 0;
  uint16_t text_size = 1;
  uint16_t _color = 0xFFFF;  // White
  uint16_t _light_color = TFT_WHITE;  // Default foreground
  uint16_t _dark_color = TFT_BLACK;   // Default background
  static const uint16_t SCREEN_WIDTH = 240;
  static const uint16_t SCREEN_HEIGHT = 135;
  static const uint8_t CHAR_WIDTH = 6;
  static const uint8_t CHAR_HEIGHT = 8;

  // Each frame is drawn in memory and sent to the panel in one go, so the screen no longer flashes
  // black while it is redrawn. Without PSRAM the buffer is only taken when enough memory is left
  // for the radio, Bluetooth and the Bitchat bridge (about 54 KB when it starts); otherwise the screen
  // is drawn directly. The UI asks for the buffer a few seconds after boot, once the bridge has started.
  static const uint32_t BUFFER_RESERVE = 64 * 1024;
  M5Canvas* _canvas = nullptr;
  bool _bufferAllowed = false;
  bool _bufferTried = false;
  uint8_t _bufferBits = 0;

  void takeBuffer() {
    _bufferTried = true;
    static const uint8_t depths[] = { 16, 8 };
    for (uint8_t bits : depths) {
      const uint32_t bytes = uint32_t(SCREEN_WIDTH) * SCREEN_HEIGHT * bits / 8;
      if (ESP.getFreeHeap() < bytes + BUFFER_RESERVE || ESP.getMaxAllocHeap() < bytes + 1024) continue;
      M5Canvas* canvas = new (std::nothrow) M5Canvas(&M5.Display);
      if (!canvas) return;
      canvas->setPsram(false);
      canvas->setColorDepth(bits);
      if (canvas->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT) && ESP.getFreeHeap() >= BUFFER_RESERVE) {
        _canvas = canvas;
        _bufferBits = bits;
        return;
      }
      canvas->deleteSprite();
      delete canvas;
    }
  }

public:
  // Where drawing goes: the frame buffer when there is one, else the panel.
  lgfx::LovyanGFX& gfx() {
    if (_canvas) return *_canvas;
    return M5.Display;
  }
  // From now on, frames may be drawn in memory if there is room for the buffer.
  void allowBuffer() { _bufferAllowed = true; }
  // 16 or 8 when frames are buffered (bits per pixel), 0 when drawn directly.
  uint8_t bufferBits() const { return _bufferBits; }

public:
  M5CardputerDisplay() : DisplayDriver(SCREEN_WIDTH, SCREEN_HEIGHT) {}

  bool begin() {
    _isOn = true;
    // Landscape: one quarter turn from the panel's native portrait. The library may already count part of
    // it in the panel's offset (M5GFX 0.3.0 does), so only the rest is asked for.
    const uint8_t offset = M5.Display.getPanel() ? (M5.Display.getPanel()->config().offset_rotation & 3) : 0;
    M5.Display.setRotation((1 - offset) & 3);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    Serial.println("M5Cardputer Display initialized");
    return true;
  }

  bool isOn() override { return _isOn; }
  
  void turnOn() override {
    if (!_isOn) {
      M5.Display.wakeup();
      _isOn = true;
    }
  }
  
  void turnOff() override {
    if (_isOn) {
      M5.Display.sleep();
      _isOn = false;
    }
  }

  void startFrame(ColorVal bkg = CardColor::DARK) override {
    if (!_isOn) return;
    if (_bufferAllowed && !_bufferTried) takeBuffer();
    // Clear screen at start of each frame
    uint16_t bgColor;
    switch(bkg) {
      case CardColor::DARK:   bgColor = _dark_color; break;
      case CardColor::LIGHT:  bgColor = _light_color; break;
      case CardColor::RED:    bgColor = TFT_RED; break;
      case CardColor::GREEN:  bgColor = TFT_GREEN; break;
      case CardColor::BLUE:   bgColor = TFT_BLUE; break;
      case CardColor::YELLOW: bgColor = TFT_YELLOW; break;
      case CardColor::ORANGE: bgColor = TFT_ORANGE; break;
      default:     bgColor = _dark_color; break;
    }
    gfx().fillScreen(bgColor);
    gfx().setCursor(0, 0);
    cursor_x = 0;
    cursor_y = 0;
  }

  void endFrame() override {
    if (!_isOn) return;
    if (_canvas) _canvas->pushSprite(0, 0);
  }

  void clear() override {
    if (!_isOn) return;
    gfx().fillScreen(TFT_BLACK);
    if (_canvas) _canvas->pushSprite(0, 0);
    cursor_x = 0;
    cursor_y = 0;
  }

  void setCursor(int x, int y) override {
    cursor_x = x;
    cursor_y = y;
    gfx().setCursor(x, y);
  }

  void setTextSize(int sz) override {
    text_size = sz;
    gfx().setTextSize(sz);
  }

  void setColor(ColorVal c) override {
    switch(c) {
      case CardColor::DARK:   _color = _dark_color; break;
      case CardColor::LIGHT:  _color = _light_color; break;
      case CardColor::RED:    _color = TFT_RED; break;
      case CardColor::GREEN:  _color = TFT_GREEN; break;
      case CardColor::BLUE:   _color = TFT_BLUE; break;
      case CardColor::YELLOW: _color = TFT_YELLOW; break;
      case CardColor::ORANGE: _color = TFT_ORANGE; break;
      default:     _color = _light_color; break;
    }
    gfx().setTextColor(_color, _dark_color);
  }
  
  // Methods to customize theme colors
  void setLightColor(uint16_t color) {
    _light_color = color;
  }
  
  void setDarkColor(uint16_t color) {
    _dark_color = color;
  }

  void print(const char* str) override {
    if (!_isOn) return;
    gfx().print(str);
  }

  void drawRect(int x, int y, int w, int h) override {
    if (!_isOn) return;
    gfx().drawRect(x, y, w, h, _color);
  }

  void fillRect(int x, int y, int w, int h) override {
    if (!_isOn) return;
    gfx().fillRect(x, y, w, h, _color);
  }

  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    if (!_isOn) return;
    // XBM drawing - simplified implementation
    for (int yy = 0; yy < h; yy++) {
      for (int xx = 0; xx < w; xx++) {
        int byte_idx = (yy * ((w + 7) / 8)) + (xx / 8);
        int bit_idx = xx % 8;
        if (bits[byte_idx] & (1 << bit_idx)) {
          gfx().drawPixel(x + xx, y + yy, _color);
        }
      }
    }
  }

  uint16_t getTextWidth(const char* str) override {
    return strlen(str) * CHAR_WIDTH * text_size;
  }

};

// Helper inline for text drawing
inline void drawTextLine(M5CardputerDisplay& disp, int y, const char* str) {
  disp.setCursor(0, y);
  disp.print(str);
}
