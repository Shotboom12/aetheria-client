#pragma once
#include <cstdint>

namespace M2AnimationKeys {
struct Pair { uint32_t first = 0, second = 0; float ratio = 0.0f; };

// Keys need not begin at time zero. Use an exclusive upper bound so a time
// before the first authored key cannot underflow an unsigned search index.
inline Pair Find(const uint32_t* keys, uint32_t count, uint32_t time, uint32_t hint) {
    if (count < 2 || time <= keys[0]) return {};
    if (time >= keys[count - 1]) return {count - 1, count - 1, 0.0f};
    uint32_t first;
    if (hint < count - 1 && keys[hint] <= time && time < keys[hint + 1]) {
        first = hint;
    } else {
        uint32_t low = 0, high = count;
        while (low < high) {
            uint32_t const middle = low + (high - low) / 2;
            if (keys[middle] <= time) low = middle + 1;
            else high = middle;
        }
        first = low - 1;
    }
    uint32_t const second = first + 1;
    uint32_t const span = keys[second] - keys[first];
    return {first, second, span ? float(time - keys[first]) / float(span) : 0.0f};
}
}
