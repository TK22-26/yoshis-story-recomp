#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <mutex>
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <memory>
#include <filesystem>
#include <csignal>
#ifndef _WIN32
#include <execinfo.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#endif
#include <thread>
#include <chrono>

#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <objbase.h> // COM interfaces used by RT64's DXC headers.
#endif
#include "platform.h"

#include <span>
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"

// RT64 renderer — must come after SDL.h so that PLUME_SDL_VULKAN_ENABLED is seen.
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_render_target.h"

#include "overlay.h"
#include "game_pause.h"
#include "launcher.h"

// Pull in the generated section table from N64Recomp
#include "recomp_overlays.inl"

// Forward-declare the game entry point (generated in funcs_*.c, named bootproc at 0x80000470)
extern "C" void bootproc(uint8_t* rdram, recomp_context* ctx);

// Controller pak functions not implemented by librecomp — stub them out
extern "C" void __osPfsSelectBank_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = -1; }
extern "C" void __osContRamRead_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = -1; }
extern "C" void __osContRamWrite_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = -1; }

// Ignored by N64Recomp (renamed to *_recomp, body not generated) but librecomp
// doesn't ship an impl either. The sweep pulled in code paths that reference
// them; stub as traps so link succeeds and we see a clear error if actually hit.
extern "C" void osDpGetCounters_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
extern "C" void __d_to_ull_recomp(uint8_t*, recomp_context* ctx) {
    // double → unsigned long long. The 32-bit N64 FP regs ($f12/$f13 hold the
    // double) map to ctx->f12 pair. Reinterpret and convert.
    uint64_t bits = ((uint64_t)ctx->f12.u32l) | ((uint64_t)ctx->f13.u32l << 32);
    double d; std::memcpy(&d, &bits, 8);
    uint64_t r = (uint64_t)d;
    ctx->r2 = (uint32_t)(r >> 32);
    ctx->r3 = (uint32_t)(r & 0xFFFFFFFF);
}

// ---- Game debug output -------------------------------------------------
//
// The retail ROM's osSyncPrintf is an empty stub, but callers still pass the
// format string and arguments. config.toml hooks the recompiled osSyncPrintf
// and Fault_HungUp to call these, so the game's own debug prints (including
// "HungUp file:line" assertions) reach stderr. YOSHI_SYNCPRINTF=0 silences.

static bool vram_ok(uint32_t vram) {
    return vram >= 0x80000000u && vram < 0x80800000u;
}

// Byte at a VRAM address (RDRAM is stored word-swapped: XOR 3 for bytes).
static uint8_t vram_u8(uint8_t* rdram, uint32_t vram) {
    return rdram[(vram ^ 3u) - 0x80000000u];
}

static uint32_t vram_u32(uint8_t* rdram, uint32_t vram) {
    return vram_ok(vram) ? *(uint32_t*)(rdram + (vram - 0x80000000u)) : 0;
}

static std::string vram_cstr(uint8_t* rdram, uint32_t vram, size_t max_len = 256) {
    if (!vram_ok(vram)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "<bad ptr 0x%08X>", vram);
        return buf;
    }
    std::string s;
    for (size_t i = 0; i < max_len && vram_ok(vram + i); ++i) {
        char c = (char)vram_u8(rdram, vram + i);
        if (c == '\0') break;
        s += c;
    }
    return s;
}

// MIPS o32 varargs: argument words 0-3 in $a0-$a3, the rest on the caller's
// stack at $sp + 4*i. 64-bit values (long long, double) are 8-byte aligned
// and big-endian (high word first).
struct VarArgs {
    uint8_t* rdram;
    recomp_context* ctx;
    int index;
    uint32_t word() {
        int i = index++;
        switch (i) {
            case 0: return (uint32_t)ctx->r4;
            case 1: return (uint32_t)ctx->r5;
            case 2: return (uint32_t)ctx->r6;
            case 3: return (uint32_t)ctx->r7;
        }
        uint32_t addr = (uint32_t)ctx->r29 + 4u * (uint32_t)i;
        return vram_ok(addr) ? *(uint32_t*)(rdram + (addr - 0x80000000u)) : 0;
    }
    uint64_t dword() {
        index = (index + 1) & ~1;
        uint64_t hi = word();
        return (hi << 32) | word();
    }
};

static std::string format_mips_printf(uint8_t* rdram, recomp_context* ctx) {
    VarArgs args{ rdram, ctx, 0 };
    std::string fmt = vram_cstr(rdram, args.word(), 1024);
    std::string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') { out += fmt[i]; continue; }
        // Collect the conversion spec: flags, width, precision, length.
        size_t start = i++;
        while (i < fmt.size() && strchr("-+ #0", fmt[i])) ++i;
        if (i < fmt.size() && fmt[i] == '*') { args.word(); ++i; }
        while (i < fmt.size() && isdigit((unsigned char)fmt[i])) ++i;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            if (i < fmt.size() && fmt[i] == '*') { args.word(); ++i; }
            while (i < fmt.size() && isdigit((unsigned char)fmt[i])) ++i;
        }
        int longs = 0;
        while (i < fmt.size() && strchr("hlLqjzt", fmt[i])) { if (fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'q') ++longs; ++i; }
        if (i >= fmt.size()) break;
        char conv = fmt[i];
        // Rebuild a host spec without length modifiers (star widths dropped).
        std::string spec;
        for (size_t j = start; j < i; ++j)
            if (!strchr("hlLqjzt*", fmt[j])) spec += fmt[j];
        char buf[512];
        switch (conv) {
            case '%': out += '%'; continue;
            case 'd': case 'i':
                if (longs >= 2) snprintf(buf, sizeof(buf), (spec + "lld").c_str(), (long long)args.dword());
                else snprintf(buf, sizeof(buf), (spec + "d").c_str(), (int32_t)args.word());
                break;
            case 'u': case 'x': case 'X': case 'o':
                if (longs >= 2) snprintf(buf, sizeof(buf), (spec + "ll" + conv).c_str(), (unsigned long long)args.dword());
                else snprintf(buf, sizeof(buf), (spec + conv).c_str(), args.word());
                break;
            case 'c': snprintf(buf, sizeof(buf), (spec + "c").c_str(), (int)(args.word() & 0xFF)); break;
            case 'p': snprintf(buf, sizeof(buf), "0x%08X", args.word()); break;
            case 's': snprintf(buf, sizeof(buf), (spec + "s").c_str(), vram_cstr(rdram, args.word()).c_str()); break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                uint64_t bits = args.dword();
                double d; memcpy(&d, &bits, sizeof(d));
                snprintf(buf, sizeof(buf), (spec + conv).c_str(), d);
                break;
            }
            default: snprintf(buf, sizeof(buf), "%%%c", conv); break;
        }
        out += buf;
    }
    return out;
}

extern "C" void yoshi_sync_printf(uint8_t* rdram, recomp_context* ctx) {
    static const bool enabled = [] {
        const char* v = getenv("YOSHI_SYNCPRINTF");
        return !(v && v[0] == '0');
    }();
    if (!enabled) return;
    std::string s = format_mips_printf(rdram, ctx);
    if (s.empty() || s.back() != '\n') s += '\n';
    fprintf(stderr, "[game] %s", s.c_str());
    fflush(stderr);
}

