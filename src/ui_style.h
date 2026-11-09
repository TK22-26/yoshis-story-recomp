#pragma once
#include <array>
struct ImFont;
namespace ui {
void apply_style(float scale = 1.0f);
ImFont* add_font(float size, bool semibold = false);
// Pre-rasterize common monitor densities. Moving between monitors does not
// stretch the atlas or require rebuilding a texture in use by the GPU.
struct Fonts {
    std::array<ImFont*, 13> fonts{};
    void load(float size, bool semibold = false);
    ImFont* at(float scale) const;
};
}
