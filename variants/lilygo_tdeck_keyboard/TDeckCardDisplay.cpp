#include "TDeckCardDisplay.h"
#include "CardScale.h"
#include "M5Cardputer.h"
#include <Wire.h>

#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

#define PANEL_WIDTH 240
#define PANEL_HEIGHT 320
#define PANEL_ROTATION 3
#define FRAME_TOP ((240 - cardscale::TARGET_HEIGHT) / 2)

#define KEYBOARD_ADDRESS 0x55
#define KEYBOARD_ALT_C 0x0C
#define TRACKBALL_UP 3
#define TRACKBALL_DOWN 15
#define TRACKBALL_LEFT 1
#define TRACKBALL_RIGHT 2
#define TRACKBALL_CLICK 0
// Falling edges of one direction that make one step, as Meshtastic counts them on this trackball.
#define TRACKBALL_STEP 3

extern TDeckCardDisplay display;
M5CardputerStandIn M5Cardputer;

// Colours by role for code shared with the other companion UIs.
ColorVal UIColor::window_bkg = CardColor::DARK;
ColorVal UIColor::title_bkg = CardColor::DARK;
ColorVal UIColor::title_txt = CardColor::LIGHT;
ColorVal UIColor::primary_txt = CardColor::LIGHT;
ColorVal UIColor::secondary_txt = CardColor::LIGHT;
ColorVal UIColor::warning_txt = CardColor::YELLOW;
ColorVal UIColor::popup_bkg = CardColor::DARK;
ColorVal UIColor::popup_txt = CardColor::LIGHT;
ColorVal UIColor::corp_blue = CardColor::BLUE;

// U+00C0..U+00FF in the panel font's code page 437, or the plain letter where it has no such glyph.
static const uint8_t LATIN[64] = {
    'A',  'A',  'A',  'A',  0x8E, 0x8F, 0x92, 0x80, 'E',  0x90, 'E',  'E',  'I',  'I',  'I',  'I',
    'D',  0xA5, 'O',  'O',  'O',  'O',  0x99, 'x',  'O',  'U',  'U',  'U',  0x9A, 'Y',  '?',  0xE1,
    0x85, 0xA0, 0x83, 'a',  0x84, 0x86, 0x91, 0x87, 0x8A, 0x82, 0x88, 0x89, 0x8D, 0xA1, 0x8C, 0x8B,
    'd',  0xA4, 0x95, 0xA2, 0x93, 'o',  0x94, 0xF6, 'o',  0x97, 0xA3, 0x96, 0x81, 'y',  '?',  0x98};

// The next glyph of a UTF-8 string; anything the font cannot show becomes '?'.
static uint8_t nextGlyph(const uint8_t *&text)
{
  uint8_t first = *text++;
  if (first < 0x80)
    return first;
  if (first < 0xC0)
    return '?';
  uint8_t second = *text;
  while ((*text & 0xC0) == 0x80)
    text++;
  if (first == 0xC3 && (second & 0xC0) == 0x80)
    return LATIN[second & 0x3F];
  return '?';
}

TDeckCardDisplay::TDeckCardDisplay()
    : DisplayDriver(cardscale::SOURCE_WIDTH, cardscale::SOURCE_HEIGHT), displaySPI(HSPI),
      panel(&displaySPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST)
{
}

bool TDeckCardDisplay::begin()
{
  if (!started) {
    started = true;
    pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);

    // The same start as the stock T-Deck display driver.
    displaySPI.begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
    panel.init(PANEL_WIDTH, PANEL_HEIGHT);
    panel.setRotation(PANEL_ROTATION);
    panel.setSPISpeed(40e6);
    panel.fillScreen(0x0000);
    panel.cp437(true);

    canvas = new GFXcanvas16(cardscale::SOURCE_WIDTH, cardscale::SOURCE_HEIGHT);
    if (canvas && !canvas->getBuffer()) {
      delete canvas;
      canvas = nullptr;
    }
    if (canvas) {
      canvas->cp437(true);
      canvas->fillScreen(0x0000);
    }
    surface().setTextColor(0xFFFF, 0x0000);
    surface().setTextSize(1);
  }
  setBacklightLevel(brightness);
  _isOn = true;
  return true;
}

