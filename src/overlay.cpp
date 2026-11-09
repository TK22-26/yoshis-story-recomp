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
#include "ui_style.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "backends/imgui_impl_sdlrenderer2.h"

namespace {

std::atomic<bool> g_visible{false};
std::mutex g_attach_mutex;
bool g_attached = false;
RT64::Application* g_app = nullptr;   // only valid while g_attached is true
ui::Fonts g_menu_fonts;
ui::Fonts g_dialog_fonts;
float g_menu_scale = 1.0f;

// DKR-R-style capture state for button rebinding in the Controls tab.
// When CaptureTarget != None, handle_event() intercepts the next key/button.
enum class CaptureTarget { None, Keyboard, Controller };
CaptureTarget g_capture_target = CaptureTarget::None;
int g_capture_action_idx = -1;
// Only implemented settings appear in the menu.
enum class Page { Display, Audio, Controls, About, Count };
int g_active = int(Page::Display); // Main-thread dialog state
int g_pending_msaa = -1;
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
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void heading(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// Compact labels and wrapped descriptions share the launcher palette.
void field_label(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void field_desc(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
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

void draw_controls_tab() {
    heading("Controller mapping");
    ImGui::PushItemWidth(-1.0f);

    // --- Device ---
    field_label("Controller");
    const char* cname = input_current_controller_name();
    if (cname) {
        ImGui::TextUnformatted(cname);
    } else {
        ImGui::TextDisabled("No controller connected");
    }
    ImGui::Spacing();

    // --- Stick ---
    field_label("Stick deadzone");
    ImGui::SliderFloat("##dz", &g_settings.bindings.deadzone, 0.0f, 0.5f, "%.2f");
    field_desc("Minimum stick deflection registered as movement. Default: 0.15.");

    field_label("STICK AXIS");
    ImGui::Checkbox("Invert horizontal (X)", &g_settings.bindings.invert_x);
    ImGui::SameLine();
    ImGui::Checkbox("Invert vertical (Y)", &g_settings.bindings.invert_y);
    ImGui::Spacing();

    // --- Button mapping table ---
    heading("BUTTON MAPPING");
    field_desc("Click a key or button cell to rebind it. Esc cancels. "
               "Axis inputs (right stick, triggers) always active alongside any button binding.");
    ImGui::Spacing();

    static const char* const kActionLabels[] = {
        "A", "B", "Z", "Start",
        "L (shoulder)", "R (shoulder)",
        "C Up", "C Down", "C Left", "C Right",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right",
        "Stick Up", "Stick Down", "Stick Left", "Stick Right",
    };
    // Non-null = axis-driven; show label instead of rebind button.
    static const char* const kAxisLabels[] = {
        nullptr,                  // A
        nullptr,                  // B
        "L-trigger (axis)",       // Z
        nullptr,                  // Start
        nullptr,                  // L
        nullptr,                  // R
        "R-stick \xe2\x86\x91",  // CUp
        "R-stick \xe2\x86\x93",  // CDown
        "R-stick \xe2\x86\x90",  // CLeft
        "R-stick \xe2\x86\x92",  // CRight
        nullptr,                  // DUp
        nullptr,                  // DDown
        nullptr,                  // DLeft
        nullptr,                  // DRight
        "L-stick \xe2\x86\x91",  // StickUp
        "L-stick \xe2\x86\x93",  // StickDown
        "L-stick \xe2\x86\x90",  // StickLeft
        "L-stick \xe2\x86\x92",  // StickRight
    };
    static_assert(sizeof(kActionLabels) / sizeof(kActionLabels[0]) ==
                  static_cast<int>(N64Action::Count), "label count mismatch");
    static_assert(sizeof(kAxisLabels) / sizeof(kAxisLabels[0]) ==
                  static_cast<int>(N64Action::Count), "axis label count mismatch");

    if (ImGui::BeginTable("##bindings", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("N64 BUTTON", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("KEYBOARD",   ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < int(N64Action::Count); i++) {
            ImGui::TableNextRow();
            const float btnW = ImGui::GetContentRegionAvail().x;

            // Col 0: action name
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kActionLabels[i]);

            // Col 1: keyboard binding
            ImGui::TableSetColumnIndex(1);
            {
                const int sc = g_settings.bindings.actions[i].key;
                char label[80];
                const bool capturing = g_capture_target == CaptureTarget::Keyboard &&
                                       g_capture_action_idx == i;
                if (capturing) {
                    snprintf(label, sizeof(label), "Press a key...##kb%d", i);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.18f, 1.0f));
                } else if (sc < 0) {
                    snprintf(label, sizeof(label), "Unbound##kb%d", i);
                } else {
                    snprintf(label, sizeof(label), "%s##kb%d",
                             SDL_GetScancodeName(static_cast<SDL_Scancode>(sc)), i);
                }
                if (ImGui::Button(label, ImVec2(btnW, 0))) {
                    g_capture_action_idx = i;
                    g_capture_target = CaptureTarget::Keyboard;
                }
                if (capturing) ImGui::PopStyleColor(2);
            }

            // Col 2: controller binding
            ImGui::TableSetColumnIndex(2);
            {
                const char* axis = kAxisLabels[i];
                if (axis) {
                    // Axis-controlled: informational only.
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1.0f));
                    ImGui::TextUnformatted(axis);
                    ImGui::PopStyleColor();
                } else {
                    const int ct = g_settings.bindings.actions[i].btn;
                    char label[80];
                    const bool capturing = g_capture_target == CaptureTarget::Controller &&
                                           g_capture_action_idx == i;
                    if (capturing) {
                        snprintf(label, sizeof(label), "Press button...##ct%d", i);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.12f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.25f, 0.18f, 1.0f));
                    } else if (ct < 0) {
                        snprintf(label, sizeof(label), "Unbound##ct%d", i);
                    } else {
                        snprintf(label, sizeof(label), "%s##ct%d", button_name(ct), i);
                    }
                    if (ImGui::Button(label, ImVec2(btnW, 0))) {
                        g_capture_action_idx = i;
                        g_capture_target = CaptureTarget::Controller;
                    }
                    if (capturing) ImGui::PopStyleColor(2);
                }
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset to defaults")) {
        input_bindings_reset_defaults(g_settings.bindings);
        g_capture_target = CaptureTarget::None;
    }
    help_tip("Restore all keyboard and controller bindings to their factory defaults.");

    ImGui::PopItemWidth();
}

void draw_audio_tab() {
    heading("Audio");
    ImGui::PushItemWidth(-1.0f);

    // Work in 0..100 so SliderFloat can use "%.0f%%" as a live printf format.
    // ImGui substitutes the current float value into the format string each
    // frame — passing a pre-formatted string like "75%" freezes the display.
    float volPct = audio_get_volume() * 100.0f;
    bool muted   = audio_get_muted();

    field_label("Master volume");
    const float muteW  = ImGui::GetFontSize() * 4.5f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - muteW - spacing);
    if (ImGui::SliderFloat("##vol", &volPct, 0.0f, 100.0f,
                           muted ? "Muted" : "%.0f%%")) {
        audio_set_volume(volPct / 100.0f);
        if (muted) { audio_set_muted(false); muted = false; }
    }
    ImGui::SameLine();
    if (ImGui::Button(muted ? "Unmute" : "Mute", ImVec2(muteW, 0))) {
        audio_set_muted(!muted);
    }

