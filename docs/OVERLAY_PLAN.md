> Historical upstream plan. This fork now uses a compact desktop menu and native
> owned settings windows with cooperative game pause. See the README for current controls.

# In-game settings overlay — implementation plan

Target: an in-game ImGui settings overlay inspired by [ThatGuyMcd/DKR-R](https://github.com/ThatGuyMcd/DKR-R).
Opens with **ESC** (keyboard) or **Back / Select** (controller), pauses input while visible, applies live.

## Approach decision

**No RT64 patch.** RT64's `Inspector` is a public, user-constructible struct
([rt64/src/gui/rt64_inspector.h](../rt64/src/gui/rt64_inspector.h)) that RT64 only
drives when `userConfig->developerMode == true`
([rt64/src/hle/rt64_state.cpp:1653](../rt64/src/hle/rt64_state.cpp#L1653)). With
developer mode off, we construct our own `RT64::Inspector`, assign it to
`application.presentQueue->inspector`, and drive its `newFrame()` /
`endFrame()` ourselves — RT64's present path
([rt64/src/hle/rt64_present_queue.cpp:363](../rt64/src/hle/rt64_present_queue.cpp#L363))
still composites the resulting ImDrawData onto the game's swapchain.

This is exactly how DKR-R does it; see
`runtime-recomp/src/game/runtime_ui.cpp` in that repo
(`attach` ~L9301, `detach` ~L9333, `draw` ~L9337 — their `inspector->newFrame`
is ~L9410, `endFrame` ~L9652).

**patches/rt64/ stays at 3 tracked + the untracked 0004.** Overlay adds no
new patch.

## Files to add

- `src/overlay.h` — public API (attach/detach/draw/handle_event/toggle/is_visible)
- `src/overlay.cpp` — implementation (~250 lines target)

## Files to edit

- `src/main.cpp`:
  - include `overlay.h`
  - after `apply_display_settings(app.get())` (around line 559): `overlay::attach(*app)`
  - inside `shutdown()`: `overlay::detach(*app)`
  - after `app->updateScreen()` (line 663 inside `update_screen()`): `overlay::draw(*app)`
  - in `update_gfx()` (line 719+) at the top of the SDL event loop:
    give `overlay::handle_event(&e)` first dibs; if it returns true, continue
    to next event (skip `input_handle_event`).
  - toggle logic: in `handle_event`, ESC key (SDL_SCANCODE_ESCAPE) and
    `SDL_CONTROLLER_BUTTON_BACK` flip visibility.
- `CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/src/overlay.cpp` to the
  `add_executable(yoshis_story_recomp ...)` list (after launcher.cpp line 114).

## Public API (overlay.h)

```cpp
#pragma once
union SDL_Event;
namespace RT64 { struct Application; }

namespace overlay {
    void attach(RT64::Application& app);   // after RT64 setup succeeds
    void detach(RT64::Application& app);   // during shutdown
    void draw(RT64::Application& app);     // after app->updateScreen()
    bool handle_event(SDL_Event* event);   // true = consumed, skip game input
    void toggle();
    bool is_visible();
}
```

## Pause behavior

Simplest minimum: when visible, `handle_event` returns `true` for all
`SDL_KEY*`, `SDL_CONTROLLER*`, `SDL_JOY*` events, so the game sees no input
and effectively "pauses" (characters stop, menus stop advancing). Audio keeps
playing. Not touching the N64 scheduler avoids deadlock risk.

Future: silence audio while visible via SDL audio callback gate.

## Tabs (confirmed layout)

| Tab | V1 content | Status |
|---|---|---|
| **Display** | Resolution, SSAA, MSAA, Upscale 2D, Filter, CRT, Crop, Fullscreen — all live, reusing widgets from `launcher.cpp` | Working |
| **Audio** | "Coming soon" placeholder | Phase 2 |
| **Controls** | "Coming soon" placeholder — N64ModernRuntime has the engine | Phase 2 |
| **Saves** | "Coming soon" placeholder — backup/restore `yoshis_story.bin` | Phase 2 |
| **Texture Packs** | "Coming soon" placeholder — RT64 ships the system | Phase 2/3 |
| **Mods** | "Coming soon" placeholder — `data/mods.json` exists but unused | Long-term |
| **HUD Layout** | "Coming soon" placeholder — low priority but kept visible | Long-term |
| **About** | Version, RT64 commit, GitHub link, patch list | Working |

Explicitly **dropped**: Netplay. Explicitly **excluded** (not applicable
to Yoshi): Custom Tracks, Character Select, Camera Tuning, Gyro Steering,
Modern/Accurate preset.

## Live-apply hooks

Settings changes while overlay is visible call:
- `apply_display_settings(&app)` for resolution/SSAA/MSAA/upscale2D/filter
- `app.enhancementConfig.presentation.crtStrength = ...` + `app.updateEnhancementConfig()` for CRT
- `app.enhancementConfig.presentation.edgeCrop = ...` + `app.updateEnhancementConfig()` for crop
- `SDL_SetWindowFullscreen(...)` for fullscreen
- `settings_save(g_settings_path)` on close

These are the same hooks `update_gfx()`'s F10 / F11 handlers already use —
no new plumbing needed.

## Implementation order

1. **Scaffold**: overlay.h/.cpp with empty Inspector lifecycle + ESC toggle +
   a blank modal window. Verify game still boots and overlay toggles.
2. **Tabs scaffold**: tab bar + 8 tab stubs; About tab filled.
3. **Display tab**: port widgets from launcher.cpp (resolution, SSAA, MSAA,
   upscale 2D, filter, CRT, crop, fullscreen) with live apply.
4. **Pause gate**: input swallow when visible.
5. **Polish**: controller nav, Save/Cancel wiring, settings.ini write on close.

## Risk notes

- **Don't touch RT64 render/VI/send_dl code.** The 99%-working renderer stays
  as-is. All overlay work is in new files + three small main.cpp hook calls.
- **Thread safety**: use `std::scoped_lock(presentQueue->inspectorMutex)`
  around every `inspector->newFrame`/`endFrame`. DKR-R enforces this; copy
  the pattern exactly.
- **inspector->handleSdlEvent**: route SDL events (that we want ImGui to see)
  through this so ImGui's input state updates. ESC toggle is checked BEFORE
  routing so we can still close the overlay with ESC even if ImGui wants to
  consume it.
- **Developer mode must stay off** (default). If it ever gets turned on,
  RT64 will also try to drive the Inspector — results undefined.

## Status (2026-10-14)

**Phase 1 SHIPPED.** Overlay opens on Esc/F1/Back, pauses audio and swallows
game input, Display tab is live with every launcher widget, 7 placeholder
tabs plus About + credits, Restart (execvp) + Exit buttons in sidebar.

Build is clean and runs. User-confirmed working in-game.

### Known issue — font size

User reports the overlay font looks the same size regardless of what
`io.FontGlobalScale` is set to (tested 1.55 → 1.95 → 2.6, no visible change).
Buttons / spacing / window all scale correctly; only the font text does not.

**Hypothesis**: ImGui's default `AddFontDefault()` is a 13 px bitmap
(ProggyClean) and `FontGlobalScale` only up-samples it. The user's monitor
probably has HiDPI enabled, so the SDL2 ImGui backend may be dividing by
device pixel ratio somewhere, cancelling the scale. OR the atlas is already
pre-built by RT64's `Inspector` ctor with a baked size and `FontGlobalScale`
only affects runtime post-scaling that visually gets snapped.

**Fix for next session** — add a bigger TTF font to the atlas at `attach()`
time BEFORE RT64 uploads the atlas to Vulkan, then PushFont / PopFont around
`draw_overlay_window()`. Pattern:

```cpp
// In overlay::attach, before constructing Inspector:
ImFontConfig cfg;
cfg.SizePixels = 26.0f;   // or 32 for extra large
io.Fonts->AddFontDefault(&cfg);   // appends as fonts[1]
// Then PushFont(io.Fonts->Fonts[1]) in draw_overlay_window.
```

Caveat: the atlas must not be built yet when we add fonts. The Inspector
ctor in [rt64/src/gui/rt64_inspector.cpp](../rt64/src/gui/rt64_inspector.cpp)
does `ImGui_ImplVulkan_Init` but does NOT call
`ImGui_ImplVulkan_CreateFontsTexture` explicitly — the Vulkan backend
builds the atlas on first NewFrame. So add our font in `attach()` AFTER
constructing Inspector but BEFORE the first draw() call. The first draw's
`newFrame()` triggers the atlas build with our font included.

If that still doesn't work (because ImGui_ImplSDL2 reads the DPI and sets
`io.DisplayFramebufferScale`), the next step is to inspect
`io.DisplayFramebufferScale` at draw time and factor it in explicitly.

### Phase 2 candidates (unordered, pick what matters)

1. **Audio tab**: SDL volume slider, output device picker, mute toggle. The
   runtime already owns `g_audio_device`; just expose it.
2. **Save Manager tab**: list / backup / restore / rename `yoshis_story.bin`
   from `data/saves/`. Trivial filesystem UI.
3. **Controls tab**: controller rebinding. Needs digging into
   `../N64ModernRuntime` input mapping — Zelda64Recomp has the UI pattern.
4. **Credits logo image**: upload icon_data.h as an ImGui texture via RT64's
   RenderInterface + ImGui_ImplVulkan_AddTexture. Puts the logo at sidebar top.
5. **Backdrop pane**: solid colour or blurred game frame behind the panel so
   it's not just floating over gameplay. ImGui window flag or custom draw.

### Files touched this iteration

- NEW `src/overlay.h`, `src/overlay.cpp` — all overlay code
- NEW `docs/OVERLAY_PLAN.md` — this document
- EDIT `src/launcher.h` — exposed `g_settings_path`, `g_sdl_window`,
  `apply_display_settings`, `audio_set_paused`
- EDIT `src/main.cpp` — un-static `g_settings_path`, `g_sdl_window`,
  `g_audio_device`, `apply_display_settings`; added `yoshi_argv0` for
  Restart; added `audio_set_paused()`; wired `overlay::attach` /
  `overlay::detach` / `overlay::draw` / `overlay::handle_event` in four
  short hook points.
- EDIT `CMakeLists.txt` — added `src/overlay.cpp` to executable.

Note: `patches/rt64/` has pre-existing uncommitted work (0002 removed in a
local-only commit, 0003 modified, 0004 untracked) — unrelated to overlay,
not touched this iteration.

## References

- DKR-R `runtime_ui.hpp` — public API shape to copy
- DKR-R `runtime_ui.cpp` lines 9301-9800 — attach/detach/draw/toggle bodies
- DKR-R `rt64_renderer.cpp` line 903 — where `ui::draw(app)` is called
- RT64 `rt64_inspector.h/.cpp` — Inspector API we consume
- RT64 `rt64_state.cpp:1653, 1860, 1996` — where RT64 drives Inspector itself
  (gated on `developerMode`); we stay out of its way.
- Our `src/main.cpp:559` — insertion point for `attach`
- Our `src/main.cpp:663` — insertion point for `draw` (after `app->updateScreen()`)
- Our `src/main.cpp:719` — insertion point for `handle_event` (SDL loop)
- Our `src/launcher.cpp:362-434` — widget source to port into Display tab
