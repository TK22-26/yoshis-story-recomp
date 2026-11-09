# Yoshi's Story Recomp

[![Windows checks](https://github.com/TK22-26/yoshis-story-recomp/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/TK22-26/yoshis-story-recomp/actions/workflows/windows.yml)

A native **Windows x64** recompilation of Yoshi's Story for Nintendo 64.
**Version 1.0.1 supports the original USA ROM.** Open the app, select your ROM
in the native file picker, and play. No developer tools or first-run compilation
are needed. This version replaces the withdrawn 1.0.0 download.

[Windows downloads](https://github.com/TK22-26/yoshis-story-recomp/releases) |
[Install and play](#getting-started-on-windows) | [Controls](#controls) |
[Changelog](CHANGELOG.md) | [Release verification and limits](https://github.com/TK22-26/yoshis-story-recomp/blob/main/docs/releases/1.0.1.md)

This is TK22-26's Windows fork of
[damobeanrod's Linux port](https://github.com/damobeanrod/yoshis-story-recomp),
built with [N64Recomp](https://github.com/N64Recomp/N64Recomp),
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), and
[RT64](https://github.com/rt64/rt64). The Windows work uses AI assistance under
maintainer direction, review, and playtesting; upstream authorship is credited below.

**No ROMs or extracted game assets are included.** Textures, audio, models, and
other game data come from your own local ROM. The package contains compiled
programs, host libraries, documentation, and the approved original/interface art
listed in [assets/README.md](https://github.com/TK22-26/yoshis-story-recomp/blob/main/assets/README.md).

## Windows features

| Feature | Details |
| --- | --- |
| Native rendering | Direct3D 12 by default, Vulkan fallback, resolution scaling, anti-aliasing, filtering, and optional CRT effects |
| Desktop interface | Compact top menu, green theme, native owned settings dialogs, installed Windows fonts, and DPI-aware diagrams |
| Window modes | Windowed, borderless window, and borderless fullscreen with corrected monitor coverage |
| Controls | Saved controller selection; keyboard, button, trigger, and stick-direction rebinding; one controller input per action |
| Controller glyphs | Xbox labels and PlayStation shapes, triangular triggers, and rectangular bumpers |
| Playback controls | Pause/resume, mute/unmute, persistent volume, and F1 menu visibility |
| Rendering correction | Storybook texture-boundary fix tested on Windows and separately against upstream Linux source |
| Portable package | Runtime DLLs, setup guide, notices, source identity, SHA-256 checksum, and per-file inventory |

## Requirements

- A Yoshi's Story (US) ROM: `yoshis_story.z64`
- A Direct3D 12-capable GPU/driver on Windows, or Vulkan for the fallback renderer and Linux
- Windows 10 version 1803 or newer, x64; or Linux (Ubuntu 24.04+ or equivalent; NixOS also supported)

## Windows validation

Built and exercised on Windows 11 x64 with Direct3D 12. Checked startup from a
separate working directory, keyboard/controller input, EEPROM save creation,
owned settings windows, pause/resume, restart, and all three window modes.
Fullscreen client/frame bounds match the physical monitor; exiting restores
the previous window size and style. Automated tests cover RDRAM aliases,
the pause clock, texture boundaries, axis rebinding and persistence, and SDL
controller selection/reconnect without a ROM. Release checks additionally verify
the package inventory, checksums, and compiled source identity.

The storybook seam correction was also tested separately on upstream Linux
source under Ubuntu 26.04 / WSL2 / Vulkan, after reproducing the seams in the
official Linux v1.0.0 AppImage. It clamps single-tile mirrored CI4 panels at
their loaded image bounds, preventing filtering into unused TMEM while
preserving intentional texture repetition. This is a game-specific rendering
correction, not a general change to N64 texture-addressing rules.

This is an initial Windows port, not a complete campaign playthrough. The
complete Windows fork on Linux and the Windows Vulkan backend have not been
retested.

## Getting started on Windows

1. Download `Yoshis-Story-1.0.1-windows-x64.zip` from the
   [releases page](https://github.com/TK22-26/yoshis-story-recomp/releases).
2. Extract the entire ZIP into a writable folder. Keep all DLLs beside
   `yoshis_story_recomp.exe`.
3. Open `yoshis_story_recomp.exe`. Select your own USA ROM in the Windows file
   picker (`.z64`, `.n64`, or `.v64`). Its name and location do not need to change.
4. The app validates the ROM, stores a private local copy in `data/`, and starts
   the game. Later launches use that copy automatically. Cancel exits cleanly.

Use **Game > Select ROM...** to select a ROM again. Unsupported or modified ROMs
are rejected without changing the selected file. Display settings are available
from **Video > Video settings...**; there is no startup settings launcher.

The package includes `START HERE.txt` and the Microsoft Visual C++ runtime.
Git, Python, Visual Studio, and WSL are not needed to play. The app opens without a console; diagnostics are written
to `data/runtime.log` (replaced on each launch). Only the USA ROM is supported.

```text
yoshis_story_recomp.exe
SDL2.dll
dxcompiler.dll
dxil.dll
msvcp140*.dll, vcruntime140*.dll, other runtime DLLs
data/
  yoshis_story.z64       <- local copy imported from your selected ROM
  saves/                <- created by the game
```

Use a current GPU driver. The Windows renderer chooses Direct3D 12 by default
and can fall back to Vulkan. The game reserves a 4 GiB address range; Windows needs about
4 GiB of available memory/page-file commit, although physical pages are
populated as they are used.

Before updating, back up `data/`. Extract a new version into a separate folder
and copy your existing data folder across to retain the ROM, saves, and settings.
The release's empty data folder contains no personal files.

Each release has a matching `.sha256` file and `.inventory.json`. To check your
ZIP in PowerShell, run `Get-FileHash .\Yoshis-Story-1.0.1-windows-x64.zip -Algorithm SHA256`
and compare it with the checksum file. Maintainer preparation and publication
are described in [the release guide](https://github.com/TK22-26/yoshis-story-recomp/blob/main/docs/releases/maintenance.md).

## Build from source on Windows

Prerequisites:

- Git, Python 3.9+, CMake 3.24+, and Ninja.
- Visual Studio 2022 or Build Tools with **Desktop development with C++**, an
  x64 MSVC toolchain, and a Windows 10/11 SDK.
- LLVM 20.1.8 or newer with `clang-cl`, `clang`, and `lld-link`. The helper
  searches the normal LLVM installation and Visual Studio tool directories.

From PowerShell:

```powershell
git clone --recursive https://github.com/TK22-26/yoshis-story-recomp.git
cd yoshis-story-recomp
powershell -ExecutionPolicy Bypass -File tools/build_windows.ps1 -Rom 'C:\Games\YoshisStoryUS.z64' -Package
.\build\yoshis_story_recomp.exe
```

The helper downloads SDL 2.32.10 with a pinned SHA-256, checks out
N64ModernRuntime at `cdf5abbd5026fef5c364c676e4667c45e42b6863` under `.deps/`,
builds the game and RSP audio recompiler, and runs the five runtime/input tests.
The ROM is copied only to the ignored local `data/` directory. Generated
audio code stays under `build/rsp/`.

`-Package` creates `dist/yoshis-story-recomp-windows-x64.zip`. Packaging uses
an explicit executable/DLL/documentation list and creates an empty `data/`
folder; it never copies your ROM, saves, settings, or generated source files.
`tools/check_package.py` rejects files outside the program/documentation allowlist;
the CRT libraries are required members.

For an existing dependency setup, run from an x64 developer shell with LLVM
and Ninja on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DN64MR_PATH=C:/src/N64ModernRuntime -DSDL2_DIR=C:/deps/SDL2-2.32.10/cmake
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -B dist -C Release
```

Windows CI runs the memory, pause, texture-boundary, input-binding, virtual-controller,
and release-metadata checks without a ROM. A full game
build and gameplay test require a locally supplied ROM. Runtime changes are
reproducible patches in `patches/runtime/`; Linux retains its existing memory
and SDL/Vulkan paths.

## Getting started on Linux — upstream AppImage

Download the original Linux AppImage from the [upstream releases](https://github.com/damobeanrod/yoshis-story-recomp/releases), make it
executable, then set up your files next to it:

```
yoshis-story-recomp-x86_64.AppImage   ← the download
data/
  yoshis_story.z64                     ← your ROM goes here
  saves/                               ← created automatically on first run
```

```bash
chmod +x yoshis-story-recomp-x86_64.AppImage
./yoshis-story-recomp-x86_64.AppImage
```

The AppImage is self-contained and portable — move the AppImage and its
`data/` folder together anywhere on your system and it will work. No
installation required. Requires SDL2, GTK3, and a Vulkan driver on the
host system (present by default on Ubuntu 24.04+).

---

## Getting started — build from source (Ubuntu 24.04+)

### 1. Install dependencies

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 \
    libsdl2-dev libvulkan-dev libgtk-3-dev libx11-dev libxkbcommon-dev \
    libwayland-dev
```

> Ubuntu 22.04 ships SDL 2.0.20 which is too old. Use 24.04 or later.

### 2. Clone

```bash
git clone --recursive https://github.com/TK22-26/yoshis-story-recomp
cd yoshis-story-recomp
```

N64ModernRuntime must sit **next to** this repo as a sibling folder:

```bash
cd ..
git clone https://github.com/N64Recomp/N64ModernRuntime
git -C N64ModernRuntime checkout cdf5abbd5026fef5c364c676e4667c45e42b6863
git -C N64ModernRuntime submodule update --init --recursive
cd yoshis-story-recomp
```

### 3. Add your ROM

```bash
cp /path/to/yoshis_story.z64 data/
```

### 4. Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)
./build/yoshis_story_recomp
```

### Optional: app menu entry

```bash
tools/install_desktop.sh
```

Installs `~/.local/share/applications/yoshis-story-recomp.desktop`. On
Wayland the window icon is matched by app id `yoshis-story-recomp`; on
X11 it is set directly.

---

## NixOS

```bash
nix-shell tools/recomp-shell.nix
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)
./build/yoshis_story_recomp
```

The binary links against Nix store paths and runs outside the shell on the
machine it was built on until `nix-collect-garbage` removes those paths;
rebuild if that happens.

---

## Building a release AppImage (Ubuntu 24.04)

AppImages are not committed to the repo. To produce one:

### 1. Build the binary

Inside an Ubuntu 24.04 environment (distrobox works well):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)
```

### 2. Download the packaging tools

```bash
mkdir -p appimage-tools
wget -O appimage-tools/linuxdeploy \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -O appimage-tools/appimagetool \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimage-tools/linuxdeploy appimage-tools/appimagetool
```

### 3. Assemble the AppDir

```bash
mkdir -p AppDir/usr/bin
cp build/yoshis_story_recomp AppDir/usr/bin/
cp assets/icon.png AppDir/yoshis-story-recomp.png

cat > AppDir/yoshis-story-recomp.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=Yoshi's Story Recomp
Exec=yoshis_story_recomp
Icon=yoshis-story-recomp
Categories=Game;
EOF

cat > AppDir/AppRun << 'EOF'
#!/bin/bash
APPDIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH}"
if [ -z "$YOSHI_DATA_DIR" ] && [ -n "$APPIMAGE" ]; then
    export YOSHI_DATA_DIR="$(dirname "$(readlink -f "$APPIMAGE")")/data"
fi
exec "${APPDIR}/usr/bin/yoshis_story_recomp" "$@"
EOF
chmod +x AppDir/AppRun
```

### 4. Create the AppImage

Run `appimagetool` from the host (it needs the `file` command, which may not be
in a distrobox). On NixOS: `nix-shell -p file --run "..."`.

```bash
APPIMAGE_EXTRACT_AND_RUN=1 appimage-tools/appimagetool \
    AppDir yoshis-story-recomp-x86_64.AppImage
```

The resulting AppImage requires SDL2, GTK3, and a Vulkan driver on the target
system — all present by default on Ubuntu 24.04+. See the file layout in
**Getting started — AppImage** above.

---

## Data folder

The game stores everything in one folder: the ROM (`yoshis_story.z64`),
saves (`saves/`), `settings.ini` and `mods.json`. The folder is located
in this order:

1. `YOSHI_DATA_DIR` environment variable, if set
2. `data/` next to the executable
3. `../data/` relative to the executable (so `build/` inside the repo works)

The executable's real path is used, so symlinks and PATH launches work.
RT64's own config (`rt64.json`) lives in `%LOCALAPPDATA%/yoshis_story_recomp/`
on Windows and `~/.config/.yoshis_story_recomp/` on Linux.

---

## Controls

SDL-supported gamepads are selectable in Controls and work alongside the
keyboard. Device availability and feature support depend on the SDL mapping. Rumble is forwarded to the controller.

| N64 | Keyboard | Controller (Xbox layout) |
| --- | --- | --- |
| Stick | WASD / arrow keys | Left stick |
| A | Space | A |
| B | Left Shift | X |
| Z | Z | Left trigger |
| Start | Enter | Start |
| L / R | Q / E | LB / RB |
| C buttons | I J K L | Right stick |
| D-pad | T F G H | D-pad |

Use **Controllers > Controller mapping** to remap inputs, or **Esc** /
controller **Back** to open settings. Settings are native windows owned by the
game window. They pause the game until closed, preserving a manual pause.

Video, Audio, Controls, and About use the supplied settings designs: rounded
dark-green cards, segmented options, switches, and controller glyphs drawn in
code. Settings use installed Segoe UI and Segoe UI Semibold on Windows, with
no bundled fonts. The controller card uses the supplied Xbox and DualSense
diagrams, and About uses the project egg PNG and GitHub's official mark.
Only these approved UI images are embedded; game assets remain excluded.
Changes save while the dialog is open; anti-aliasing
still requires a restart. Controls supports scrolling, keyboard/controller
rebinding, and reset to defaults. Every controller cell accepts a button,
trigger, or signed stick direction. Release held inputs before moving the
desired control; Esc cancels. Rebinding replaces the previous controller
assignment while keeping its keyboard key. Each action accepts only one
controller input; R defaults to RB. Axis mappings and direction are
saved with the other settings. About links to the projects and keeps the
credits and shortcut list accessible from its footer.

The Controls device dropdown lists the PC's connected gamepads. Selecting one
switches both gameplay and settings navigation to that device and remembers
the choice. Its diagram follows the controller type; the status dot sits
directly below the dropdown. A selected device that disconnects stays selected
until it returns, with keyboard input still available. **Automatic** uses an
available controller. Devices that expose neither a serial number nor a stable
path are remembered by player slot or enumeration order.

The desktop menu uses JFG's compact 24 dp bar and 26 dp dropdown rows with a
dark green, cream, and soft green palette. Windows uses an installed Segoe UI
font and per-monitor DPI awareness; no font assets are bundled. **Game >
Pause game / Resume game** or the **Pause** key freezes/resumes guest execution,
audio, and game timers while the menu stays usable. **Audio > Mute / Unmute**
or **Ctrl+M** toggles mute. **Alt+F4** quits.
**F1** hides or shows the menu bar in every window mode. Without an explicit
F1 choice, the bar hides in borderless fullscreen until settings or pause is opened.

---

## Display settings

Open **Video > Video settings...** in the game for resolution
(native 320×240, fit window, or 2×–8×), supersampling, MSAA, 2D
upscaling, output filter, CRT scanline filter, overscan crop and
window mode. Settings are saved to `data/settings.ini`.

**Video** offers three window modes: **Windowed**, **Borderless window**
(the current window size without its title bar), and **Borderless fullscreen**
(the entire monitor, including the taskbar area). Fullscreen removes the
windowed DWM border and rounded corners on Windows.

In-game: **F11** or **Alt+Enter** toggles fullscreen and restores the previous
windowed or borderless-window style on exit. **F10** cycles the
CRT filter (off → 0.25 → 0.5 → 1.0). Both are remembered.

Environment variable overrides (one run only, not saved):

| Variable | Effect |
| --- | --- |
| `YOSHI_RES=native\|window\|N` | 320×240, fit window, or N × 240p (e.g. `4` = 1280×960) |
| `YOSHI_SSAA=N` | Render N× larger and downsample (fixed resolution only) |
| `YOSHI_MSAA=0\|2\|4\|8` | Multisample anti-aliasing |
| `YOSHI_UPSCALE2D=all\|scaled\|original` | Which 2D elements render at the upscaled resolution |
| `YOSHI_FILTER=pixel\|nearest\|linear` | Final image scaling filter |
| `YOSHI_CRT=0..1` | CRT filter strength (needs ≥720 px tall window) |
| `YOSHI_CROP=N` | Framebuffer pixels hidden per edge (default 2; 0 = off) |
| `YOSHI_FULLSCREEN=0\|1` | Start fullscreen or windowed |

---

## Regenerating the recompiled code

`recomp_funcs/` is pre-generated and committed — most users never need to
touch it. If you need to regenerate (e.g. after a config or ELF change):

```bash
# Paths assume sibling layout; adjust if yours differs
ELF=../yoshis-story-matching/build/yoshisstory-us.elf
ROM=../yoshis-story-matching/baseroms/us/baserom.z64

cmake -S ../N64ModernRuntime/N64Recomp -B build-n64recomp -DCMAKE_BUILD_TYPE=Release
cmake --build build-n64recomp --target N64Recomp -j$(nproc)

python3 tools/check_elf_matches.py --elf $ELF --rom $ROM   # must print OK
python3 tools/gen_function_sizes.py --elf $ELF --config config.toml
python3 tools/regen.py --n64recomp build-n64recomp/N64Recomp
```

The decomp ELF comes from [decompals/yoshis-story](https://github.com/decompals/yoshis-story)
built in MATCHING mode (`make` must end with `build/yoshisstory-us.z64: OK`).
Never hand-edit `recomp_funcs/`; regeneration overwrites all changes. Fixes
to generated code go in `tools/fix_*.py` or `src/ignored_funcs.h`.

---

## Credits

### Tools this port is built on

**[Mr-Wiseguy](https://github.com/Mr-Wiseguy)** created
[N64Recomp](https://github.com/N64Recomp/N64Recomp) and
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) — the
static recompilation framework that makes native N64 ports like this one
possible. Without this work, this project could not exist.

**[Dario](https://github.com/DarioSamo)** and the
[RT64 team](https://github.com/rt64/rt64/graphs/contributors) built the
renderer that drives all 3D and 2D output.

**The [decompals/yoshis-story](https://github.com/decompals/yoshis-story)
team** produced the matching decompilation of the game whose ELF was used
to generate `recomp_funcs/`.

### Upstream Linux port

The original port was directed by **damobeanrod** (concept, testing, and engineering
decisions) and implemented almost entirely by
**[Claude](https://claude.ai)** (Anthropic's AI assistant), which wrote the
C++ runtime glue, ROM picker, settings windows, audio, input, RT64 patches,
Python tooling, and CMake build system through an extended interactive
session.

### Windows fork

**[TK22-26](https://github.com/TK22-26)** directed and reviewed the Windows port,
settings designs, controller diagrams, and local playtesting with **OpenAI Codex**
assistance. The desktop menu and release process follow the maintainer's
[Jet Force Gemini Recomp](https://github.com/TK22-26/JetForceGemini-Recomp) project.

The original project egg is generated by `tools/gen_icon.py`. GitHub's mark and
the supplied controller diagrams are documented in [assets/README.md](https://github.com/TK22-26/yoshis-story-recomp/blob/main/assets/README.md).

### Third-party libraries

- [N64Recomp / N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) — Mr-Wiseguy
- [RT64](https://github.com/rt64/rt64) — Dario and contributors
- [SDL2](https://www.libsdl.org/)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [NanoSVG](https://github.com/memononen/nanosvg)

---

## Licensing and IP disclaimer

Yoshi's Story and all related names, characters, and assets are the property
of Nintendo. This project does not include, distribute, or grant rights to
the game ROM or any Nintendo-owned assets. You must supply your own legally
obtained copy of the game.

This repository contains `recomp_funcs/`, which is machine-generated C code
produced by N64Recomp from a decompilation ELF. It follows the same
precedent as other public N64 recompilation projects.