    ImGui::Spacing();
    field_desc("Music, effects and voices share one volume control.");

    ImGui::PopItemWidth();
}

void draw_display_tab() {
    YoshiSettings& s = g_settings;
    const YoshiSettings before = s;
    if (ImGui::BeginTable("##video", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 160.0f * platform::window_scale(g_dialog_window));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        auto row = [](const char* label, const char* help) {
            ImGui::TableNextRow(0, 44.0f * platform::window_scale(g_dialog_window));
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            help_tip(help);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
        };
        static const char* resNames[] = {"Native (320 x 240)", "Fit window", "2x (640 x 480)",
            "3x (960 x 720)", "4x (1280 x 960)", "5x (1600 x 1200)",
            "6x (1920 x 1440)", "8x (2560 x 1920)", "10x (3200 x 2400)"};
        int res = resolution_index(s);
        row("Resolution", "Fit window chooses the largest whole multiple of 240 lines that fits.");
        if (ImGui::Combo("##resolution", &res, resNames, IM_ARRAYSIZE(resNames))) {
            set_resolution_index(s, res);
            if (s.resolution == YoshiSettings::Native) s.ssaa = 1;
        }
        const char* ssaaNames[] = {"Off", "2x", "3x", "4x", "5x"};
        int ssaa = std::clamp(s.ssaa, 1, 5) - 1;
        row("Supersampling", "Renders at a larger size and reduces it for smoother edges. Requires a fixed resolution.");
        ImGui::BeginDisabled(s.resolution != YoshiSettings::Manual);
        if (ImGui::Combo("##ssaa", &ssaa, ssaaNames, IM_ARRAYSIZE(ssaaNames))) s.ssaa = ssaa + 1;
        ImGui::EndDisabled();
        const char* msaaNames[] = {"Off", "2x", "4x", "8x"};
        int pending = g_pending_msaa < 0 ? s.msaa : g_pending_msaa;
        int msaa = pending >= 8 ? 3 : pending >= 4 ? 2 : pending >= 2 ? 1 : 0;
        row("Anti-aliasing", "Smooths polygon edges. Changes apply after restarting the game.");
        if (ImGui::Combo("##msaa", &msaa, msaaNames, IM_ARRAYSIZE(msaaNames)))
            g_pending_msaa = msaa == 0 ? 0 : 1 << msaa;
        const char* upNames[] = {"Original", "Scaled only", "All"};
        row("Upscale 2D", "Choose which sprites and backgrounds use the higher rendering resolution.");
        ImGui::Combo("##upscale", &s.upscale2D, upNames, IM_ARRAYSIZE(upNames));
        const char* filters[] = {"Nearest", "Linear", "Pixel scaling"};
        row("Output filter", "Controls how the image is scaled to the window.");
        ImGui::Combo("##filter", &s.filter, filters, IM_ARRAYSIZE(filters));
        row("CRT filter", "Adds scanlines and a color mask. F10 cycles the strength.");
        ImGui::SliderFloat("##crt", &s.crt, 0, 1, s.crt <= 0 ? "Off" : "%.2f");
        row("Overscan crop", "Hides pixels at each edge, like a TV. Two pixels is the default.");
        ImGui::SliderInt("##crop", &s.crop, 0, 8, "%d px");
        row("Window mode", "F11 or Alt+Enter also switches between windowed and borderless fullscreen.");
        if (ImGui::Checkbox("Borderless fullscreen", &s.fullscreen)) request_fullscreen(s.fullscreen);
        ImGui::EndTable();
    }
    if (g_pending_msaa >= 0 && g_pending_msaa != s.msaa)
        ImGui::TextColored(ImVec4(1, .70f, .24f, 1), "Anti-aliasing will change after restarting.");
    if ((before.resolution != s.resolution || before.multiplier != s.multiplier ||
         before.ssaa != s.ssaa || before.upscale2D != s.upscale2D || before.filter != s.filter) && g_app)
        apply_display_settings(g_app);
    if (before.crt != s.crt && g_app) {
        g_app->enhancementConfig.presentation.crtStrength = s.crt;
        g_app->updateEnhancementConfig();
    }
}

