// Desktop menu dialogs and a controller settings overlay in the game window.
// F1 shows/hides the menu; Esc / Start + Back opens settings; Pause pauses.

#pragma once

union SDL_Event;
namespace RT64 { struct Application; }

namespace overlay {

// Install our RT64::Inspector on the application's PresentQueue. Must be
// called once after RT64::Application::setup() returns Success.
void attach(RT64::Application& app);

// Remove the Inspector before the application tears down.
void detach(RT64::Application& app);

// Drive the Inspector's ImGui frame on SDL's main thread, via process_actions.
void draw(RT64::Application& app);

// Returns true if the event was consumed and should not reach the game.
// F1 toggles the menu; Esc / Start + Back opens settings (Back tap also works). Routes events to
// the Inspector's ImGui backend when visible.
bool handle_event(SDL_Event* event);
// Dispatch restart, exit and folder-opening requests on SDL's main thread.
void process_actions();

void toggle();
bool is_visible();

} // namespace overlay
