// Desktop top menu and in-game settings, following the JFG launcher layout.
//
// Pattern copied from ThatGuyMcd/DKR-R runtime_ui.cpp attach/detach/draw: we
// construct our own RT64::Inspector, install it in app.presentQueue->inspector,
// and drive its newFrame/endFrame ourselves. RT64 only drives the Inspector
// itself when userConfig->developerMode is true (rt64_state.cpp:1653), which
// we keep off, so we have exclusive ownership of its ImGui frame.

#include "overlay.h"
#include "game_pause.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include <cstdlib>
#include "platform.h"

#include <SDL.h>
#include "imgui.h"
// PushItemFlag(ImGuiItemFlags_NoNav, ...) lives in the internal header in
// ImGui 1.90 (promoted to public later). Used by the sidebar to lock the
// D-pad / stick to the content panel; see draw_sidebar().
#include "imgui_internal.h"

#include "hle/rt64_application.h"
#include "hle/rt64_application_window.h"
#include "hle/rt64_present_queue.h"
#include "gui/rt64_inspector.h"

#include "launcher.h"
#include "rom_setup.h"
#include "ui_style.h"
#include "settings_widgets.h"
#include "ui_images.h"
#include "version.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace {

std::atomic<bool> g_visible{false};
std::mutex g_attach_mutex;
bool g_attached = false;
RT64::Application* g_app = nullptr;   // only valid while g_attached is true
ui::Fonts g_menu_fonts;
settings_ui::Fonts g_dialog_fonts;
ui_images::Textures g_dialog_images;
float g_menu_scale = 1.0f;

// Capture state for keyboard, button and signed axis rebinding.
enum class CaptureTarget { None, Keyboard, Controller };
CaptureTarget g_capture_target = CaptureTarget::None;
int g_capture_action_idx = -1;
input::AxisCapture g_axis_capture;
// Only implemented settings appear in the menu.
enum class Page { Display, Audio, Controls, About, Count };
int g_active = int(Page::Display); // Main-thread dialog state
int g_pending_msaa = -1;
int g_active_msaa = 0;
std::atomic<bool> g_focus_content{false};
std::atomic<bool> g_menu_open{false};
std::atomic<int> g_menu_visibility{-1}; // -1: automatic; F1 explicitly shows/hides
std::atomic<float> g_menu_height{24.0f};
std::atomic<int> g_page_request{-1};
std::atomic<bool> g_dialog_close{false};
SDL_Window* g_dialog_window = nullptr;
SDL_Renderer* g_dialog_renderer = nullptr;
ImGuiContext* g_game_context = nullptr;
ImGuiContext* g_dialog_context = nullptr;
Uint64 g_dialog_last_frame = 0;
struct ScopedContext {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    explicit ScopedContext(ImGuiContext* context) { ImGui::SetCurrentContext(context); }
    ~ScopedContext() { ImGui::SetCurrentContext(previous); }
};
std::atomic<int> g_menu_action{0}; // restart, exit, data folder, save folder, pause
bool g_manual_pause = false; // main thread only
bool g_about_details = false;
bool g_dialog_saved = false;
bool g_dialog_save_failed = false;
YoshiSettings g_saved_settings;
Uint64 g_last_settings_save = 0;

constexpr double kMultipliers[] = { 2, 3, 4, 5, 6, 8, 10 };
constexpr int kMultiplierCount = int(sizeof(kMultipliers) / sizeof(kMultipliers[0]));

int resolution_index(const YoshiSettings& s) {
    if (s.resolution == YoshiSettings::Native) return 0;
    if (s.resolution == YoshiSettings::Window) return 1;
    int best = 0;
    for (int i = 1; i < kMultiplierCount; i++) {
        if (std::fabs(kMultipliers[i] - s.multiplier) < std::fabs(kMultipliers[best] - s.multiplier)) best = i;
    }
    return 2 + best;
}

void set_resolution_index(YoshiSettings& s, int index) {
    if (index == 0) s.resolution = YoshiSettings::Native;
    else if (index == 1) s.resolution = YoshiSettings::Window;
    else {
        s.resolution = YoshiSettings::Manual;
        s.multiplier = kMultipliers[index - 2];
    }
}

// Translate a SDL game controller event to ImGui gamepad input. RT64's SDL2
// ImGui backend ignores controllers so the overlay feeds them in itself.
void feed_controller_to_imgui(const SDL_Event& e) {
    ImGuiIO& io = ImGui::GetIO();
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
        ImGuiKey key = ImGuiKey_None;
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:        key = ImGuiKey_GamepadDpadUp; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:      key = ImGuiKey_GamepadDpadDown; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:      key = ImGuiKey_GamepadDpadLeft; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:     key = ImGuiKey_GamepadDpadRight; break;
            case SDL_CONTROLLER_BUTTON_A:              key = ImGuiKey_GamepadFaceDown; break;
            case SDL_CONTROLLER_BUTTON_B:              key = ImGuiKey_GamepadFaceRight; break;
            case SDL_CONTROLLER_BUTTON_X:              key = ImGuiKey_GamepadFaceLeft; break;
            case SDL_CONTROLLER_BUTTON_Y:              key = ImGuiKey_GamepadFaceUp; break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:   key = ImGuiKey_GamepadL1; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  key = ImGuiKey_GamepadR1; break;
            default: break;
        }
        if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
    } else if (e.type == SDL_CONTROLLERAXISMOTION) {
        const float v = e.caxis.value / 32767.0f;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            io.AddKeyEvent(ImGuiKey_GamepadLStickLeft,  v < -0.3f);
            io.AddKeyEvent(ImGuiKey_GamepadLStickRight, v > 0.3f);
        } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            io.AddKeyEvent(ImGuiKey_GamepadLStickUp,    v < -0.3f);
            io.AddKeyEvent(ImGuiKey_GamepadLStickDown,  v > 0.3f);
        }
    }
}

void help_tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, settings_ui::px(12, 8));
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleVar();
    }
}

void heading(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// Human-readable name for an SDL_GameControllerButton value.
// Mirrors DKR-R runtime_controls_ui.inl FriendlyBinding.
static const char* button_name(int btn) {
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_A:             return "A";
        case SDL_CONTROLLER_BUTTON_B:             return "B";
        case SDL_CONTROLLER_BUTTON_X:             return "X";
        case SDL_CONTROLLER_BUTTON_Y:             return "Y";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return "LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RB";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return "L-stick";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return "R-stick";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return "D-pad \xe2\x86\x91";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return "D-pad \xe2\x86\x93";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return "D-pad \xe2\x86\x90";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return "D-pad \xe2\x86\x92";
        case SDL_CONTROLLER_BUTTON_START:         return "Start";
        case SDL_CONTROLLER_BUTTON_BACK:          return "Back";
        case SDL_CONTROLLER_BUTTON_GUIDE:         return "Guide";
        default: return "?";
    }
}

