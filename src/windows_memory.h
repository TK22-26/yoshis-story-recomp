#pragma once

#include <cstdint>

// Called by the Windows-only N64ModernRuntime patch before any game threads
// start. The two N64 RAM segments must refer to the same physical pages.
uint8_t* yoshi_allocate_rdram();
bool yoshi_free_rdram(uint8_t* rdram);
