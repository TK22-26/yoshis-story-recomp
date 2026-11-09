#include "game_pause.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace {
using Clock = std::chrono::high_resolution_clock;
std::atomic<bool> paused{false};
std::mutex mutex;
std::condition_variable resumed;
Clock::time_point pause_start;
Clock::duration pause_duration{};
}

bool game_pause::is_paused() { return paused.load(std::memory_order_acquire); }

void game_pause::set_paused(bool value) {
    {
        std::lock_guard lock(mutex);
        if (value == paused.load(std::memory_order_relaxed)) return;
        const auto current = Clock::now();
        if (value) pause_start = current;
        else pause_duration += current - pause_start;
        paused.store(value, std::memory_order_release);
    }
    if (!value) resumed.notify_all();
}

void game_pause::wait_if_paused() {
    if (!is_paused()) return;
    std::unique_lock lock(mutex);
    resumed.wait(lock, [] { return !paused.load(std::memory_order_relaxed); });
}

std::chrono::high_resolution_clock::time_point game_pause::now() {
    std::lock_guard lock(mutex);
    return (paused.load(std::memory_order_relaxed) ? pause_start : Clock::now()) - pause_duration;
}