Adafruit_GFX &TDeckCardDisplay::surface()
{
  if (canvas)
    return *canvas;
  return panel;
}

// The backlight driver counts pulses: 16 levels, each pulse one level down, wrapping around.
void TDeckCardDisplay::setBacklightLevel(uint8_t level)
{
  const uint8_t steps = 16;
  if (level > steps)
    level = steps;
  if (level == 0) {
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    delay(3);
    backlight = 0;
    return;
  }
  if (backlight == 0) {
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    backlight = steps;
    delayMicroseconds(30);
  }
  int pulses = (steps + backlight - level) % steps;
  for (int i = 0; i < pulses; i++) {
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
  }
  backlight = level;
}

void TDeckCardDisplay::setBrightness(uint8_t value)
{
  uint8_t level = (uint8_t)((value * 16 + 127) / 255);
  if (value > 0 && level == 0)
    level = 1;
  brightness = level;
  if (_isOn)
    setBacklightLevel(level);
}

void TDeckCardDisplay::turnOn()
{
  if (!started) {
    begin();
    return;
  }
  if (!_isOn) {
    setBacklightLevel(brightness ? brightness : 1);
    _isOn = true;
  }
}

void TDeckCardDisplay::turnOff()
{
  if (_isOn) {
    setBacklightLevel(0);
    _isOn = false;
  }
}

uint16_t TDeckCardDisplay::themed(ColorVal c, uint16_t fallback) const
{
  switch (c) {
  case CardColor::DARK:
    return _dark_color;
  case CardColor::LIGHT:
    return _light_color;
  case CardColor::RED:
    return ST77XX_RED;
  case CardColor::GREEN:
    return ST77XX_GREEN;
  case CardColor::BLUE:
    return ST77XX_BLUE;
  case CardColor::YELLOW:
    return ST77XX_YELLOW;
  case CardColor::ORANGE:
    return ST77XX_ORANGE;
  default:
    return fallback;
  }
}

void TDeckCardDisplay::startFrame(ColorVal bkg)
{
  if (!_isOn)
    return;
  surface().fillScreen(themed(bkg, _dark_color));
  surface().setCursor(0, 0);
}

void TDeckCardDisplay::clear()
{
  if (!_isOn)
    return;
  surface().fillScreen(0x0000);
  surface().setCursor(0, 0);
  show();
}

void TDeckCardDisplay::setTextSize(int sz)
{
  text_size = sz;
  surface().setTextSize(sz);
}

void TDeckCardDisplay::setColor(ColorVal c)
{
  _color = themed(c, _light_color);
  surface().setTextColor(_color, _dark_color);
}

void TDeckCardDisplay::setCursor(int x, int y)
{
  surface().setCursor(x, y);
}

void TDeckCardDisplay::print(const char *str)
{
  if (!_isOn)
    return;
  const uint8_t *text = (const uint8_t *)str;
  while (*text)
    surface().write(nextGlyph(text));
}

void TDeckCardDisplay::fillRect(int x, int y, int w, int h)
{
  if (!_isOn)
    return;
  surface().fillRect(x, y, w, h, _color);
}

void TDeckCardDisplay::drawRect(int x, int y, int w, int h)
{
  if (!_isOn)
    return;
  surface().drawRect(x, y, w, h, _color);
}

void TDeckCardDisplay::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  if (!_isOn)
    return;
  surface().drawXBitmap(x, y, bits, w, h, _color);
}

uint16_t TDeckCardDisplay::getTextWidth(const char *str)
{
  const uint8_t *text = (const uint8_t *)str;
  uint16_t glyphs = 0;
  while (*text) {
    nextGlyph(text);
    glyphs++;
  }
  return glyphs * 6 * text_size;
}

void TDeckCardDisplay::endFrame()
{
  if (!_isOn)
    return;
  show();
}

void TDeckCardDisplay::show()
{
  if (!canvas)
    return;
  const uint16_t *frame = canvas->getBuffer();
  panel.startWrite();
  panel.setAddrWindow(0, FRAME_TOP, cardscale::TARGET_WIDTH, cardscale::TARGET_HEIGHT);
  for (int row = 0; row < cardscale::TARGET_HEIGHT; row++) {
    cardscale::row(frame, row, line, scratch);
    panel.writePixels(line, cardscale::TARGET_WIDTH);
  }
  panel.endWrite();
}

