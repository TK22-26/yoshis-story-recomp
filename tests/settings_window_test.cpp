// Exercise both the in-game overlay and desktop dialogs without a ROM or GPU.
// The host below supplies audio/window hooks; SDL and ImGui are the real ones.
#include "../src/overlay.cpp"
#include <iostream>

SDL_Window* g_sdl_window = nullptr;
std::filesystem::path g_settings_path;
static input::Controllers controllers;
static int quit_requests = 0;
static SDL_Renderer* game_renderer = nullptr;
static ImGuiContext* active_context() { return g_dialog_context ? g_dialog_context : g_game_context; }
static bool settings_open() { return g_visible.load() && (g_ingame_visible.load() || g_dialog_window); }
void apply_display_settings(RT64::Application*, bool) {}
void request_window_mode(WindowMode mode) {
    g_settings.fullscreen = mode == WindowMode::BorderlessFullscreen;
    g_settings.borderlessWindow = mode == WindowMode::BorderlessWindow;
}
void request_fullscreen(bool value) { g_settings.fullscreen = value; }
void audio_set_paused(bool) {}
void audio_set_volume(float value) { g_settings.volume = value; }
float audio_get_volume() { return g_settings.volume; }
void audio_set_muted(bool value) { g_settings.muted = value; }
bool audio_get_muted() { return g_settings.muted; }
bool ensure_rom(const std::filesystem::path&, SDL_Window*, bool) { return false; }
SDL_JoystickID input_current_controller_id() { return controllers.id(); }
input::ControllerState input_current_controller_state() { return controllers.state(); }
const std::vector<input::ControllerDevice>& input_controllers() { return controllers.devices(); }
const char* input_current_controller_name() {
    return controllers.get() ? SDL_GameControllerName(controllers.get()) : nullptr;
}
SDL_GameControllerType input_current_controller_type() {
    return controllers.get() ? SDL_GameControllerGetType(controllers.get()) : SDL_CONTROLLER_TYPE_UNKNOWN;
}
void input_select_controller(const std::string& key) { g_settings.controller = key; controllers.refresh(key); }

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << ": " << SDL_GetError() << '\n'; std::exit(1); }
}
static void frame(int count = 3) {
    for (int i = 0; i < count; ++i) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED)
                controllers.refresh(g_settings.controller);
            overlay::route_event(&event);
        }
        g_dialog_last_frame = SDL_GetTicks64() - 17;
        // Observe the real queued exit action without terminating this test.
        if (g_menu_action.load() == 2) { ++quit_requests; g_menu_action.store(0); }
        overlay::process_actions();
        // Use the same UI preparation/drawing as RT64, with a software backend
        // on the existing game window so this regression test needs no GPU.
        {
            ScopedContext context(g_game_context);
            prepare_game_ui();
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            draw_game_ui();
            ImGui::Render();
            SDL_SetRenderDrawColor(game_renderer, 35, 50, 70, 255);
            SDL_RenderClear(game_renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
            SDL_RenderPresent(game_renderer);
        }
        SDL_Delay(17);
    }
}
static void button(SDL_Joystick* pad, SDL_GameControllerButton button, bool down) {
    require(SDL_JoystickSetVirtualButton(pad, button, down) == 0, "Set virtual button");
    SDL_JoystickUpdate();
    frame();
}
static void tap(SDL_Joystick* pad, SDL_GameControllerButton value) {
    button(pad, value, true);
    button(pad, value, false);
}
static ImGuiID focused() {
    ScopedContext context(active_context());
    return ImGui::GetCurrentContext()->NavId;
}
static void screenshot(const char* name) {
    auto* renderer = g_dialog_window ? g_dialog_renderer : game_renderer;
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    auto* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, "Create screenshot");
    require(SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) == 0,
            "Read settings screenshot");
    require(SDL_SaveBMP(surface, name) == 0, "Save settings screenshot");
    SDL_FreeSurface(surface);
}

