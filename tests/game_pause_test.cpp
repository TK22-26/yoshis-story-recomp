#include "game_pause.h"
#include <atomic>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;
int main() {
    game_pause::set_paused(true);
    const auto frozen = game_pause::now();
    std::atomic<bool> entered{false}, finished{false};
    std::thread guest([&] {
        entered = true;
        game_pause::wait_if_paused();
        finished = true;
    });
    while (!entered) std::this_thread::yield();
    std::this_thread::sleep_for(60ms);
    game_pause::set_paused(true); // Repeated requests must not move the clock.
    const bool blocked = !finished && game_pause::now() == frozen;
    game_pause::set_paused(false);
    guest.join();
    const auto resumed = game_pause::now();
    std::this_thread::sleep_for(20ms);
    const bool advancing = game_pause::now() > resumed;
    // The guest clock must not jump forward by the 60 ms pause on resume.
    const bool continuous = resumed - frozen < 40ms;
    if (!blocked || !finished || !advancing || !continuous) {
        std::fprintf(stderr, "Pause failed: blocked=%d resumed=%d advancing=%d continuous=%d\n",
            blocked, bool(finished), advancing, continuous);
        return 1;
    }
    std::puts("Guest execution and clock pause/resume passed.");
}