extern "C" void yoshi_report_hungup(uint8_t* rdram, recomp_context* ctx) {
    // No caller address: recompiled calls are native and never set $ra.
    fprintf(stderr, "[HungUp] %s:%d\n",
            vram_cstr(rdram, (uint32_t)ctx->r4).c_str(), (int32_t)ctx->r5);
    // Recompiled functions are exported (ENABLE_EXPORTS), so this names the
    // game call chain that led here.
    platform::print_backtrace();

    // castManager.c:161: func_80051194(id) found no loaded cast entry with that id.
    // The hook runs before Fault_HungUp's prologue, so $sp is still
    // func_80051194's: its id argument is saved at sp+0x20, and the spawning
    // actor (func_80045CAC's a0) at sp+0x20+0x28. The loaded cast list at
    // *0x800F84D4 is a count byte, then entry pointers sorted by id (entry+0x3C).
    if ((int32_t)ctx->r5 == 161 && vram_cstr(rdram, (uint32_t)ctx->r4).find("castManager") != std::string::npos) {
        const uint32_t sp = (uint32_t)ctx->r29;
        const uint32_t id = vram_u32(rdram, sp + 0x20) & 0xFFFF;
        const uint32_t actor = vram_u32(rdram, sp + 0x20 + 0x28);
        fprintf(stderr, "[castmiss] id 0x%04X requested by actor 0x%08X (actor+0xC=0x%08X)\n",
                id, actor, vram_u32(rdram, actor + 0xC));
        const uint32_t list = vram_u32(rdram, 0x800F84D4u);
        const uint32_t count = vram_u32(rdram, list) >> 24;
        fprintf(stderr, "[castmiss] loaded list 0x%08X, %u entries:", list, count);
        for (uint32_t i = 0; i < count && i < 256; i++) {
            const uint32_t entry = vram_u32(rdram, list + 4 + 4 * i);
            fprintf(stderr, " %04X", vram_u32(rdram, entry + 0x3C) >> 16);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

// ---- Relocatable overlays --------------------------------------------
//
// Overlay_Load(vromStart, vromEnd, vramStart, vramEnd, allocatedRamAddr)
// (0x8008169C, src/main/O2/loadfragment2.c) DMAs a .ovl file into the heap and
// relocates it with DoRelocation. Tell librecomp where it landed so the
// recompiled functions of every overlay section in that ROM range resolve at
// their relocated addresses (and section_addresses[] serves RELOC_HI16/LO16).
// Hooked at function entry; allocatedRamAddr is the 5th argument, on the stack.
extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);

extern "C" void yoshi_overlay_load(uint8_t* rdram, recomp_context* ctx) {
    uint32_t vrom_start = (uint32_t)ctx->r4;
    uint32_t vrom_end   = (uint32_t)ctx->r5;
    uint32_t vram_start = (uint32_t)ctx->r6;
    uint32_t vram_end   = (uint32_t)ctx->r7;
    uint32_t ram        = vram_u32(rdram, (uint32_t)ctx->r29 + 0x10);
    fprintf(stderr, "[ovl] load rom 0x%08X-0x%08X vram 0x%08X-0x%08X -> ram 0x%08X\n",
            vrom_start, vrom_end, vram_start, vram_end, ram);
    fflush(stderr);
    load_overlays(vrom_start, (int32_t)ram, vrom_end - vrom_start);
}

// ---- Debug toggles -----------------------------------------------------
//
// Diagnostics are opt-in (see README "Opt-in debug hooks").
static bool env_flag(const char* name) {
    const char* v = getenv(name);
    return v && v[0] != '0';
}

static constexpr size_t rdram_off(uint32_t vram) { return vram - 0x80000000u; }

// func_8000AD10 is libultra's osAiSetNextBuffer, unnamed in the decomp, so
// N64Recomp compiled the game's copy, which writes the AI registers
// (0xA4500000/4) - nowhere in the recomp. A hook at its entry hands the
// buffer to the runtime instead (config.toml) and returns.
extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx);
extern "C" void yoshi_ai_set_next_buffer(uint8_t* rdram, recomp_context* ctx) {
    osAiSetNextBuffer_recomp(rdram, ctx);
}

static uint8_t* g_rdram = nullptr;

// ---- RSP microcode dispatch ------------------------------------------
// Graphics tasks are handled by RT64 via RendererContext::send_dl().
// The stub just returns Broke so librecomp's run_task() exits cleanly.
// Audio tasks (type 2) run the recompiled aspMain when rsp/aspMain.cpp exists.

static RspExitReason stub_rsp_run(uint8_t*, uint32_t) {
    return RspExitReason::Broke;
}

#ifdef YOSHI_HAVE_ASPMAIN
RspExitReason aspMain(uint8_t* rdram, uint32_t ucode_addr);  // rsp/aspMain.cpp
#endif

static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
#ifdef YOSHI_HAVE_ASPMAIN
    if (task->t.type == 2) return aspMain;  // M_AUDTASK
#endif
    return stub_rsp_run;
}

// ---- RT64 renderer ---------------------------------------------------
// Wraps RT64::Application as a RendererContext for N64ModernRuntime.

SDL_Window* g_sdl_window = nullptr;   // declared in launcher.h

// VI register mirrors — RT64 reads these via Application::Core::decodeVI().
// We copy from ultramodern's ViRegs each frame before calling updateScreen().
static uint32_t g_vi_status   = 0;
static uint32_t g_vi_origin   = 0;
static uint32_t g_vi_width    = 0;
static uint32_t g_vi_intr     = 0;
static uint32_t g_vi_vcurrent = 0;
static uint32_t g_vi_timing   = 0;
static uint32_t g_vi_vsync    = 0;
static uint32_t g_vi_hsync    = 0;
static uint32_t g_vi_leap     = 0;
static uint32_t g_vi_hstart   = 0;
static uint32_t g_vi_vstart   = 0;
static uint32_t g_vi_vburst   = 0;
static uint32_t g_vi_xscale   = 0;
static uint32_t g_vi_yscale   = 0;

// Dummy DPC / MI registers — RT64 needs non-null pointers.
static uint32_t g_dpc_start = 0, g_dpc_end = 0, g_dpc_current = 0;
static uint32_t g_dpc_status = 0, g_dpc_clock = 0, g_dpc_bufbusy = 0;
static uint32_t g_dpc_pipebusy = 0, g_dpc_tmem = 0;
static uint32_t g_mi_intr = 0;
static uint8_t  g_dmem[0x1000] = {};
static uint8_t  g_imem[0x1000] = {};
static uint8_t  g_rom_header[64] = {};

// ---- Display-list summary (diagnostic) ------------------------------------
//
// Walks one frame's F3DEX (GBI1) display list through every G_DL call,
// resolving segmented addresses the way the microcode does (G_MOVEWORD
// G_MW_SEGMENT updates the table), and prints a histogram of commands. Answers
// "is the game drawing geometry/sprites, or only clearing and setting state?"
// without touching RT64. Triggered for a few send_dl calls (see send_dl).
static const char* gbi1_name(uint8_t op) {
    switch (op) {
        case 0x00: return "SPNOOP";     case 0x01: return "MTX";
        case 0x03: return "MOVEMEM";    case 0x04: return "VTX";
        case 0x06: return "DL";         case 0x09: return "SPRITE2D_BASE";
        case 0xAF: return "LOAD_UCODE"; case 0xB0: return "BRANCH_Z";
        case 0xB1: return "TRI2";       case 0xB2: return "MODIFYVTX";
        case 0xB3: return "RDPHALF_2";  case 0xB4: return "RDPHALF_1";
        case 0xB5: return "LINE3D";     case 0xB6: return "CLEARGEOMETRYMODE";
        case 0xB7: return "SETGEOMETRYMODE"; case 0xB8: return "ENDDL";
        case 0xB9: return "SETOTHERMODE_L";  case 0xBA: return "SETOTHERMODE_H";
        case 0xBB: return "TEXTURE";    case 0xBC: return "MOVEWORD";
        case 0xBD: return "POPMTX";     case 0xBE: return "CULLDL";
        case 0xBF: return "TRI1";       case 0xE4: return "TEXRECT";
        case 0xE5: return "TEXRECTFLIP"; case 0xE6: return "RDPLOADSYNC";
        case 0xE7: return "RDPPIPESYNC"; case 0xE8: return "RDPTILESYNC";
        case 0xE9: return "RDPFULLSYNC"; case 0xED: return "SETSCISSOR";
        case 0xF0: return "LOADTLUT";   case 0xF2: return "SETTILESIZE";
        case 0xF3: return "LOADBLOCK";  case 0xF4: return "LOADTILE";
        case 0xF5: return "SETTILE";    case 0xF6: return "FILLRECT";
        case 0xF7: return "SETFILLCOLOR"; case 0xF8: return "SETFOGCOLOR";
        case 0xF9: return "SETBLENDCOLOR"; case 0xFA: return "SETPRIMCOLOR";
        case 0xFB: return "SETENVCOLOR"; case 0xFC: return "SETCOMBINE";
        case 0xFD: return "SETTIMG";    case 0xFE: return "SETZIMG";
        case 0xFF: return "SETCIMG";
    }
    return nullptr;
}

// F9 (update_gfx) requests a full listing of the next frame's display list.
static std::atomic<bool> g_dl_dump_request{false};
// F10 (update_gfx) cycles the CRT filter; the renderer thread applies it.
static std::atomic<bool> g_crt_changed{false};
std::filesystem::path g_settings_path;

