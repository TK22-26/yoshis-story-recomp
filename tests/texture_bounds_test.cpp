#include "yoshi_texture_bounds.h"
#include <cstdlib>
#include <iostream>
#include <limits>

int main() {
    using yoshi::graphics::clamp_sprite_tile_axis;
    auto check = [](bool result, const char* message) {
        if (!result) { std::cerr << message << '\n'; std::exit(1); }
    };
    check(clamp_sprite_tile_axis(0, 42, 1, 0, 164, 6, 1), "42-row strip must clamp before unused TMEM");
    check(clamp_sprite_tile_axis(0, 96, 1, 0, 380, 7, 1), "96-column panel must clamp");
    check(clamp_sprite_tile_axis(0, 84, .5f, 0, 164, 6, 1), "tile shifts must be applied");
    check(clamp_sprite_tile_axis(4, 46, 1, 16, 180, 6, 1), "offset tile must use its own bounds");
    check(!clamp_sprite_tile_axis(-1, 42, 1, 0, 164, 6, 1), "negative mirrored coordinates must remain mirrored");
    check(!clamp_sprite_tile_axis(0, 84, 1, 0, 164, 6, 1), "intentional repetition must remain intact");
    check(!clamp_sprite_tile_axis(0, 64, 1, 0, 252, 6, 1), "full power-of-two texture needs no correction");
    check(!clamp_sprite_tile_axis(0, 42, 1, 0, 164, 6, 0), "ordinary wrapping is outside this fix");
    check(!clamp_sprite_tile_axis(0, 42, 1, 0, 164, 6, 2), "existing clamp must remain unchanged");
    check(!clamp_sprite_tile_axis(0, 42, 1, 0, 164, 0, 1), "unmasked tile needs no correction");
    check(!clamp_sprite_tile_axis(4, 20, 1, 0, 164, 6, 1), "partial texture mapping is outside this fix");
    check(!clamp_sprite_tile_axis(INT32_MAX, INT32_MIN, 1, 0, 164, 6, 1), "unknown UV bounds must remain unchanged");
    check(!clamp_sprite_tile_axis(0, 42, std::numeric_limits<float>::infinity(), 0, 164, 6, 1), "invalid shift must remain unchanged");
    std::cout << "Texture edge and intentional wrapping checks passed\n";
}
