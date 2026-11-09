#include "input_bindings.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static bool near(float a, float b) { return std::abs(a - b) < .0001f; }

int main() {
    InputBindings b;
    input::ControllerState pad;
    auto& z = b.actions[int(N64Action::Z)];
    z = {29, -1, 4, 1};
    pad.axes[4] = .4f;
    require(input::sample_controller(b, pad).buttons == 0x2000, "Trigger activates Z");
    z.bind_button(1);
    require(input::sample_controller(b, pad).buttons == 0, "Rebinding removes the old hardcoded trigger");
    pad.buttons[1] = true;
    require(input::sample_controller(b, pad).buttons == 0x2000, "Z accepts a button binding");
    require(z.key == 29, "Controller rebinding preserves the keyboard key");

    // Migrate the old R=RB+RT mapping to a single input. RT must not continue
    // activating R after loading an older settings file.
    auto& r = b.actions[int(N64Action::R)];
    r = {8, 10, 5, 1};
    input::normalize_bindings(b);
    pad = {}; pad.axes[5] = 1;
    require(r.btn == 10 && r.axis == -1 && input::sample_controller(b, pad).buttons == 0,
            "Legacy dual binding migrates to one controller input");
    pad.buttons[10] = true;
    require(input::sample_controller(b, pad).buttons == 0x0010, "Migrated R uses RB only");
    r.bind_axis(5, 1);
    pad.axes[5] = 0;
    require(input::sample_controller(b, pad).buttons == 0, "Choosing RT removes RB entirely");
    pad.axes[5] = 1;
    require(input::sample_controller(b, pad).buttons == 0x0010, "R now uses RT only");

    pad = {};
    auto& cu = b.actions[int(N64Action::CUp)];
    cu.bind_axis(0, -1);
    pad.axes[0] = -.7f;
    require(input::sample_controller(b, pad).buttons == 0x0008, "C button accepts a different stick/direction");
    pad.axes[0] = .7f;
    require(input::sample_controller(b, pad).buttons == 0, "Opposite direction does not activate the binding");
    cu.bind_button(2);
    pad.buttons[2] = true;
    require(input::sample_controller(b, pad).buttons == 0x0008, "C button accepts a digital button");

    b = {};
    b.actions[int(N64Action::StickRight)].bind_axis(2, 1);
    b.actions[int(N64Action::StickLeft)].bind_axis(2, -1);
    b.actions[int(N64Action::StickUp)].bind_axis(3, -1);
    b.actions[int(N64Action::StickDown)].bind_axis(3, 1);
    pad = {};
    pad.axes[0] = 1;
    require(near(input::sample_controller(b, pad).x, 0), "Old left stick is no longer active after remapping");
    pad.axes[2] = .575f;
    require(near(input::sample_controller(b, pad).x, .5f), "Remapped stick preserves proportional input and dead zone");
    b.invert_x = true;
    require(near(input::sample_controller(b, pad).x, -.5f), "Inversion applies to the remapped axis");
    pad.axes[2] = .1f;
    require(near(input::sample_controller(b, pad).x, 0), "Small movements stay inside the dead zone");
    b.deadzone = 0;
    pad = {};
    const auto center = input::sample_controller(b, pad);
    require(center.x == 0 && center.y == 0, "Zero dead zone at center stays finite");
    pad.axes[2] = 1; pad.axes[3] = -1;
    const auto diagonal = input::sample_controller(b, pad);
    require(near(std::hypot(diagonal.x, diagonal.y), 1), "Diagonal stick stays within unit radius");
    b.actions[int(N64Action::StickUp)].bind_button(0);
    pad = {}; pad.buttons[0] = true;
    require(near(input::sample_controller(b, pad).y, 1), "Analog action accepts a controller button");

    input::AxisCapture capture;
    pad = {}; pad.axes[4] = 1;
    capture.begin(pad);
    N64Binding captured{44, 2};
    require(!capture.motion(4, 1, captured), "Held trigger cannot immediately bind");
    require(!capture.motion(0, .12f, captured), "Stick drift cannot bind");
    require(!capture.motion(4, 0, captured), "Releasing a trigger arms capture");
    require(capture.motion(4, .9f, captured), "Fresh trigger movement binds");
    require(captured.axis == 4 && captured.axis_direction == 1 && captured.btn == -1 && captured.key == 44,
            "Axis capture replaces only the controller binding");
    capture.begin({});
    require(capture.motion(3, -.8f, captured) && captured.axis_direction == -1, "Capture records negative direction");
    require(!capture.motion(99, 1, captured), "Invalid axis event is ignored");

    // Round-trip the same binding serializer/parser used by settings.ini.
    b.actions[int(N64Action::CLeft)] = captured;
    std::stringstream file;
    input::save_bindings(file, b);
    InputBindings loaded;
    loaded.actions[int(N64Action::StickUp)].axis = 1; // old default must be explicitly cleared
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find(" = ");
        input::load_binding(loaded, std::string_view(line).substr(5, eq - 5), std::stoi(line.substr(eq + 3)));
    }
    for (int i = 0; i < int(N64Action::Count); ++i) {
        const auto& a = b.actions[i]; const auto& c = loaded.actions[i];
        require(a.key == c.key && a.btn == c.btn && a.axis == c.axis && a.axis_direction == c.axis_direction,
                "Bindings survive saving and loading, including explicitly unbound axes");
    }
    loaded.actions[int(N64Action::Z)] = {29, -1, 4, 1};
    input::load_binding(loaded, "Z.key", 30);
    input::load_binding(loaded, "Z.btn", -1);
    require(loaded.actions[int(N64Action::Z)].axis == 4, "Legacy settings preserve default axis mappings");
    input::load_binding(loaded, "Z.axis", 999);
    require(loaded.actions[int(N64Action::Z)].axis == -1, "Invalid saved axes cannot index past the state");
    std::cout << "Controller rebinding, capture, analog input and persistence passed.\n";
}
