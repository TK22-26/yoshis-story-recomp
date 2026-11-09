cmake_minimum_required(VERSION 3.20)
project(yoshis_story_recomp VERSION 1.0.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 2.26 REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Windows builds require x64 Clang (clang-cl) and the Windows SDK.")
    endif()
    add_compile_definitions(NOMINMAX SDL_MAIN_HANDLED _CRT_SECURE_NO_WARNINGS)
endif()

set(N64MR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../N64ModernRuntime" CACHE PATH "N64ModernRuntime checkout")
if(NOT EXISTS "${N64MR_PATH}/librecomp/CMakeLists.txt")
    message(FATAL_ERROR "N64ModernRuntime was not found. See README or pass -DN64MR_PATH=...")
endif()
set(RT64_PATH  "${CMAKE_CURRENT_SOURCE_DIR}/rt64")

# Build RT64 as a static library using the SDL/Vulkan window path.
# RT64 does not use X11 at runtime, but SDL's SDL_syswm.h unconditionally
# includes X11/Xlib.h (sdl2-compat is built with X11 support enabled).
# Both libX11 headers (Xlib.h) and xorgproto headers (X11/X.h) are required.
# Pass them via add_compile_options() before add_subdirectory so RT64 sees them.

# Prefer explicit hints (pass -DX11_INCLUDE_DIR=... and -DXORGPROTO_INCLUDE_DIR=...
# on the cmake command line if FindX11 can't locate them automatically).
if(UNIX AND NOT APPLE)
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
    if(_xproto_paths)
        list(GET _xproto_paths 0 XORGPROTO_INCLUDE_DIR)
    endif()
endif()
if(NOT X11_INCLUDE_DIR AND EXISTS "/nix/store")
    file(GLOB _x11_paths "/nix/store/*-libx11-*-dev/include")
    if(_x11_paths)
        list(GET _x11_paths 0 X11_INCLUDE_DIR)
    endif()
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
endif()

set(RT64_STATIC ON CACHE BOOL "" FORCE)
if(WIN32)
    set(RT64_SDL_WINDOW_VULKAN OFF CACHE BOOL "" FORCE)
else()
    set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
endif()
# Local RT64 fixes kept as patches (upstream RT64 lacks them), applied to the
# submodule at configure time unless already applied. See patches/rt64/.
file(GLOB RT64_PATCHES "${CMAKE_CURRENT_SOURCE_DIR}/patches/rt64/*.patch")
list(SORT RT64_PATCHES)
# Ignore context whitespace so Windows CRLF checkouts work. A failed forward
# check is only accepted when the reverse check confirms the patch is present.
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
        execute_process(COMMAND git apply --ignore-space-change --reverse --check "${RT64_PATCH}"
            WORKING_DIRECTORY "${RT64_PATH}" RESULT_VARIABLE RT64_PATCH_REVERSES OUTPUT_QUIET ERROR_QUIET)
        if(NOT RT64_PATCH_REVERSES EQUAL 0)
            message(FATAL_ERROR "RT64 patch does not apply and is not already present: ${RT64_PATCH}")
        endif()
        message(STATUS "RT64 patch already applied: ${RT64_PATCH}")
    endif()
endforeach()

# The game-specific RT64 patch and standalone regression test share this rule.
# RT64 already includes the generated-header directory below.
configure_file(src/yoshi_texture_bounds.h "${CMAKE_BINARY_DIR}/src/yoshi_texture_bounds.h" COPYONLY)
add_subdirectory("${RT64_PATH}" rt64_build)

if(WIN32)
    set(_runtime_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/runtime/0001-windows-rdram.patch")
    execute_process(COMMAND git apply --reverse --check "${_runtime_patch}"
        WORKING_DIRECTORY "${N64MR_PATH}" RESULT_VARIABLE _already_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT _already_applied EQUAL 0)
        execute_process(COMMAND git apply "${_runtime_patch}"
            WORKING_DIRECTORY "${N64MR_PATH}" RESULT_VARIABLE _patch_result)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "Cannot patch N64ModernRuntime. Use the pinned revision in README.")
        endif()
    endif()
endif()

# The pause patch is platform independent; no game bytes are embedded.
set(_pause_patch "${CMAKE_CURRENT_SOURCE_DIR}/patches/runtime/0002-game-pause.patch")
execute_process(COMMAND git apply --ignore-space-change --reverse --check "${_pause_patch}"
    WORKING_DIRECTORY "${N64MR_PATH}" RESULT_VARIABLE _pause_applied OUTPUT_QUIET ERROR_QUIET)
if(NOT _pause_applied EQUAL 0)
    execute_process(COMMAND git apply --ignore-space-change "${_pause_patch}"
        WORKING_DIRECTORY "${N64MR_PATH}" RESULT_VARIABLE _pause_result)
    if(NOT _pause_result EQUAL 0)
        message(FATAL_ERROR "Cannot apply game pause patch to N64ModernRuntime.")
    endif()
endif()
add_subdirectory("${N64MR_PATH}/ultramodern" ultramodern_build)
target_sources(ultramodern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game_pause.cpp")
target_include_directories(ultramodern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
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
if(MSVC)
    target_compile_options(game_funcs PRIVATE
        $<IF:$<CONFIG:Debug>,/Od,/O2> /clang:-fno-strict-aliasing
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/ignored_funcs.h")
else()
    target_compile_options(game_funcs PRIVATE
        $<IF:$<CONFIG:Debug>,-O0,-O2> -fno-strict-aliasing
        -Wno-unused-variable -Wno-unused-but-set-variable
        -include "${CMAKE_CURRENT_SOURCE_DIR}/src/ignored_funcs.h")
endif()

# Main executable
add_executable(yoshis_story_recomp
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_file.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rom_setup.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_style.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/settings_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_images.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/controller_devices.cpp"
    # Shared settings persistence and SDL window icon.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher.cpp"
    # In-game settings overlay (ImGui drawn through RT64's Inspector). See
    # docs/OVERLAY_PLAN.md.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/overlay.cpp"
    "${RT64_PATH}/src/contrib/imgui/backends/imgui_impl_sdlrenderer2.cpp"
)
# Embed a fixed allowlist of approved UI artwork; no game assets are read here.
set(UI_IMAGES_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_images_data.h")
add_custom_command(OUTPUT "${UI_IMAGES_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ui_images.py"
        --output "${UI_IMAGES_HEADER}"
    DEPENDS tools/embed_ui_images.py assets/icon.png assets/github-mark-white.svg
        assets/controller-xbox.svg assets/controller-playstation.svg
    VERBATIM)
target_sources(yoshis_story_recomp PRIVATE "${UI_IMAGES_HEADER}")
configure_file(src/version.h.in generated/version.h @ONLY)
target_include_directories(yoshis_story_recomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(yoshi_build_identity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/write_build_identity.py"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated/build_identity.h"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/build_identity.h"
    VERBATIM)
add_dependencies(yoshis_story_recomp yoshi_build_identity)
# Build the audio recompiler with the same generator/toolchain, then generate
# only into the build directory. Neither the ROM nor generated audio is shipped.
set(YOSHI_ROM "${CMAKE_CURRENT_SOURCE_DIR}/data/yoshis_story.z64" CACHE FILEPATH "User-supplied US ROM")
if(NOT EXISTS "${YOSHI_ROM}")
    message(FATAL_ERROR "Copy your US ROM to data/yoshis_story.z64 or set -DYOSHI_ROM=...")
endif()
set(ASP_MAIN_CPP "${CMAKE_CURRENT_BINARY_DIR}/rsp/aspMain.cpp")
add_custom_command(OUTPUT "${ASP_MAIN_CPP}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_aspmain.py"
        --rom "${YOSHI_ROM}" --rsprecomp "$<TARGET_FILE:RSPRecomp>"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}/rsp"
    DEPENDS RSPRecomp "${YOSHI_ROM}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_aspmain.py"
    VERBATIM)
target_sources(yoshis_story_recomp PRIVATE "${ASP_MAIN_CPP}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
    if(MSVC)
        set_source_files_properties("${ASP_MAIN_CPP}" PROPERTIES COMPILE_OPTIONS "/clang:-msse4.1")
    else()
        set_source_files_properties("${ASP_MAIN_CPP}" PROPERTIES COMPILE_OPTIONS "-msse4.1")
    endif()
endif()

if(WIN32)
    enable_language(RC)
    set(YOSHI_ICON "${CMAKE_CURRENT_BINARY_DIR}/generated/yoshi.ico")
    add_custom_command(OUTPUT "${YOSHI_ICON}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_windows_icon.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png" "${YOSHI_ICON}"
        DEPENDS tools/make_windows_icon.py assets/icon.png VERBATIM)
    configure_file(src/windows.rc.in generated/app_resources.rc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/app_resources.rc"
        PROPERTIES OBJECT_DEPENDS "${YOSHI_ICON}")
    target_sources(yoshis_story_recomp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/app_resources.rc" "${YOSHI_ICON}")
    set_target_properties(yoshis_story_recomp PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(yoshis_story_recomp PRIVATE /ENTRY:mainCRTStartup)
    target_sources(librecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/windows_memory.cpp")
    target_include_directories(librecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(librecomp PRIVATE YOSHI_WINDOWS_RDRAM)
    target_link_libraries(librecomp PRIVATE synchronization onecore)
    target_include_directories(yoshis_story_recomp PRIVATE "${RT64_PATH}/src/contrib/dxc/inc")
    target_link_libraries(yoshis_story_recomp PRIVATE dwmapi ole32 shell32 uuid)
    target_compile_options(yoshis_story_recomp PRIVATE /Zi)
    target_link_options(yoshis_story_recomp PRIVATE /DEBUG /OPT:REF /OPT:ICF)
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
    # RT64 uses HLSL_CPU to enable CPU-side HLSL type definitions (interop namespace etc.)
    HLSL_CPU
    # RT64 uses FFX_GCC for the AMD FidelityFX math headers
    FFX_GCC
    # RT64 disables the Vulkan prototypes (loaded dynamically via volk)
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
)
if(NOT WIN32)
    target_compile_definitions(yoshis_story_recomp PRIVATE PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
endif()
if(MSVC)
    target_compile_options(yoshis_story_recomp PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(yoshis_story_recomp PRIVATE $<$<NOT:$<CONFIG:Debug>>:-O2>)
endif()
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
    Threads::Threads
)

if(WIN32)
    # Ship the release CRT next to the executable, as in JFG's portable bundle.
    find_path(YOSHI_VC_RUNTIME_DIR msvcp140.dll
        HINTS "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
    if(NOT YOSHI_VC_RUNTIME_DIR)
        message(FATAL_ERROR "Run from the VS x64 developer shell or set YOSHI_VC_RUNTIME_DIR to the x64 Microsoft.VC143.CRT redistributable directory.")
    endif()
    set(YOSHI_VC_RUNTIME_FILES concrt140.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll
        msvcp140_atomic_wait.dll msvcp140_codecvt_ids.dll vccorlib140.dll
        vcruntime140.dll vcruntime140_1.dll vcruntime140_threads.dll)
    set(YOSHI_VC_RUNTIME_PATHS)
    foreach(_dll IN LISTS YOSHI_VC_RUNTIME_FILES)
        if(NOT EXISTS "${YOSHI_VC_RUNTIME_DIR}/${_dll}")
            message(FATAL_ERROR "Missing redistributable library: ${YOSHI_VC_RUNTIME_DIR}/${_dll}")
        endif()
        list(APPEND YOSHI_VC_RUNTIME_PATHS "${YOSHI_VC_RUNTIME_DIR}/${_dll}")
    endforeach()
    # RT64 dynamically loads DXC, including when its Vulkan backend is used.
    add_custom_command(TARGET yoshis_story_recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:yoshis_story_recomp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RT64_PATH}/src/contrib/dxc/bin/x64/dxcompiler.dll"
            "${RT64_PATH}/src/contrib/dxc/bin/x64/dxil.dll"
            "$<TARGET_FILE_DIR:yoshis_story_recomp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${YOSHI_VC_RUNTIME_PATHS}
            "$<TARGET_FILE_DIR:yoshis_story_recomp>"
        VERBATIM)
    install(TARGETS yoshis_story_recomp RUNTIME DESTINATION . COMPONENT Runtime)
    install(FILES "$<TARGET_FILE:SDL2::SDL2>"
        "${RT64_PATH}/src/contrib/dxc/bin/x64/dxcompiler.dll"
        "${RT64_PATH}/src/contrib/dxc/bin/x64/dxil.dll" DESTINATION . COMPONENT Runtime)
    install(FILES ${YOSHI_VC_RUNTIME_PATHS} DESTINATION . COMPONENT Runtime)
    # Create an empty data folder. Never package the source/local data folder.
    install(DIRECTORY DESTINATION data COMPONENT Runtime)
    install(FILES README.md THIRD_PARTY_NOTICES.md CHANGELOG.md "START HERE.txt" DESTINATION . COMPONENT Runtime)
    install(DIRECTORY licenses/ DESTINATION licenses COMPONENT Runtime FILES_MATCHING PATTERN "*.txt")
    set(CPACK_GENERATOR ZIP)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL Runtime)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    set(CPACK_PACKAGE_FILE_NAME "yoshis-story-recomp-windows-x64")
    include(CPack)

    include(CTest)
    if(BUILD_TESTING)
        add_executable(rom_file_test tests/rom_file_test.cpp src/rom_file.cpp)
        target_include_directories(rom_file_test PRIVATE src)
        add_test(NAME rom_file COMMAND rom_file_test)
        add_executable(windows_memory_test tests/windows_memory_test.cpp src/windows_memory.cpp)
        target_include_directories(windows_memory_test PRIVATE src)
        target_link_libraries(windows_memory_test PRIVATE onecore)
        add_test(NAME windows_rdram_alias COMMAND windows_memory_test)
        add_executable(game_pause_test tests/game_pause_test.cpp src/game_pause.cpp)
        target_include_directories(game_pause_test PRIVATE src)
        target_link_libraries(game_pause_test PRIVATE Threads::Threads)
        add_test(NAME game_pause COMMAND game_pause_test)
        add_executable(texture_bounds_test tests/texture_bounds_test.cpp)
        target_include_directories(texture_bounds_test PRIVATE src)
        add_test(NAME texture_bounds COMMAND texture_bounds_test)
        add_executable(input_bindings_test tests/input_bindings_test.cpp)
        target_include_directories(input_bindings_test PRIVATE src)
        add_test(NAME input_bindings COMMAND input_bindings_test)
        add_executable(controller_devices_test tests/controller_devices_test.cpp src/controller_devices.cpp)
        target_include_directories(controller_devices_test PRIVATE src)
        target_link_libraries(controller_devices_test PRIVATE SDL2::SDL2)
        add_test(NAME controller_devices COMMAND controller_devices_test)
    endif()
endif()
