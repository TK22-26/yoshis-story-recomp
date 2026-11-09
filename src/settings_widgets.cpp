#include "settings_widgets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace settings_ui {
namespace {
float density = 1;
Fonts *active_fonts = nullptr;
ImU32 ink(ImU32 color) { return ImGui::GetColorU32(color); }
ImVec4 rgba(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }
ImFont *font(Type type) {
    switch (type) {
    case Type::Small:
        return active_fonts->detail;
    case Type::Label:
        return active_fonts->label;
    case Type::Title:
        return active_fonts->title;
    case Type::Number:
        return active_fonts->number;
    default:
        return active_fonts->body;
    }
}
ImVec2 point(float x, float y) {
    const auto origin = ImGui::GetWindowPos();
    return ImVec2(origin.x + x * density - ImGui::GetScrollX(), origin.y + y * density - ImGui::GetScrollY());
}
void centered(float x, float y, float w, float h, const char *label, ImU32 color, Type type = Type::Label) {
    auto *f = font(type);
    const auto size = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label);
    text(x + (w - size.x / density) / 2, y + (h - size.y / density) / 2, label, color, type);
}
void rounded(float x, float y, float w, float h, ImU32 fill, ImU32 border, float radius) {
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(point(x, y), point(x + w, y + h), ink(fill), radius * density);
    if (border)
        draw->AddRect(point(x, y), point(x + w, y + h), ink(border), radius * density, 0, density);
}
void direction(int dir, float x, float y, float size, ImU32 color) {
    // Up, down, left, right; filled, rounded-looking directional triangle.
    const float angle[] = {0, 3.14159265f, -1.57079633f, 1.57079633f};
    const float c = std::cos(angle[dir]), s = std::sin(angle[dir]);
    auto p = [&](float dx, float dy) {
        return point(x + (dx * c - dy * s) * size, y + (dx * s + dy * c) * size);
    };
    ImGui::GetWindowDrawList()->AddTriangleFilled(p(0, -.30f), p(.27f, .20f), p(-.27f, .20f), ink(color));
}
void dpad(int dir, float x, float y, float size) {
    const float u = size / 24;
    const float parts[4][4] = {
        {8.5f, 1.5f, 7, 7.5f}, {8.5f, 15, 7, 7.5f}, {1.5f, 8.5f, 7.5f, 7}, {15, 8.5f, 7.5f, 7}};
    for (int i = 0; i < 4; i++)
        rounded(x + parts[i][0] * u, y + parts[i][1] * u, parts[i][2] * u, parts[i][3] * u,
                i == dir ? Text : IM_COL32(74, 92, 80, 255), 0, 1.5f * u);
    rounded(x + 8.5f * u, y + 8.5f * u, 7 * u, 7 * u, FieldBorder, 0, 0);
}
bool playstation(SDL_GameControllerType type) {
    return type == SDL_CONTROLLER_TYPE_PS3 || type == SDL_CONTROLLER_TYPE_PS4 ||
           type == SDL_CONTROLLER_TYPE_PS5;
}
} // namespace