int main() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, "Initialize SDL");
    g_sdl_window = SDL_CreateWindow("Settings test", 0, 0, 1040, 800, SDL_WINDOW_HIDDEN);
    require(g_sdl_window != nullptr, "Create host window");
    g_settings_path = std::filesystem::current_path() / "settings-window-test.ini";
    input_bindings_reset_defaults(g_settings.bindings);
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask = (1 << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1 << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.name = "Settings test controller";
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    require(index >= 0, "Attach virtual controller");
    auto* pad = SDL_JoystickOpen(index);
    controllers.refresh("");
    for (const auto& device : controllers.devices()) {
        if (device.id == SDL_JoystickInstanceID(pad)) { input_select_controller(device.key); break; }
    }
    require(controllers.id() == SDL_JoystickInstanceID(pad), "Select the virtual controller");
    RT64::ApplicationConfiguration config{};
    config.detectDataPath = false;
    RT64::Application app(RT64::Application::Core{}, config);
    app.presentQueue = std::make_unique<RT64::PresentQueue>();
    g_app = &app;
    g_attached = true;
    g_game_context = ImGui::CreateContext();
    game_renderer = SDL_CreateRenderer(g_sdl_window, -1, SDL_RENDERER_SOFTWARE);
    require(game_renderer != nullptr, "Create game test renderer");
    ImGui_ImplSDL2_InitForSDLRenderer(g_sdl_window, game_renderer);
    ImGui_ImplSDLRenderer2_Init(game_renderer);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasGamepad;
    ImGui::GetIO().IniFilename = nullptr;
    initialize_game_ui();
    frame();

    tap(pad, SDL_CONTROLLER_BUTTON_START);
    require(!settings_open(), "Start alone must not open settings");
    button(pad, SDL_CONTROLLER_BUTTON_START, true);
    button(pad, SDL_CONTROLLER_BUTTON_BACK, true);
    require(settings_open() && game_pause::is_paused(), "Start + Back opens settings and pauses the game");
    require(g_ingame_visible.load() && !g_dialog_window && !g_dialog_context,
            "Start + Back must use the existing game window and ImGui context, without creating a dialog");
    require(SDL_GetWindowFromID(SDL_GetWindowID(g_sdl_window) + 1) == nullptr,
            "Opening the controller overlay must not create another SDL window");
    button(pad, SDL_CONTROLLER_BUTTON_START, false);
    button(pad, SDL_CONTROLLER_BUTTON_BACK, false);
    SDL_SetWindowSize(g_sdl_window, 1040, 800);
    frame();
    require(focused() != 0, "First Video control receives focus automatically");
    const auto initialFocus = focused();
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    require(focused() != initialFocus, "D-pad moves between settings");
    screenshot("settings-video.bmp");

    // Reach resolution from the window-mode row and open its dropdown.
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    {
        ScopedContext context(active_context());
        require(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                "A opens the focused resolution dropdown");
    }
    tap(pad, SDL_CONTROLLER_BUTTON_B);
    require(settings_open(), "B dismisses dropdown without closing settings");
    {
        ScopedContext context(active_context());
        require(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
                "B dismissed dropdown");
    }

    tap(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    require(g_active == int(Page::Audio), "RB changes to Audio");
    int w, h;
    SDL_GetWindowSize(g_sdl_window, &w, &h);
    require(w == 1040 && h == 800, "Tabs keep a stable window size");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(g_settings.volume < 1.f, "Controller changes volume");
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    {
        ScopedContext context(active_context());
        require(ImGui::IsAnyItemActive(), "A starts slider editing");
    }
    const float volume = g_settings.volume;
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    require(g_settings.volume < volume, "D-pad adjusts active volume slider");
    tap(pad, SDL_CONTROLLER_BUTTON_B);
    require(settings_open(), "B finishes slider editing without closing settings");
    screenshot("settings-audio.bmp");

    tap(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    require(g_active == int(Page::Controls) && focused(), "RB opens Controls with focus");
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(g_capture_target == CaptureTarget::Controller, "A starts controller rebinding");
    tap(pad, SDL_CONTROLLER_BUTTON_BACK);
    require(g_capture_target == CaptureTarget::None && settings_open(), "Back cancels rebinding without closing");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    tap(pad, SDL_CONTROLLER_BUTTON_X);
    require(g_settings.bindings.actions[0].btn == SDL_CONTROLLER_BUTTON_X, "Controller binding changes");
    screenshot("settings-controls.bmp");
    for (int i = 0; i < 17; ++i) tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    {
        ScopedContext context(active_context());
        auto* window = ImGui::GetCurrentContext()->NavWindow;
        require(window && window->Scroll.y > 0, "Controller navigation scrolls the full binding list");
        require(ImGui::FindWindowByName("##native-settings")->Scroll.y == 0, "Header stays fixed during navigation");
    }
    SDL_SetWindowSize(g_sdl_window, 760, 560);
    frame();
    screenshot("settings-small.bmp");
    SDL_SetWindowSize(g_sdl_window, 1040, 800);
    frame();
    tap(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    require(g_active == int(Page::About), "RB opens About");
    screenshot("settings-about.bmp");
    for (int i = 0; i < 4; ++i) tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    {
        ScopedContext context(active_context());
        auto* root = ImGui::FindWindowByName("##native-settings");
        const auto credits = ImHashStr("##button", 0, ImHashStr("##about-details", 0, root->ID));
        require(focused() == credits, "Credits button is reachable without opening an external link");
    }
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(g_about_details, "A opens credits");
    for (int i = 0; i < 4; ++i) tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    {
        ScopedContext context(active_context());
        require(ImGui::GetCurrentContext()->NavWindow->Scroll.y > 0, "Controller scrolls the credits page");
    }
    tap(pad, SDL_CONTROLLER_BUTTON_B);
    require(!g_about_details && settings_open(), "B returns from credits to About");
    for (int i = 0; i < 4; ++i) tap(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    {
        ScopedContext context(active_context());
        auto* root = ImGui::FindWindowByName("##native-settings");
        const auto quit = ImHashStr("##button", 0, ImHashStr("##quit", 0, root->ID));
        require(focused() == quit, "Quit is reachable beside Return to game");
    }
    button(pad, SDL_CONTROLLER_BUTTON_A, true);
    frame(8);
    require(g_quit_confirmation && quit_requests == 0, "Holding A to open Quit cannot confirm it");
    screenshot("settings-quit-confirmation.bmp");
    button(pad, SDL_CONTROLLER_BUTTON_A, false);
    tap(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    require(g_active == int(Page::About) && g_quit_confirmation, "Confirmation blocks tab changes");
    tap(pad, SDL_CONTROLLER_BUTTON_B);
    require(!g_quit_confirmation && settings_open() && quit_requests == 0, "B cancels Quit and keeps settings open");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(g_quit_confirmation, "Quit can be reopened after cancellation");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(!g_quit_confirmation && quit_requests == 0, "Cancel is the default confirmation selection");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    tap(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    require(!g_quit_confirmation && quit_requests == 1, "Only selecting Quit game queues an exit");
    tap(pad, SDL_CONTROLLER_BUTTON_A);
    SDL_Event cancelQuit{};
    cancelQuit.type = SDL_KEYDOWN;
    cancelQuit.key.windowID = SDL_GetWindowID(g_sdl_window);
    cancelQuit.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    cancelQuit.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&cancelQuit);
    frame();
    require(!g_quit_confirmation && settings_open() && quit_requests == 1, "Escape cancels Quit without closing settings");
    screenshot("settings-about.bmp");
    tap(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    require(g_active == int(Page::Display), "RB wraps to Video");
    tap(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    require(g_active == int(Page::About), "LB wraps to About");

    // Closing with the reverse chord must not immediately reopen or leak Start.
    button(pad, SDL_CONTROLLER_BUTTON_BACK, true);
    button(pad, SDL_CONTROLLER_BUTTON_START, true);
    require(!settings_open() && overlay::is_visible(), "Closing chord suppresses held gameplay inputs");
    button(pad, SDL_CONTROLLER_BUTTON_BACK, false);
    button(pad, SDL_CONTROLLER_BUTTON_START, false);
    require(!overlay::is_visible() && !game_pause::is_paused(), "Releasing closing chord resumes input");
    const auto savedVolume = g_settings.volume;
    g_settings = YoshiSettings{};
    settings_load(g_settings_path);
    require(std::fabs(g_settings.volume - savedVolume) < .001f &&
            g_settings.bindings.actions[0].btn == SDL_CONTROLLER_BUTTON_X, "Volume and rebinding persist");

    g_manual_pause = true;
    tap(pad, SDL_CONTROLLER_BUTTON_BACK);
    require(settings_open(), "Back tap still opens unified settings");
    const auto beforeStick = focused();
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 30000);
    SDL_JoystickUpdate();
    frame();
    require(focused() != beforeStick, "Left stick navigates settings");
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0);
    SDL_JoystickUpdate();
    frame();
    button(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 30000);
    SDL_JoystickUpdate();
    frame();
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0);
    SDL_JoystickUpdate();
    frame();
    {
        ScopedContext context(active_context());
        require(ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "Releasing stick preserves held D-pad input");
    }
    button(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
    tap(pad, SDL_CONTROLLER_BUTTON_B);
    require(!settings_open() && game_pause::is_paused(), "Closing settings preserves a manual pause");
    tap(pad, SDL_CONTROLLER_BUTTON_BACK);
    button(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
    button(pad, SDL_CONTROLLER_BUTTON_BACK, true);
    SDL_JoystickClose(pad);
    SDL_JoystickDetachVirtual(index);
    frame();
    require(settings_open() && !g_shortcut_blocks_input.load(), "Disconnect clears a held settings chord");
    {
        ScopedContext context(active_context());
        require(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown), "Disconnect releases navigation buttons");
    }
    SDL_Event escape{};
    escape.type = SDL_KEYDOWN;
    escape.key.windowID = SDL_GetWindowID(g_sdl_window);
    escape.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&escape);
    frame();
    require(!settings_open(), "Keyboard can close settings after controller disconnect");
    // These are the exact requests made by the desktop menu's entries. They
    // must keep opening owned dialogs rather than the controller overlay.
    for (int page = 0; page < int(Page::Count); ++page) {
        open_page(Page(page));
        frame();
        require(g_dialog_window && g_dialog_context != g_game_context && !g_ingame_visible.load(),
                "Desktop menu keeps its separate settings window");
        require(SDL_GetWindowID(g_dialog_window) != SDL_GetWindowID(g_sdl_window) && g_active == page,
                "Desktop dialog opens the requested page");
        if (page == int(Page::About)) screenshot("settings-desktop.bmp");
        escape.key.windowID = SDL_GetWindowID(g_dialog_window);
        SDL_PushEvent(&escape);
        frame();
        require(!settings_open() && !g_dialog_window, "Escape closes desktop dialog");
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(g_game_context);
    g_game_context = nullptr;
    g_app = nullptr;
    SDL_DestroyRenderer(game_renderer);
    SDL_DestroyWindow(g_sdl_window);
    std::filesystem::remove(g_settings_path);
    std::cout << "In-game overlay, separate desktop dialogs, controller navigation, editing, persistence and pause passed.\n";
}
