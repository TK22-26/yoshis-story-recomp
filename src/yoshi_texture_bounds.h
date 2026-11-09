#pragma once

#include <cmath>
#include <cstdint>

namespace yoshi::graphics {

// The storybook's mirrored CI4 panels use a power-of-two wrap mask larger
// than their loaded image. Filtering at the last texel can read unused TMEM.
// Only close that edge when the quad covers this one tile: coordinates that
// deliberately repeat or mirror outside it must keep their original addressing.
inline bool clamp_sprite_tile_axis(int32_t minCoord, int32_t maxCoord, float shift,
    uint16_t upper, uint16_t lower, uint8_t mask, uint8_t mode) {
    constexpr uint8_t Mirror = 1;
    if (mode != Mirror || mask == 0 || mask > 15 || lower < upper ||
        (upper & 3) || (lower & 3) || minCoord > maxCoord ||
        !std::isfinite(shift) || shift <= 0) {
        return false;
    }
    const double start = upper / 4.0;
    const double last = lower / 4.0;
    const double size = last - start + 1.0;
    if (size >= (1u << mask)) return false;

    const double firstUV = double(minCoord) * shift;
    const double lastUV = double(maxCoord) * shift;
    return firstUV >= start && firstUV < start + 1.0 &&
        lastUV >= last && lastUV <= last + 1.0;
}

} // namespace yoshi::graphics
