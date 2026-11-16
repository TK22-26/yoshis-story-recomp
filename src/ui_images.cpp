#include "ui_images.h"
#include "ui_images_data.h"
#include "stb/stb_image.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
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
struct Pixels { int width = 0, height = 0; std::vector<unsigned char> rgba; };
template<size_t N>
Pixels rasterize_svg(const unsigned char (&data)[N], float width) {
    std::vector<char> source(data, data + N);
    source.push_back(0);
    auto* document = nsvgParse(source.data(), "px", 96);
    if (!document) return {};
    Pixels result;
    auto* rasterizer = nsvgCreateRasterizer();
    if (rasterizer && document->width > 0 && document->height > 0) {
        const float scale = width / document->width;
        const int w = int(std::ceil(width)), h = int(std::ceil(document->height * scale));
        result.width = w;
        result.height = h;
        result.rgba.resize(w * h * 4);
        nsvgRasterize(rasterizer, document, 0, 0, scale, result.rgba.data(), w, h, w * 4);
    }
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(document);
    return result;
}
template<size_t N>
SDL_Texture* svg(SDL_Renderer* renderer, const unsigned char (&data)[N], float width) {
    auto pixels = rasterize_svg(data, width);
    return texture(renderer, pixels.rgba.data(), pixels.width, pixels.height);
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

void AtlasImages::load(ImFontAtlas* atlas, float density) {
    Pixels images[4];
    int components = 0;
    auto* png = stbi_load_from_memory(egg_png, sizeof(egg_png), &images[0].width, &images[0].height, &components, 4);
    if (png) images[0].rgba.assign(png, png + images[0].width * images[0].height * 4);
    stbi_image_free(png);
    images[1] = rasterize_svg(github_svg, 64 * density);
    images[2] = rasterize_svg(xbox_svg, 160 * density);
    images[3] = rasterize_svg(playstation_svg, 160 * density);
    AtlasImage* slots[] = {&egg, &github, &xbox, &playstation};
    for (int i = 0; i < 4; ++i) {
        *slots[i] = {};
        if (images[i].rgba.empty()) continue;
        *slots[i] = {atlas->AddCustomRectRegular(images[i].width + 2, images[i].height + 2),
                     images[i].width, images[i].height};
    }
    // All fonts must have been added before this build. GPU backends upload
    // these RGBA pixels with the atlas on the first frame; it stays immutable.
    unsigned char* pixels = nullptr;
    int width, height;
    atlas->GetTexDataAsRGBA32(&pixels, &width, &height);
    atlas->TexPixelsUseColors = true;
    for (int i = 0; i < 4; ++i) {
        if (slots[i]->rect < 0) continue;
        const auto* rect = atlas->GetCustomRectByIndex(slots[i]->rect);
        for (int y = 0; y < rect->Height; ++y)
            std::memset(pixels + ((rect->Y + y) * width + rect->X) * 4, 0, rect->Width * 4);
        for (int y = 0; y < images[i].height; ++y)
            std::memcpy(pixels + ((rect->Y + y + 1) * width + rect->X + 1) * 4,
                        images[i].rgba.data() + y * images[i].width * 4, images[i].width * 4);
    }
}
} // namespace ui_images