// Apply g_settings (data/settings.ini, the launcher, YOSHI_* env overrides)
// to RT64's user configuration. Runs after setup(), which loads RT64's own
// rt64.json and would overwrite anything set earlier. Also called from the
// in-game overlay when the user changes a value.
void apply_display_settings(RT64::Application* app, bool initialize) {
    using UC = RT64::UserConfiguration;
    UC& cfg = app->userConfig;
    const UC::Antialiasing oldAA = cfg.antialiasing;

    cfg.resolution = g_settings.resolution == YoshiSettings::Native ? UC::Resolution::Original
                   : g_settings.resolution == YoshiSettings::Manual ? UC::Resolution::Manual
                   : UC::Resolution::WindowIntegerScale;
    cfg.resolutionMultiplier = g_settings.multiplier;
    // Supersampling (render larger, scale down) only applies to a fixed multiplier.
    cfg.downsampleMultiplier = std::max(1, g_settings.ssaa);
    if (initialize) cfg.antialiasing = g_settings.msaa >= 8 ? UC::Antialiasing::MSAA8X
                     : g_settings.msaa >= 4 ? UC::Antialiasing::MSAA4X
                     : g_settings.msaa >= 2 ? UC::Antialiasing::MSAA2X
                     : UC::Antialiasing::None;
    cfg.upscale2D = UC::Upscale2D(std::clamp(g_settings.upscale2D, 0, 2));
    cfg.filtering = UC::Filtering(std::clamp(g_settings.filter, 0, 2));
    cfg.validate();

    if (cfg.antialiasing != oldAA) {
        // Keep the setting only if both possible colour formats and the depth
        // format support it (RT64 does the same check during setup()).
        const auto supported =
            app->device->getSampleCountsSupported(RT64::RenderTarget::colorBufferFormat(false)) &
            app->device->getSampleCountsSupported(RT64::RenderTarget::colorBufferFormat(true)) &
            app->device->getSampleCountsSupported(RT64::RenderTarget::depthBufferFormat());
        if ((supported & cfg.msaaSampleCount()) == 0) {
            fprintf(stderr, "[display] MSAA x%u is not supported by this GPU, left off\n", unsigned(cfg.msaaSampleCount()));
            cfg.antialiasing = oldAA;
        } else {
            app->updateMultisampling();
        }
    }
    app->updateUserConfig(true);

    static const char* const resNames[] = { "native", "window", "manual" };
    static const char* const up2dNames[] = { "original", "scaled", "all" };
    static const char* const filterNames[] = { "nearest", "linear", "pixel" };
    fprintf(stderr, "[display] resolution=%s x%.2g ssaa=%d msaa=%u upscale2D=%s filter=%s crt=%.2f crop=%d\n",
        resNames[int(cfg.resolution)], cfg.resolutionMultiplier, cfg.downsampleMultiplier,
        unsigned(cfg.msaaSampleCount()), up2dNames[int(cfg.upscale2D)], filterNames[int(cfg.filtering)],
        g_settings.crt, g_settings.crop);
}

