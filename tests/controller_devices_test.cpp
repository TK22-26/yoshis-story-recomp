#include "controller_devices.h"
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << ": " << SDL_GetError() << '\n'; std::exit(1); }
}
static int attach(const char* name) {
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask = (1 << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1 << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.name = name;
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    require(index >= 0, "Attach virtual gamepad");
    return index;
}
static std::string key_for(const input::Controllers& controllers, SDL_JoystickID id) {
    for (const auto& d : controllers.devices()) if (d.id == id) return d.key;
    require(false, "Virtual gamepad appears in selector");
    return {};
}
int main() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "Initialize SDL controller subsystem");
    {
        const int first = attach("Yoshi virtual controller A"), second = attach("Yoshi virtual controller B");
        const auto firstId = SDL_JoystickGetDeviceInstanceID(first), secondId = SDL_JoystickGetDeviceInstanceID(second);
        input::Controllers controllers;
        controllers.refresh("");
        const auto firstKey = key_for(controllers, firstId), secondKey = key_for(controllers, secondId);
        require(firstKey != secondKey, "Device preferences distinguish gamepads");
        controllers.refresh(firstKey);
        require(controllers.id() == firstId, "Explicitly select first gamepad");
        auto* firstJoystick = SDL_JoystickOpen(first);
        auto* secondJoystick = SDL_JoystickOpen(second);
        SDL_JoystickSetVirtualButton(firstJoystick, SDL_CONTROLLER_BUTTON_A, 1);
        SDL_JoystickSetVirtualButton(secondJoystick, SDL_CONTROLLER_BUTTON_B, 1);
        SDL_JoystickUpdate();
        require(controllers.state().buttons[SDL_CONTROLLER_BUTTON_A] &&
                !controllers.state().buttons[SDL_CONTROLLER_BUTTON_B], "Only selected gamepad supplies input");
        controllers.refresh(secondKey);
        require(controllers.id() == secondId && !controllers.state().buttons[SDL_CONTROLLER_BUTTON_A] &&
                controllers.state().buttons[SDL_CONTROLLER_BUTTON_B], "Switch gamepad and discard previous button state");

        // Actual SDL axis events feed the same neutral-gated capture and
        // controller-state evaluator used by the game and settings dialog.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        input::AxisCapture capture;
        capture.begin(controllers.state());
        N64Binding binding;
        SDL_JoystickSetVirtualAxis(secondJoystick, SDL_CONTROLLER_AXIS_RIGHTY, -28000);
        SDL_JoystickUpdate();
        bool captured = false;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_CONTROLLERAXISMOTION && event.caxis.which == controllers.id())
                captured |= capture.motion(event.caxis.axis, event.caxis.value / 32767.f, binding);
        }
        require(captured && binding.axis == SDL_CONTROLLER_AXIS_RIGHTY && binding.axis_direction == -1,
                "Capture remapping from an actual SDL controller-axis event");
        InputBindings bindings;
        bindings.actions[int(N64Action::StickUp)] = binding;
        require(input::sample_controller(bindings, controllers.state()).y > .8f,
                "Remapped virtual stick produces analog game input");

        SDL_JoystickClose(secondJoystick);
        require(SDL_JoystickDetachVirtual(second) == 0, "Unplug selected controller");
        controllers.refresh(secondKey);
        require(!controllers.get(), "Unplugged preference waits instead of switching to another person's gamepad");
        const int reconnected = attach("Yoshi virtual controller B");
        controllers.refresh(secondKey);
        require(controllers.id() == SDL_JoystickGetDeviceInstanceID(reconnected), "Remember preference when controller reconnects");
        controllers.refresh("");
        require(controllers.get(), "Automatic mode chooses an available controller");
        SDL_JoystickClose(firstJoystick);
        SDL_JoystickDetachVirtual(reconnected);
        SDL_JoystickDetachVirtual(first);
        controllers.refresh("missing-device");
        require(!controllers.get(), "Missing saved device keeps keyboard-only input available");
    }
    SDL_Quit();
    std::cout << "SDL controller selection, axis capture, switching and reconnect passed.\n";
}
