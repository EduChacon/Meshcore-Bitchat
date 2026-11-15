#pragma once
#include <stddef.h>
#include <stdint.h>

// Enlarges the keyboard UI's 240x135 frame by 4/3 for the T-Deck's 320x240 panel: every three
// source pixels become four, the two inner ones blended from their neighbours. Plain C++ so the
// result can be checked on a computer.
namespace cardscale
{
constexpr int SOURCE_WIDTH = 240, SOURCE_HEIGHT = 135;
constexpr int TARGET_WIDTH = 320, TARGET_HEIGHT = 180;

// `share` eighths of b, the rest of a, in RGB565.
inline uint16_t mix(uint16_t a, uint16_t b, unsigned share)
{
    if (a == b)
        return a;
    unsigned rest = 8 - share;
    unsigned red = ((a >> 11) * rest + (b >> 11) * share + 4) >> 3;
    unsigned green = (((a >> 5) & 63) * rest + ((b >> 5) & 63) * share + 4) >> 3;
    unsigned blue = ((a & 31) * rest + (b & 31) * share + 4) >> 3;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

inline void widen(const uint16_t *source, uint16_t *target)
{
    for (int block = 0; block < SOURCE_WIDTH / 3; block++, source += 3, target += 4) {
        target[0] = source[0];
        target[1] = mix(source[0], source[1], 5);
        target[2] = mix(source[1], source[2], 3);
        target[3] = source[2];
    }
}

// Target row `row` (0..179) of the frame; `scratch` holds two rows of TARGET_WIDTH pixels.
inline void row(const uint16_t *frame, int row, uint16_t *out, uint16_t *scratch)
{
    const uint16_t *block = frame + (size_t)(row / 4) * 3 * SOURCE_WIDTH;
    switch (row % 4) {
    case 0:
        widen(block, out);
        return;
    case 3:
        widen(block + 2 * SOURCE_WIDTH, out);
        return;
    case 1:
        widen(block, scratch);
        widen(block + SOURCE_WIDTH, scratch + TARGET_WIDTH);
        for (int x = 0; x < TARGET_WIDTH; x++)
            out[x] = mix(scratch[x], scratch[TARGET_WIDTH + x], 5);
        return;
    default:
        widen(block + SOURCE_WIDTH, scratch);
        widen(block + 2 * SOURCE_WIDTH, scratch + TARGET_WIDTH);
        for (int x = 0; x < TARGET_WIDTH; x++)
            out[x] = mix(scratch[x], scratch[TARGET_WIDTH + x], 3);
        return;
    }
}
} // namespace cardscale
