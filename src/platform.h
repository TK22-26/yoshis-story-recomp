#pragma once

#include <filesystem>
struct SDL_Window;

namespace platform {
std::filesystem::path executable_path();
void print_backtrace();
bool restart();
void apply_window_chrome(SDL_Window* window);
float window_scale(SDL_Window* window);
void set_dialog_owner(SDL_Window* dialog, SDL_Window* parent);
void enable_window(SDL_Window* window, bool enabled);
bool open_folder(const std::filesystem::path& path);
}
