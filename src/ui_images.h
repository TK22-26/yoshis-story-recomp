#pragma once
#include <SDL.h>
struct ImFontAtlas;

namespace ui_images {
// Textures belong to one settings-window renderer, not the RT64 renderer.
struct Textures {
    SDL_Texture *egg = nullptr, *github = nullptr, *xbox = nullptr, *playstation = nullptr;
    float loaded_scale = 0;
    void load(SDL_Renderer* renderer, float density);
    void clear();
};
// The in-game renderer shares its font atlas with these small UI images, so
// they work with both RT64 graphics backends without SDL texture handles.
struct AtlasImage {
    int rect = -1, width = 0, height = 0;
};
struct AtlasImages {
    AtlasImage egg, github, xbox, playstation;
    void load(ImFontAtlas* atlas, float density);
};
} // namespace ui_images