static void dump_dl_summary(uint8_t* rdram, uint32_t dl_vram, int frame, bool verbose = false) {
    uint32_t segments[16] = {};
    uint32_t counts[256] = {};
    auto resolve = [&](uint32_t addr) -> uint32_t {
        // KSEG0 pointers pass through; segmented ones use the table.
        if ((addr & 0xF0000000u) == 0x80000000u) return addr;
        return 0x80000000u | ((segments[(addr >> 24) & 0xF] + (addr & 0x00FFFFFF)) & 0x00FFFFFF);
    };
    uint32_t stack[18];
    int depth = 0;
    uint32_t pc = dl_vram;
    int total = 0, fills_printed = 0;
    // After a G_LOAD_UCODE the commands belong to another microcode (likely
    // S2DEX), so the GBI1 names above no longer apply: print them raw.
    uint32_t last_rdphalf1 = 0;
    int raw_after_ucode = 0;
    fprintf(stderr, "[dl] frame %d: walking DL 0x%08X\n", frame, dl_vram);
    while (total < 20000) {
        if (!vram_ok(pc)) {
            fprintf(stderr, "[dl]   bad DL address 0x%08X, stopping\n", pc);
            break;
        }
        uint32_t w0 = vram_u32(rdram, pc), w1 = vram_u32(rdram, pc + 4);
        uint8_t op = (uint8_t)(w0 >> 24);
        counts[op]++;
        total++;
        pc += 8;
        if (verbose) {
            const char* name = gbi1_name(op);
            fprintf(stderr, "[dl] %08X %*s%08X %08X %s\n", pc - 8, depth * 2, "", w0, w1, name ? name : "");
            // S2DEX object commands: print the struct they point at (words as the
            // game wrote them): 05 matrix, C1 uObjTxtr, C2-C4 uObjTxtr + uObjSprite,
            // 03/04/B2 uObjSprite.
            int nwords = 0;
            if (op == 0x05) nwords = (int)(((w0 >> 16) & 0xFF) + 1) / 4;
            else if (op == 0xC1) nwords = 6;
            else if (op >= 0xC2 && op <= 0xC4) nwords = 12;
            else if (op == 0x03 || op == 0x04 || op == 0xB2) nwords = 6;
            else if (op == 0x01 || op == 0x02) nwords = 10;  // S2DEX BG_1CYC / BG_COPY: uObjBg
            uint32_t sp = resolve(w1);
            if (nwords > 0 && nwords <= 16 && vram_ok(sp)) {
                fprintf(stderr, "[dl] %*s  -> %08X:", depth * 2 + 9, "", sp);
                for (int i = 0; i < nwords; i++) fprintf(stderr, " %08X", vram_u32(rdram, sp + 4 * i));
                fprintf(stderr, "\n");
            }
        } else if (raw_after_ucode > 0) {
            fprintf(stderr, "[dl]   %*s  raw %08X %08X\n", depth * 2, "", w0, w1);
            raw_after_ucode--;
        }
        if (op == 0xB4) last_rdphalf1 = w1;                 // RDPHALF_1 (ucode data for LOAD_UCODE)
        if (op == 0xAF) {                                   // G_LOAD_UCODE
            fprintf(stderr, "[dl]   %*sLOAD_UCODE text=0x%08X data=0x%08X (size 0x%X); next commands raw:\n",
                    depth * 2, "", w1, last_rdphalf1, (w0 & 0xFFFF) + 1);
            raw_after_ucode = 24;
            continue;
        }
        if (op == 0xBC && (w0 & 0xFF) == 0x06) {            // MOVEWORD G_MW_SEGMENT
            segments[((w0 >> 8) & 0xFFFF) / 4 & 0xF] = w1 & 0x00FFFFFF;
        } else if (op == 0x06) {                            // G_DL (push or branch)
            uint32_t target = resolve(w1);
            fprintf(stderr, "[dl]   %*sDL %s 0x%08X -> 0x%08X\n", depth * 2, "",
                    ((w0 >> 16) & 0xFF) ? "branch" : "call", w1, target);
            if (((w0 >> 16) & 0xFF) == 0) {
                if (depth >= 18) { fprintf(stderr, "[dl]   DL stack overflow\n"); break; }
                stack[depth++] = pc;
            }
            pc = target;
        } else if (op == 0xB8) {                            // G_ENDDL
            if (depth == 0) break;
            pc = stack[--depth];
        } else if ((op == 0xF6 || op == 0xF7) && fills_printed < 8) {
            fprintf(stderr, "[dl]   %*s%s %08X %08X\n", depth * 2, "", gbi1_name(op), w0, w1);
            fills_printed++;
        }
    }
    fprintf(stderr, "[dl]   %d commands:", total);
    for (int op = 0; op < 256; op++) {
        if (!counts[op]) continue;
        const char* name = gbi1_name((uint8_t)op);
        if (name) fprintf(stderr, " %s=%u", name, counts[op]);
        else fprintf(stderr, " op%02X=%u", op, counts[op]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

class RT64RendererContext final : public ultramodern::renderer::RendererContext {
public:
    std::unique_ptr<RT64::Application> app;
    bool initialized = false;

    RT64RendererContext(uint8_t* rdram, ultramodern::renderer::WindowHandle window) {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api   = ultramodern::renderer::GraphicsApi::Vulkan;

        RT64::Application::Core core{};
        // Windows uses RT64's native HWND backend; Linux uses SDL Vulkan.
#ifdef _WIN32
        core.window          = window.window;
#else
        core.window          = window;
#endif
        core.HEADER          = g_rom_header;
        core.RDRAM           = rdram;
        core.DMEM            = g_dmem;
        core.IMEM            = g_imem;
        core.MI_INTR_REG     = &g_mi_intr;
        core.DPC_START_REG   = &g_dpc_start;
        core.DPC_END_REG     = &g_dpc_end;
        core.DPC_CURRENT_REG = &g_dpc_current;
        core.DPC_STATUS_REG  = &g_dpc_status;
        core.DPC_CLOCK_REG   = &g_dpc_clock;
        core.DPC_BUFBUSY_REG = &g_dpc_bufbusy;
        core.DPC_PIPEBUSY_REG= &g_dpc_pipebusy;
        core.DPC_TMEM_REG    = &g_dpc_tmem;
        core.VI_STATUS_REG         = &g_vi_status;
        core.VI_ORIGIN_REG         = &g_vi_origin;
        core.VI_WIDTH_REG          = &g_vi_width;
        core.VI_INTR_REG           = &g_vi_intr;
        core.VI_V_CURRENT_LINE_REG = &g_vi_vcurrent;
        core.VI_TIMING_REG         = &g_vi_timing;
        core.VI_V_SYNC_REG         = &g_vi_vsync;
        core.VI_H_SYNC_REG         = &g_vi_hsync;
        core.VI_LEAP_REG           = &g_vi_leap;
        core.VI_H_START_REG        = &g_vi_hstart;
        core.VI_V_START_REG        = &g_vi_vstart;
        core.VI_V_BURST_REG        = &g_vi_vburst;
        core.VI_X_SCALE_REG        = &g_vi_xscale;
        core.VI_Y_SCALE_REG        = &g_vi_yscale;
        core.checkInterrupts       = [](){};

        RT64::ApplicationConfiguration appCfg{};
        appCfg.appId = "yoshis_story_recomp";
        appCfg.detectDataPath = true;
        appCfg.useConfigurationFile = true;

        app = std::make_unique<RT64::Application>(core, appCfg);

        // The window belongs to the SDL event thread, not the renderer thread.
#ifdef _WIN32
        auto result = app->setup(window.thread_id);
#else
        auto result = app->setup(0);
#endif
        if (result != RT64::Application::SetupResult::Success) {
            fprintf(stderr, "[RT64] Application::setup() failed: %d\n", (int)result);
            setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
            app.reset();
            return;
        }
        // RT64's S2DEX "framebuffer fast path" (an enhancement, on by default)
        // draws G_BG_1CYC backgrounds as one tile from a tracked framebuffer when
        // one overlaps the image. For this game it matches stale GPU data
        // instead of the CPU-unpacked background images, so level backgrounds
        // were black and nothing repainted the screen (sprites left trails).
        // The accurate per-slice path works; YOSHI_BG_FASTPATH=1 re-enables it.
        if (!env_flag("YOSHI_BG_FASTPATH")) {
            app->enhancementConfig.s2dex.framebufferFastPath = false;
        }
        // CRT filter (RT64 patch 0003-crt-filter): scanlines and an aperture
        // grille, strength 0..1 (default off). F10 cycles it while playing.
        app->enhancementConfig.presentation.crtStrength = g_settings.crt;
        app->updateEnhancementConfig();
        apply_display_settings(app.get(), true);
        // The launcher may have consumed SDL window ID 1. Supply the actual
        // game window instead of relying on RT64's first-window fallback.
        app->appWindow->sdlWindow = g_sdl_window;
        chosen_api = app->chosenGraphicsAPI == RT64::UserConfiguration::GraphicsAPI::D3D12
            ? ultramodern::renderer::GraphicsApi::D3D12
            : ultramodern::renderer::GraphicsApi::Vulkan;
        overlay::attach(*app);
        initialized = true;
        fprintf(stderr, "[RT64] Application initialized\n"); fflush(stderr);
    }

    bool valid() override { return initialized; }

    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override {
        if (initialized) app->updateUserConfig(false);
        return true;
    }

    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        if (!initialized) return;
        if (task->t.type != M_GFXTASK) return;

        static int dl_count = 0;
        dl_count++;

        // task->t.data_ptr is a VRAM pointer (0x80xxxxxx).
        // RT64 wants a byte offset into the flat RDRAM array.
        uint32_t dl_start = (uint32_t)(task->t.data_ptr) - 0x80000000u;

        // Game task with a real F3D microcode. RT64 picks the HLE GBI
        // (command decoder) by identifying the ucode in RDRAM, so load it
        // first — without this hleGBI is null and the first command
        // dereferences it (segfault at 0x750 in the gfx thread). Same
        // sequence as Zelda64Recomp's renderer. dlEndAddress = 0 means
        // "process until G_ENDDL".
        // reset() only once; the game expects segment state to persist.
        // Load the task's GBI at the start of EVERY task: display lists
        // switch microcode mid-list with G_LOAD_UCODE (F3DEX <-> S2DEX), so
        // RT64's active GBI at the end of one task says nothing about the
        // next task's. Comparing with the previous task's ucode left RT64
        // decoding S2DEX tasks (level-start G_BG_1CYC backgrounds) as F3DEX:
        // black screen and sprites smearing. Task microcode changes are logged.
        static bool rsp_initialised = false;
        static uint32_t last_ucode = 0, last_ucode_data = 0;
        uint32_t ucode      = (uint32_t)task->t.ucode & 0x3FFFFFF;
        uint32_t ucode_data = (uint32_t)task->t.ucode_data & 0x3FFFFFF;
        if (!rsp_initialised) {
            app->state->rsp->reset();
            rsp_initialised = true;
        }
        if (ucode != last_ucode || ucode_data != last_ucode_data) {
            fprintf(stderr, "[RT64] ucode change at send_dl #%d: text 0x%08X data 0x%08X (was 0x%08X/0x%08X)\n",
                    dl_count, ucode, ucode_data, last_ucode, last_ucode_data);
            fflush(stderr);
            last_ucode = ucode;
            last_ucode_data = ucode_data;
        }
        app->interpreter->loadUCodeGBI(ucode, ucode_data, true);
        // Display-list dumps: F9 lists every command of the next frame,
        // YOSHI_DLDUMP=N prints a command histogram of every Nth frame.
        static const int dl_dump_every = [] {
            const char* v = getenv("YOSHI_DLDUMP");
            return v ? atoi(v) : 0;
        }();
        if (g_dl_dump_request.exchange(false)) {
            static const char* gbi_names[] = { "Unknown", "RDP", "F3D", "F3DGOLDEN", "F3DPD", "F3DWAVE",
                "F3DEX", "F3DEX2", "F3DZEX2", "S2DEX", "S2DEX2", "L3DEX2" };
            RT64::GBI* g = app->interpreter->hleGBI;
            uint32_t gu = g ? (uint32_t)g->ucode : 0;
            fprintf(stderr, "[dl] task ucode text 0x%08X data 0x%08X -> RT64 GBI %s\n",
                    ucode, ucode_data, gu < 12 ? gbi_names[gu] : "?");
            dump_dl_summary(g_rdram, (uint32_t)task->t.data_ptr, dl_count, /*verbose=*/true);
        } else if (dl_dump_every > 0 && dl_count % dl_dump_every == 0) {
            dump_dl_summary(g_rdram, (uint32_t)task->t.data_ptr, dl_count);
        }
        app->processDisplayLists(g_rdram, dl_start, 0, /*isHLE=*/true);
    }

    void send_dummy_workload(uint32_t) override {}

    void update_screen() override {
        if (!initialized) return;

        if (g_crt_changed.exchange(false)) {
            app->enhancementConfig.presentation.crtStrength = g_settings.crt;
            app->updateEnhancementConfig();
        }

        // Sync VI register mirrors from ultramodern so decodeVI() is current.
        const ultramodern::renderer::ViRegs* vi = ultramodern::renderer::get_vi_regs();
        if (vi) {
            g_vi_status   = vi->VI_STATUS_REG;
            g_vi_origin   = vi->VI_ORIGIN_REG;
            g_vi_width    = vi->VI_WIDTH_REG;
            g_vi_intr     = vi->VI_INTR_REG;
            g_vi_vcurrent = vi->VI_V_CURRENT_LINE_REG;
            g_vi_timing   = vi->VI_TIMING_REG;
            g_vi_vsync    = vi->VI_V_SYNC_REG;
            g_vi_hsync    = vi->VI_H_SYNC_REG;
            g_vi_leap     = vi->VI_LEAP_REG;
            g_vi_hstart   = vi->VI_H_START_REG;
            g_vi_vstart   = vi->VI_V_START_REG;
            g_vi_vburst   = vi->VI_V_BURST_REG;
            g_vi_xscale   = vi->VI_X_SCALE_REG;
            g_vi_yscale   = vi->VI_Y_SCALE_REG;
        }

        overlay::draw(*app);
        // Present the completed image again with fresh UI while paused. Do
        // not replay the display list: repeated alpha blending would change
        // the frozen picture even though the guest itself has stopped.
        if (game_pause::is_paused() && app->state->presentId > 0) {
            auto& queue = *app->presentQueue;
            queue.waitForPresentId(app->state->presentId);
            queue.waitForIdle();
            auto& last = queue.presents[queue.previousWriteCursor()];
            app->state->advancePresent(last, true);
            queue.repeatLastPresent();
        } else {
            app->updateScreen();
        }
    }

    void shutdown() override {
        if (!initialized) return;
        overlay::detach(*app);
        app->end();
        app.reset();
        initialized = false;
    }

    uint32_t get_display_framerate() const override { return 60; }
    float    get_resolution_scale()  const override { return 1.0f; }
};

static std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool) {
    fprintf(stderr, "[RECOMP] create_render_context called\n"); fflush(stderr);
    return std::make_unique<RT64RendererContext>(rdram, window_handle);
}

// ---- Window creation -------------------------------------------------

static void* create_gfx() { return nullptr; }

static ultramodern::renderer::WindowHandle create_window(void*) {
    fprintf(stderr, "[RECOMP] create_window called\n"); fflush(stderr);
    // Default window: the largest whole multiple of 320x240 that fits 85% of
    // the screen (e.g. 1280x960 on 1440p), so fit-window resolution looks sharp.
    int win_w = 960, win_h = 720;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        const int scale = std::max(2, std::min(usable.w * 85 / 100 / 320, usable.h * 85 / 100 / 240));
        win_w = 320 * scale;
        win_h = 240 * scale;
    }
    // RT64 manages the Vulkan swap chain directly on the SDL window.
    g_sdl_window = SDL_CreateWindow(
        "Yoshi's Story Recomp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN |
            (g_settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)
    );
    if (!g_sdl_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }
    set_window_icon(g_sdl_window);
    platform::apply_window_chrome(g_sdl_window);
    fprintf(stderr, "[RECOMP] SDL window created (driver=%s)\n",
            SDL_GetCurrentVideoDriver()); fflush(stderr);
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_sdl_window, &info)) {
        fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        std::exit(1);
    }
    return {info.info.win.window, GetCurrentThreadId()};
