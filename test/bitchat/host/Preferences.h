// Host stand-in for the ESP32 key-value store; never compiled into firmware.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct Preferences {
    static inline std::map<std::string, std::vector<uint8_t>> data;
    static inline unsigned writes = 0;
    static inline std::string space;
    bool begin(const char *name, bool)
    {
        space = name;
        return true;
    }
    bool isKey(const char *key) { return data.count(key); }
    size_t getBytesLength(const char *key) { return data.at(key).size(); }
    size_t getBytes(const char *key, void *out, size_t n)
    {
        memcpy(out, data.at(key).data(), n);
        return n;
    }
    size_t putBytes(const char *key, const void *in, size_t n)
    {
        ++writes;
        const auto *bytes = static_cast<const uint8_t *>(in);
        data[key] = {bytes, bytes + n};
        return n;
    }
};
