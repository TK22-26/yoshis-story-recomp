#include "controller_devices.h"
#include <map>

namespace input {
namespace {
std::string hex(const char* value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    if (value) for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        result += digits[*p >> 4]; result += digits[*p & 15];
    }
    return result;
}
}
Controllers::~Controllers() { activate(-1); }
SDL_JoystickID Controllers::id() const {
    return current_ ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(current_)) : -1;
}
void Controllers::activate(SDL_JoystickID wanted) {
    if (wanted == id()) return;
    SDL_GameController* next = nullptr;
    if (wanted >= 0) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_JoystickGetDeviceInstanceID(i) == wanted) {
                next = SDL_GameControllerOpen(i);
                break;
            }
        }
    }
    if (current_) {
        if (SDL_GameControllerGetAttached(current_)) SDL_GameControllerRumble(current_, 0, 0, 0);
        SDL_GameControllerClose(current_);
    }
    current_ = next;
}
void Controllers::refresh(const std::string& preference) {
    devices_.clear();
    std::map<std::string, int> guidOrdinals, nameCounts;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        auto* c = SDL_GameControllerOpen(i);
        if (!c) continue;
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof(guid));
        const int ordinal = guidOrdinals[guid]++;
        const char* serial = SDL_GameControllerGetSerial(c);
        const char* path = SDL_GameControllerPath(c);
        const int player = SDL_GameControllerGetPlayerIndex(c);
        std::string key = guid;
        if (serial && *serial) key += "/serial/" + hex(serial);
        else if (path && *path) key += "/path/" + hex(path);
        else if (player >= 0) key += "/player/" + std::to_string(player);
        else key += "/ordinal/" + std::to_string(ordinal);
        const char* name = SDL_GameControllerName(c);
        devices_.push_back({SDL_JoystickGetDeviceInstanceID(i), key, name ? name : "Controller", {}});
        ++nameCounts[devices_.back().name];
        SDL_GameControllerClose(c);
    }
    std::map<std::string, int> nameOrdinals;
    SDL_JoystickID wanted = -1;
    for (auto& device : devices_) {
        device.label = device.name;
        if (nameCounts[device.name] > 1) device.label += " (" + std::to_string(++nameOrdinals[device.name]) + ")";
        if (!preference.empty() && device.key == preference) wanted = device.id;
    }
    if (preference.empty() && !devices_.empty()) {
        wanted = devices_.front().id;
        // Automatic mode keeps the existing device when another is plugged in.
        for (const auto& d : devices_) if (d.id == id()) wanted = d.id;
    }
    activate(wanted);
}
ControllerState Controllers::state() const {
    ControllerState result;
    if (!current_) return result;
    static_assert(SDL_CONTROLLER_AXIS_MAX == axis_count);
    static_assert(SDL_CONTROLLER_BUTTON_MAX <= button_count);
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i)
        result.axes[i] = std::clamp(SDL_GameControllerGetAxis(current_, SDL_GameControllerAxis(i)) / 32767.f, -1.f, 1.f);
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
        result.buttons[i] = SDL_GameControllerGetButton(current_, SDL_GameControllerButton(i)) != 0;
    return result;
}
} // namespace input
