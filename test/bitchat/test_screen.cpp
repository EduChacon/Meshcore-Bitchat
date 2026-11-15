// The T-Deck shows the keyboard UI's 240x135 frame enlarged to 320x180.
#include "CardScale.h"
#include <cstdio>
#include <vector>

using namespace cardscale;

static int checks = 0, failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(condition)) {                                                                                            \
            failures++;                                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);                                                 \
        }                                                                                                              \
    } while (0)

int main()
{
    std::vector<uint16_t> frame(SOURCE_WIDTH * SOURCE_HEIGHT, 0x1234);
    uint16_t out[TARGET_WIDTH], scratch[2 * TARGET_WIDTH];

    CHECK(TARGET_WIDTH * 3 == SOURCE_WIDTH * 4 && TARGET_HEIGHT * 3 == SOURCE_HEIGHT * 4);
    CHECK(mix(0xFFFF, 0x0000, 0) == 0xFFFF && mix(0xFFFF, 0x0000, 8) == 0x0000);
    CHECK(mix(0xFFFF, 0x0000, 4) == 0x8410);
    CHECK(mix(0xF800, 0xF800, 5) == 0xF800);

    // a flat frame stays flat
    bool flat = true;
    for (int r = 0; r < TARGET_HEIGHT; r++) {
        row(frame.data(), r, out, scratch);
        for (int x = 0; x < TARGET_WIDTH; x++)
            flat = flat && out[x] == 0x1234;
    }
    CHECK(flat);

    // the outer pixels of every block of three are copied, the inner two are blends
    for (size_t i = 0; i < frame.size(); i++)
        frame[i] = (uint16_t)((i * 2654435761u) >> 16);
    row(frame.data(), 0, out, scratch);
    CHECK(out[0] == frame[0] && out[3] == frame[2] && out[4] == frame[3] && out[319] == frame[239]);
    CHECK(out[1] == mix(frame[0], frame[1], 5) && out[2] == mix(frame[1], frame[2], 3));
    row(frame.data(), 3, out, scratch);
    CHECK(out[0] == frame[2 * SOURCE_WIDTH] && out[319] == frame[2 * SOURCE_WIDTH + 239]);
    row(frame.data(), TARGET_HEIGHT - 1, out, scratch);
    CHECK(out[0] == frame[(SOURCE_HEIGHT - 1) * SOURCE_WIDTH] && out[319] == frame[SOURCE_HEIGHT * SOURCE_WIDTH - 1]);
    row(frame.data(), 1, out, scratch);
    CHECK(out[0] == mix(frame[0], frame[SOURCE_WIDTH], 5));
    row(frame.data(), 2, out, scratch);
    CHECK(out[0] == mix(frame[SOURCE_WIDTH], frame[2 * SOURCE_WIDTH], 3));

    // a one-pixel vertical line keeps its place and never vanishes
    std::fill(frame.begin(), frame.end(), 0x0000);
    for (int y = 0; y < SOURCE_HEIGHT; y++)
        frame[y * SOURCE_WIDTH + 100] = 0xFFFF;
    row(frame.data(), 10, out, scratch);
    int lit = 0, first = -1;
    for (int x = 0; x < TARGET_WIDTH; x++)
        if (out[x]) {
            lit++;
            if (first < 0)
                first = x;
        }
    CHECK(lit >= 1 && lit <= 3 && first >= 132 && first <= 134);

    if (failures) {
        printf("%d of %d screen checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: %d screen checks\n", checks);
    return 0;
}