void Fonts::load(float value) {
    loaded_scale = value;
#ifdef _WIN32
    value *= 2724.0f / 2048.0f; // Segoe UI em size, matching the desktop menu.
#endif
    body = ui::add_font(std::round(14 * value));
    detail = ui::add_font(std::round(12 * value));
    label = ui::add_font(std::round(14 * value), true);
    title = ui::add_font(std::round(32 * value), true);
    number = ui::add_font(std::round(44 * value), true);
}
float scale() { return density; }
ImVec2 px(float x, float y) { return ImVec2(x * density, y * density); }
void at(float x, float y) { ImGui::SetCursorPos(px(x, y)); }
float width() { return ImGui::GetWindowWidth() / density; }
float height() { return ImGui::GetWindowHeight() / density; }
void begin_frame(float value, Fonts &fonts) {
    density = value;
    active_fonts = &fonts;
    ImGui::GetIO().FontDefault = font(Type::Body);
    ImGui::GetStyle() = ImGuiStyle{};
    auto &s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(12, 10);
    s.ItemSpacing = ImVec2(10, 8);
    s.ChildRounding = 14;
    s.FrameRounding = 10;
    s.PopupRounding = 10;
    s.WindowRounding = 0;
    s.ScrollbarSize = 8;
    s.ScrollbarRounding = 4;
    s.GrabMinSize = 16;
    s.FrameBorderSize = 1;
    s.Colors[ImGuiCol_Text] = rgba(Text);
    s.Colors[ImGuiCol_TextDisabled] = rgba(Muted);
    s.Colors[ImGuiCol_WindowBg] = rgba(Background);
    s.Colors[ImGuiCol_ChildBg] = rgba(Card);
    s.Colors[ImGuiCol_PopupBg] = rgba(Field);
    s.Colors[ImGuiCol_Border] = rgba(Border);
    s.Colors[ImGuiCol_FrameBg] = rgba(Field);
    s.Colors[ImGuiCol_FrameBgHovered] = rgba(Hover);
    s.Colors[ImGuiCol_FrameBgActive] = rgba(Selected);
    s.Colors[ImGuiCol_Button] = rgba(Field);
    s.Colors[ImGuiCol_ButtonHovered] = rgba(Hover);
    s.Colors[ImGuiCol_ButtonActive] = rgba(Selected);
    s.Colors[ImGuiCol_Header] = rgba(Selected);
    s.Colors[ImGuiCol_HeaderHovered] = rgba(Hover);
    s.Colors[ImGuiCol_HeaderActive] = rgba(Selected);
    s.Colors[ImGuiCol_CheckMark] = rgba(Accent);
    s.Colors[ImGuiCol_SliderGrab] = rgba(Accent);
    s.Colors[ImGuiCol_SliderGrabActive] = rgba(AccentText);
    s.Colors[ImGuiCol_NavHighlight] = rgba(Accent);
    s.Colors[ImGuiCol_Separator] = rgba(Border);
    s.Colors[ImGuiCol_ScrollbarBg] = rgba(Card);
    s.Colors[ImGuiCol_ScrollbarGrab] = rgba(FieldBorder);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = rgba(IM_COL32(74, 92, 80, 255));
    s.Colors[ImGuiCol_ScrollbarGrabActive] = rgba(Accent);
    s.ScaleAllSizes(density);
}
void text(float x, float y, const char *value, ImU32 color, Type type) {
    auto *f = font(type);
    ImGui::GetWindowDrawList()->AddText(f, f->FontSize, point(x, y), ink(color), value);
}
void text_right(float right, float y, const char *value, ImU32 color, Type type) {
    auto *f = font(type);
    text(right - f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, value).x / density, y, value, color, type);
}
void paragraph(float x, float y, float wrap, const char *value, ImU32 color) {
    auto *f = font(Type::Small);
    ImGui::GetWindowDrawList()->AddText(f, f->FontSize, point(x, y), ink(color), value, nullptr,
                                        wrap * density);
}
void caption(float x, float y, const char *value, ImU32 color) {
    auto *f = font(Type::Small);
    const float size = f->FontSize * 11 / 12;
    for (const char *p = value; *p; p++) {
        const char letter[] = {*p, 0};
        ImGui::GetWindowDrawList()->AddText(f, size, point(x, y), ink(color), letter);
        x += f->CalcTextSizeA(size, FLT_MAX, 0, letter).x / density + 1.4f;
    }
}
void line(float x, float y, float w) {
    ImGui::GetWindowDrawList()->AddLine(point(x, y), point(x + w, y), ink(Border), density);
}
void badge(float right, float y, const char *value) {
    auto *f = font(Type::Small);
    float w = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, value).x / density + 16;
    rounded(right - w, y, w, 24, IM_COL32(10, 16, 12, 255), Border, 6);
    centered(right - w, y, w, 24, value, AccentText, Type::Small);
}
void begin_card(const char *id, float x, float y, float w, float h) {
    at(x, y);
    ImGui::BeginChild(id, px(w, h), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void end_card() { ImGui::EndChild(); }
bool button(const char *id, const char *label, float x, float y, float w, float h, bool primary,
            bool listening) {
    at(x, y);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, rgba(primary ? Accent : listening ? Selected : Field));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(primary ? AccentText : Hover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba(primary ? AccentText : Selected));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(primary ? Accent : listening ? Accent : FieldBorder));
    const bool pressed = ImGui::Button("##button", px(w, h));
    centered(x, y, w, h, label, primary ? DarkText : listening ? AccentText : Text);
    if (listening) {
        const float alpha = .45f + .35f * float(std::sin(ImGui::GetTime() * 5));
        auto pulse = rgba(Accent);
        pulse.w = alpha;
        ImGui::GetWindowDrawList()->AddRect(point(x - 1, y - 1), point(x + w + 1, y + h + 1),
                                            ImGui::GetColorU32(pulse), 10 * density, 0, 2 * density);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return pressed;
}
bool segments(const char *id, float x, float y, float w, const char *const *labels, int count,
              int &selected) {
    rounded(x, y, w, 42, IM_COL32(10, 16, 12, 255), Border, 10);
    bool changed = false;
    const float item = (w - 6 - (count - 1) * 2) / count;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * density);
    for (int i = 0; i < count; i++) {
        const float left = x + 3 + i * (item + 2);
        at(left, y + 3);
        ImGui::PushID(i);
        const bool on = selected == i;
        ImGui::PushStyleColor(ImGuiCol_Button, rgba(on ? Selected : IM_COL32(0, 0, 0, 0)));
        ImGui::PushStyleColor(ImGuiCol_Border, rgba(on ? Accent : IM_COL32(0, 0, 0, 0)));
        if (ImGui::Button("##segment", px(item, 36))) {
            selected = i;
            changed = true;
        }
        centered(left, y + 3, item, 36, labels[i], on ? AccentText : Secondary, Type::Small);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}
bool combo(const char *id, float x, float y, float w, int &selected, const char *const *labels, int count) {
    at(x, y);
    ImGui::SetNextItemWidth(w * density);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(12, (40 - ImGui::GetFontSize() / density) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, px(6, 6));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(FieldBorder));
    const bool changed = ImGui::Combo(id, &selected, labels, count);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return changed;
}
bool slider(const char *id, float x, float y, float w, float h, float &value, float min, float max) {
    at(x, y);
    ImGui::SetNextItemWidth(w * density);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, px(0, (h - ImGui::GetFontSize() / density) / 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    for (auto c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab,
                   ImGuiCol_SliderGrabActive, ImGuiCol_Text})
        ImGui::PushStyleColor(c, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::SliderFloat(id, &value, min, max, "", ImGuiSliderFlags_NoInput);
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    const float left = x + 10, right = x + w - 10;
    const float knob = left + (right - left) * std::clamp((value - min) / (max - min), 0.0f, 1.0f);
    rounded(left, y + h / 2 - 3, right - left, 6, FieldBorder, 0, 3);
    rounded(left, y + h / 2 - 3, std::max(0.0f, knob - left), 6, Accent, 0, 3);
    ImGui::GetWindowDrawList()->AddCircleFilled(point(knob, y + h / 2), 8 * density, ink(Accent), 24);
    return changed;
}
bool toggle(const char *id, float x, float y, bool &value, bool warning) {
    at(x, y);
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15 * density);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          rgba(value ? (warning ? Warning : Accent) : IM_COL32(27, 39, 30, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(value ? (warning ? Warning : AccentText) : Hover));
    const bool changed = ImGui::Button("##switch", px(52, 30));
    if (changed)
        value = !value;
    ImGui::GetWindowDrawList()->AddCircleFilled(point(x + (value ? 37 : 15), y + 15), 11 * density,
                                                ink(value ? DarkText : Muted), 24);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}
void egg(float x, float y, float size) {
    auto *d = ImGui::GetWindowDrawList();
    d->AddEllipseFilled(point(x + size * .5f, y + size * .5f), size * .32f * density, size * .46f * density,
                        ink(Text));
    for (const auto p : {ImVec2(.38f, .22f), ImVec2(.62f, .40f), ImVec2(.34f, .62f), ImVec2(.58f, .79f)})
        d->AddEllipseFilled(point(x + size * p.x, y + size * p.y), size * .10f * density,
                            size * .12f * density, ink(IM_COL32(75, 157, 65, 255)));
}
void gamepad(float x, float y, float size) {
    auto *d = ImGui::GetWindowDrawList();
    const float u = size / 24;
    ImVec2 pts[] = {point(x + 6 * u, y + 6 * u),   point(x + 18 * u, y + 6 * u),
                    point(x + 22 * u, y + 18 * u), point(x + 18 * u, y + 20 * u),
                    point(x + 14 * u, y + 16 * u), point(x + 10 * u, y + 16 * u),
                    point(x + 6 * u, y + 20 * u),  point(x + 2 * u, y + 18 * u)};
    d->AddPolyline(pts, 8, ink(AccentText), ImDrawFlags_Closed, 1.6f * density);
    d->AddLine(point(x + 6 * u, y + 11 * u), point(x + 10 * u, y + 11 * u), ink(AccentText), 1.5f * density);
    d->AddLine(point(x + 8 * u, y + 9 * u), point(x + 8 * u, y + 13 * u), ink(AccentText), 1.5f * density);
    d->AddCircleFilled(point(x + 16 * u, y + 10 * u), u * density, ink(AccentText));
    d->AddCircleFilled(point(x + 18 * u, y + 13 * u), u * density, ink(AccentText));
}
void external_arrow(float x, float y) {
    auto *d = ImGui::GetWindowDrawList();
    d->AddLine(point(x, y + 10), point(x + 10, y), ink(Muted), 1.6f * density);
    d->AddLine(point(x + 3, y), point(x + 10, y), ink(Muted), 1.6f * density);
    d->AddLine(point(x + 10, y), point(x + 10, y + 7), ink(Muted), 1.6f * density);
}
void project_icon(float x, float y) {
    rounded(x, y, 32, 32, IM_COL32(27, 39, 30, 255), 0, 8);
    auto *d = ImGui::GetWindowDrawList();
    // Source-branch symbol, drawn in code.
    d->AddLine(point(x + 11, y + 10), point(x + 11, y + 24), ink(Secondary), 1.6f * density);
    d->AddBezierCubic(point(x + 22, y + 10), point(x + 22, y + 17), point(x + 11, y + 13),
                      point(x + 11, y + 20), ink(Secondary), 1.6f * density);
    for (auto p : {ImVec2(11, 8), ImVec2(22, 8), ImVec2(11, 25)})
        d->AddCircle(point(x + p.x, y + p.y), 2.6f * density, ink(Text), 12, 1.5f * density);
}
void n64_glyph(int action, float x, float y) {
    if (action >= 10 && action < 14) {
        dpad(action - 10, x, y, 28);
        return;
    }
    if (action >= 14) {
        auto *d = ImGui::GetWindowDrawList();
        d->AddCircleFilled(point(x + 14, y + 14), 14 * density, ink(IM_COL32(42, 53, 48, 255)));
        d->AddCircle(point(x + 14, y + 14), 11 * density, ink(FieldBorder), 8, 1.5f * density);
        const ImVec2 offsets[] = {{0, -5}, {0, 5}, {-5, 0}, {5, 0}};
        const auto o = offsets[action - 14];
        d->AddCircleFilled(point(x + 14 + o.x, y + 14 + o.y), 6.6f * density,
                           ink(IM_COL32(185, 198, 188, 255)));
        d->AddCircle(point(x + 14 + o.x, y + 14 + o.y), 4 * density, ink(Muted), 16, density);
        return;
    }
    if (action >= 6) {
        rounded(x + 1, y + 1, 26, 26, Warning, IM_COL32(184, 150, 42, 255), 13);
        direction(action - 6, x + 14, y + 14, 22, DarkText);
        return;
    }
    const char *names[] = {"A", "B", "Z", "S", "L", "R"};
    const ImU32 colors[] = {IM_COL32(59, 111, 224, 255),
                            IM_COL32(62, 155, 79, 255),
                            FieldBorder,
                            IM_COL32(210, 59, 50, 255),
                            FieldBorder,
                            FieldBorder};
    const float w = action >= 4 ? 34 : action == 2 ? 20 : 26;
    const float h = action == 2 ? 32 : 26;
    rounded(x + (28 - w) / 2, y + (28 - h) / 2, w, h, colors[action], 0, action == 2 || action >= 4 ? 7 : 13);
    centered(x + (28 - w) / 2, y + (28 - h) / 2, w, h, names[action], Text);
}
void controller_glyph(int button, SDL_GameControllerType type, float x, float y, float size) {
    if (button >= SDL_CONTROLLER_BUTTON_DPAD_UP && button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
        dpad(button - SDL_CONTROLLER_BUTTON_DPAD_UP, x, y, size);
        return;
    }
    const bool ps = playstation(type);
    auto *d = ImGui::GetWindowDrawList();
    if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
        const bool left = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        rounded(x - 4, y + 3, size + 8, size - 6, Field, IM_COL32(74, 92, 80, 255), 7);
        centered(x - 4, y + 3, size + 8, size - 6, ps ? (left ? "L1" : "R1") : (left ? "LB" : "RB"), Text,
                 Type::Small);
        return;
    }
    rounded(x, y, size, size, IM_COL32(27, 39, 30, 255), IM_COL32(74, 92, 80, 255), size / 2);
    if (button >= 0 && button <= 3 && ps) {
        const float a = size * .28f, b = size * .72f, c = size * .5f;
        const ImU32 colors[] = {IM_COL32(143, 180, 245, 255), IM_COL32(240, 138, 138, 255),
                                IM_COL32(228, 160, 212, 255), IM_COL32(111, 214, 180, 255)};
        const auto color = ink(colors[button]);
        if (button == 0) {
            d->AddLine(point(x + a, y + a), point(x + b, y + b), color, 1.8f * density);
            d->AddLine(point(x + b, y + a), point(x + a, y + b), color, 1.8f * density);
        } else if (button == 1)
            d->AddCircle(point(x + c, y + c), size * .25f * density, color, 20, 1.8f * density);
        else if (button == 2)
            d->AddRect(point(x + a, y + a), point(x + b, y + b), color, 0, 0, 1.8f * density);
        else
            d->AddTriangle(point(x + c, y + a), point(x + b, y + b), point(x + a, y + b), color,
                           1.8f * density);
        return;
    }
    const char *label = "?";
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        label = "A";
        break;
    case SDL_CONTROLLER_BUTTON_B:
        label = "B";
        break;
    case SDL_CONTROLLER_BUTTON_X:
        label = "X";
        break;
    case SDL_CONTROLLER_BUTTON_Y:
        label = "Y";
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        label = ps ? "L1" : "LB";
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        label = ps ? "R1" : "RB";
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        label = ps ? "L3" : "LS";
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        label = ps ? "R3" : "RS";
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        label = ps ? "..." : "<";
        break;
    case SDL_CONTROLLER_BUTTON_GUIDE:
        label = "G";
        break;
    case SDL_CONTROLLER_BUTTON_START:
        for (int i = 0; i < 3; i++)
            d->AddLine(point(x + 8, y + 8 + i * 5), point(x + size - 8, y + 8 + i * 5), ink(Text),
                       1.5f * density);
        return;
    default:
        break;
    }
    centered(x, y, size, size, label, Text, Type::Small);
}
void axis_glyph(int action, SDL_GameControllerType type, float x, float y) {
    const bool ps = playstation(type);
    if (action == 2) {
        rounded(x, y + 1, 32, 26, Field, FieldBorder, 7);
        centered(x, y + 1, 32, 26, ps ? "L2" : "LT", Text, Type::Small);
        return;
    }
    rounded(x + 3, y, 26, 26, Field, FieldBorder, 13);
    centered(x + 3, y, 26, 26, action >= 14 ? (ps ? "L" : "LS") : (ps ? "R" : "RS"), Text, Type::Small);
}
} // namespace settings_ui