namespace sw = settings_ui;

void draw_controls_tab() {
    const float w = std::max(900.0f, sw::width()), h = std::max(556.0f, sw::height());
    const float side = 296, table = w - 60 - side;
    const auto type = input_current_controller_type();
    sw::begin_card("##mapping", 20, 20, table, h - 40);
    sw::text(18, 14, "Button mapping", sw::Text, sw::Type::Label);
    sw::text_right(table - 18, 17, "Select a binding. Esc cancels.");
    const float actionW = std::min(170.0f, table * .27f);
    const float cellW = (table - 36 - actionW - 20) / 2;
    const float kbX = 18 + actionW + 10, padX = kbX + cellW + 10;
    sw::line(0, 47, table);
    sw::line(0, 77, table);
    sw::caption(18, 55, "N64");
    sw::caption(kbX, 55, "KEYBOARD");
    sw::caption(padX, 55, "CONTROLLER");
    sw::at(0, 78);
    ImGui::BeginChild("##mapping-scroll", sw::px(table, sw::height() - 78), false,
                      ImGuiWindowFlags_NavFlattened);
    const char *labels[] = {"A button",   "B button",   "Z trigger",  "Start",       "L shoulder",
                            "R shoulder", "C Up",       "C Down",     "C Left",      "C Right",
                            "D-pad Up",   "D-pad Down", "D-pad Left", "D-pad Right", "Stick Up",
                            "Stick Down", "Stick Left", "Stick Right"};
    float y = 4;
    for (int i = 0; i < int(N64Action::Count); i++) {
        if (i == 0 || i == 6 || i == 10 || i == 14) {
            sw::caption(18, y + 7,
                        i == 0    ? "BUTTONS"
                        : i == 6  ? "C BUTTONS"
                        : i == 10 ? "D-PAD"
                                  : "ANALOG STICK",
                        IM_COL32(127, 184, 108, 255));
            y += 30;
        }
        ImGui::PushID(i);
        sw::n64_glyph(i, 26, y + 4);
        sw::text(65, y + 8, labels[i]);
        const bool kbCapture = g_capture_target == CaptureTarget::Keyboard && g_capture_action_idx == i;
        const int key = g_settings.bindings.actions[i].key;
        const char *keyName = key < 0 ? "Unbound" : SDL_GetScancodeName(SDL_Scancode(key));
        if (sw::button("key", kbCapture ? "Press a key..." : keyName, kbX, y + 3, cellW, 30, false,
                       kbCapture)) {
            g_capture_target = kbCapture ? CaptureTarget::None : CaptureTarget::Keyboard;
            g_capture_action_idx = i;
        }
        const bool capture = g_capture_target == CaptureTarget::Controller && g_capture_action_idx == i;
        const auto& binding = g_settings.bindings.actions[i];
        if (sw::button("pad", capture ? "Move / press input..." :
                       binding.btn < 0 && binding.axis < 0 ? "Unbound" : "",
                       padX, y + 3, cellW, 30, false, capture)) {
            g_capture_target = capture ? CaptureTarget::None : CaptureTarget::Controller;
            g_capture_action_idx = i;
            g_axis_capture.begin(input_current_controller_state());
            ImGui::GetIO().ClearInputKeys();
        }
        help_tip(capture ? "Release a held stick or trigger, then move it in the direction to bind. Esc cancels."
                         : "Select, then press a controller button or move a stick or trigger. This replaces the controller binding.");
        if (!capture && binding.axis >= 0) {
            const float axisX = padX + 8;
            sw::axis_glyph(binding.axis, type, axisX, y + 4);
            const bool vertical = binding.axis == SDL_CONTROLLER_AXIS_LEFTY || binding.axis == SDL_CONTROLLER_AXIS_RIGHTY;
            const char* dir = binding.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "Left trigger" :
                binding.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? "Right trigger" :
                vertical ? (binding.axis_direction < 0 ? "Up" : "Down") :
                           (binding.axis_direction < 0 ? "Left" : "Right");
            sw::text(axisX + 40, y + 9, dir, sw::Secondary, sw::Type::Small);
            sw::text_right(padX + cellW - 7, y + 10, "AXIS", sw::Muted, sw::Type::Small);
        } else if (!capture && binding.btn >= 0) {
            sw::controller_glyph(binding.btn, type, padX + (cellW - 26) / 2, y + 5);
        }
        ImGui::PopID();
        y += 36;
    }
    sw::at(0, y + 10);
    ImGui::Dummy(sw::px(1, 1));
    ImGui::EndChild();
    sw::end_card();

    const float sideX = w - 20 - side;
    sw::begin_card("##device", sideX, 20, side, 164);
    sw::caption(16, 16, "CONTROLLER");
    const auto& devices = input_controllers();
    std::vector<const char*> choices{"Automatic"};
    int selected = g_settings.controller.empty() ? 0 : -1;
    std::string preview = "No controller";
    for (const auto& d : devices) {
        choices.push_back(d.label.c_str());
        if (d.key == g_settings.controller) selected = int(choices.size()) - 1;
        if (d.id == input_current_controller_id()) preview = d.label;
    }
    if (sw::combo("##controller", 88, 43, side - 104, selected, choices.data(), int(choices.size()),
                   preview.c_str(), sw::Type::Small)) {
        const std::string key = selected > 0 ? devices[selected - 1].key : "";
        input_select_controller(key);
        g_capture_target = CaptureTarget::None;
    }
    help_tip(preview.c_str());
    const char *device = input_current_controller_name();
    const auto selectedType = input_current_controller_type();
    const bool ps = selectedType == SDL_CONTROLLER_TYPE_PS3 || selectedType == SDL_CONTROLLER_TYPE_PS4 || selectedType == SDL_CONTROLLER_TYPE_PS5;
    if (device) sw::image(ps ? g_dialog_images.playstation : g_dialog_images.xbox, 16, 43, 64, 60);
    else sw::gamepad(24, 49, 48);
    sw::status(88, 87, device ? "Connected" : "Disconnected", device != nullptr);
    sw::paragraph(16, 119, side - 32, "Keyboard bindings stay active alongside the controller.");
    sw::end_card();

    sw::begin_card("##stick", sideX, 198, side, 252);
    sw::caption(16, 16, "ANALOG STICK");
    sw::text(16, 46, "Dead zone", sw::Text, sw::Type::Label);
    char value[24];
    snprintf(value, sizeof(value), "%.2f", g_settings.bindings.deadzone);
    sw::badge(side - 16, 43, value);
    if (sw::slider("##deadzone", 16, 77, side - 32, 24, g_settings.bindings.deadzone, 0, .5f))
        g_settings.bindings.deadzone = std::round(g_settings.bindings.deadzone * 100) / 100;
    sw::paragraph(16, 108, side - 32, "How far the stick must move before it counts. Default 0.15.");
    sw::line(16, 151, side - 32);
    sw::text(16, 174, "Invert horizontal (X)");
    sw::toggle("##invert-x", side - 68, 168, g_settings.bindings.invert_x);
    sw::text(16, 214, "Invert vertical (Y)");
    sw::toggle("##invert-y", side - 68, 208, g_settings.bindings.invert_y);
    sw::end_card();
    if (sw::button("##reset", "Reset to defaults", sideX, h - 62, side, 42)) {
        input_bindings_reset_defaults(g_settings.bindings);
        g_capture_target = CaptureTarget::None;
    }
}