#else
    return g_sdl_window;
#endif
}

static void input_handle_event(const SDL_Event& e);
static void input_sample();
static std::atomic<int> g_fullscreen_request{-1};

void request_fullscreen(bool fullscreen) {
    g_fullscreen_request.store(fullscreen ? 1 : 0, std::memory_order_release);
}

static void update_gfx(void*) {
    // RT64 handles all rendering; we only need to pump SDL events here.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT &&
            e.window.windowID == SDL_GetWindowID(g_sdl_window) && e.window.event == SDL_WINDOWEVENT_CLOSE))
            std::_Exit(0);
        // Window changes must run on this thread, outside the Inspector lock.
        // The renderer thread can otherwise deadlock against Win32 messages.
        if (e.type == SDL_KEYDOWN && !e.key.repeat &&
            (e.key.keysym.scancode == SDL_SCANCODE_F11 ||
             ((e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
              (e.key.keysym.mod & KMOD_ALT)))) {
            request_fullscreen(!(SDL_GetWindowFlags(g_sdl_window) & SDL_WINDOW_FULLSCREEN));
            continue;
        }
        // Route menu and owned-dialog events before sampling game controls.
        if (overlay::handle_event(&e)) continue;
        input_handle_event(e);
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F9 && !e.key.repeat) {
            g_dl_dump_request = true;
            fprintf(stderr, "[dl] F9: dumping the next display list\n");
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F10 && !e.key.repeat) {
            // Cycle off -> 0.25 -> 0.5 -> 1.0 and remember it in data/settings.ini.
            float& crt = g_settings.crt;
            crt = (crt < 0.125f) ? 0.25f : (crt < 0.375f) ? 0.5f : (crt < 0.75f) ? 1.0f : 0.0f;
            settings_save(g_settings_path);
            g_crt_changed = true;
            fprintf(stderr, "[display] F10: CRT filter %.2f\n", crt);
        }
        if (e.type == SDL_QUIT) {
            // Use _exit to bypass C++ cleanup: ultramodern::quit() → munmap races
            // with still-running N64 threads. The kernel tears down everything safely.
            std::_Exit(0);
        }
    }
    const int fullscreen = g_fullscreen_request.exchange(-1, std::memory_order_acq_rel);
    if (fullscreen >= 0) {
        if (SDL_SetWindowFullscreen(g_sdl_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
            fprintf(stderr, "[display] Fullscreen change failed: %s\n", SDL_GetError());
        g_settings.fullscreen = (SDL_GetWindowFlags(g_sdl_window) & SDL_WINDOW_FULLSCREEN) != 0;
        if (!g_settings.fullscreen) platform::apply_window_chrome(g_sdl_window);
        settings_save(g_settings_path);
        fprintf(stderr, "[display] Window mode: %s\n", g_settings.fullscreen ? "borderless" : "windowed");
    }
    overlay::process_actions();
    input_sample();
}

// ---- Audio output ----------------------------------------------------

SDL_AudioDeviceID g_audio_device = 0;
static std::mutex g_audio_mutex;

void audio_set_paused(bool paused) {
    std::lock_guard lock(g_audio_mutex);
    if (g_audio_device != 0) {
        SDL_PauseAudioDevice(g_audio_device, paused ? 1 : 0);
        if (paused) SDL_ClearQueuedAudio(g_audio_device);
    }
}

// Software master volume, patterned on DKR-R runtime_platform.cpp:g_master_volume.
// Applied per-sample in audio_queue_samples() before SDL_QueueAudio.
static std::atomic<float> g_audio_volume{1.0f};
static std::atomic<bool>  g_audio_muted{false};

void audio_set_volume(float v) {
    g_audio_volume.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_release);
    g_settings.volume = g_audio_volume.load(std::memory_order_acquire);
}
float audio_get_volume() { return g_audio_volume.load(std::memory_order_acquire); }
void audio_set_muted(bool m) {
    g_audio_muted.store(m, std::memory_order_release);
    g_settings.muted = m;
}
bool audio_get_muted() { return g_audio_muted.load(std::memory_order_acquire); }

static uint32_t g_audio_freq = 32000;

// Open (or reopen) the SDL audio device at the given sample rate.
static bool open_audio_device(uint32_t freq) {
    std::lock_guard lock(g_audio_mutex);
    if (g_audio_device != 0) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    SDL_AudioSpec desired{};
    desired.freq     = static_cast<int>(freq);
    desired.format   = AUDIO_S16SYS;
    desired.channels = 2;          // stereo
    desired.samples  = 512;        // hardware buffer hint (samples per callback)
    desired.callback = nullptr;    // push mode — we use SDL_QueueAudio
    SDL_AudioSpec obtained{};
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        fprintf(stderr, "[AUDIO] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        fflush(stderr);
        return false;
    }
    g_audio_freq = freq;
    fprintf(stderr, "[AUDIO] opened device %u: freq=%d fmt=0x%04x ch=%d\n",
            g_audio_device, obtained.freq, obtained.format, obtained.channels);
    fflush(stderr);
    SDL_PauseAudioDevice(g_audio_device, game_pause::is_paused() ? 1 : 0);
    return true;
}

static void audio_set_frequency(uint32_t freq) {
    if (freq == 0) return;
    if (freq == g_audio_freq && g_audio_device != 0) return;
    fprintf(stderr, "[AUDIO] set_frequency %u Hz\n", freq); fflush(stderr);
    open_audio_device(freq);
}

static void audio_queue_samples(int16_t* samples, size_t num_samples) {
    std::lock_guard lock(g_audio_mutex);
    if (game_pause::is_paused()) return;
    if (g_audio_device == 0 || samples == nullptr || num_samples == 0) return;
    const float vol = g_audio_muted.load(std::memory_order_relaxed)
                      ? 0.0f
                      : g_audio_volume.load(std::memory_order_relaxed);
    if (vol < 0.9999f) {
        for (size_t i = 0; i < num_samples; ++i) {
            samples[i] = static_cast<int16_t>(
                std::clamp(static_cast<float>(samples[i]) * vol, -32768.0f, 32767.0f));
        }
    }
    SDL_QueueAudio(g_audio_device, samples,
                   static_cast<Uint32>(num_samples * sizeof(int16_t)));
}

// Stereo frames still queued for playback (what osAiGetLength reports to the
// game, so it knows how much more audio to generate) - not the free space.
static size_t audio_get_frames_remaining() {
    std::lock_guard lock(g_audio_mutex);
    if (g_audio_device == 0) return 0;
    return SDL_GetQueuedAudioSize(g_audio_device) / (2 * sizeof(int16_t));
}

// ---- Input -----------------------------------------------------------
// SDL events are pumped on the main thread (update_gfx); the game polls from
// its own thread. input_sample() snapshots keyboard + game controller state
// after each pump, input_get() hands the game the latest snapshot.

namespace n64btn {
    constexpr uint16_t A = 0x8000, B = 0x4000, Z = 0x2000, START = 0x1000,
                       DU = 0x0800, DD = 0x0400, DL = 0x0200, DR = 0x0100,
                       L = 0x0020, R = 0x0010,
                       CU = 0x0008, CD = 0x0004, CL = 0x0002, CR = 0x0001;
}

