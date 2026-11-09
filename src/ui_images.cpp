#include "ui_images.h"
#include "ui_images_data.h"
#include "stb/stb_image.h"
#include <algorithm>
#include <cmath>
#include <vector>
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvg/nanosvgrast.h"

namespace ui_images {
namespace {
SDL_Texture* texture(SDL_Renderer* renderer, unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(rgba, width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    auto* result = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (result) {
        SDL_SetTextureBlendMode(result, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(result, SDL_ScaleModeLinear);
    }
    return result;
}
template<size_t N>
SDL_Texture* svg(SDL_Renderer* renderer, const unsigned char (&data)[N], float width) {
    std::vector<char> source(data, data + N);
    source.push_back(0);
    auto* document = nsvgParse(source.data(), "px", 96);
    if (!document) return nullptr;
    SDL_Texture* result = nullptr;
    auto* rasterizer = nsvgCreateRasterizer();
    if (rasterizer && document->width > 0 && document->height > 0) {
        const float scale = width / document->width;
        const int w = int(std::ceil(width)), h = int(std::ceil(document->height * scale));
        std::vector<unsigned char> pixels(w * h * 4);
        nsvgRasterize(rasterizer, document, 0, 0, scale, pixels.data(), w, h, w * 4);
        result = texture(renderer, pixels.data(), w, h);
    }
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(document);
    return result;
}
} // namespace
void Textures::clear() {
    for (auto** image : {&egg, &github, &xbox, &playstation}) {
        SDL_DestroyTexture(*image);
        *image = nullptr;
    }
    loaded_scale = 0;
}
void Textures::load(SDL_Renderer* renderer, float density) {
    clear();
    int w = 0, h = 0, components = 0;
    auto* pixels = stbi_load_from_memory(egg_png, sizeof(egg_png), &w, &h, &components, 4);
    egg = texture(renderer, pixels, w, h);
    stbi_image_free(pixels);
    // Rasterize the vectors above their display resolution for smooth edges
    // at fractional DPI. Recreate them on monitor/DPI changes.
    github = svg(renderer, github_svg, 64 * density);
    xbox = svg(renderer, xbox_svg, 160 * density);
    playstation = svg(renderer, playstation_svg, 160 * density);
    loaded_scale = density;
    if (!egg || !github || !xbox || !playstation)
        SDL_Log("Could not load one or more embedded settings images: %s", SDL_GetError());
}
} // namespace ui_images