void draw_audio_tab() {
    const float w = std::max(520.0f, sw::width()), h = std::max(296.0f, sw::height());
    sw::begin_card("##master-volume", 20, 20, w - 40, h - 40);
    const float cardW = sw::width(), cardH = sw::height();
    bool muted = audio_get_muted();
    float volume = audio_get_volume() * 100;
    sw::caption(18, 18, "MASTER VOLUME");
    sw::text(18, 44, "Music, effects and voices share one volume.", sw::Muted, sw::Type::Small);
    char value[12];
    snprintf(value, sizeof(value), "%.0f", volume);
    sw::text_right(cardW - 42, 8, value, muted ? sw::Muted : sw::AccentText, sw::Type::Number);
    sw::text(cardW - 38, 37, "%", sw::AccentText, sw::Type::Label);
    const float sliderY = cardH * .43f - 4;
    bool changed = false;
    if (sw::button("##volume-down", "-", 18, sliderY, 44, 44)) {
        volume = std::max(0.0f, volume - 5);
        changed = true;
    }
    if (muted)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, .5f);
    if (sw::slider("##volume", 76, sliderY, cardW - 152, 44, volume, 0, 100)) {
        volume = std::round(volume);
        changed = true;
    }
    if (muted)
        ImGui::PopStyleVar();
    if (sw::button("##volume-up", "+", cardW - 62, sliderY, 44, 44)) {
        volume = std::min(100.0f, volume + 5);
        changed = true;
    }
    if (changed)
        audio_set_volume(volume / 100);
    sw::line(18, cardH - 81, cardW - 36);
    sw::text(18, cardH - 65, "Mute", sw::Text, sw::Type::Label);
    sw::text(18, cardH - 41, "Ctrl+M", sw::Muted, sw::Type::Small);
    if (sw::toggle("##mute", cardW - 70, cardH - 60, muted, true))
        audio_set_muted(muted);
    sw::end_card();
}

void draw_display_tab() {
    YoshiSettings &s = g_settings;
    const auto before = s;
    const float w = std::max(900.0f, sw::width()), h = std::max(580.0f, sw::height());
    const float cardW = (w - 54) / 2, upperH = h - 244;
    sw::begin_card("##display", 20, 20, cardW, upperH);
    const float inner = cardW - 36;
    sw::caption(18, 16, "DISPLAY");
    sw::text(18, 48, "Window mode", sw::Text, sw::Type::Label);
    // Preserve the separately requested borderless-window mode alongside fullscreen.
    const char *modes[] = {"Windowed", "Borderless window", "Borderless fullscreen"};
    int mode = int(s.window_mode());
    if (sw::segments("##mode", 18, 76, inner, modes, 3, mode))
        request_window_mode(WindowMode(mode));
    help_tip("Fullscreen covers the entire monitor without borders. F11 or Alt+Enter toggles it.");
    sw::text(18, 139, "Resolution", sw::Text, sw::Type::Label);
    sw::text_right(cardW - 18, 142, "Internal render size");
    const char *resolutions[] = {"Native (320 x 240)", "Fit window",       "2x (640 x 480)",
                                 "3x (960 x 720)",     "4x (1280 x 960)",  "5x (1600 x 1200)",
                                 "6x (1920 x 1440)",   "8x (2560 x 1920)", "10x (3200 x 2400)"};
    int res = resolution_index(s);
    if (sw::combo("##resolution", 18, 167, inner, res, resolutions, IM_ARRAYSIZE(resolutions))) {
        set_resolution_index(s, res);
        if (s.resolution == YoshiSettings::Native)
            s.ssaa = 1;
    }
    const bool fixed = s.resolution == YoshiSettings::Manual;
    sw::text(18, 228, "Supersampling", fixed ? sw::Text : sw::Muted, sw::Type::Label);
    sw::text_right(cardW - 18, 231, fixed ? "Renders larger, then shrinks" : "Needs a fixed resolution",
                   fixed ? sw::Muted : sw::Warning);
    const char *ssNames[] = {"Off", "2x", "3x", "4x", "5x"};
    int ss = fixed ? std::clamp(s.ssaa, 1, 5) - 1 : 0;
    ImGui::BeginDisabled(!fixed);
    if (sw::combo("##ssaa", 18, 256, inner, ss, ssNames, 5))
        s.ssaa = ss + 1;
    ImGui::EndDisabled();
    sw::end_card();

    sw::begin_card("##image-quality", 34 + cardW, 20, cardW, upperH);
    sw::caption(18, 16, "IMAGE QUALITY");
    sw::text(18, 48, "Anti-aliasing", sw::Text, sw::Type::Label);
    sw::text_right(cardW - 18, 51, "Smooths jagged edges");
    const char *aaNames[] = {"Off", "2x", "4x", "8x"};
    const int pending = g_pending_msaa < 0 ? s.msaa : g_pending_msaa;
    int aa = pending >= 8 ? 3 : pending >= 4 ? 2 : pending >= 2 ? 1 : 0;
    if (sw::segments("##aa", 18, 76, inner, aaNames, 4, aa))
        g_pending_msaa = aa ? 1 << aa : 0;
    sw::text(18, 139, "Upscale 2D", sw::Text, sw::Type::Label);
    sw::text_right(cardW - 18, 142, "Sprites and backgrounds");
    const char *upNames[] = {"Original", "Scaled only", "All"};
    sw::segments("##upscale", 18, 167, inner, upNames, 3, s.upscale2D);
    sw::text(18, 228, "Output filter", sw::Text, sw::Type::Label);
    sw::text_right(cardW - 18, 231, "Scale to the window");
    const char *filters[] = {"Nearest", "Linear", "Pixel scaling"};
    sw::combo("##filter", 18, 256, inner, s.filter, filters, 3);
    if (pending != g_active_msaa)
        sw::text(18, 305, "Anti-aliasing applies after restarting.", sw::Warning, sw::Type::Small);
    sw::end_card();

    sw::begin_card("##effects", 20, h - 210, w - 40, 190);
    sw::caption(18, 16, "EFFECTS");
    const float effectW = (w - 40 - 36 - 28) / 2;
    sw::text(18, 48, "CRT filter", sw::Text, sw::Type::Label);
    sw::text_right(18 + effectW, 51, "F10 cycles in game");
    const char *crtNames[] = {"Off", "0.25", "0.5", "1.0"};
    const float strengths[] = {0, .25f, .5f, 1};
    int crt = 0;
    for (int i = 1; i < 4; i++)
        if (std::fabs(s.crt - strengths[i]) < std::fabs(s.crt - strengths[crt]))
            crt = i;
    if (sw::segments("##crt", 18, 76, effectW, crtNames, 4, crt))
        s.crt = strengths[crt];
    const float cropX = 46 + effectW;
    sw::text(cropX, 48, "Overscan crop", sw::Text, sw::Type::Label);
    char cropLabel[16];
    snprintf(cropLabel, sizeof(cropLabel), "%d px", s.crop);
    sw::badge(w - 58, 44, cropLabel);
    float crop = float(s.crop);
    if (sw::slider("##crop", cropX, 76, effectW, 42, crop, 0, 16))
        s.crop = int(std::round(crop));
    sw::paragraph(18, 142, w - 76,
                  "Overscan crop trims the screen edges where the original game drew stray pixels.");
    sw::end_card();
    if ((before.resolution != s.resolution || before.multiplier != s.multiplier || before.ssaa != s.ssaa ||
         before.upscale2D != s.upscale2D || before.filter != s.filter) &&
        g_app)
        apply_display_settings(g_app);
    if (before.crt != s.crt && g_app) {
        g_app->enhancementConfig.presentation.crtStrength = s.crt;
        g_app->updateEnhancementConfig();
    }
}

