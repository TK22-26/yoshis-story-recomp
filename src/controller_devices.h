#pragma once

#include "input_bindings.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace input {
struct ControllerDevice {
    SDL_JoystickID id;
    std::string key, name, label;
};

// SDL device indices change on removal; instance IDs select the live device.
// A stable key (GUID + serial/path/slot) remembers the choice across runs.
class Controllers {
    SDL_GameController* current_ = nullptr;
    std::vector<ControllerDevice> devices_;
    void activate(SDL_JoystickID id);
public:
    ~Controllers();
    void refresh(const std::string& preference);
    SDL_GameController* get() const { return current_; }
    SDL_JoystickID id() const;
    const std::vector<ControllerDevice>& devices() const { return devices_; }
    ControllerState state() const;
};
} // namespace input
