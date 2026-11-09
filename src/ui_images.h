#pragma once
#include <SDL.h>

namespace ui_images {
// Textures belong to one settings-window renderer, not the RT64 renderer.
struct Textures {
    SDL_Texture *egg = nullptr, *github = nullptr, *xbox = nullptr, *playstation = nullptr;
    float loaded_scale = 0;
    void load(SDL_Renderer* renderer, float density);
    void clear();
};
} // namespace ui_images