void draw_about_details();
void draw_about_tab() {
    const float w = std::max(600.0f, sw::width()), h = std::max(484.0f, sw::height());
    if (g_about_details) {
        sw::at(20, 20);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sw::px(18, 16));
        ImGui::BeginChild("##credits", sw::px(w - 40, h - 40), true);
        ImGui::PushTextWrapPos(0);
        draw_about_details();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }
    sw::begin_card("##about", 20, 20, w - 40, h - 40);
    const float cw = sw::width();
#ifdef _WIN32
    sw::caption(18, 16, "NATIVE PC RECOMPILATION - WINDOWS VERSION", IM_COL32(127, 184, 108, 255));
#else
    sw::caption(18, 16, "NATIVE PC RECOMPILATION", IM_COL32(127, 184, 108, 255));
#endif
    sw::image(g_dialog_images.egg, 18, 44, 40, 40);
    sw::text(70, 37, "Yoshi's Story Recomp", sw::Text, sw::Type::Title);
    sw::text(18, 93, "Native static recompilation - N64Recomp + RT64", sw::Secondary, sw::Type::Small);
    sw::badge(69, 123, app_version::string);
    sw::caption(18, 166, "PROJECTS");
    const char *labels[] = {"Windows fork", "Original port", "RT64 renderer", "N64ModernRuntime"};
    const char *urls[] = {"github.com/TK22-26/yoshis-story-recomp",
                          "github.com/damobeanrod/yoshis-story-recomp", "github.com/rt64/rt64",
                          "github.com/N64Recomp/N64ModernRuntime"};
    const char *links[] = {"https://github.com/TK22-26/yoshis-story-recomp",
                           "https://github.com/damobeanrod/yoshis-story-recomp",
                           "https://github.com/rt64/rt64", "https://github.com/N64Recomp/N64ModernRuntime"};
    for (int i = 0; i < 4; i++) {
        const float y = 190 + i * 62;
        if (sw::button(labels[i], "", 18, y, cw - 36, 54))
            SDL_OpenURL(links[i]);
        sw::image(g_dialog_images.github, 34, y + 15, 24, 24);
        sw::text(74, y + 7, labels[i], sw::Text, sw::Type::Label);
        sw::text(74, y + 30, urls[i], sw::Muted, sw::Type::Small);
        sw::external_arrow(cw - 42, y + 22);
    }
    sw::end_card();
}

void draw_about_details() {
    heading("About");
    ImGui::TextUnformatted("Yoshi's Story Recomp");
    ImGui::TextDisabled("Native static recompilation using N64Recomp + RT64");
    ImGui::Spacing();
    ImGui::BulletText("Windows fork: github.com/TK22-26/yoshis-story-recomp");
    ImGui::BulletText("Original port: github.com/damobeanrod/yoshis-story-recomp");
    ImGui::BulletText("RT64 submodule: github.com/rt64/rt64");
    ImGui::BulletText("N64ModernRuntime: github.com/N64Recomp/N64ModernRuntime");
    ImGui::Spacing();


    heading("In-game shortcuts");
    ImGui::BulletText("F1 - hide or show the menu bar");
    ImGui::BulletText("Esc / Controller Back - open or close settings");
    ImGui::BulletText("F10 - cycle CRT filter (off / 0.25 / 0.5 / 1.0)");
    ImGui::BulletText("F11 or Alt+Enter - toggle fullscreen");
    ImGui::BulletText("Pause - pause or resume the game");
    ImGui::BulletText("Ctrl+M - mute or unmute");
    ImGui::Spacing();

    heading("Credits");
    ImGui::TextWrapped(
        "This recomp would not exist without the people and projects whose "
        "work it stands on. Thank you.");
    ImGui::Spacing();
    ImGui::TextDisabled("Recomp toolchain and runtime");
    ImGui::BulletText("Mr-Wiseguy - N64Recomp and the original recomp approach");
    ImGui::BulletText("N64Recomp contributors - N64ModernRuntime, librecomp, ultramodern");
    ImGui::BulletText("Zelda64Recomp / Zelda64Recompiled team - input engine, mod system lineage");
    ImGui::Spacing();
    ImGui::TextDisabled("Renderer");
    ImGui::BulletText("rt64 team - RT64 (HLE renderer, Vulkan/D3D12 backends, texture replacement)");
    ImGui::BulletText("olivieryuyu - S2DEX microcode decoding referenced by patch 0001");
    ImGui::BulletText("GLideN64 project - prior art reference for S2DEX (independent impl here)");
    ImGui::Spacing();
    ImGui::TextDisabled("UI inspiration");
    ImGui::BulletText("ThatGuyMcd - DKR-R overlay design informed this one");
    ImGui::BulletText("Omar Cornut and Dear ImGui contributors - the whole UI");
    ImGui::Spacing();
    ImGui::TextDisabled("Libraries");
    ImGui::BulletText("SDL2 - window, audio, input");
    ImGui::BulletText("plume, hlslpp, im3d, implot - vendored in RT64");
    ImGui::Spacing();
    ImGui::TextDisabled("Decompilation");
    ImGui::BulletText("decompals/yoshis-story contributors - decomp this recomp was built against");
    ImGui::Spacing();
    ImGui::TextDisabled("The game itself remains the property of Nintendo. "
                        "This recomp ships no game data; a user-supplied ROM is required.");
}

