#include "windows_memory.h"

#include <windows.h>
#include <memoryapi.h>
#include <array>
#include <cstdio>

namespace {
constexpr size_t ram_size = 8ULL * 1024 * 1024;
constexpr size_t alias_offset = 0x20000000;
constexpr size_t address_space = 4ULL * 1024 * 1024 * 1024;
struct Region {
    size_t offset;
    size_t size;
    bool shared;
};
constexpr std::array<Region, 4> regions{{
    {0, ram_size, true},
    {ram_size, alias_offset - ram_size, false},
    {alias_offset, ram_size, true},
    {alias_offset + ram_size, address_space - alias_offset - ram_size, false},
}};
}

uint8_t* yoshi_allocate_rdram() {
    // Placeholders keep the entire address range reserved throughout splitting
    // and mapping, so another thread cannot claim either alias address.
    auto* base = static_cast<uint8_t*>(VirtualAlloc2(GetCurrentProcess(), nullptr,
        address_space, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    if (!base) return nullptr;

    HANDLE backing = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, static_cast<DWORD>(ram_size), nullptr);
    if (!backing) {
        VirtualFree(base, 0, MEM_RELEASE);
        return nullptr;
    }

    size_t split_count = 0;
    size_t mapped_count = 0;
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& region = regions[i];
        auto* address = base + region.offset;
        if (i + 1 < regions.size()) {
            if (!VirtualFree(address, region.size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) break;
            ++split_count;
        }
        void* mapped = region.shared
            ? MapViewOfFile3(backing, GetCurrentProcess(), address, 0, region.size,
                MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0)
            : VirtualAlloc2(GetCurrentProcess(), address, region.size,
                MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        if (!mapped) break;
        ++mapped_count;
    }
    const DWORD error = GetLastError();
    CloseHandle(backing);
    if (mapped_count == regions.size()) return base;

    // Each successful split leaves one additional independent placeholder.
    for (size_t i = 0; i <= split_count; ++i) {
        auto* address = base + regions[i].offset;
        if (i < mapped_count && regions[i].shared) UnmapViewOfFile(address);
        else VirtualFree(address, 0, MEM_RELEASE);
    }
    fprintf(stderr, "[RECOMP] Windows RDRAM allocation failed (error=%lu)\n", error);
    return nullptr;
}

bool yoshi_free_rdram(uint8_t* rdram) {
    bool ok = true;
    for (const auto& region : regions) {
        const bool freed = region.shared
            ? UnmapViewOfFile(rdram + region.offset) != 0
            : VirtualFree(rdram + region.offset, 0, MEM_RELEASE) != 0;
        ok = freed && ok;
    }
    return ok;
}
