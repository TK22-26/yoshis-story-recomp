// Settings file + the launcher window shown before the game starts.
//
// The launcher draws with Dear ImGui (RT64's copy) on an SDL_Renderer in its
// own window, which is destroyed before the game's Vulkan window is created.
// RT64's SDL2 ImGui backend has controller support removed, so controller
// navigation is fed to ImGui here.

#include "launcher.h"
#include "platform.h"
#include "ui_style.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <SDL.h>
#include "imgui.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "icon_data.h"

YoshiSettings g_settings;

// --- Input bindings ---------------------------------------------------------

static const char* const kActionSaveNames[] = {
    "A", "B", "Z", "Start", "L", "R",
    "CUp", "CDown", "CLeft", "CRight",
    "DUp", "DDown", "DLeft", "DRight",
    "StickUp", "StickDown", "StickLeft", "StickRight",
};
static_assert(sizeof(kActionSaveNames) / sizeof(kActionSaveNames[0]) ==
              static_cast<int>(N64Action::Count), "kActionSaveNames size mismatch");

void input_bindings_reset_defaults(InputBindings& b) {
    b.actions[int(N64Action::A)]          = { SDL_SCANCODE_SPACE,  SDL_CONTROLLER_BUTTON_A };
    b.actions[int(N64Action::B)]          = { SDL_SCANCODE_LSHIFT, SDL_CONTROLLER_BUTTON_X };
    b.actions[int(N64Action::Z)]          = { SDL_SCANCODE_Z,      -1 };  // left trigger axis
    b.actions[int(N64Action::Start)]      = { SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START };
    b.actions[int(N64Action::L)]          = { SDL_SCANCODE_Q,      SDL_CONTROLLER_BUTTON_LEFTSHOULDER };
    b.actions[int(N64Action::R)]          = { SDL_SCANCODE_E,      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
    b.actions[int(N64Action::CUp)]        = { SDL_SCANCODE_I,      -1 };  // right stick Y-
    b.actions[int(N64Action::CDown)]      = { SDL_SCANCODE_K,      -1 };  // right stick Y+
    b.actions[int(N64Action::CLeft)]      = { SDL_SCANCODE_J,      -1 };  // right stick X-
    b.actions[int(N64Action::CRight)]     = { SDL_SCANCODE_L,      -1 };  // right stick X+
    b.actions[int(N64Action::DUp)]        = { SDL_SCANCODE_T,      SDL_CONTROLLER_BUTTON_DPAD_UP };
    b.actions[int(N64Action::DDown)]      = { SDL_SCANCODE_G,      SDL_CONTROLLER_BUTTON_DPAD_DOWN };
    b.actions[int(N64Action::DLeft)]      = { SDL_SCANCODE_F,      SDL_CONTROLLER_BUTTON_DPAD_LEFT };
    b.actions[int(N64Action::DRight)]     = { SDL_SCANCODE_H,      SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    b.actions[int(N64Action::StickUp)]    = { SDL_SCANCODE_W,      -1 };  // left stick Y- (also: Up arrow)
    b.actions[int(N64Action::StickDown)]  = { SDL_SCANCODE_S,      -1 };  // left stick Y+
    b.actions[int(N64Action::StickLeft)]  = { SDL_SCANCODE_A,      -1 };  // left stick X-
    b.actions[int(N64Action::StickRight)] = { SDL_SCANCODE_D,      -1 };  // left stick X+
    b.deadzone = 0.15f;
    b.invert_x = false;
    b.invert_y = false;
}

// --- Window icon ------------------------------------------------------------

SDL_Surface* icon_surface() {
    return SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(kIconRGBA), kIconSize, kIconSize,
        32, kIconSize * 4, SDL_PIXELFORMAT_RGBA32);
}

void set_window_icon(SDL_Window* window) {
    if (SDL_Surface* icon = icon_surface()) {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
}

// --- Settings file ----------------------------------------------------------

void settings_load(const std::filesystem::path& path) {
    // Defaults first so missing bind.* keys fall back to standard layout.
    input_bindings_reset_defaults(g_settings.bindings);

    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const size_t a = s.find_first_not_of(" \t\r");
            const size_t b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        const double v = atof(val.c_str());
        YoshiSettings& s = g_settings;
        if (key == "resolution") s.resolution = std::clamp(int(v), 0, 2);
        else if (key == "multiplier") s.multiplier = std::clamp(v, 1.0, 16.0);
        else if (key == "ssaa") s.ssaa = std::clamp(int(v), 1, 5);
        else if (key == "msaa") s.msaa = int(v);
        else if (key == "upscale2d") s.upscale2D = std::clamp(int(v), 0, 2);
        else if (key == "filter") s.filter = std::clamp(int(v), 0, 2);
        else if (key == "crt") s.crt = std::clamp(float(v), 0.0f, 1.0f);
        else if (key == "crop") s.crop = std::clamp(int(v), 0, 16);
        else if (key == "fullscreen") s.fullscreen = v != 0.0;
        else if (key == "borderless_window") s.borderlessWindow = v != 0.0;
        else if (key == "volume") s.volume = std::clamp(float(v), 0.0f, 1.0f);
        else if (key == "muted") s.muted = v != 0.0;
        else if (key == "show_launcher") s.showLauncher = v != 0.0;
        else if (key == "launcher_width") s.launcherW = std::max(0, int(v));
        else if (key == "launcher_height") s.launcherH = std::max(0, int(v));
        else if (key.rfind("bind.", 0) == 0) {
            const std::string rest = key.substr(5);
            if (rest == "deadzone") {
                s.bindings.deadzone = std::clamp(float(v), 0.0f, 0.5f);
            } else if (rest == "invert_x") {
                s.bindings.invert_x = v != 0.0;
            } else if (rest == "invert_y") {
                s.bindings.invert_y = v != 0.0;
            } else {
                // Format: {ActionName}.{key|btn}
                const auto dot = rest.rfind('.');
                if (dot != std::string::npos) {
                    const std::string aname = rest.substr(0, dot);
                    const std::string field = rest.substr(dot + 1);
                    for (int i = 0; i < int(N64Action::Count); i++) {
                        if (aname == kActionSaveNames[i]) {
                            if (field == "key") s.bindings.actions[i].key = int(v);
                            else if (field == "btn") s.bindings.actions[i].btn = int(v);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void settings_save(const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        fprintf(stderr, "[settings] could not write %s\n", path.string().c_str());
        return;
    }
    const YoshiSettings& s = g_settings;
    out << "# Yoshi's Story recomp display settings (edited by the launcher window).\n"
        << "# resolution: 0 native, 1 fit window, 2 multiplier; upscale2d: 0 original,\n"
        << "# 1 scaled only, 2 all; filter: 0 nearest, 1 linear, 2 pixel scaling.\n"
        << "resolution = " << s.resolution << "\n"
        << "multiplier = " << s.multiplier << "\n"
        << "ssaa = " << s.ssaa << "\n"
        << "msaa = " << s.msaa << "\n"
        << "upscale2d = " << s.upscale2D << "\n"
        << "filter = " << s.filter << "\n"
        << "crt = " << s.crt << "\n"
        << "crop = " << s.crop << "\n"
        << "fullscreen = " << (s.fullscreen ? 1 : 0) << "\n"
        << "borderless_window = " << (s.borderlessWindow ? 1 : 0) << "\n"
        << "volume = " << s.volume << "\n"
        << "muted = " << (s.muted ? 1 : 0) << "\n"
        << "show_launcher = " << (s.showLauncher ? 1 : 0) << "\n"
        << "launcher_width = " << s.launcherW << "\n"
        << "launcher_height = " << s.launcherH << "\n";
    // Input bindings
    out << "# Input bindings: SDL_Scancode for key, SDL_GameControllerButton for btn, -1 = unbound.\n";
    for (int i = 0; i < int(N64Action::Count); i++) {
        out << "bind." << kActionSaveNames[i] << ".key = " << s.bindings.actions[i].key << "\n"
            << "bind." << kActionSaveNames[i] << ".btn = " << s.bindings.actions[i].btn << "\n";
    }
    out << "bind.deadzone = " << s.bindings.deadzone << "\n"
        << "bind.invert_x = " << (s.bindings.invert_x ? 1 : 0) << "\n"
        << "bind.invert_y = " << (s.bindings.invert_y ? 1 : 0) << "\n";
}

void settings_apply_env() {
    YoshiSettings& s = g_settings;
    if (const char* v = getenv("YOSHI_RES")) {
        if (!SDL_strcasecmp(v, "native") || !SDL_strcasecmp(v, "original")) {
            s.resolution = YoshiSettings::Native;
        } else if (!SDL_strcasecmp(v, "window") || !SDL_strcasecmp(v, "auto")) {
            s.resolution = YoshiSettings::Window;
        } else if (atof(v) > 0.0) {
            s.resolution = YoshiSettings::Manual;
            s.multiplier = atof(v);
        } else {
            fprintf(stderr, "[display] YOSHI_RES=%s not understood (native, window or a number)\n", v);
        }
    }
    if (const char* v = getenv("YOSHI_SSAA")) s.ssaa = std::max(1, atoi(v));
    if (const char* v = getenv("YOSHI_MSAA")) s.msaa = atoi(v);
    if (const char* v = getenv("YOSHI_UPSCALE2D")) {
        if (!SDL_strcasecmp(v, "all") || !strcmp(v, "1")) s.upscale2D = 2;
        else if (!SDL_strcasecmp(v, "scaled")) s.upscale2D = 1;
        else s.upscale2D = 0;
    }
    if (const char* v = getenv("YOSHI_FILTER")) {
        if (!SDL_strcasecmp(v, "nearest")) s.filter = 0;
        else if (!SDL_strcasecmp(v, "linear")) s.filter = 1;
        else s.filter = 2;
    }
    if (const char* v = getenv("YOSHI_CRT")) s.crt = std::clamp(float(atof(v)), 0.0f, 1.0f);
    if (const char* v = getenv("YOSHI_CROP")) s.crop = std::max(0, atoi(v));
    if (const char* v = getenv("YOSHI_FULLSCREEN")) s.fullscreen = v[0] != '0';
}

// --- Launcher window --------------------------------------------------------

namespace {

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

// Controller -> ImGui gamepad navigation keys.
void feed_controller_event(const SDL_Event& e) {
    ImGuiIO& io = ImGui::GetIO();
    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
        ImGuiKey key = ImGuiKey_None;
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    key = ImGuiKey_GamepadDpadUp; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  key = ImGuiKey_GamepadDpadDown; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  key = ImGuiKey_GamepadDpadLeft; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = ImGuiKey_GamepadDpadRight; break;
        case SDL_CONTROLLER_BUTTON_A:          key = ImGuiKey_GamepadFaceDown; break;
        case SDL_CONTROLLER_BUTTON_B:          key = ImGuiKey_GamepadFaceRight; break;
        default: break;
        }
        if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
    } else if (e.type == SDL_CONTROLLERAXISMOTION) {
        // Left stick acts as the d-pad.
        const float v = e.caxis.value / 32767.0f;
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, v < -0.5f);
            io.AddKeyEvent(ImGuiKey_GamepadDpadRight, v > 0.5f);
        } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            io.AddKeyEvent(ImGuiKey_GamepadDpadUp, v < -0.5f);
            io.AddKeyEvent(ImGuiKey_GamepadDpadDown, v > 0.5f);
        }
    }
}

void note(const char* fmt, ...) IM_FMTARGS(1);
void note(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

// Tooltip for the previous item: shown on mouse hover, and for the focused
// item when navigating with the keyboard or a controller.
void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

constexpr const char* kTipResolution =
    "The resolution the game is drawn at.\n\n"
    "Native: the console's own 320x240.\n"
    "Fit window: the largest whole multiple of 240 lines that fits the window; "
    "it follows the window when you resize it.\n"
    "2x-8x: a fixed size, whatever the window. Needed for supersampling.";
constexpr const char* kTipSupersampling =
    "Supersampling draws the game this many times larger again than the chosen "
    "resolution, then shrinks the picture back down to it. Every edge, sprite and "
    "background gets averaged over several pixels, so jagged edges and shimmer "
    "disappear.\n\n"
    "It is the most thorough smoothing but also the most expensive: 2x draws 4 times "
    "the pixels, 4x draws 16 times. Only available with a fixed 2x-8x resolution.";
constexpr const char* kTipMsaa =
    "Multisample anti-aliasing smooths the edges of 3D polygons cheaply. "
    "Yoshi's Story is almost all 2D sprites, so expect little difference.";
constexpr const char* kTipUpscale2D =
    "Which 2D pieces (sprites, backgrounds, text) are drawn at the high resolution.\n\n"
    "Original: all at console resolution (blocky, never seams).\n"
    "Scaled only: only scaled or rotated sprites (the default).\n"
    "All: everything; sharpest, but tiled backgrounds can show thin seams.";
constexpr const char* kTipFilter =
    "How the finished picture is stretched to fit the window.\n\n"
    "Pixel scaling: crisp pixels without uneven sizes or shimmer (recommended).\n"
    "Nearest: hard pixels; uneven at sizes that are not whole multiples.\n"
    "Linear: smooth but blurry.";
constexpr const char* kTipCrt =
    "Imitates a CRT television: dark gaps between the scanlines and an RGB "
    "phosphor mask. Needs a tall window (720+ pixels) or fullscreen; it fades out "
    "when the window is too small to draw the lines.\n\nIn game, F10 cycles off / 0.25 / 0.5 / 1.0.";
constexpr const char* kTipCrop =
    "Hides this many pixels along each edge, like a TV's overscan. The game leaves "
    "a stale 1-pixel border, so 2 is recommended; 0 shows everything.";
constexpr const char* kTipFullscreen =
    "Borderless window keeps the window size without its title bar. Borderless "
    "fullscreen covers the monitor. F11 or Alt+Enter toggles fullscreen.";
constexpr const char* kTipLauncher =
    "Untick to go straight into the game next time. Run with YOSHI_LAUNCHER=1 "
    "to bring this window back.";

} // namespace

bool run_launcher(const std::filesystem::path& settingsPath) {
    // Opens at the size it had last time (saved on Start), else 1100x680;
    // shrunk only if the screen is smaller than that.
    int winW = g_settings.launcherW > 0 ? g_settings.launcherW : 1100;
    int winH = g_settings.launcherH > 0 ? g_settings.launcherH : 680;
    // Only shrink for a plausible screen: on Wayland the usable bounds can be
    // a placeholder before the outputs are known.
    SDL_Rect usable = {};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w >= 1024 && usable.h >= 720) {
        winW = std::min(winW, usable.w * 95 / 100);
        winH = std::min(winH, usable.h * 95 / 100);
    }
    // Created hidden: making the SDL_Renderer can recreate the window (to add
    // OpenGL), and some compositors size a window on its first configure. The
    // size is applied again before showing and after the first frame.
    SDL_Window* window = SDL_CreateWindow("Yoshi's Story Recomp - Settings",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "[launcher] SDL_CreateWindow failed: %s (starting the game)\n", SDL_GetError());
        return true;
    }
    SDL_SetWindowMinimumSize(window, 640, 480);
    set_window_icon(window);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "[launcher] SDL_CreateRenderer failed: %s (starting the game)\n", SDL_GetError());
        SDL_DestroyWindow(window);
        return true;
    }

    SDL_SetWindowSize(window, winW, winH);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    platform::apply_window_chrome(window);
    SDL_ShowWindow(window);

    SDL_Texture* logo = nullptr;
    if (SDL_Surface* icon = icon_surface()) {
        logo = SDL_CreateTextureFromSurface(renderer, icon);
        SDL_FreeSurface(icon);
    }

    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !controller; i++) {
        if (SDL_IsGameController(i)) controller = SDL_GameControllerOpen(i);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    const float density = platform::window_scale(window);
    ui::apply_style(density);
    ImGuiStyle& style = ImGui::GetStyle();
    ui::Fonts fonts;
    fonts.load(14);
    io.FontDefault = fonts.at(density);

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    YoshiSettings& s = g_settings;
    bool start = false, quit = false, firstFrame = true;
    while (!start && !quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            feed_controller_event(e);
            if (e.type == SDL_QUIT ||
                (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                quit = true;
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                start = true;
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !controller) {
                controller = SDL_GameControllerOpen(e.cdevice.which);
            }
        }

        ui::apply_style(platform::window_scale(window));
        io.FontDefault = fonts.at(platform::window_scale(window));
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        if (logo) {
            ImGui::Image((ImTextureID)logo, ImVec2(56, 56));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Yoshi's Story Recomp");
        ImGui::TextDisabled("Display settings - hover or select an option for details");
        ImGui::EndGroup();
        ImGui::Separator();

        // Label column: wide enough for the longest label, at least 40%.
        const float labelWidth = std::max(ImGui::GetContentRegionAvail().x * 0.4f,
            ImGui::CalcTextSize("Anti-aliasing (MSAA)").x + style.WindowPadding.x + style.ItemSpacing.x * 2.0f);
        ImGui::PushItemWidth(-1.0f);
        auto label = [&](const char* text, const char* help) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
            tip(help);
            ImGui::SameLine(labelWidth);
        };

        // Resolution
        static const char* const resNames[] = {
            "Native (320x240)", "Fit window", "2x (640x480)", "3x (960x720)",
            "4x (1280x960)", "5x (1600x1200)", "6x (1920x1440)", "8x (2560x1920)",
            "10x (3200x2400)" };
        int resIndex = resolution_index(s);
        label("Resolution", kTipResolution);
        if (ImGui::Combo("##res", &resIndex, resNames, IM_ARRAYSIZE(resNames))) {
            set_resolution_index(s, resIndex);
            if (s.resolution == YoshiSettings::Native) s.ssaa = 1;
        }
        tip(kTipResolution);

        const bool manual = s.resolution == YoshiSettings::Manual;
        static const char* const ssaaNames[] = { "Off", "2x", "3x", "4x", "5x" };
        int ssaaIndex = std::clamp(s.ssaa, 1, 5) - 1;
        label("Supersampling", kTipSupersampling);
        ImGui::BeginDisabled(!manual);
        if (ImGui::Combo("##ssaa", &ssaaIndex, ssaaNames, IM_ARRAYSIZE(ssaaNames))) s.ssaa = ssaaIndex + 1;
        tip(kTipSupersampling);
        ImGui::EndDisabled();

        static const char* const msaaNames[] = { "Off", "2x", "4x", "8x" };
        int msaaIndex = s.msaa >= 8 ? 3 : s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0;
        label("Anti-aliasing (MSAA)", kTipMsaa);
        if (ImGui::Combo("##msaa", &msaaIndex, msaaNames, IM_ARRAYSIZE(msaaNames))) {
            s.msaa = msaaIndex == 0 ? 0 : (1 << msaaIndex);
        }
        tip(kTipMsaa);

        static const char* const up2dNames[] = { "Original", "Scaled only", "All" };
        label("Upscale 2D", kTipUpscale2D);
        ImGui::Combo("##up2d", &s.upscale2D, up2dNames, IM_ARRAYSIZE(up2dNames));
        tip(kTipUpscale2D);

        static const char* const filterNames[] = { "Nearest", "Linear", "Pixel scaling" };
        label("Output filter", kTipFilter);
        ImGui::Combo("##filter", &s.filter, filterNames, IM_ARRAYSIZE(filterNames));
        tip(kTipFilter);

        label("CRT filter", kTipCrt);
        ImGui::SliderFloat("##crt", &s.crt, 0.0f, 1.0f, s.crt <= 0.0f ? "Off" : "%.2f");
        tip(kTipCrt);

        label("Overscan crop (px)", kTipCrop);
        ImGui::SliderInt("##crop", &s.crop, 0, 8);
        tip(kTipCrop);

        label("Window mode", kTipFullscreen);
        int mode = int(s.window_mode());
        if (ImGui::Combo("##windowmode", &mode, "Windowed\0Borderless window\0Borderless fullscreen\0")) {
            s.fullscreen = mode == int(WindowMode::BorderlessFullscreen);
            if (!s.fullscreen) s.borderlessWindow = mode == int(WindowMode::BorderlessWindow);
        }
        tip(kTipFullscreen);

        ImGui::PopItemWidth();
        ImGui::Separator();

        // What the settings mean, in one line.
        if (s.resolution == YoshiSettings::Native) {
            note("Renders at 320x240.");
        } else if (s.resolution == YoshiSettings::Window) {
            note("Renders at the largest multiple of 240 lines that fits the window.");
        } else {
            const int w = int(320 * s.multiplier * s.ssaa), h = int(240 * s.multiplier * s.ssaa);
            note("Renders at %dx%d%s.", w, h, s.ssaa > 1 ? " and scales down" : "");
        }
        note("In game: F10 cycles the CRT filter, F11 or Alt+Enter toggles fullscreen.");

        ImGui::Checkbox("Show this window at startup", &s.showLauncher);
        tip(kTipLauncher);

        const float buttonW = 140.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * buttonW - style.ItemSpacing.x - style.WindowPadding.x);
        if (ImGui::Button("Quit", ImVec2(buttonW, 0))) quit = true;
        ImGui::SameLine();
        if (firstFrame) ImGui::SetKeyboardFocusHere();   // Enter / A starts straight away
        if (ImGui::Button("Start", ImVec2(buttonW, 0))) start = true;

        ImGui::End();
        ImGui::Render();

        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
        if (firstFrame) {
            // Re-apply once the compositor has configured the window, and log
            // what we got (driver, usable bounds, requested vs actual size).
            int gotW = 0, gotH = 0;
            SDL_GetWindowSize(window, &gotW, &gotH);
            if (gotW != winW || gotH != winH) SDL_SetWindowSize(window, winW, winH);
            fprintf(stderr, "[launcher] window %dx%d requested, %dx%d after first frame (video=%s, usable %dx%d)\n",
                winW, winH, gotW, gotH, SDL_GetCurrentVideoDriver(), usable.w, usable.h);
        }
        firstFrame = false;
    }

    if (start) {
        SDL_GetWindowSize(window, &g_settings.launcherW, &g_settings.launcherH);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    if (logo) SDL_DestroyTexture(logo);
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (controller) SDL_GameControllerClose(controller);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    if (start) settings_save(settingsPath);
    return start;
}
