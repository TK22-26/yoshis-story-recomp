#include "rom_file.h"

#include <algorithm>
#include <fstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

rom_file::Result rom_file::read(const std::filesystem::path& path, std::vector<uint8_t>& data) {
    data.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Result::CannotRead;
    if (file.tellg() != std::streamoff(size)) return Result::WrongSize;
    file.seekg(0);
    data.resize(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) {
        data.clear();
        return Result::CannotRead;
    }
    const uint32_t header = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                            (uint32_t(data[2]) << 8) | data[3];
    if (header == 0x37804012) {
        for (size_t i = 0; i < data.size(); i += 2) std::swap(data[i], data[i + 1]);
    } else if (header == 0x40123780) {
        for (size_t i = 0; i < data.size(); i += 4) {
            std::swap(data[i], data[i + 3]);
            std::swap(data[i + 1], data[i + 2]);
        }
    } else if (header != 0x80371240) {
        data.clear();
        return Result::InvalidHeader;
    }
    return Result::Good;
}

bool rom_file::store(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    if (data.size() != size) return false;
    auto temporary = path;
#ifdef _WIN32
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
#else
    temporary += "." + std::to_string(getpid()) + ".tmp";
#endif
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();
    bool committed = false;
    if (out) {
#ifdef _WIN32
        committed = MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        committed = !ec;
#endif
    }
    if (!committed) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
    }
    return committed;
}
