#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string_view>

enum class N64Action : int {
    A = 0, B, Z, Start, L, R,
    CUp, CDown, CLeft, CRight,
    DUp, DDown, DLeft, DRight,
    StickUp, StickDown, StickLeft, StickRight,
    Count
};

// SDL scancode/button/axis numbers. An axis binds one signed direction;
// -1 means unbound. Each action has exactly one controller button OR signed
// axis direction. Its keyboard binding is independent.
struct N64Binding {
    int key = -1;
    int btn = -1;
    int axis = -1;
    int axis_direction = 1;
    void bind_button(int value) { btn = value; axis = -1; axis_direction = 1; }
    void bind_axis(int value, int direction) { btn = -1; axis = value; axis_direction = direction; }
};

struct InputBindings {
    N64Binding actions[static_cast<int>(N64Action::Count)];
    float deadzone = .15f;
    bool invert_x = false, invert_y = false;
};

namespace input {
constexpr int axis_count = 6, button_count = 32;
constexpr const char* action_names[] = {
    "A", "B", "Z", "Start", "L", "R", "CUp", "CDown", "CLeft", "CRight",
    "DUp", "DDown", "DLeft", "DRight", "StickUp", "StickDown", "StickLeft", "StickRight"
};
static_assert(std::size(action_names) == int(N64Action::Count));

struct ControllerState {
    std::array<float, axis_count> axes{}; // sticks -1..1, triggers 0..1
    std::array<bool, button_count> buttons{};
};
struct Sample { uint16_t buttons = 0; float x = 0, y = 0; };

inline float strength(const N64Binding& binding, const ControllerState& state) {
    if (binding.btn >= 0 && binding.btn < button_count) return state.buttons[binding.btn] ? 1.f : 0.f;
    if (binding.axis >= 0 && binding.axis < axis_count)
        return std::clamp(state.axes[binding.axis] * binding.axis_direction, 0.f, 1.f);
    return 0;
}

inline Sample sample_controller(const InputBindings& bindings, const ControllerState& state) {
    constexpr uint16_t masks[] = {0x8000, 0x4000, 0x2000, 0x1000, 0x0020, 0x0010,
                                 0x0008, 0x0004, 0x0002, 0x0001, 0x0800, 0x0400, 0x0200, 0x0100};
    Sample result;
    for (int i = 0; i < int(N64Action::StickUp); ++i) {
        const auto& b = bindings.actions[i];
        if (strength(b, state) > (b.axis >= 4 ? .3f : .5f)) result.buttons |= masks[i];
    }
    auto value = [&](N64Action a) { return strength(bindings.actions[int(a)], state); };
    result.x = value(N64Action::StickRight) - value(N64Action::StickLeft);
    result.y = value(N64Action::StickUp) - value(N64Action::StickDown);
    if (bindings.invert_x) result.x = -result.x;
    if (bindings.invert_y) result.y = -result.y;
    const float magnitude = std::sqrt(result.x * result.x + result.y * result.y);
    const float deadzone = std::clamp(bindings.deadzone, 0.f, .5f);
    // The <= also handles a centered stick with the dead zone set to zero.
    if (magnitude <= deadzone) result.x = result.y = 0;
    else {
        const float factor = std::min(1.f, (magnitude - deadzone) / (1 - deadzone)) / magnitude;
        result.x *= factor;
        result.y *= factor;
    }
    return result;
}

// A held stick/trigger must return to neutral before capture. This prevents
// navigation input or stick drift from immediately replacing a binding.
class AxisCapture {
    std::array<bool, axis_count> armed{};
public:
    void begin(const ControllerState& state) {
        for (int i = 0; i < axis_count; ++i) armed[i] = std::abs(state.axes[i]) <= .2f;
    }
    bool motion(int axis, float value, N64Binding& binding) {
        if (axis < 0 || axis >= axis_count) return false;
        if (std::abs(value) <= .2f) armed[axis] = true;
        if (!armed[axis] || std::abs(value) < .6f) return false;
        binding.bind_axis(axis, value < 0 ? -1 : 1);
        return true;
    }
};

// Only present fields replace defaults, so pre-axis settings retain the
// original layout. Saving explicitly writes -1 for an axis replaced by a button.
inline void load_binding(InputBindings& bindings, std::string_view key, int value) {
    const auto dot = key.rfind('.');
    if (dot == key.npos) return;
    for (int i = 0; i < int(N64Action::Count); ++i) {
        if (key.substr(0, dot) != action_names[i]) continue;
        auto& b = bindings.actions[i];
        const auto field = key.substr(dot + 1);
        if (field == "key") b.key = value >= 0 && value < 512 ? value : -1;
        else if (field == "btn") b.btn = value >= 0 && value < button_count ? value : -1;
        else if (field == "axis") b.axis = value >= 0 && value < axis_count ? value : -1;
        else if (field == "axis_direction") b.axis_direction = value < 0 ? -1 : 1;
        return;
    }
}
inline void normalize_bindings(InputBindings& bindings) {
    // Older versions let R (and custom legacy mappings) have a button plus an
    // implicit axis. Preserve the explicit button when migrating those files.
    for (auto& binding : bindings.actions)
        if (binding.btn >= 0) { binding.axis = -1; binding.axis_direction = 1; }
}
inline void save_bindings(std::ostream& out, const InputBindings& bindings) {
    for (int i = 0; i < int(N64Action::Count); ++i) {
        const auto& b = bindings.actions[i];
        out << "bind." << action_names[i] << ".key = " << b.key << '\n'
            << "bind." << action_names[i] << ".btn = " << b.btn << '\n'
            << "bind." << action_names[i] << ".axis = " << b.axis << '\n'
            << "bind." << action_names[i] << ".axis_direction = " << b.axis_direction << '\n';
    }
}
} // namespace input