void restart_game() {
    if (!platform::restart()) {
        fprintf(stderr, "[overlay] restart failed; keeping the current game open\n");
        return;
    }
    std::_Exit(0);
}

void open_page(Page page) {
    g_page_request.store(int(page), std::memory_order_release);
    g_visible.store(true, std::memory_order_release);
    g_focus_content.store(true, std::memory_order_release);
}

bool begin_desktop_menu(const char* label) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(252 * g_menu_scale, 0), ImVec2(252 * g_menu_scale, FLT_MAX));
    const bool open = ImGui::BeginMenu(label);
    if (open) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    return open;
}

bool desktop_menu_item(const char* label, const char* shortcut = nullptr, bool checked = false) {
    // Use explicit rows: MenuItem's half-spacing expansion makes the visible
    // inset depend on font metrics and can make an auto-sized popup scroll.
    const float scale = g_menu_scale;
    ImGui::PushID(label);
    const bool pressed = ImGui::Selectable("##item", false,
        ImGuiSelectableFlags_NoPadWithHalfSpacing | ImGuiSelectableFlags_SelectOnRelease,
        ImVec2(0, 26 * scale));
    const ImVec2 start = ImGui::GetItemRectMin();
    const ImVec2 end = ImGui::GetItemRectMax();
    const float textY = start.y + (26 * scale - ImGui::GetFontSize()) / 2;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(start.x + 12 * scale, textY), ImGui::GetColorU32(ImGuiCol_Text), label);
    float right = end.x - 12 * scale;
    if (checked) {
        ImGui::RenderCheckMark(draw, ImVec2(right - 10 * scale, start.y + 8 * scale),
            ImGui::GetColorU32(ImGuiCol_Text), 10 * scale);
        right -= 20 * scale;
    }
    if (shortcut) draw->AddText(ImVec2(right - ImGui::CalcTextSize(shortcut).x, textY),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), shortcut);
    ImGui::PopID();
    return pressed;
}

void desktop_menu_separator() {
    const float scale = g_menu_scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x + 4 * scale, pos.y + 4 * scale),
        ImVec2(pos.x + width - 4 * scale, pos.y + 5 * scale),
        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(width, 9 * scale));
}

void end_desktop_menu() {
    ImGui::PopStyleVar();
    ImGui::EndMenu();
}

bool menu_bar_visible() {
    const int visibility = g_menu_visibility.load();
    return visibility >= 0 ? visibility != 0 :
        (!g_settings.fullscreen || g_visible.load() || g_menu_open.load() || game_pause::is_paused());
}

void draw_menu_bar() {
    if (!menu_bar_visible()) {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            ImGui::ClosePopupToLevel(0, true);
        return;
    }
    const float scale = g_menu_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * scale, (24 * scale - ImGui::GetFontSize()) / 2));
    // JFG uses 3 dp of inset plus a 1 dp border around each dropdown.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * scale, 4 * scale));
    // BeginMenu adds spacing on both sides of a heading.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale, 24 * scale - ImGui::GetFontSize()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    const bool draw = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar(2);
    if (draw) {
        g_menu_height.store(ImGui::GetWindowHeight());
        ImGui::SetCursorPosX(4 * scale);
        if (begin_desktop_menu("Game")) {
            if (desktop_menu_item(game_pause::is_paused() ? "Resume game" : "Pause game", "Pause"))
                g_menu_action.store(5);
            if (desktop_menu_item("Restart game")) g_menu_action.store(1);
            if (desktop_menu_item("Select ROM...")) g_menu_action.store(6);
            desktop_menu_separator();
            if (desktop_menu_item("Quit", "Alt+F4")) g_menu_action.store(2);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Controllers")) {
            if (desktop_menu_item("Controller mapping...")) open_page(Page::Controls);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Video")) {
            if (desktop_menu_item("Video settings...")) open_page(Page::Display);
            desktop_menu_separator();
            if (desktop_menu_item("Windowed", nullptr, g_settings.window_mode() == WindowMode::Windowed))
                request_window_mode(WindowMode::Windowed);
            if (desktop_menu_item("Borderless window", nullptr, g_settings.window_mode() == WindowMode::BorderlessWindow))
                request_window_mode(WindowMode::BorderlessWindow);
            if (desktop_menu_item("Borderless fullscreen", "F11", g_settings.fullscreen))
                request_fullscreen(!g_settings.fullscreen);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Audio")) {
            if (desktop_menu_item("Audio settings...")) open_page(Page::Audio);
            if (desktop_menu_item(audio_get_muted() ? "Unmute" : "Mute", "Ctrl+M")) {
                audio_set_muted(!audio_get_muted());
                settings_save(g_settings_path);
            }
            end_desktop_menu();
        }
        if (begin_desktop_menu("Tools")) {
            if (desktop_menu_item("Open data folder")) g_menu_action.store(3);
            if (desktop_menu_item("Open save folder")) g_menu_action.store(4);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Help")) {
            if (desktop_menu_item("About Yoshi's Story...")) open_page(Page::About);
            end_desktop_menu();
        }
        const char* hint = game_pause::is_paused() ? "Paused    |    F1  Hide menu" : "F1  Hide menu";
        const float hintX = ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x - 20;
        if (hintX > ImGui::GetCursorPosX() + 20) {
            ImGui::SetCursorPosX(hintX);
            ImGui::TextDisabled("%s", hint);
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleVar(3);
}

const char* page_title() {
    static const char* titles[] = {"Video settings - Yoshi's Story", "Audio settings - Yoshi's Story",
        "Controller mapping - Yoshi's Story", "About Yoshi's Story"};
    return titles[g_active];
}

bool same_settings(const YoshiSettings &a, const YoshiSettings &b) {
    if (a.resolution != b.resolution || a.multiplier != b.multiplier || a.ssaa != b.ssaa ||
        a.msaa != b.msaa || a.upscale2D != b.upscale2D || a.filter != b.filter || a.crt != b.crt ||
        a.crop != b.crop || a.fullscreen != b.fullscreen || a.borderlessWindow != b.borderlessWindow ||
        a.volume != b.volume || a.muted != b.muted || a.bindings.deadzone != b.bindings.deadzone ||
        a.bindings.invert_x != b.bindings.invert_x || a.bindings.invert_y != b.bindings.invert_y ||
        a.controller != b.controller)
        return false;
    for (int i = 0; i < int(N64Action::Count); i++)
        if (a.bindings.actions[i].key != b.bindings.actions[i].key ||
            a.bindings.actions[i].btn != b.bindings.actions[i].btn ||
            a.bindings.actions[i].axis != b.bindings.actions[i].axis ||
            a.bindings.actions[i].axis_direction != b.bindings.actions[i].axis_direction)
            return false;
    return true;
}

void draw_settings_contents() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##native-settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float w = sw::width(), h = sw::height(), footerY = h - 60;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::BeginChild("##settings-content", sw::px(w, footerY), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NavFlattened);
    ImGui::PopStyleVar();
    if (g_focus_content.exchange(false))
        ImGui::SetKeyboardFocusHere();
    switch (Page(g_active)) {
    case Page::Display:
        draw_display_tab();
        break;
    case Page::Audio:
        draw_audio_tab();
        break;
    case Page::Controls:
        draw_controls_tab();
        break;
    case Page::About:
        draw_about_tab();
        break;
    default:
        break;
    }
    ImGui::EndChild();
    const auto origin = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(origin.x, origin.y + footerY * sw::scale()),
                                              ImVec2(origin.x + w * sw::scale(), origin.y + h * sw::scale()),
                                              IM_COL32(16, 24, 18, 255));
    sw::line(0, footerY, w);
    if (Page(g_active) == Page::About) {
        if (sw::button("##about-details", g_about_details ? "Back to overview" : "Credits & shortcuts", 20,
                       footerY + 9, 166, 42))
            g_about_details = !g_about_details;
    } else if (g_dialog_save_failed) {
        sw::text(20, footerY + 21, "Could not save settings", sw::Warning, sw::Type::Small);
    } else if (g_dialog_saved) {
        ImGui::RenderCheckMark(ImGui::GetWindowDrawList(),
                               ImVec2(origin.x + 20 * sw::scale(), origin.y + (footerY + 24) * sw::scale()),
                               sw::Accent, 12 * sw::scale());
        sw::text(42, footerY + 21, "Changes saved", sw::Accent, sw::Type::Small);
    }
    if (sw::button("##done", "Done", w - 116, footerY + 9, 96, 42, true))
        g_dialog_close.store(true);
    ImGui::End();
    ImGui::PopStyleVar();
}

