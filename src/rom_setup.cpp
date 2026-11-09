#include "rom_setup.h"
#include "rom_file.h"
#include "platform.h"
#include "xxHash/xxh3.h"
#include <fstream>
#include <SDL.h>

namespace {
bool valid(const std::vector<uint8_t>& data) {
    return XXH3_64bits(data.data(), data.size()) == rom_file::hash;
}
void error(SDL_Window* owner, const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Yoshi's Story - ROM selection", message, owner);
}
}

bool ensure_rom(const std::filesystem::path& directory, SDL_Window* owner, bool force_selection) {
    const auto stored = directory / "yoshis_story.z64";
    std::vector<uint8_t> data;
    if (!force_selection && rom_file::read(stored, data) == rom_file::Result::Good && valid(data)) {
        // Runtime storage uses z64 byte order. This also migrates a manually
        // placed byteswapped image without ever changing the original source.
        std::ifstream header(stored, std::ios::binary);
        if (header.get() == 0x80) return true;
        header.close();
        if (rom_file::store(stored, data)) return true;
    }
    for (;;) {
        std::filesystem::path selected;
        std::string dialog_error;
        if (!platform::select_rom_file(owner, selected, dialog_error)) {
            if (!dialog_error.empty()) error(owner, dialog_error.c_str());
            return false;
        }
        const auto result = rom_file::read(selected, data);
        if (result != rom_file::Result::Good || !valid(data)) {
            error(owner, result == rom_file::Result::CannotRead
                ? "The selected file could not be read. Choose an accessible copy of your USA Yoshi's Story ROM."
                : "This file is not the supported USA Yoshi's Story ROM.\n\nChoose an unmodified .z64, .n64, or .v64 dump of the USA game. Your file has not been changed.");
            continue;
        }
        if (!rom_file::store(stored, data)) {
            error(owner, "The ROM could not be saved in the data folder.\n\nExtract the complete app into a writable folder and try again. Your selected file has not been changed.");
            return false;
        }
        return true;
    }
}
