#pragma once
// The keyboard UI was written for the M5Stack Cardputer and its library. On the T-Deck this header
// stands in for that library: the same names, backed by the T-Deck's screen, keyboard and trackball.
#include <Arduino.h>
#include <vector>

class M5GFX {
public:
  void fillScreen(uint16_t color);
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void setTextColor(uint16_t color);
  // 0..255 as on the Cardputer; the T-Deck's backlight has 16 steps.
  void setBrightness(uint8_t value);
};

class Keyboard_Class {
public:
  struct KeysState {
    bool tab = false, fn = false, shift = false, ctrl = false, opt = false, alt = false;
    bool del = false, enter = false, space = false;
    std::vector<char> word;
  };

  void begin();
  // One event per call. The T-Deck's keyboard reports presses only, so each press is followed by
  // a release on the next call.
  void update();
  bool isChange() const { return changed; }
  bool isPressed() const { return pressed; }
  KeysState &keysState() { return state; }
  // The trackball was held down for a second; cleared by the call.
  bool takeLongPress();

private:
  KeysState state;
  bool started = false, changed = false, pressed = false;
  bool clickDown = false, clickLong = false, longPress = false;
  uint32_t polledAt = 0, clickAt = 0, rolledAt = 0;

  bool readKeyboard();
  bool readTrackball();
  bool readClick(uint32_t now);
  void arrow(char key);
};

struct M5CardputerStandIn {
  M5GFX Display;
  Keyboard_Class Keyboard;
  void update() { Keyboard.update(); }
};
extern M5CardputerStandIn M5Cardputer;
#define M5 M5Cardputer
