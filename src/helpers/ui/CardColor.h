#pragma once
#include "DisplayDriver.h"

// The keyboard UI names colours by role, as older MeshCore did; its displays map them to the theme.
struct CardColor {
  enum : ColorVal { DARK = 0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE };
};
