#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace rom_file {
inline constexpr size_t size = 16 * 1024 * 1024;
inline constexpr uint64_t hash = 0xf1d8a6737dda5b82ULL;
enum class Result { Good, CannotRead, WrongSize, InvalidHeader };
// Read a bounded N64 image and normalize z64/v64/n64 byte order in memory.
Result read(const std::filesystem::path& path, std::vector<uint8_t>& data);
// Commit an already validated image atomically. Never truncate an existing ROM.
bool store(const std::filesystem::path& path, const std::vector<uint8_t>& data);
}