void close_settings_window() {
    if (!g_dialog_window) { g_visible.store(false); return; }
    {
        std::scoped_lock lock(g_app->presentQueue->inspectorMutex);
        ScopedContext context(g_dialog_context);
        if (g_pending_msaa >= 0) g_settings.msaa = g_pending_msaa;
        settings_save(g_settings_path);
        g_capture_target = CaptureTarget::None;
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(g_dialog_context);
        g_dialog_context = nullptr;
    }
    // Destroying/reparenting a Win32 window can synchronously dispatch messages.
    // Do it only on the main thread, after releasing the renderer's UI lock.
    platform::enable_window(g_sdl_window, true);
    g_dialog_images.clear();
    SDL_DestroyRenderer(g_dialog_renderer);
    SDL_DestroyWindow(g_dialog_window);
    g_dialog_window = nullptr;
    g_dialog_renderer = nullptr;
    g_visible.store(false);
    SDL_RaiseWindow(g_sdl_window);
}

void show_settings_window(int page) {
    const bool changing_page = g_active != page;
    g_active = page;
    g_capture_target = CaptureTarget::None;
    g_about_details = false;
    const float scale = platform::window_scale(g_sdl_window);
    static const ImVec2 sizes[] = {{960, 640}, {560, 356}, {960, 616}, {600, 544}};
    static const ImVec2 minimums[] = {{760, 500}, {480, 320}, {760, 500}, {520, 460}};
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(g_sdl_window), &usable) != 0)
        usable = {0, 0, int(1920 * scale), int(1080 * scale)};
    const int width = std::min(int(sizes[page].x * scale), usable.w - 20);
    const int height = std::min(int(sizes[page].y * scale), usable.h - int(64 * scale));
    const int minWidth = std::min(width, int(minimums[page].x * scale));
    const int minHeight = std::min(height, int(minimums[page].y * scale));
    if (g_dialog_window) {
        SDL_SetWindowTitle(g_dialog_window, page_title());
        if (changing_page) {
            SDL_SetWindowMinimumSize(g_dialog_window, minWidth, minHeight);
            SDL_SetWindowSize(g_dialog_window, width, height);
        }
        SDL_RaiseWindow(g_dialog_window);
        g_focus_content.store(true);
        return;
    }
    int x, y, w, h;
    SDL_GetWindowPosition(g_sdl_window, &x, &y);
    SDL_GetWindowSize(g_sdl_window, &w, &h);
    const int dialogX = std::clamp(x + (w - width) / 2, usable.x, usable.x + usable.w - width);
    const int dialogY =
        std::clamp(y + (h - height) / 2, usable.y + int(40 * scale), usable.y + usable.h - height);
    g_dialog_window = SDL_CreateWindow(page_title(), dialogX, dialogY, width, height,
                                       SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_dialog_window) {
        g_visible.store(false);
        return;
    }
    SDL_SetWindowMinimumSize(g_dialog_window, minWidth, minHeight);
    platform::set_dialog_owner(g_dialog_window, g_sdl_window);
    g_dialog_renderer = SDL_CreateRenderer(g_dialog_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_dialog_renderer)
        g_dialog_renderer = SDL_CreateRenderer(g_dialog_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_dialog_renderer) {
        SDL_DestroyWindow(g_dialog_window);
        g_dialog_window = nullptr;
        g_visible.store(false);
        return;
    }
    // SDL_Renderer may recreate the native HWND; apply ownership and chrome last.
    platform::set_dialog_owner(g_dialog_window, g_sdl_window);
    platform::apply_window_chrome(g_dialog_window);
    set_window_icon(g_dialog_window);
    {
        std::scoped_lock lock(g_app->presentQueue->inspectorMutex);
        ScopedContext context(g_game_context);
        g_dialog_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_dialog_context);
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        g_dialog_fonts.load(scale);
        settings_ui::begin_frame(scale, g_dialog_fonts);
        ImGui_ImplSDL2_InitForSDLRenderer(g_dialog_window, g_dialog_renderer);
        ImGui_ImplSDLRenderer2_Init(g_dialog_renderer);
    }
    platform::enable_window(g_sdl_window, false);
    g_visible.store(true);
    g_focus_content.store(true);
    g_dialog_last_frame = 0;
    g_dialog_saved = false;
    g_dialog_save_failed = false;
    g_saved_settings = g_settings;
    if (g_pending_msaa >= 0)
        g_saved_settings.msaa = g_pending_msaa;
    g_last_settings_save = 0;
    SDL_ShowWindow(g_dialog_window);
    SDL_RaiseWindow(g_dialog_window);
}