void draw_about_tab() {
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
    if (open) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        ImVec2(20 * g_menu_scale, 26 * g_menu_scale - ImGui::GetFontSize()));
    return open;
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15 * scale, 6 * scale));
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
            if (ImGui::MenuItem(game_pause::is_paused() ? "Resume game" : "Pause game", "Pause"))
                g_menu_action.store(5);
            if (ImGui::MenuItem("Restart game")) g_menu_action.store(1);
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) g_menu_action.store(2);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Controllers")) {
            if (ImGui::MenuItem("Controller mapping...")) open_page(Page::Controls);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Video")) {
            if (ImGui::MenuItem("Video settings...")) open_page(Page::Display);
            if (ImGui::MenuItem("Borderless fullscreen", "F11", g_settings.fullscreen))
                request_fullscreen(!g_settings.fullscreen);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Audio")) {
            if (ImGui::MenuItem("Audio settings...")) open_page(Page::Audio);
            if (ImGui::MenuItem("Mute", "Ctrl+M", audio_get_muted())) {
                audio_set_muted(!audio_get_muted());
                settings_save(g_settings_path);
            }
            end_desktop_menu();
        }
        if (begin_desktop_menu("Tools")) {
            if (ImGui::MenuItem("Open data folder")) g_menu_action.store(3);
            if (ImGui::MenuItem("Open save folder")) g_menu_action.store(4);
            end_desktop_menu();
        }
        if (begin_desktop_menu("Help")) {
            if (ImGui::MenuItem("About Yoshi's Story...")) open_page(Page::About);
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

void draw_settings_contents() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##native-settings", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float scale = platform::window_scale(g_dialog_window);
    const float footer = ImGui::GetFrameHeightWithSpacing() + 10 * scale;
    ImGui::BeginChild("##settings-content", ImVec2(0, -footer), false, ImGuiWindowFlags_NavFlattened);
    if (g_focus_content.exchange(false)) ImGui::SetKeyboardFocusHere();
    switch (Page(g_active)) {
        case Page::Display: draw_display_tab(); break;
        case Page::Audio: draw_audio_tab(); break;
        case Page::Controls: draw_controls_tab(); break;
        case Page::About: draw_about_tab(); break;
        default: break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("LB / RB: settings    B / Esc: close");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 90 * scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.30f, .49f, .27f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.39f, .59f, .33f, 1));
    if (ImGui::Button("Done", ImVec2(90 * scale, 0))) g_dialog_close.store(true);
    ImGui::PopStyleColor(2);
    ImGui::End();
    ImGui::PopStyleVar(2);
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
    SDL_DestroyRenderer(g_dialog_renderer);
    SDL_DestroyWindow(g_dialog_window);
    g_dialog_window = nullptr;
    g_dialog_renderer = nullptr;
    g_visible.store(false);
    SDL_RaiseWindow(g_sdl_window);
}

