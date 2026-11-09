// Shared settings persistence, input defaults, and window icon.

#include "launcher.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <SDL.h>
#include "icon_data.h"

YoshiSettings g_settings;

// --- Input bindings ---------------------------------------------------------

void input_bindings_reset_defaults(InputBindings& b) {
    b.actions[int(N64Action::A)]          = { SDL_SCANCODE_SPACE,  SDL_CONTROLLER_BUTTON_A };
    b.actions[int(N64Action::B)]          = { SDL_SCANCODE_LSHIFT, SDL_CONTROLLER_BUTTON_X };
    b.actions[int(N64Action::Z)]          = { SDL_SCANCODE_Z,      -1, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1 };  // left trigger axis
    b.actions[int(N64Action::Start)]      = { SDL_SCANCODE_RETURN, SDL_CONTROLLER_BUTTON_START };
    b.actions[int(N64Action::L)]          = { SDL_SCANCODE_Q,      SDL_CONTROLLER_BUTTON_LEFTSHOULDER };
    b.actions[int(N64Action::R)]          = { SDL_SCANCODE_E,      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
    b.actions[int(N64Action::CUp)]        = { SDL_SCANCODE_I,      -1, SDL_CONTROLLER_AXIS_RIGHTY, -1 };  // right stick Y-
    b.actions[int(N64Action::CDown)]      = { SDL_SCANCODE_K,      -1, SDL_CONTROLLER_AXIS_RIGHTY, 1 };  // right stick Y+
    b.actions[int(N64Action::CLeft)]      = { SDL_SCANCODE_J,      -1, SDL_CONTROLLER_AXIS_RIGHTX, -1 };  // right stick X-
    b.actions[int(N64Action::CRight)]     = { SDL_SCANCODE_L,      -1, SDL_CONTROLLER_AXIS_RIGHTX, 1 };  // right stick X+
    b.actions[int(N64Action::DUp)]        = { SDL_SCANCODE_T,      SDL_CONTROLLER_BUTTON_DPAD_UP };
    b.actions[int(N64Action::DDown)]      = { SDL_SCANCODE_G,      SDL_CONTROLLER_BUTTON_DPAD_DOWN };
    b.actions[int(N64Action::DLeft)]      = { SDL_SCANCODE_F,      SDL_CONTROLLER_BUTTON_DPAD_LEFT };
    b.actions[int(N64Action::DRight)]     = { SDL_SCANCODE_H,      SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    b.actions[int(N64Action::StickUp)]    = { SDL_SCANCODE_W,      -1, SDL_CONTROLLER_AXIS_LEFTY, -1 };  // left stick Y- (also: Up arrow)
    b.actions[int(N64Action::StickDown)]  = { SDL_SCANCODE_S,      -1, SDL_CONTROLLER_AXIS_LEFTY, 1 };  // left stick Y+
    b.actions[int(N64Action::StickLeft)]  = { SDL_SCANCODE_A,      -1, SDL_CONTROLLER_AXIS_LEFTX, -1 };  // left stick X-
    b.actions[int(N64Action::StickRight)] = { SDL_SCANCODE_D,      -1, SDL_CONTROLLER_AXIS_LEFTX, 1 };  // left stick X+
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
        else if (key == "controller") s.controller = val;
        else if (key.rfind("bind.", 0) == 0) {
            const std::string rest = key.substr(5);
            if (rest == "deadzone") {
                s.bindings.deadzone = std::clamp(float(v), 0.0f, 0.5f);
            } else if (rest == "invert_x") {
                s.bindings.invert_x = v != 0.0;
            } else if (rest == "invert_y") {
                s.bindings.invert_y = v != 0.0;
            } else {
                input::load_binding(s.bindings, rest, int(v));
            }
        }
    }
    input::normalize_bindings(g_settings.bindings);
}

bool settings_save(const std::filesystem::path& path, const YoshiSettings* values) {
    std::ofstream out(path);
    if (!out) {
        fprintf(stderr, "[settings] could not write %s\n", path.string().c_str());
        return false;
    }
    const YoshiSettings& s = values ? *values : g_settings;
    out << "# Yoshi's Story Recomp settings.\n"
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
        << "controller = " << s.controller << "\n";
    // Input bindings
    out << "# Input bindings: SDL scancode, button, axis (-1 = unbound); axis_direction: -1 or 1.\n";
    input::save_bindings(out, s.bindings);
    out << "bind.deadzone = " << s.bindings.deadzone << "\n"
        << "bind.invert_x = " << (s.bindings.invert_x ? 1 : 0) << "\n"
        << "bind.invert_y = " << (s.bindings.invert_y ? 1 : 0) << "\n";
    out.flush();
    if (!out) fprintf(stderr, "[settings] write failed: %s\n", path.string().c_str());
    return bool(out);
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
