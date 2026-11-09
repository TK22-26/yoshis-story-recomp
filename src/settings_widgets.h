#pragma once

#include "ui_style.h"
#include "imgui.h"
#include <SDL.h>

// Native drawing primitives for the user's Settings HTML references.
// All coordinates are device-independent pixels. Fonts come from the OS.
namespace settings_ui {
constexpr ImU32 Background = IM_COL32(13, 19, 15, 255);
constexpr ImU32 Card = IM_COL32(18, 26, 20, 255);
constexpr ImU32 Border = IM_COL32(36, 50, 42, 255);
constexpr ImU32 FieldBorder = IM_COL32(49, 66, 54, 255);
constexpr ImU32 Field = IM_COL32(22, 32, 25, 255);
constexpr ImU32 Hover = IM_COL32(33, 48, 37, 255);
constexpr ImU32 Text = IM_COL32(234, 242, 234, 255);
constexpr ImU32 Muted = IM_COL32(143, 162, 147, 255);
constexpr ImU32 Secondary = IM_COL32(191, 205, 193, 255);
constexpr ImU32 Accent = IM_COL32(140, 224, 112, 255);
constexpr ImU32 AccentText = IM_COL32(196, 242, 178, 255);
constexpr ImU32 Selected = IM_COL32(29, 51, 23, 255);
constexpr ImU32 Warning = IM_COL32(230, 194, 60, 255);
constexpr ImU32 DarkText = IM_COL32(14, 26, 10, 255);

struct Fonts {
    ImFont *body = nullptr, *detail = nullptr, *label = nullptr, *title = nullptr, *number = nullptr;
    float loaded_scale = 0;
    void load(float density);
};
void begin_frame(float scale, Fonts &fonts);
float scale();
ImVec2 px(float x, float y);
void at(float x, float y);
float width();
float height();
enum class Type { Body, Small, Label, Title, Number };
void text(float x, float y, const char *value, ImU32 color = Text, Type type = Type::Body);
void text_right(float right, float y, const char *value, ImU32 color = Muted, Type type = Type::Small);
float paragraph(float x, float y, float wrap, const char *value, ImU32 color = Muted);
void caption(float x, float y, const char *value, ImU32 color = Muted);
void line(float x, float y, float w);
void badge(float right, float y, const char *value);
void begin_card(const char *id, float x, float y, float w, float h);
void end_card();
bool button(const char *id, const char *label, float x, float y, float w, float h = 40, bool primary = false,
            bool listening = false);
bool segments(const char *id, float x, float y, float w, const char *const *labels, int count, int &selected);
bool combo(const char *id, float x, float y, float w, int &selected, const char *const *labels, int count,
           const char* preview = nullptr, Type type = Type::Body);
bool slider(const char *id, float x, float y, float w, float h, float &value, float min, float max);
bool toggle(const char *id, float x, float y, bool &value, bool warning = false);
void image(SDL_Texture* texture, float x, float y, float w, float h);
void status(float x, float y, const char* label, bool connected);
void gamepad(float x, float y, float size);
void external_arrow(float x, float y);
void n64_glyph(int action, float x, float y);
void controller_glyph(int button, SDL_GameControllerType type, float x, float y, float size = 26);
void axis_glyph(int axis, SDL_GameControllerType type, float x, float y);
} // namespace settings_ui
