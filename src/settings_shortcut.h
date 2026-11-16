#pragma once

namespace input {
// Start remains a game button. Back is deferred until release so pressing the
// two buttons in either order toggles settings exactly once. A Back tap keeps
// the original shortcut. After a chord, both buttons must be released.
class SettingsShortcut {
    bool start_ = false, back_ = false, used_ = false;
public:
    bool button(bool start_button, bool down, bool enabled = true) {
        bool& held = start_button ? start_ : back_;
        if (held == down) return false;
        held = down;
        bool toggle = false;
        if (!enabled) used_ = true;
        else if (!used_ && start_ && back_) toggle = used_ = true;
        else if (!used_ && !start_button && !down) toggle = true;
        if (!start_ && !back_) used_ = false;
        return toggle;
    }
    bool blocks_gameplay() const { return back_ || used_; }
    void reset() { start_ = back_ = used_ = false; }
};
} // namespace input
