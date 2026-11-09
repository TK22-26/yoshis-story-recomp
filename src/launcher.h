#pragma once

#include <filesystem>
#include <SDL.h>

#include "input_bindings.h"
#include "controller_devices.h"

// Populate b with the default bindings (WASD + controller Xbox layout).
void input_bindings_reset_defaults(InputBindings& b);

enum class WindowMode { Windowed, BorderlessWindow, BorderlessFullscreen };

// Display settings, saved to data/settings.ini. The launcher window edits
// them before the game starts; YOSHI_* environment variables override them
// for one run without being saved.
struct YoshiSettings {
    enum Resolution { Native = 0, Window = 1, Manual = 2 };
    int resolution = Window;    // Native 320x240, fit the window, or Manual
    double multiplier = 4.0;    // Manual: N x 240p
    int ssaa = 1;               // supersampling factor (Manual only)
    int msaa = 0;               // 0, 2, 4 or 8
    int upscale2D = 1;          // RT64 Upscale2D: 0 original, 1 scaled only, 2 all
    int filter = 2;             // RT64 Filtering: 0 nearest, 1 linear, 2 pixel scaling
    float crt = 0.0f;           // CRT filter strength 0..1
    int crop = 2;               // framebuffer pixels hidden per edge
    bool fullscreen = false;
    bool borderlessWindow = false; // Window style to restore after leaving fullscreen.
    float volume = 1.0f;    // master volume 0..1, applied per-sample before SDL_QueueAudio
    bool muted = false;     // silences output without losing the volume level
    bool showLauncher = true;
    int launcherW = 0, launcherH = 0;   // launcher window size last time (0 = default)
    InputBindings bindings;             // keyboard, controller button and signed axis mappings
    std::string controller;             // stable device key; empty = automatic

    WindowMode window_mode() const {
        return fullscreen ? WindowMode::BorderlessFullscreen :
            borderlessWindow ? WindowMode::BorderlessWindow : WindowMode::Windowed;
    }
};

extern YoshiSettings g_settings;
// Path to data/settings.ini; set during main() init, used by the in-game
// overlay to persist changes.
extern std::filesystem::path g_settings_path;

void settings_load(const std::filesystem::path& path);
bool settings_save(const std::filesystem::path& path, const YoshiSettings* values = nullptr);
// Push settings into RT64. MSAA is applied only at initialization; changing it
// while rendering can invalidate resources still in use.
// CRT, crop and fullscreen have their own apply paths; see overlay.cpp.
namespace RT64 { struct Application; }
void apply_display_settings(RT64::Application* app, bool initialize = false);
// Apply YOSHI_RES / YOSHI_SSAA / YOSHI_MSAA / YOSHI_UPSCALE2D / YOSHI_FILTER /
// YOSHI_CRT / YOSHI_CROP / YOSHI_FULLSCREEN over g_settings.
void settings_apply_env();

// The game's Vulkan window, created in main(). Used by the overlay for its
// fullscreen toggle. Null before window creation and after shutdown.
extern SDL_Window* g_sdl_window;
// Queue window changes for SDL's main thread; safe from the renderer UI.
void request_fullscreen(bool fullscreen);
void request_window_mode(WindowMode mode);

// Mute / unmute the SDL audio device (hardware pause). Separate from the
// software volume scale; used if needed for a true hardware silence.
void audio_set_paused(bool paused);
// Software master volume (0..1). Applied per-sample in audio_queue_samples().
// Mirrors g_settings.volume so either path writes through to the other.
void audio_set_volume(float volume);
float audio_get_volume();
void audio_set_muted(bool muted);
bool audio_get_muted();
// The app icon (src/icon_data.h, from tools/gen_icon.py) as a new surface.
SDL_Surface* icon_surface();
void set_window_icon(SDL_Window* window);

// Name of the currently connected game controller, or nullptr if none.
const char* input_current_controller_name();
SDL_GameControllerType input_current_controller_type();
input::ControllerState input_current_controller_state();
SDL_JoystickID input_current_controller_id();
const std::vector<input::ControllerDevice>& input_controllers();
void input_select_controller(const std::string& key);

// Show the settings window. Saves on Start. Returns false if the user quit.
bool run_launcher(const std::filesystem::path& settingsPath);
