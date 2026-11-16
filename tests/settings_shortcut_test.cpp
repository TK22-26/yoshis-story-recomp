#include "settings_shortcut.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    input::SettingsShortcut shortcut;
    require(!shortcut.button(true, true) && !shortcut.blocks_gameplay(), "Start alone belongs to the game");
    require(!shortcut.button(true, false), "Releasing Start does not open settings");
    for (bool start_first : {false, true}) {
        require(!shortcut.button(start_first, true), "First button does not toggle");
        require(shortcut.button(!start_first, true), "Either button order opens settings");
        require(shortcut.blocks_gameplay(), "Opening chord is suppressed in game");
        require(!shortcut.button(!start_first, true), "Repeated down does not toggle");
        require(!shortcut.button(start_first, false), "First release does not toggle");
        require(shortcut.blocks_gameplay(), "Chord remains suppressed until both buttons are released");
        require(!shortcut.button(start_first, true), "Re-press with the other button held does not toggle");
        require(!shortcut.button(start_first, false), "Re-release does not toggle");
        require(!shortcut.button(!start_first, false) && !shortcut.blocks_gameplay(), "Release rearms shortcut");
    }
    require(!shortcut.button(false, true), "Back waits for a possible chord");
    require(shortcut.button(false, false), "Back tap preserves the original shortcut");
    require(!shortcut.button(false, true, false), "Back cancels binding capture without toggling");
    require(!shortcut.button(false, false), "Capture cancellation release does not close settings");
    require(!shortcut.button(true, true, false), "Start can be rebound without opening settings");
    require(!shortcut.button(true, false), "Rebound Start release does not toggle");
    shortcut.button(false, true);
    shortcut.reset();
    require(!shortcut.blocks_gameplay(), "Unplugging clears held buttons");
    std::cout << "Settings shortcut button order, repeat, release, capture and reset passed.\n";
}