void render_settings_window() {
    if (!g_dialog_window)
        return;
    const auto now = SDL_GetTicks64();
    if (now - g_dialog_last_frame < 16)
        return;
    g_dialog_last_frame = now;
    std::scoped_lock lock(g_app->presentQueue->inspectorMutex);
    ScopedContext context(g_dialog_context);
    const float density = platform::window_scale(g_dialog_window);
    if (g_dialog_fonts.loaded_scale != density) {
        // This SDL renderer owns its atlas. Rebuild at the new monitor DPI
        // instead of allocating every heading size at every DPI in one texture.
        ImGui_ImplSDLRenderer2_DestroyFontsTexture();
        ImGui::GetIO().Fonts->Clear();
        g_dialog_fonts.load(density);
    }
    if (g_dialog_images.loaded_scale != density) g_dialog_images.load(g_dialog_renderer, density);
    settings_ui::begin_frame(density, g_dialog_fonts);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_settings_contents();
    auto values = g_settings;
    if (g_pending_msaa >= 0)
        values.msaa = g_pending_msaa;
    if (!same_settings(values, g_saved_settings)) {
        g_dialog_saved = false;
        if ((!ImGui::IsAnyItemActive() && !g_dialog_save_failed) || now - g_last_settings_save >= 500) {
            g_last_settings_save = now;
            g_dialog_save_failed = !settings_save(g_settings_path, &values);
            if (!g_dialog_save_failed) {
                g_saved_settings = values;
                g_dialog_saved = true;
            }
        }
    }
    ImGui::Render();
    const auto scale = ImGui::GetIO().DisplayFramebufferScale;
    SDL_RenderSetScale(g_dialog_renderer, scale.x, scale.y);
    SDL_SetRenderDrawColor(g_dialog_renderer, 14, 27, 20, 255);
    SDL_RenderClear(g_dialog_renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(g_dialog_renderer);
}

} // namespace

