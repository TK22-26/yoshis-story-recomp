#include "platform.h"

#include <cstdio>
#include <cctype>
#include <vector>
#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <io.h>
#include <SDL_syswm.h>
#else
#include <execinfo.h>
#include <unistd.h>
#include "gui/rt64_file_dialog.h"
#endif

namespace platform {
void start_log(const std::filesystem::path& directory) {
#ifdef _WIN32
    // Explorer launches a GUI executable without a console. Explicit output
    // redirection (including --build-info) remains available to tools.
    if (GetFileType(GetStdHandle(STD_ERROR_HANDLE)) != FILE_TYPE_UNKNOWN) return;
    FILE* stream = nullptr;
    const auto path = directory / "runtime.log";
    _wfreopen_s(&stream, path.c_str(), L"w", stderr);
    if (stream) {
        // Opening the same file twice through freopen can fail Windows sharing
        // checks. Establish stdout's descriptor first, then share stderr's file.
        FILE* output = nullptr;
        _wfreopen_s(&output, L"NUL", L"w", stdout);
        if (output) {
            _dup2(_fileno(stderr), _fileno(stdout));
            setvbuf(stdout, nullptr, _IONBF, 0);
        }
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
#else
    (void)directory;
#endif
}

#ifdef _WIN32
namespace {
LRESULT CALLBACK rom_dialog_icon(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE) {
        const HWND window = reinterpret_cast<HWND>(wp);
        wchar_t class_name[32]{};
        GetClassNameW(window, class_name, 32);
        if (wcscmp(class_name, L"#32770") == 0) {
            const HINSTANCE module = GetModuleHandleW(nullptr);
            for (const auto kind : {ICON_SMALL, ICON_BIG}) {
                const int side = GetSystemMetrics(kind == ICON_SMALL ? SM_CXSMICON : SM_CXICON);
                const HANDLE icon = LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON,
                    side, side, LR_SHARED);
                SendMessageW(window, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
}
#endif

bool select_rom_file(SDL_Window* owner, std::filesystem::path& selected, std::string& error) {
    selected.clear();
    error.clear();
#ifdef _WIN32
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) {
        error = "Windows could not initialize the ROM picker.";
        return false;
    }
    HRESULT result;
    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
        if (SUCCEEDED(result)) {
            const COMDLG_FILTERSPEC filters[] = {{L"Nintendo 64 ROM (*.z64; *.n64; *.v64)", L"*.z64;*.n64;*.v64"}};
            dialog->SetTitle(L"Select your USA Yoshi's Story ROM");
            dialog->SetOkButtonLabel(L"Use this ROM");
            dialog->SetFileTypes(1, filters);
            dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
            HWND handle = nullptr;
            SDL_SysWMinfo info{};
            SDL_VERSION(&info.version);
            if (owner && SDL_GetWindowWMInfo(owner, &info)) handle = info.info.win.window;
            // Native common dialogs have their own HWND; give the startup
            // picker the same embedded egg icon even before an SDL window exists.
            const HHOOK hook = SetWindowsHookExW(WH_CBT, rom_dialog_icon, nullptr, GetCurrentThreadId());
            result = dialog->Show(handle);
            if (hook) UnhookWindowsHookEx(hook);
            if (SUCCEEDED(result)) {
                Microsoft::WRL::ComPtr<IShellItem> item;
                result = dialog->GetResult(&item);
                PWSTR path = nullptr;
                if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result)) selected = path;
                CoTaskMemFree(path);
            }
        }
    }
    CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        error = "Windows could not open the ROM picker. Please restart the app and try again.";
#else
    (void)owner;
    RT64::FileDialog::initialize();
    selected = RT64::FileDialog::getOpenFilename({{"Nintendo 64 ROM", "z64,n64,v64"}});
    RT64::FileDialog::finish();
#endif
    return !selected.empty();
}

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
