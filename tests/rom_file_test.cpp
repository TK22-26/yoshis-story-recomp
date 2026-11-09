#include "rom_file.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <windows.h>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()), data.size());
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"Yoshi ROM test \u00e9 " + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    try {
        // Synthetic data only: no game bytes or real ROM needed by CI.
        std::vector<uint8_t> original(rom_file::size);
        for (size_t i = 0; i < original.size(); ++i) original[i] = uint8_t(i);
        original[0] = 0x80; original[1] = 0x37; original[2] = 0x12; original[3] = 0x40;
        const auto source = directory / L"ROM with spaces \u00e9.z64";
        std::vector<uint8_t> data;
        for (int order : {0, 1, 3}) {
            auto encoded = original;
            for (size_t i = 0; i < encoded.size(); ++i) encoded[i] = original[i ^ order];
            write(source, encoded);
            require(rom_file::read(source, data) == rom_file::Result::Good, "byte order rejected");
            require(data == original, "normalized bytes differ");
        }
        const auto stored = directory / "stored.z64";
        require(rom_file::store(stored, data), "initial import failed");
        data[10] ^= 1;
        require(rom_file::store(stored, data), "replacement failed");
        std::vector<uint8_t> loaded;
        require(rom_file::read(stored, loaded) == rom_file::Result::Good && loaded == data, "replacement differs");
        require(!rom_file::store(stored, {}), "empty image accepted");
        require(rom_file::read(stored, loaded) == rom_file::Result::Good && loaded == data, "failed import changed previous file");
        // A locked destination must preserve the old file and remove its temporary copy.
        HANDLE locked = CreateFileW(stored.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "could not lock destination");
        const bool replaced = rom_file::store(stored, original);
        CloseHandle(locked);
        require(!replaced, "locked destination was replaced");
        require(rom_file::read(stored, loaded) == rom_file::Result::Good && loaded == data, "write failure changed previous file");
        require(!rom_file::store(directory / "missing" / "rom.z64", original), "unwritable destination accepted");
        require(rom_file::read(directory / "absent", loaded) == rom_file::Result::CannotRead, "missing file accepted");
        write(source, {0x80, 0x37, 0x12, 0x40});
        require(rom_file::read(source, loaded) == rom_file::Result::WrongSize, "short file accepted");
        original[0] = 0;
        write(source, original);
        require(rom_file::read(source, loaded) == rom_file::Result::InvalidHeader, "invalid header accepted");
        original.push_back(0);
        write(source, original);
        require(rom_file::read(source, loaded) == rom_file::Result::WrongSize, "oversized file accepted");
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            require(entry.path().extension() != ".tmp", "temporary file left after failed import");
        std::filesystem::remove_all(directory);
        std::cout << "ROM byte order, Unicode paths, bounds, and atomic import checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
