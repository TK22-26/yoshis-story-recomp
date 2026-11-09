#include "platform.h"

#include <cstdio>
#include <cctype>
#include <vector>
#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <SDL_syswm.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

namespace platform {
float window_scale(SDL_Window* window) {
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) {
        const auto dpi = GetDpiForWindow(info.info.win.window);
        if (dpi) return float(dpi) / 96.0f;
    }
#else
    float dpi = 96;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), nullptr, &dpi, nullptr) == 0)
        return dpi / 96.0f;
#endif
    return 1.0f;
}

void set_dialog_owner(SDL_Window* dialog, SDL_Window* parent) {
#ifdef _WIN32
    SDL_SysWMinfo child{}, owner{};
    SDL_VERSION(&child.version);
    SDL_VERSION(&owner.version);
    if (SDL_GetWindowWMInfo(dialog, &child) && SDL_GetWindowWMInfo(parent, &owner))
        SetWindowLongPtrW(child.info.win.window, GWLP_HWNDPARENT,
            reinterpret_cast<LONG_PTR>(owner.info.win.window));
#else
    SDL_SetWindowModalFor(dialog, parent);
#endif
}

void enable_window(SDL_Window* window, bool enabled) {
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) EnableWindow(info.info.win.window, enabled);
#else
    (void)window; (void)enabled;
#endif
}

void apply_window_chrome(SDL_Window* window) {
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    const HWND handle = info.info.win.window;
    // Match the JFG launcher's native DWM frame. Windows retains caption
    // buttons, snap layouts and resizing. Unsupported attributes are harmless.
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
    const Uint32 flags = SDL_GetWindowFlags(window);
    const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    const bool frameless = fullscreen || (flags & SDL_WINDOW_BORDERLESS) != 0;
    const DWORD round = frameless ? 1 : 2; // DWMWCP_DONOTROUND / ROUND
    DwmSetWindowAttribute(handle, 33, &round, sizeof(round));
    const DWORD border = frameless ? 0xFFFFFFFEu : 0xFFFFFFFFu; // NONE / DEFAULT
    DwmSetWindowAttribute(handle, 34, &border, sizeof(border));
    const DWORD mica = frameless ? 1 : 2; // DWMSBT_NONE / MAINWINDOW
    DwmSetWindowAttribute(handle, 38, &mica, sizeof(mica));
    if (fullscreen) {
        // Match JFG's physical monitor bounds (not the desktop work area).
        // Clear the windowed DWM treatment before completing the frame change.
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor))
            SetWindowPos(handle, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left,
                monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#else
    (void)window;
#endif
}

bool open_folder(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) return false;
#ifdef _WIN32
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    const auto raw = std::filesystem::absolute(path).generic_string();
    std::string url = "file://";
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') url += c;
        else { char encoded[4]; snprintf(encoded, sizeof(encoded), "%%%02X", c); url += encoded; }
    }
    return SDL_OpenURL(url.c_str()) == 0;
#endif
}

std::filesystem::path executable_path() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size) return {};
        if (size < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), size));
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
}

void print_backtrace() {
    void* frames[64];
#ifdef _WIN32
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) fprintf(stderr, "  [%u] %p\n", i, frames[i]);
#else
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fileno(stderr));
#endif
}

bool restart() {
    const auto exe = executable_path();
    if (exe.empty()) return false;
#ifdef _WIN32
    // Pass the application path separately and quote argv[0] for paths with
    // spaces. Inherit the working directory and environment (YOSHI_DATA_DIR).
    std::wstring command = L"\"" + exe.native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &startup, &child)) {
        fprintf(stderr, "[overlay] CreateProcessW failed (error=%lu)\n", GetLastError());
        return false;
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return true;
#else
    char* args[] = {const_cast<char*>(exe.c_str()), nullptr};
    execv(exe.c_str(), args);
    return false;
#endif
}
}