// ---- the Cardputer library's screen, as the keyboard UI uses it directly ----

void M5GFX::fillScreen(uint16_t color)
{
  display.surface().fillScreen(color);
}

void M5GFX::fillRect(int x, int y, int w, int h, uint16_t color)
{
  display.surface().fillRect(x, y, w, h, color);
}

void M5GFX::setTextColor(uint16_t color)
{
  display.surface().setTextColor(color);
}

void M5GFX::setBrightness(uint8_t value)
{
  display.setBrightness(value);
}

// ---- keyboard and trackball ----

static volatile uint8_t rolled[4];
static void IRAM_ATTR rolledUp() { rolled[0]++; }
static void IRAM_ATTR rolledDown() { rolled[1]++; }
static void IRAM_ATTR rolledLeft() { rolled[2]++; }
static void IRAM_ATTR rolledRight() { rolled[3]++; }

void Keyboard_Class::begin()
{
  static const uint8_t pins[4] = {TRACKBALL_UP, TRACKBALL_DOWN, TRACKBALL_LEFT, TRACKBALL_RIGHT};
  static void (*const handlers[4])() = {rolledUp, rolledDown, rolledLeft, rolledRight};
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    attachInterrupt(pins[i], handlers[i], FALLING);
  }
  pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
  started = true;
}

// The UI reads arrows as Fn with the Cardputer's arrow keys.
void Keyboard_Class::arrow(char key)
{
  state.fn = true;
  state.word.push_back(key);
}

bool Keyboard_Class::readKeyboard()
{
  if (Wire.requestFrom((uint8_t)KEYBOARD_ADDRESS, (uint8_t)1) != 1)
    return false;
  uint8_t key = Wire.read();
  if (key == 0x0D) {
    state.enter = true;
  } else if (key == 0x08) {
    state.del = true;
  } else if (key == KEYBOARD_ALT_C) {
    // stands for the Cardputer's Fn+Del: remove the selected contact or channel
    state.fn = true;
    state.del = true;
  } else if (key == ' ') {
    state.space = true;
    state.word.push_back(' ');
  } else if (key > ' ' && key < 0x7F) {
    state.word.push_back((char)key);
  } else {
    return false;
  }
  return true;
}

bool Keyboard_Class::readClick(uint32_t now)
{
  bool down = digitalRead(TRACKBALL_CLICK) == LOW;
  if (down && !clickDown) {
    clickDown = true;
    clickLong = false;
    clickAt = now;
  } else if (down && !clickLong && now - clickAt >= 1000) {
    clickLong = true;
    longPress = true;
  } else if (!down && clickDown) {
    clickDown = false;
    if (!clickLong && now - clickAt >= 30) {
      state.enter = true;
      return true;
    }
  }
  return false;
}

bool Keyboard_Class::readTrackball()
{
  static const char keys[4] = {';', '.', ',', '/'};
  uint8_t most = 0, direction = 0, any = 0;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t count = rolled[i];
    any |= count;
    if (count > most) {
      most = count;
      direction = i;
    }
  }
  uint32_t now = millis();
  if (!any) {
    rolledAt = now;
    return false;
  }
  // a nudge that never became a step is forgotten
  if (most < TRACKBALL_STEP && now - rolledAt < 400)
    return false;
  noInterrupts();
  rolled[0] = rolled[1] = rolled[2] = rolled[3] = 0;
  interrupts();
  rolledAt = now;
  if (most < TRACKBALL_STEP)
    return false;
  arrow(keys[direction]);
  return true;
}

bool Keyboard_Class::takeLongPress()
{
  bool was = longPress;
  longPress = false;
  return was;
}

void Keyboard_Class::update()
{
  if (!started)
    begin();
  if (pressed) {
    pressed = false;
    changed = true;
    return;
  }
  changed = false;
  uint32_t now = millis();
  state = KeysState();
  bool event = readClick(now) || readTrackball();
  if (!event && now - polledAt >= 20) {
    polledAt = now;
    event = readKeyboard();
  }
  if (event)
    pressed = changed = true;
}
