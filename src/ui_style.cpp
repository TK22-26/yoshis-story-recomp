#include "ui_style.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ui {
ImFont* add_font(float size) {
    // Use an installed UI font. No font or game asset is copied into the repo
    // or release. Dear ImGui's built-in font is the last-resort fallback.
    std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
    wchar_t windows[MAX_PATH];
    if (GetWindowsDirectoryW(windows, MAX_PATH))
        candidates.push_back(std::filesystem::path(windows) / "Fonts" / "segoeui.ttf");
#else
    candidates = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};
#endif
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) continue;
        const auto length = file.tellg();
        if (length <= 0 || length > 16 * 1024 * 1024) continue;
        void* bytes = ImGui::MemAlloc(static_cast<size_t>(length));
        file.seekg(0);
        if (!file.read(static_cast<char*>(bytes), length)) { ImGui::MemFree(bytes); continue; }
        ImFontConfig config;
        config.PixelSnapH = true;
        config.OversampleH = 2;
        config.OversampleV = 2;
        config.RasterizerMultiply = 1.1f;
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(bytes, static_cast<int>(length), size, &config);
    }
    ImFontConfig cfg;
    cfg.SizePixels = size;
    return ImGui::GetIO().Fonts->AddFontDefault(&cfg);
}

void Fonts::load(float size) {
#ifdef _WIN32
    // RmlUi uses em sizes; stb_truetype measures ascent + descent. Segoe UI's
    // hhea span is 2724 units for a 2048-unit em. Preserve JFG's 13 dp visual
    // text size instead of shrinking the glyphs into a 13-pixel line box.
    size *= 2724.0f / 2048.0f;
#endif
    for (size_t i = 0; i < fonts.size(); ++i) fonts[i] = add_font(std::round(size * (1.0f + i * .25f)));
}
ImFont* Fonts::at(float scale) const {
    return fonts[std::clamp(int(std::round((scale - 1.0f) * 4)), 0, int(fonts.size()) - 1)];
}

void apply_style(float scale) {
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(16, 16);
    s.FramePadding = ImVec2(10, 5);
    s.ItemSpacing = ImVec2(8, 8);
    s.WindowRounding = 8;
    s.ChildRounding = 6;
    s.FrameRounding = 4;
    s.PopupRounding = 0;
    s.ScrollbarSize = 10;
    s.GrabRounding = 4;
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    s.TabRounding = 5;
    auto& c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(.95f, .95f, .88f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(.58f, .67f, .60f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(.055f, .105f, .080f, .99f);
    c[ImGuiCol_ChildBg] = ImVec4(.040f, .078f, .060f, 1);
    c[ImGuiCol_PopupBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_MenuBarBg] = ImVec4(.040f, .078f, .060f, 1);
    c[ImGuiCol_Border] = ImVec4(.19f, .30f, .23f, 1);
    c[ImGuiCol_Separator] = ImVec4(.13f, .23f, .17f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(.09f, .16f, .12f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(.14f, .24f, .17f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(.18f, .30f, .20f, 1);
    c[ImGuiCol_Button] = c[ImGuiCol_FrameBg];
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgHovered];
    c[ImGuiCol_ButtonActive] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_Header] = ImVec4(.13f, .24f, .15f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(.20f, .34f, .22f, 1);
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = ImVec4(.61f, .83f, .52f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(.74f, .92f, .65f, 1);
    c[ImGuiCol_NavHighlight] = c[ImGuiCol_CheckMark];
    c[ImGuiCol_ScrollbarBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_ScrollbarGrab] = c[ImGuiCol_Border];
    c[ImGuiCol_ScrollbarGrabHovered] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_ScrollbarGrabActive] = c[ImGuiCol_CheckMark];
    c[ImGuiCol_TitleBg] = c[ImGuiCol_MenuBarBg];
    c[ImGuiCol_TitleBgActive] = c[ImGuiCol_FrameBg];
    c[ImGuiCol_TextSelectedBg] = ImVec4(.34f, .54f, .33f, .55f);
    s.ScaleAllSizes(scale);
}
}