namespace overlay {

void attach(RT64::Application& app) {
    std::scoped_lock guard(g_attach_mutex);
    if (g_attached) return;
    if (app.presentQueue == nullptr || app.device == nullptr ||
        app.swapChain == nullptr || app.appWindow == nullptr) {
        return;
    }
    std::scoped_lock lock(app.presentQueue->inspectorMutex);
    app.presentQueue->inspector = std::make_unique<RT64::Inspector>(
        app.device.get(), app.swapChain.get(), app.chosenGraphicsAPI,
        app.appWindow->sdlWindow);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard
                    | ImGuiConfigFlags_NavEnableGamepad;
    // RT64's SDL2 ImGui backend strips controller handling, so we feed the
    // gamepad into ImGui ourselves (see feed_controller_to_imgui). Marking
    // HasGamepad is required for Nav to pick up those input events.
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    g_game_context = ImGui::GetCurrentContext();
    g_menu_scale = platform::window_scale(g_sdl_window);
    ui::apply_style(g_menu_scale);
    g_menu_fonts.load(13);
    io.FontDefault = g_menu_fonts.at(g_menu_scale);

    g_app = &app;
    g_attached = true;
    g_pending_msaa = -1;
    g_active_msaa = g_settings.msaa;
}

void detach(RT64::Application& app) {
    std::scoped_lock guard(g_attach_mutex);
    if (g_visible.load(std::memory_order_acquire)) {
        // Persist any pending edits on the way out.
        if (g_pending_msaa >= 0) g_settings.msaa = g_pending_msaa;
        settings_save(g_settings_path);
    }
    g_visible.store(false, std::memory_order_release);
    if (app.presentQueue != nullptr) {
        std::scoped_lock lock(app.presentQueue->inspectorMutex);
        app.presentQueue->inspector.reset();
    }
    g_app = nullptr;
    g_attached = false;
}

void draw(RT64::Application& app) {
    if (!g_attached) return;
    if (app.presentQueue == nullptr || app.framebufferGraphicsWorker == nullptr) return;
    std::scoped_lock lock(app.presentQueue->inspectorMutex);
    RT64::Inspector* inspector = app.presentQueue->inspector.get();
    if (inspector == nullptr) return;

    ScopedContext context(g_game_context);
    g_menu_scale = platform::window_scale(g_sdl_window);
    ui::apply_style(g_menu_scale);
    ImGui::GetIO().FontDefault = g_menu_fonts.at(g_menu_scale);
    inspector->newFrame(app.framebufferGraphicsWorker.get());
    draw_menu_bar();
    g_menu_open.store(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
    inspector->endFrame();
}

void process_actions() {
    const int action = g_menu_action.exchange(0);
    if (action > 0 && action <= 2) {
        if (g_pending_msaa >= 0) g_settings.msaa = g_pending_msaa;
        settings_save(g_settings_path);
        if (action == 1) restart_game();
        else std::_Exit(0);
    } else if (action == 5) {
        g_manual_pause = !g_manual_pause;
    } else if (action == 6) {
        const bool paused = game_pause::is_paused();
        game_pause::set_paused(true);
        audio_set_paused(true);
        // Native dialogs run on SDL's main thread, outside the renderer lock.
        ensure_rom(g_settings_path.parent_path(), g_sdl_window, true);
        // Every accepted ROM has the same verified contents; no restart needed.
        game_pause::set_paused(paused);
        audio_set_paused(paused);
    } else if (action > 2) {
        const auto folder = action == 4 ? g_settings_path.parent_path() / "saves" : g_settings_path.parent_path();
        if (!platform::open_folder(folder)) fprintf(stderr, "[menu] Could not open folder\n");
    }
    if (!g_attached || !g_app || !g_app->presentQueue) return;
    if (g_dialog_close.exchange(false)) close_settings_window();
    const int page = g_page_request.exchange(-1);
    if (page >= 0) show_settings_window(page);
    const bool paused = g_manual_pause || g_visible.load();
    if (paused != game_pause::is_paused()) {
        game_pause::set_paused(paused);
        audio_set_paused(paused);
        fprintf(stderr, "[game] %s\n", paused ? "Paused" : "Resumed");
    }
    render_settings_window();
}

void cycle_page(int delta) {
    g_page_request.store((g_active + delta + int(Page::Count)) % int(Page::Count));
}

bool handle_event(SDL_Event* event) {
    if (!g_attached || event == nullptr || !g_app || !g_app->presentQueue) return false;
    std::scoped_lock lock(g_app->presentQueue->inspectorMutex);
    auto* inspector = g_app->presentQueue->inspector.get();
    if (!inspector) return false;
    // Only the selected device may navigate or rebind the settings window.
    if ((event->type == SDL_CONTROLLERBUTTONDOWN || event->type == SDL_CONTROLLERBUTTONUP) &&
        event->cbutton.which != input_current_controller_id()) return true;
    if (event->type == SDL_CONTROLLERAXISMOTION && event->caxis.which != input_current_controller_id()) return true;
    const Uint32 dialogId = g_dialog_window ? SDL_GetWindowID(g_dialog_window) : 0;
    bool dialogEvent = false;
    switch (event->type) {
        case SDL_WINDOWEVENT: dialogEvent = dialogId && event->window.windowID == dialogId; break;
        case SDL_KEYDOWN: case SDL_KEYUP: dialogEvent = dialogId && event->key.windowID == dialogId; break;
        case SDL_TEXTINPUT: dialogEvent = dialogId && event->text.windowID == dialogId; break;
        case SDL_MOUSEMOTION: dialogEvent = dialogId && event->motion.windowID == dialogId; break;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: dialogEvent = dialogId && event->button.windowID == dialogId; break;
        case SDL_MOUSEWHEEL: dialogEvent = dialogId && event->wheel.windowID == dialogId; break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: case SDL_CONTROLLERAXISMOTION:
            dialogEvent = dialogId != 0; break;
        default: break;
    }
    ScopedContext context(dialogEvent ? g_dialog_context : g_game_context);
    if (dialogEvent && event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE) {
        g_dialog_close.store(true);
        return true;
    }
    if(dialogEvent && event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)
        g_capture_target=CaptureTarget::None;
    if (event->type == SDL_CONTROLLERDEVICEREMOVED)
        g_capture_target = CaptureTarget::None;

    if (event->type == SDL_KEYDOWN && !event->key.repeat && event->key.keysym.scancode == SDL_SCANCODE_F1) {
        g_menu_visibility.store(menu_bar_visible() ? 0 : 1);
        return true;
    }

    // Capture mode (DKR-R pattern): intercept the next key/button press to
    // apply as a new binding. Takes priority over all other event handling so
    // that Esc cancels capture rather than closing the overlay.
    if (g_capture_target != CaptureTarget::None) {
        if (event->type == SDL_KEYDOWN && !event->key.repeat) {
            const SDL_Scancode sc = event->key.keysym.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) {
                // Esc cancels without assigning
                g_capture_target = CaptureTarget::None;
                return true;
            }
            if (g_capture_target == CaptureTarget::Keyboard && g_capture_action_idx >= 0) {
                g_settings.bindings.actions[g_capture_action_idx].key = sc;
                g_capture_target = CaptureTarget::None;
            }
            return true;
        }
        if (event->type == SDL_CONTROLLERBUTTONDOWN) {
            const int btn = event->cbutton.button;
            // Back cancels capture. Shoulder buttons remain valid game bindings;
            // capture takes precedence over their dialog page-navigation action.
            const bool reserved = btn == SDL_CONTROLLER_BUTTON_BACK;
            if (!reserved && g_capture_target == CaptureTarget::Controller &&
                g_capture_action_idx >= 0) {
                if (event->cbutton.which != input_current_controller_id()) return true;
                g_settings.bindings.actions[g_capture_action_idx].bind_button(btn);
            }
            g_capture_target = CaptureTarget::None;
            return true;
        }
        if (event->type == SDL_CONTROLLERAXISMOTION && g_capture_target == CaptureTarget::Controller &&
            g_capture_action_idx >= 0 && event->caxis.which == input_current_controller_id()) {
            if (g_axis_capture.motion(event->caxis.axis, event->caxis.value / 32767.f,
                                      g_settings.bindings.actions[g_capture_action_idx]))
                g_capture_target = CaptureTarget::None;
            return true;
        }
        // Keep mouse/focus events flowing so a different binding, Reset or Done
        // can cancel capture, and ImGui does not retain stale pointer state.
        if(event->type!=SDL_MOUSEMOTION && event->type!=SDL_MOUSEBUTTONDOWN &&
            event->type!=SDL_MOUSEBUTTONUP && event->type!=SDL_MOUSEWHEEL && event->type!=SDL_WINDOWEVENT)
            return true;
    }

    // Esc / Controller Back toggle settings and consume the event.
    if (event->type == SDL_KEYDOWN && !event->key.repeat) {
        const SDL_Scancode code = event->key.keysym.scancode;
        if (code == SDL_SCANCODE_PAUSE && !g_visible.load()) {
            g_menu_action.store(5);
            return true;
        }
        if (code == SDL_SCANCODE_ESCAPE) {
            toggle();
            return true;
        }
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN &&
        event->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        toggle();
        return true;
    }

    if (event->type == SDL_KEYDOWN && !event->key.repeat &&
            event->key.keysym.scancode == SDL_SCANCODE_M && (event->key.keysym.mod & KMOD_CTRL)) {
        audio_set_muted(!audio_get_muted());
        settings_save(g_settings_path);
        return true;
    }
    if (g_visible.load() && event->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event->cbutton.button) {
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: cycle_page(-1); return true;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: cycle_page(1); return true;
            case SDL_CONTROLLER_BUTTON_B: g_dialog_close.store(true); return true;
            default: break;
        }
    }
    // Feed pointer movement even while no dialog is open, so the persistent
    // top menu remains clickable. Inspector access is serialized with drawing.
    if (dialogEvent) ImGui_ImplSDL2_ProcessEvent(event);
    else inspector->handleSdlEvent(event);
    const bool capture = g_visible.load() || g_menu_open.load();
    if (capture) feed_controller_to_imgui(*event);
    if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP)
        return capture || (menu_bar_visible() && event->button.y < g_menu_height.load());
    if (event->type == SDL_MOUSEMOTION)
        return capture || (menu_bar_visible() && event->motion.y < g_menu_height.load());
    if (event->type == SDL_WINDOWEVENT) return dialogEvent;
    if (event->type == SDL_QUIT) return false;
    return capture;
}

void toggle() {
    if (g_visible.load()) g_dialog_close.store(true);
    else open_page(Page(g_active));
}

bool is_visible() {
    return g_visible.load(std::memory_order_acquire) || g_menu_open.load(std::memory_order_acquire);
}

} // namespace overlay