void show_settings_window(int page) {
    g_active = page;
    if (g_dialog_window) {
        SDL_SetWindowTitle(g_dialog_window, page_title());
        SDL_RaiseWindow(g_dialog_window);
        g_focus_content.store(true);
        return;
    }
    int x, y, w, h;
    SDL_GetWindowPosition(g_sdl_window, &x, &y);
    SDL_GetWindowSize(g_sdl_window, &w, &h);
    const float scale = platform::window_scale(g_sdl_window);
    const int width = int(700 * scale), height = int((Page(g_active) == Page::Audio ? 260 : 500) * scale);
    g_dialog_window = SDL_CreateWindow(page_title(), x + (w-width)/2, y + (h-height)/2,
        width, height, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_dialog_window) { g_visible.store(false); return; }
    SDL_SetWindowMinimumSize(g_dialog_window, int(560 * scale), int(260 * scale));
    platform::set_dialog_owner(g_dialog_window, g_sdl_window);
    g_dialog_renderer = SDL_CreateRenderer(g_dialog_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_dialog_renderer) g_dialog_renderer = SDL_CreateRenderer(g_dialog_window, -1, SDL_RENDERER_SOFTWARE);
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
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        ui::apply_style(scale);
        g_dialog_fonts.load(14);
        io.FontDefault = g_dialog_fonts.at(scale);
        ImGui_ImplSDL2_InitForSDLRenderer(g_dialog_window, g_dialog_renderer);
        ImGui_ImplSDLRenderer2_Init(g_dialog_renderer);
    }
    platform::enable_window(g_sdl_window, false);
    g_visible.store(true);
    g_focus_content.store(true);
    g_dialog_last_frame = 0;
    SDL_ShowWindow(g_dialog_window);
    SDL_RaiseWindow(g_dialog_window);
}

void render_settings_window() {
    if (!g_dialog_window) return;
    const auto now = SDL_GetTicks64();
    if (now - g_dialog_last_frame < 16) return;
    g_dialog_last_frame = now;
    std::scoped_lock lock(g_app->presentQueue->inspectorMutex);
    ScopedContext context(g_dialog_context);
    const float density = platform::window_scale(g_dialog_window);
    ui::apply_style(density);
    ImGui::GetIO().FontDefault = g_dialog_fonts.at(density);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    draw_settings_contents();
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
            // Don't let the user accidentally bind an overlay control button.
            const bool reserved = btn == SDL_CONTROLLER_BUTTON_BACK ||
                                  btn == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
                                  btn == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            if (!reserved && g_capture_target == CaptureTarget::Controller &&
                g_capture_action_idx >= 0) {
                g_settings.bindings.actions[g_capture_action_idx].btn = btn;
            }
            g_capture_target = CaptureTarget::None;
            return true;
        }
        // Swallow all other events while waiting for a binding press.
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