static std::mutex g_input_mutex;
static uint16_t g_input_buttons = 0;
static float g_input_x = 0.0f, g_input_y = 0.0f;
SDL_GameController* g_controller = nullptr;   // exposed for overlay::draw_controls_tab
static std::atomic<bool> g_rumble_want{false};
static bool g_rumble_on = false;

const char* input_current_controller_name() {
    return g_controller ? SDL_GameControllerName(g_controller) : nullptr;
}

static void input_open_controller() {
    if (g_controller) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i) && (g_controller = SDL_GameControllerOpen(i))) {
            fprintf(stderr, "[INPUT] controller: %s\n", SDL_GameControllerName(g_controller));
            return;
        }
    }
}

static void input_handle_event(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        input_open_controller();
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_controller &&
               e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_controller))) {
        SDL_GameControllerClose(g_controller);
        g_controller = nullptr;
        g_rumble_on = false;
        fprintf(stderr, "[INPUT] controller removed\n");
        input_open_controller();
    }
}

// Radial deadzone, rescaled so the stick still reaches full deflection.
static void apply_deadzone(float& x, float& y, float dz) {
    float m = sqrtf(x * x + y * y);
    if (m < dz) { x = y = 0.0f; return; }
    float k = std::min(1.0f, (m - dz) / (1.0f - dz)) / m;
    x *= k; y *= k;
}

static void input_sample() {
    using namespace n64btn;
    uint16_t btn = 0;
    float x = 0.0f, y = 0.0f;

    // Menu navigation must never leak into game controls, including a manual
    // pause with no menu open. Guest execution is gated separately.
    if (overlay::is_visible() || game_pause::is_paused()) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_buttons = 0;
        g_input_x = 0.0f;
        g_input_y = 0.0f;
        return;
    }

    const InputBindings& ib = g_settings.bindings;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    // Returns true if the scancode sc is bound and currently pressed.
    auto kb = [&](int sc) { return sc >= 0 && k[static_cast<SDL_Scancode>(sc)]; };
    auto act = [&](N64Action a) -> const N64Binding& { return ib.actions[int(a)]; };

    // Keyboard
    if (kb(act(N64Action::A).key))     btn |= A;
    if (kb(act(N64Action::B).key))     btn |= B;
    if (kb(act(N64Action::Z).key))     btn |= Z;
    // Enter is Start, except as part of Alt+Enter (fullscreen toggle).
    if (kb(act(N64Action::Start).key) && !(SDL_GetModState() & KMOD_ALT)) btn |= START;
    if (kb(act(N64Action::L).key))     btn |= L;
    if (kb(act(N64Action::R).key))     btn |= R;
    if (kb(act(N64Action::CUp).key))   btn |= CU;
    if (kb(act(N64Action::CDown).key)) btn |= CD;
    if (kb(act(N64Action::CLeft).key)) btn |= CL;
    if (kb(act(N64Action::CRight).key))btn |= CR;
    if (kb(act(N64Action::DUp).key))   btn |= DU;
    if (kb(act(N64Action::DDown).key)) btn |= DD;
    if (kb(act(N64Action::DLeft).key)) btn |= DL;
    if (kb(act(N64Action::DRight).key))btn |= DR;
    // Analog stick: configurable primary key + arrow keys as fixed fallback.
    if (kb(act(N64Action::StickUp).key)    || k[SDL_SCANCODE_UP])    y += 1.0f;
    if (kb(act(N64Action::StickDown).key)  || k[SDL_SCANCODE_DOWN])  y -= 1.0f;
    if (kb(act(N64Action::StickRight).key) || k[SDL_SCANCODE_RIGHT]) x += 1.0f;
    if (kb(act(N64Action::StickLeft).key)  || k[SDL_SCANCODE_LEFT])  x -= 1.0f;
    if (x != 0.0f && y != 0.0f) { x *= 0.7071f; y *= 0.7071f; }

    // Game controller: digital buttons read from the binding table;
    // Z and C-buttons stay axis-driven (right stick / triggers).
    if (SDL_GameController* c = g_controller) {
        auto cb = [c](int id) {
            return id >= 0 && SDL_GameControllerGetButton(
                c, static_cast<SDL_GameControllerButton>(id)) != 0;
        };
        auto ax = [c](SDL_GameControllerAxis id) {
            return SDL_GameControllerGetAxis(c, id) / 32767.0f;
        };
        if (cb(act(N64Action::A).btn))      btn |= A;
        if (cb(act(N64Action::B).btn))      btn |= B;
        if (cb(act(N64Action::Start).btn))  btn |= START;
        if (cb(act(N64Action::L).btn))      btn |= L;
        if (cb(act(N64Action::R).btn))      btn |= R;
        if (cb(act(N64Action::DUp).btn))    btn |= DU;
        if (cb(act(N64Action::DDown).btn))  btn |= DD;
        if (cb(act(N64Action::DLeft).btn))  btn |= DL;
        if (cb(act(N64Action::DRight).btn)) btn |= DR;
        // Axis-driven inputs: always active alongside any custom button binding.
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 0.3f) btn |= Z;
        if (ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f) btn |= R;
        float rx = ax(SDL_CONTROLLER_AXIS_RIGHTX), ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
        if (ry < -0.5f) btn |= CU;
        if (ry > 0.5f)  btn |= CD;
        if (rx < -0.5f) btn |= CL;
        if (rx > 0.5f)  btn |= CR;
        float lx = ax(SDL_CONTROLLER_AXIS_LEFTX), ly = -ax(SDL_CONTROLLER_AXIS_LEFTY);
        if (ib.invert_x) lx = -lx;
        if (ib.invert_y) ly = -ly;
        apply_deadzone(lx, ly, ib.deadzone);
        if (lx != 0.0f || ly != 0.0f) { x = lx; y = ly; }

        // Rumble requested by the game thread; re-sent every 500 ms while on
        // because SDL rumble expires after the given duration.
        static Uint32 rumble_refresh = 0;
        bool want = g_rumble_want.load();
        if (want != g_rumble_on || (want && SDL_GetTicks() - rumble_refresh > 500)) {
            SDL_GameControllerRumble(c, want ? 0xC000 : 0, want ? 0xC000 : 0, want ? 1000 : 0);
            g_rumble_on = want;
            rumble_refresh = SDL_GetTicks();
        }
    }

    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_input_buttons = btn;
    g_input_x = x;
    g_input_y = y;
}

static void input_poll() {}

static bool input_get(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (controller_num != 0) return false;
    std::lock_guard<std::mutex> lock(g_input_mutex);
    *buttons = g_input_buttons;
    *x = g_input_x;
    *y = g_input_y;
    return true;
}

static void input_set_rumble(int controller_num, bool rumble) {
    if (controller_num == 0) g_rumble_want.store(rumble);
}

static ultramodern::input::connected_device_info_t input_get_connected(int controller_num) {
    if (controller_num == 0) {
        return { ultramodern::input::Device::Controller, ultramodern::input::Pak::RumblePak };
    }
    return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
}

// ---- Events ----------------------------------------------------------

static std::atomic<bool> game_started{false};

static void gfx_init_callback() {
    fprintf(stderr, "[RECOMP] gfx_init_callback — starting game\n"); fflush(stderr);
    if (!game_started.exchange(true)) {
        recomp::start_game(u8"yoshis_story", "");
    }
}

