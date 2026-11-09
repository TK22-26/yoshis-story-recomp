#pragma once
#include <filesystem>
struct SDL_Window;

// Reuse a verified local ROM, otherwise ask the player to select and import one.
// A forced selection is used by Game > Select ROM. Cancel preserves existing data.
bool ensure_rom(const std::filesystem::path& data_directory, SDL_Window* owner = nullptr,
                bool force_selection = false);
