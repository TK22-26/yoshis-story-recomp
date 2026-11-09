#pragma once

#include <chrono>

// Cooperative guest pause. Host rendering and event processing keep running.
namespace game_pause {
bool is_paused();
void set_paused(bool paused);
void wait_if_paused();
// Guest timers exclude time spent paused; presentation uses the host clock.
std::chrono::high_resolution_clock::time_point now();
}