static void on_init(uint8_t* rdram, recomp_context* ctx) {
    g_rdram = rdram;
    fprintf(stderr, "[RECOMP] on_init_callback — ROM loaded, overlays init done, entrypoint starting (rdram=%p)\n", rdram); fflush(stderr);

    // N64 IPL3 sets $sp = 0xA4001FF0 (end of IMEM).  recomp_context is zero-initialized,
    // so provide a valid stack pointer; top of 8MB RDRAM is safe scratch space.
    ctx->r29 = (gpr)(int32_t)0x807FFFF0;

    // do_rom_read copies 1 MB of ROM (ROM offset 0x1000) into RDRAM[0x80000400..0x80100400].
    // This overwrites .main's BSS (0x800B4CB0..0x80112CA0, bss_size from
    // yamls/us/main.yaml) with raw ROM bytes. The entry code at 0x80000400
    // (makerom entry.s, not recompiled) zeros BSS before jumping to bootproc;
    // replicate that here.
    constexpr uint32_t bss_start = 0x800B4CB0;
    constexpr uint32_t bss_size  = 0x05DFF0;
    memset(rdram + rdram_off(bss_start), 0, bss_size);

#ifndef _WIN32
    // librecomp reserves 4 GB of address space but only makes the first 512 MB readable/writable.
    // N64 hardware registers (SP DMEM/IMEM, VI, AI, …) live at 0xA4xxxxxx → rdram+0x24000000.
    // The ROM cartridge is at physical 0x10000000 (kseg1: 0xB0000000) → rdram+0x30000000.
    // Extend the accessible window to cover all 4 GB so none of these accesses segfault.
    // All pages beyond 512 MB are lazy-allocated (MAP_ANON), so mprotect costs no physical RAM.
    constexpr size_t base_mem = 512ULL * 1024 * 1024;         // 512 MB already R/W
    constexpr size_t total    = 4096ULL * 1024 * 1024;        // 4 GB total reservation
    int mp_ret = mprotect(rdram + base_mem, total - base_mem, PROT_READ | PROT_WRITE);
    if (mp_ret != 0) {
        fprintf(stderr, "[RECOMP] mprotect(rdram+512MB, 3.5GB, RW) failed (errno=%d)\n", errno); fflush(stderr);
    }

    // Alias kseg1 onto kseg0. On hardware 0xA0xxxxxx and 0x80xxxxxx are the same
    // physical RAM; in the recomp they are rdram+0x20xxxxxx and rdram+0x00xxxxxx of
    // one private mapping, i.e. separate memory. The game writes its audio command
    // lists through uncached 0xA0... pointers (OSTask data_ptr 0xA01E2830) while the
    // RSP reads physical addresses (the kseg0 view), so the microcode saw zeros and
    // mixed silence. Back both 8 MB windows with the same shared memory.
    {
        constexpr size_t ram_size = 8u * 1024 * 1024;
        int fd = memfd_create("yoshi_rdram", 0);
        bool ok = fd >= 0 && ftruncate(fd, ram_size) == 0;
        ok = ok && pwrite(fd, rdram, ram_size, 0) == (ssize_t)ram_size;
        ok = ok && mmap(rdram, ram_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == rdram;
        ok = ok && mmap(rdram + 0x20000000, ram_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_FIXED, fd, 0) == rdram + 0x20000000;
        if (fd >= 0) close(fd);
        fprintf(stderr, "[RECOMP] kseg1 alias of 8 MB RDRAM: %s (errno=%d)\n", ok ? "OK" : "FAILED", errno);
        fflush(stderr);
    }

#else
    // The Windows allocator establishes both shared views before startup.
    fprintf(stderr, "[RECOMP] kseg1 alias of 8 MB RDRAM: OK (Windows shared views)\n");
#endif

    // Boot-state anti-piracy check. Idle_ThreadEntry reads the first word of
    // RSP DMEM (0xA4000000) and IMEM (0xA4001000), which IPL3 leaves holding
    // known values on real hardware, into 0x80108628 / 0x80108624. Later:
    //   func_80051504 only initialises a cast entry's type if DMEM[0] == -1
    //     (otherwise castManager trips Fault_HungUp castManager.c:588);
    //   func_80051DA0 sets the flag at 0x800A5AD0 only if IMEM[0] == 0x17D8.
    // Nothing populates SP memory in the recomp, so seed what IPL3 leaves.
    // kseg1 0xA4xxxxxx maps to rdram + 0x24xxxxxx (made writable above).
    *reinterpret_cast<uint32_t*>(rdram + 0x24000000) = 0xFFFFFFFFu;  // DMEM[0]
    *reinterpret_cast<uint32_t*>(rdram + 0x24001000) = 0x000017D8u;  // IMEM[0]
    // More of the same, found by scanning recomp_funcs for reads of boot
    // parameters (0x800003xx), low RAM via kseg1 (0xA0000xxx) and SP memory:
    //   func_800687B8 (Main_InitSubsystems): osCicId == 0x17D8 -> 0x800A6EB4,
    //     *(0xA0000164) == 0x01EC6021 -> 0x800A6EB8;
    //   func_8005D420: *(0xA00002B8) == 0x8941680C -> 0x800A62F0.
    // librecomp leaves osCicId unset. kseg1 0xA0xxxxxx is a separate mirror at
    // rdram + 0x20xxxxxx in the recomp, so those words are written there.
    *reinterpret_cast<uint32_t*>(rdram + 0x00000310) = 0x000017D8u;  // osCicId
    *reinterpret_cast<uint32_t*>(rdram + 0x20000164) = 0x01EC6021u;
    *reinterpret_cast<uint32_t*>(rdram + 0x200002B8) = 0x8941680Cu;

    // Map the ROM at rdram+0x10000000 (kseg0: 0x90000000) and rdram+0x30000000 (kseg1: 0xB0000000)
    // so that game code that reads directly from the cartridge address space gets real ROM data.
    // Recompiled code keeps memory as native-endian 32-bit words (MEM_W reads a native u32,
    // MEM_B uses addr ^ 3), while get_rom() is big-endian bytes, so swap each word.
    // Anti-piracy: func_8006F500 reads ROM 0x458/0x45C through 0xB0000000 and
    // func_80064AD4 compares them with 0x0260BCD5; on mismatch func_80064920 replaces the
    // game's controller input with a blank record every frame (input ignored).
    std::span<const uint8_t> rom = recomp::get_rom();
    if (!rom.empty()) {
        for (size_t i = 0; i + 4 <= rom.size(); i += 4) {
            uint32_t w = (uint32_t(rom[i]) << 24) | (uint32_t(rom[i + 1]) << 16) |
                         (uint32_t(rom[i + 2]) << 8) | uint32_t(rom[i + 3]);
            *reinterpret_cast<uint32_t*>(rdram + 0x10000000 + i) = w;
            *reinterpret_cast<uint32_t*>(rdram + 0x30000000 + i) = w;
        }
        uint32_t chk = ((*reinterpret_cast<uint32_t*>(rdram + 0x30000458) & 0xFFFF) << 16) |
                       (*reinterpret_cast<uint32_t*>(rdram + 0x3000045C) & 0xFFFF);
        fprintf(stderr, "[RECOMP] ROM mapped at 0xB0000000; cart check word = 0x%08X (%s)\n",
                chk, chk == 0x0260BCD5u ? "OK" : "expected 0x0260BCD5");
        fflush(stderr);
    }
}

static void on_thread_create(uint8_t*, recomp_context*) {}

// ---- Error handling --------------------------------------------------

static void message_box(const char* msg) {
    fprintf(stderr, "[RECOMP] %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Yoshi's Story Recomp", msg, g_sdl_window);
}

// ---- Main ------------------------------------------------------------

#ifndef _WIN32
static void crash_handler(int sig, siginfo_t* si, void*) {
    void* bt[64];
    int n = backtrace(bt, 64);
    fprintf(stderr, "\n[CRASH] Signal %d", sig);
    if (si && (sig == SIGSEGV || sig == SIGBUS)) {
        uintptr_t fault = (uintptr_t)si->si_addr;
        fprintf(stderr, " fault_addr=%p si_code=%d", si->si_addr, si->si_code);
        // si_code: 1=SEGV_MAPERR (unmapped), 2=SEGV_ACCERR (bad perms), 3=SEGV_BNDERR, 4=SEGV_PKUERR
        if (g_rdram) {
            intptr_t off = (intptr_t)fault - (intptr_t)g_rdram;
            if (off >= 0 && off < (intptr_t)(4096ULL*1024*1024))
                fprintf(stderr, " vram=0x%08X", (uint32_t)(0x80000000u + (uint32_t)off));
            else
                fprintf(stderr, " (rdram+0x%tx)", (ptrdiff_t)off);
        }
    }
    fprintf(stderr, "\n");
    backtrace_symbols_fd(bt, n, fileno(stderr));
    // Print rdram mapping state from /proc/self/maps
    if (g_rdram) {
        int fd = open("/proc/self/maps", O_RDONLY);
        if (fd >= 0) {
            static char mapbuf[131072];
            ssize_t nread = read(fd, mapbuf, sizeof(mapbuf) - 1);
            close(fd);
            if (nread > 0) {
                mapbuf[nread] = '\0';
                uintptr_t base = (uintptr_t)g_rdram;
                fprintf(stderr, "[MAPS] g_rdram=%p, rdram-adjacent entries:\n", g_rdram);
                char* line = mapbuf;
                while (line && *line) {
                    char* nl = strchr(line, '\n'); if (nl) *nl = '\0';
                    unsigned long long s, e;
                    if (sscanf(line, "%llx-%llx", &s, &e) == 2) {
                        bool near_rdram = (s < base + (4096ULL<<20) && e > base);
                        if (near_rdram) fprintf(stderr, "  %s\n", line);
                    }
                    line = nl ? nl+1 : nullptr;
                }
            }
        }
    }
    fflush(stderr);
    _exit(1);
}
#else
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* info) {
    fprintf(stderr, "\n[CRASH] Windows exception 0x%08lX at %p\n",
        info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    fprintf(stderr, "[CRASH] executable base=%p, fault RVA=0x%llX\n", GetModuleHandleW(nullptr),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)
            - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))));
    platform::print_backtrace();
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif
#ifdef _WIN32
    SetUnhandledExceptionFilter(crash_handler);
