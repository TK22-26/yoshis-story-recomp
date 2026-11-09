#include "windows_memory.h"
#include <cstdio>

int main() {
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto* ram = yoshi_allocate_rdram();
        if (!ram) return 1;
        volatile uint8_t* memory = ram;
        memory[0] = 0x12;
        memory[0x207fffff] = 0x34;
        memory[0x04000000] = 0x56;
        memory[0x24000000] = 0x78;
        memory[0xffffffffULL] = 0x9a;
        const bool ok = memory[0x20000000] == 0x12 && memory[0x7fffff] == 0x34
            && memory[0x04000000] == 0x56 && memory[0x24000000] == 0x78
            && memory[0xffffffffULL] == 0x9a;
        if (!yoshi_free_rdram(ram) || !ok) return 2;
    }
    puts("Windows RDRAM: bidirectional aliases, independent MMIO, full address range, cleanup OK");
    return 0;
}
