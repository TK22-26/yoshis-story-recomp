cmake_minimum_required(VERSION 3.20)
project(yoshis_story_recomp)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)

set(N64MR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../N64ModernRuntime")
set(RT64_PATH  "${CMAKE_CURRENT_SOURCE_DIR}/rt64")

# Build RT64 as a static library using the SDL/Vulkan window path.
# RT64 does not use X11 at runtime, but SDL's SDL_syswm.h unconditionally
# includes X11/Xlib.h (sdl2-compat is built with X11 support enabled).
# Both libX11 headers (Xlib.h) and xorgproto headers (X11/X.h) are required.
# Pass them via add_compile_options() before add_subdirectory so RT64 sees them.

# Prefer explicit hints (pass -DX11_INCLUDE_DIR=... and -DXORGPROTO_INCLUDE_DIR=...
# on the cmake command line if FindX11 can't locate them automatically).
find_package(X11 QUIET)

# xorgproto provides X11/X.h, which is a separate package from libX11 on NixOS.
if(NOT XORGPROTO_INCLUDE_DIR)
    find_path(XORGPROTO_INCLUDE_DIR "X11/X.h"
        HINTS ${X11_INCLUDE_DIR}
        PATH_SUFFIXES "include")
endif()
if(NOT XORGPROTO_INCLUDE_DIR AND EXISTS "/nix/store")
    # NixOS-specific fallback: glob for the xorgproto package.
    file(GLOB _xproto_paths "/nix/store/*-xorgproto-*/include")
    list(GET _xproto_paths 0 XORGPROTO_INCLUDE_DIR)
endif()
if(NOT X11_INCLUDE_DIR AND EXISTS "/nix/store")
    file(GLOB _x11_paths "/nix/store/*-libx11-*-dev/include")
    list(GET _x11_paths 0 X11_INCLUDE_DIR)
endif()

# FindX11 can return a list of directories (one per X11 lib on NixOS), so
# use include_directories() rather than a single -I flag.
if(X11_INCLUDE_DIR)
    include_directories(${X11_INCLUDE_DIR})
endif()
if(XORGPROTO_INCLUDE_DIR)
    include_directories(${XORGPROTO_INCLUDE_DIR})
endif()
message(STATUS "X11 include: ${X11_INCLUDE_DIR}")
message(STATUS "xorgproto include: ${XORGPROTO_INCLUDE_DIR}")

set(RT64_STATIC ON CACHE BOOL "" FORCE)
set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
# Local RT64 fixes kept as patches (upstream RT64 lacks them), applied to the
# submodule at configure time unless already applied. See patches/rt64/.
file(GLOB RT64_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/patches/rt64/*.patch")
list(SORT RT64_PATCHES)
# A patch that applies cleanly is applied; one that doesn't is assumed to be
# applied already (a reverse check can fail once a later patch edits nearby
# lines). After changing a patch: git -C rt64 checkout -- . and re-run cmake.
foreach(RT64_PATCH ${RT64_PATCHES})
    execute_process(COMMAND git apply --ignore-space-change --check "${RT64_PATCH}"
        WORKING_DIRECTORY "${RT64_PATH}" RESULT_VARIABLE RT64_PATCH_APPLIES OUTPUT_QUIET ERROR_QUIET)
    if(RT64_PATCH_APPLIES EQUAL 0)
        execute_process(COMMAND git apply --ignore-space-change "${RT64_PATCH}"
            WORKING_DIRECTORY "${RT64_PATH}" RESULT_VARIABLE RT64_PATCH_RESULT)
        if(NOT RT64_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${RT64_PATCH} to ${RT64_PATH}")
        endif()
        message(STATUS "Applied RT64 patch ${RT64_PATCH}")
    else()
        message(STATUS "RT64 patch already applied: ${RT64_PATCH}")
    endif()
endforeach()

# Share the game-specific edge rule with RT64 and the standalone regression test.
configure_file(src/yoshi_texture_bounds.h "${CMAKE_BINARY_DIR}/src/yoshi_texture_bounds.h" COPYONLY)
add_subdirectory("${RT64_PATH}" rt64_build)

add_subdirectory("${N64MR_PATH}/ultramodern" ultramodern_build)
add_subdirectory("${N64MR_PATH}/librecomp" librecomp_build)

# Compile the recompiled game functions (C99, one translation unit per file).
# N64Recomp emits funcs_N.c files; the count scales with recompiled-function
# count (sweep sized ~2700 size-0 symbols → 31 files). Glob picks them all up.
file(GLOB GAME_FUNC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp_funcs/funcs_*.c")
if(NOT GAME_FUNC_SOURCES)
    message(FATAL_ERROR "recomp_funcs/ has no generated code. Build N64Recomp "
        "standalone and run it first (see README: Regenerating the recompiled code).")
endif()
add_library(game_funcs STATIC ${GAME_FUNC_SOURCES})
target_include_directories(game_funcs PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp_funcs"
    "${N64MR_PATH}/N64Recomp/include"
    "${N64MR_PATH}/librecomp/include/librecomp"
)
set_target_properties(game_funcs PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
# -fno-strict-aliasing is REQUIRED: N64Recomp emits MEM_W/MEM_HU/MEM_B as
# `*(int32_t*)` / `*(uint16_t*)` / `*(int8_t*)` casts to the same underlying
# rdram bytes. GCC/Clang under strict aliasing treat those pointer types as
# non-aliasing and may elide a MEM_HU reload after a MEM_W store — silently
# propagating stale register values across the generated function body
# (e.g. a1=0x40B9 stored as word, then read back as halfword returning 0).
target_compile_options(game_funcs PRIVATE
    $<IF:$<CONFIG:Debug>,-O0,-O2>
    -fno-strict-aliasing
    -Wno-unused-variable -Wno-unused-but-set-variable)
# Prototypes for N64Recomp-ignored functions the generated code calls; see
# src/ignored_funcs.h. Force-included so regeneration never drops them.
target_compile_options(game_funcs PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/ignored_funcs.h")

# Main executable
add_executable(yoshis_story_recomp
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    # Settings window shown before the game (Dear ImGui from RT64, drawn with
    # SDL_Renderer; RT64 itself only builds the Vulkan backend).
    "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher.cpp"
    # In-game settings overlay (ImGui drawn through RT64's Inspector). See
    # docs/OVERLAY_PLAN.md.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/overlay.cpp"
    "${RT64_PATH}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp"
)
# rsp/aspMain.cpp is generated from the ROM at configure time and is not
# committed. On first configure, we build RSPRecomp from N64ModernRuntime and
# run gen_aspmain.py. Subsequent configures skip this if the file already exists.
set(ASP_MAIN_CPP "${CMAKE_CURRENT_SOURCE_DIR}/rsp/aspMain.cpp")
set(YOSHI_ROM    "${CMAKE_CURRENT_SOURCE_DIR}/data/yoshis_story.z64")

if(NOT EXISTS "${ASP_MAIN_CPP}")
    if(NOT EXISTS "${YOSHI_ROM}")
        message(FATAL_ERROR
            "ROM not found at data/yoshis_story.z64\n"
            "Copy your yoshis_story.z64 there before running cmake.\n"
            "The ROM is needed once to generate the recompiled audio microcode.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_rsprecomp_build "${CMAKE_BINARY_DIR}/rsprecomp_build")
    message(STATUS "Building RSPRecomp (first configure only)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${N64MR_PATH}/N64Recomp"
            -B "${_rsprecomp_build}"
            -DCMAKE_BUILD_TYPE=Release
            --no-warn-unused-cli
        RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "RSPRecomp configure failed")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${_rsprecomp_build}" --target RSPRecomp -j
        RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "RSPRecomp build failed")
    endif()
    set(_rsprecomp_bin "${_rsprecomp_build}/RSPRecomp")
    if(NOT EXISTS "${_rsprecomp_bin}")
        message(FATAL_ERROR "RSPRecomp built but binary not found at ${_rsprecomp_bin}")
    endif()
    message(STATUS "Generating rsp/aspMain.cpp from ROM audio microcode...")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_aspmain.py"
            --rom "${YOSHI_ROM}"
            --rsprecomp "${_rsprecomp_bin}"
        RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "gen_aspmain.py failed")
    endif()
    if(NOT EXISTS "${ASP_MAIN_CPP}")
        message(FATAL_ERROR "gen_aspmain.py ran but rsp/aspMain.cpp was not created")
    endif()
    message(STATUS "rsp/aspMain.cpp generated successfully")
endif()

target_sources(yoshis_story_recomp PRIVATE "${ASP_MAIN_CPP}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
    set_source_files_properties("${ASP_MAIN_CPP}"
        PROPERTIES COMPILE_OPTIONS "-msse4.1")
endif()
target_compile_definitions(yoshis_story_recomp PRIVATE YOSHI_HAVE_ASPMAIN)
target_include_directories(yoshis_story_recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp_funcs"
    "${N64MR_PATH}/N64Recomp/include"
    "${N64MR_PATH}/librecomp/include"
    "${N64MR_PATH}/ultramodern/include"
    "${N64MR_PATH}/thirdparty"
    # RT64 public headers
    "${RT64_PATH}/src"
    "${RT64_PATH}/src/contrib"
    "${RT64_PATH}/src/contrib/plume"
    "${RT64_PATH}/src/contrib/imgui"
    "${RT64_PATH}/src/contrib/hlslpp/include"
)
# RT64 requires C++17 but our project uses C++20 — that's fine.
# Definitions that RT64's CMake sets globally must also apply to our code
# since we include RT64's headers directly.
target_compile_definitions(yoshis_story_recomp PRIVATE
    PLUME_SDL_VULKAN_ENABLED
    RT64_SDL_WINDOW_VULKAN
    # RT64 uses HLSL_CPU to enable CPU-side HLSL type definitions (interop namespace etc.)
    HLSL_CPU
    # RT64 uses FFX_GCC for the AMD FidelityFX math headers
    FFX_GCC
    # RT64 disables the Vulkan prototypes (loaded dynamically via volk)
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
)
target_compile_options(yoshis_story_recomp PRIVATE -O2)
# Export symbols (-rdynamic) so backtrace_symbols() can name recompiled
# game functions in crash and Fault_HungUp reports.
set_target_properties(yoshis_story_recomp PROPERTIES ENABLE_EXPORTS ON)
# RT64 dlopen()s the Vulkan loader at runtime (volk) instead of linking it,
# so put the loader's directory in the executable's RUNPATH: the game then
# starts outside nix-shell too (desktop entry, file manager). GPU drivers are
# still found through /run/opengl-driver as usual on NixOS.
find_library(YOSHI_VULKAN_LOADER NAMES vulkan libvulkan.so.1)
if(YOSHI_VULKAN_LOADER)
    get_filename_component(YOSHI_VULKAN_LOADER_DIR "${YOSHI_VULKAN_LOADER}" REALPATH)
    get_filename_component(YOSHI_VULKAN_LOADER_DIR "${YOSHI_VULKAN_LOADER_DIR}" DIRECTORY)
    set_property(TARGET yoshis_story_recomp APPEND PROPERTY BUILD_RPATH "${YOSHI_VULKAN_LOADER_DIR}")
endif()
target_link_libraries(yoshis_story_recomp PRIVATE
    game_funcs
    librecomp
    ultramodern
    rt64
    SDL2::SDL2
    pthread
)

include(CTest)
if(BUILD_TESTING)
    add_executable(texture_bounds_test tests/texture_bounds_test.cpp)
    target_include_directories(texture_bounds_test PRIVATE src)
    add_test(NAME texture_bounds COMMAND texture_bounds_test)
endif()