#else
    struct sigaction sa{};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    // SIGUSR1 → each receiving thread prints its own backtrace.
    {
        struct sigaction su{};
        su.sa_sigaction = [](int, siginfo_t*, void*) {
            void* bt[64];
            int n = backtrace(bt, 64);
            char hdr[64];
            int hn = snprintf(hdr, sizeof(hdr), "\n[BT tid=%d]\n", (int)syscall(SYS_gettid));
            write(2, hdr, hn);
            backtrace_symbols_fd(bt, n, 2);
        };
        su.sa_flags = SA_SIGINFO;
        sigaction(SIGUSR1, &su, nullptr);
    }
#endif

    // Watchdog: YOSHI_WATCH_ADDRS=0x800C8900,... logs every change to those
    // RDRAM words (VRAM addresses, e.g. an actor's +0x20 proc). Polling
    // (100us) can miss values that change back within one interval.
    if (getenv("YOSHI_WATCH_ADDRS")) std::thread([]{
        ultramodern::set_native_thread_name("rdram watch");
        while (!g_rdram) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        struct Field { std::string name; uint32_t vram; uint32_t last; };
        std::vector<Field> fields;
        if (const char* list = getenv("YOSHI_WATCH_ADDRS")) {
            std::string s(list);
            size_t pos = 0;
            while (pos < s.size()) {
                size_t comma = s.find(',', pos);
                std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                uint32_t vram = (uint32_t)strtoul(item.c_str(), nullptr, 0) & ~3u;
                if (vram_ok(vram)) fields.push_back({ item, vram, 0 });
                else fprintf(stderr, "[WATCH] ignoring bad address '%s'\n", item.c_str());
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
        auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            for (Field& f : fields) {
                uint32_t v = *(const volatile uint32_t*)(g_rdram + rdram_off(f.vram));
                if (v == f.last) continue;
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                fprintf(stderr, "[WATCH @%llduS] %s 0x%08X -> 0x%08X\n",
                        (long long)us, f.name.c_str(), f.last, v);
                fflush(stderr);
                f.last = v;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }).detach();

    // RT64 uses Vulkan natively via SDL; no X11/XWayland hint needed.
    // App id for Wayland (and WM class on X11): desktops take the window icon
    // from the matching yoshis-story-recomp.desktop (tools/install_desktop.sh).
    // SDL_APP_ID is the SDL3 / sdl2-compat name, the others are SDL2's.
    SDL_SetHint("SDL_APP_ID", "yoshis-story-recomp");
    SDL_SetHint("SDL_VIDEO_WAYLAND_WMCLASS", "yoshis-story-recomp");
    SDL_SetHint("SDL_VIDEO_X11_WMCLASS", "yoshis-story-recomp");
    SDL_SetHint("SDL_APP_NAME", "Yoshi's Story Recomp");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Open audio at the default N64 audio frequency.
    // The game will call audio_set_frequency() with the real rate shortly after boot;
    // having the device open early avoids a gap on the first queue_samples call.
    open_audio_device(g_audio_freq);

    // Register the section/overlay table from recomp_overlays.inl
    recomp::overlays::register_overlays(
        { section_table, ARRLEN(section_table), num_sections },
        { overlay_sections_by_index, ARRLEN(overlay_sections_by_index) }
    );

    // Register the game
    recomp::register_game(recomp::GameEntry{
        .rom_hash          = 0xf1d8a6737dda5b82ULL,
        .internal_name     = "YOSHI'S STORY",
        .display_name      = "Yoshi's Story",
        .game_id           = u8"yoshis_story",
        // 16 Kbit EEPROM: the game calls eepmgr_Create(..., type 2 =
        // EEPROM_TYPE_16K, ...) and eepmgr_Probe parks its thread forever
        // (LOOP() -> pause_self) if osEepromProbe reports anything else.
        .save_type         = recomp::SaveType::Eep16k,
        .is_enabled        = true,
        // ROM 0x1000 maps to VRAM 0x80000400 (our code section starts at ROM 0x1070 → VRAM 0x80000470,
        // so 0x1000 → 0x80000470 - 0x70 = 0x80000400). librecomp's init() does:
        //   load_overlays(0x1000, entrypoint_address, 1MB)
        // which maps each section at: section.rom_addr - 0x1000 + entrypoint_address.
        // For our section: 0x1070 - 0x1000 + 0x80000400 = 0x80000470 ✓
        .entrypoint_address = (gpr)(int32_t)0x80000400,
        .entrypoint        = bootproc,
        .thread_create_callback = on_thread_create,
        .on_init_callback       = on_init,
    });

    // Data folder (ROM at yoshis_story.z64, saves, settings.ini, mods.json):
    //   YOSHI_DATA_DIR if set, else data/ next to the executable (a flat
    //   portable folder), else ../data from it (this repo's build/ + data/).
    // Resolve the executable with the platform API, so symlinks,
    // PATH lookups and any working directory all resolve the same way.
    std::filesystem::path exe_dir;
    {
        const auto exe = platform::executable_path();
        exe_dir = exe.empty() ? std::filesystem::absolute(argv[0]).parent_path() : exe.parent_path();
    }
    std::filesystem::path data_path;
    if (const char* dir = getenv("YOSHI_DATA_DIR"); dir && dir[0]) {
        data_path = dir;
    } else if (std::filesystem::is_directory(exe_dir / "data")) {
        data_path = exe_dir / "data";
    } else {
        data_path = exe_dir / ".." / "data";
    }
    auto config_path = std::filesystem::weakly_canonical(data_path);
    {
        std::error_code ec;
        std::filesystem::create_directories(config_path, ec);
    }
    fprintf(stderr, "[RECOMP] config_path = %s\n", config_path.string().c_str()); fflush(stderr);
    fprintf(stderr, "[RECOMP] ROM path = %s (exists=%d)\n",
        (config_path / "yoshis_story.z64").string().c_str(),
        (int)std::filesystem::exists(config_path / "yoshis_story.z64")); fflush(stderr);
    recomp::register_config_path(config_path);

    // Display settings: data/settings.ini, edited in the launcher window
    // (skipped when "show at startup" is off; YOSHI_LAUNCHER=1 forces it,
    // YOSHI_LAUNCHER=0 skips it), then YOSHI_* environment overrides.
    g_settings_path = config_path / "settings.ini";
    settings_load(g_settings_path);
    // Seed the audio atomics from the loaded settings so volume is correct
    // before the first sample arrives.
    g_audio_volume.store(std::clamp(g_settings.volume, 0.0f, 1.0f), std::memory_order_release);
    g_audio_muted.store(g_settings.muted, std::memory_order_release);
    {
        const char* launcher = getenv("YOSHI_LAUNCHER");
        const bool show = launcher ? launcher[0] != '0' : g_settings.showLauncher;
        if (show && !run_launcher(g_settings_path)) {
            fprintf(stderr, "[RECOMP] quit from the launcher\n");
            return 0;
        }
    }
    settings_apply_env();
    // The launcher consumed the startup controller-added events.
    input_open_controller();

    recomp::Configuration cfg{
        .argc = argc,
        .argv = argv,
        .project_version = { 0, 1, 0 },
        .rsp_callbacks = { .get_rsp_microcode = get_rsp_microcode },
        .renderer_callbacks = { .create_render_context = create_render_context },
        .audio_callbacks = {
            .queue_samples       = audio_queue_samples,
            .get_frames_remaining = audio_get_frames_remaining,
            .set_frequency       = audio_set_frequency,
        },
        .input_callbacks = {
            .poll_input          = input_poll,
            .get_input           = input_get,
            .set_rumble          = input_set_rumble,
            .get_connected_device_info = input_get_connected,
        },
        .gfx_callbacks = {
            .create_gfx    = create_gfx,
            .create_window = create_window,
            .update_gfx    = update_gfx,
        },
        .events_callbacks = {
            .vi_callback      = nullptr,
            .gfx_init_callback = gfx_init_callback,
        },
        .error_handling_callbacks = {
            .message_box = message_box,
        },
    };

    fprintf(stderr, "[RECOMP] calling recomp::start\n"); fflush(stderr);
    recomp::start(cfg);
    fprintf(stderr, "[RECOMP] recomp::start returned\n"); fflush(stderr);

    if (g_audio_device != 0) {
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    SDL_DestroyWindow(g_sdl_window);
    SDL_Quit();
    return 0;
}
