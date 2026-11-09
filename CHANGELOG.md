# Changelog

## 1.0.0

First Windows x64 release of this fork, based on damobeanrod's Linux port.

- Native Windows runtime with Direct3D 12 rendering and Vulkan fallback.
- Compact desktop menu, green settings theme, and native owned settings windows.
- Windowed, borderless window, and borderless fullscreen modes; corrected fullscreen bounds.
- Pause/resume, mute/unmute, and F1 menu visibility shortcuts.
- Controller selection with a remembered device, disconnect/reconnect handling,
  keyboard bindings, and configurable buttons, triggers, and stick directions.
- One controller input per action. N64 R defaults to RB; rebinding replaces it.
- Xbox and PlayStation glyphs, triangular triggers, rectangular bumpers, and
  controller diagrams using the settings colors.
- Corrected storybook texture seams, tested against both the Windows build and
  upstream Linux source. The full Windows fork is not Linux-qualified.
- About uses the original project icon, GitHub project links, and version 1.0.0.
- Portable Windows package with runtime DLLs, notices, a SHA-256 checksum,
  file inventory, and an executable tied to its exact source revision.

Validation and remaining limits are recorded in
[the 1.0.0 release record](https://github.com/TK22-26/yoshis-story-recomp/blob/main/docs/releases/1.0.0.md).
