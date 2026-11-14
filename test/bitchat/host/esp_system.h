// Host stand-in for the ESP32 random generator; never compiled into firmware.
#pragma once
#include <cstddef>
#include <cstdint>

inline void esp_fill_random(void *out, size_t size)
{
    auto *bytes = static_cast<uint8_t *>(out);
    for (size_t i = 0; i < size; ++i)
        bytes[i] = uint8_t(0xb0 + 7 * i);
}
