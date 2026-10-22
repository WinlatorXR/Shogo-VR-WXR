# ShogoSourceModifications

Integration notes describing **in our own words** the changes the Shogo-VR-WXR
project makes to the proprietary *Shogo: Mobile Armor Division* source that lives
in `ShogoSource/`.

## Why these are prose and not patches

Monolith's Shogo v2.2 source licence (`shogosrc/EULA.txt`, section 8(b)) forbids
redistributing the source code, and section 8(c) only allows *modifications* to be
shared for free under certain conditions. A unified diff or a quoted snippet
still contains lines of Monolith's copyrighted source, which is why these notes
deliberately contain **none** of it: no diffs, no quoted code from the Shogo
source, and no context lines. Instead each note names the file and function and
describes what the VR code needs to add or change, so a developer with their own
Shogo v2.2 source licence can make the edit by hand.

(Only the mod's *own* added lines appear in code blocks below; those are the
project's MIT-licensed code, not Monolith's.)

## Layout

```
ShogoSourceModifications/
  README.md                     <- this file
  ClientShellDLL/
    BaseMenu.h.md
    Credits.cpp.md
    MainMenu.cpp.md
    OptionsMenu.h.md
    OptionsMenu.cpp.md
    PlayerStats.cpp.md
    RiotClientShell.cpp.md
    RiotMenu.cpp.md
    RiotClientShell.h.md
    Sparam.cpp.md
    WeaponModel.cpp.md
```

## What these edits do

| File | Purpose |
|------|---------|
| `ClientShellDLL/BaseMenu.h`      | Raises the generic menu-items capacity to 28 so `CVRMenu`'s 28 rows fit `m_GenericItem` (an overflow here corrupted surfaces and crashed on level start). |
| `ClientShellDLL/OptionsMenu.h`   | Adds the `CVRMenu` sub-menu member and its lifecycle calls. |
| `ClientShellDLL/OptionsMenu.cpp` | Adds the "vr" item to the Options menu and routes to the VR sub-menu, and hides the Keyboard / Mouse / Joystick binding pages (the controllers own input). |
| `ClientShellDLL/MainMenu.cpp`    | Quits immediately from the Exit / Quit item (skips the "are you sure?" box, which needs a Y key the controllers cannot send). |
| `ClientShellDLL/Credits.cpp`     | Lays the credits / intro text out on the mod's small virtual screen so it can be drawn per eye in VR. |
| `ClientShellDLL/PlayerStats.cpp` | Brackets the two crosshair draw sites with `g_VRStereo.BeginCrosshair()`/`EndCrosshair()` so the mod can hide the game crosshair by default and otherwise place it at aim depth. |
| `ClientShellDLL/RiotClientShell.cpp` | Mounts `CVRStereo`/`g_VRStereo`: init/term/update, console `VRRecenter`, head tracking, aim depth, mech mode, player body + crouch state, the stereo-aware `RenderCamera`, the per-eye menu/pause render, the right-half splash, the per-eye intro-movie clone, the dead-state feed, the VR menu-size re-layout poll, the per-eye hurt/explosion flash (which replaces the engine camera light-add), the leaked face-button key drop (`OnKeyDown`/`OnKeyUp`), the direct Right-A view toggle (`Update`) and the guard that ignores the leaked key-driven `ChaseView` (`OnCommandOn`). |
| `ClientShellDLL/RiotMenu.cpp`    | Asks `g_VRStereo` for the virtual menu layout size in `ScreenDimsChanged()` so the options/pause tree lays out legibly in the headset. |
| `ClientShellDLL/RiotClientShell.h`   | Declares the `m_bMovieShiftRight` member used by the intro-movie clone. |
| `ClientShellDLL/WeaponModel.cpp` | Lets `g_VRStereo` place and scale the first-person weapon in the tracked hand, and draw its muzzle-flash sprite at the visible gun's muzzle (keeping the shot origin as it was). |
| `ClientShellDLL/Sparam.cpp`      | `const char*` build fixes for the modern compiler (not VR-specific). |

The VR modules these edits call into (`VRStereo.*`, `VRMenu.*`, `XrApiUDP.*`,
`VRCapture.*`, `ShogoVRShared.h`, ...) live in the repository's `game/` folder and
are compiled into `CShell.dll`; they are **not** part of `ShogoSource/`.

## How to apply

Open the matching file in your own licensed copy of the Shogo v2.2 source and make
the described change. The edits are independent and can be applied in any order.

## Rebuilding `CShell.dll`

These notes are only the connecting edits; they are not the whole port. To rebuild
the DLL you also need the rest of this repository:

- `game/` — the mod's own sources (`VRStereo.*`, `XrApiUDP.*`, `VRMenu.*`,
  `VRCapture.*`, `ShogoVRShared.h`, `dplobby.h`), which the edits call into.
- `build/CShell.vcxproj` — the MSBuild project that compiles Monolith's
  ClientShellDLL and Shared `.cpp` files together with the four `game\*.cpp`
  files. It puts `..\game` on the include path, links `ws2_32.lib` and uses
  toolset v143, Win32, `/MT`, no PCH and `/Zc:forScope-`.
- `Docs/Pre-LaunchDocs/PROJECT_PLAN.md` - build facts (defines, link libs, exports) and the
  XrApi UDP wire protocol.

Steps:

1. Put your own Shogo v2.2 source in `ShogoSource/` (the folder the project
   expects) and apply the notes above.
2. Build `build/CShell.vcxproj` with Visual Studio 2022 (Platform Toolset v143),
   `Win32`. The output is `build\out\CShell.dll`.
3. Deploy the DLL into a `ShogoVR\` overlay folder and launch Shogo with the
   `-rez ShogoVR` archive last (see `Docs/Pre-LaunchDocs/PROJECT_PLAN.md`, section 2).     

If your Monolith tree's file list differs from the project's `ClCompile` entries,
add or remove the matching entries. The stock `ClientShellDLL.rc` is not compiled
(it needs MFC's `afxres.h` and defines no real resources).

## Licence

Your own copy of Shogo: Mobile Armor Division v2.2 is required to build from
these notes. The mod is free and non-commercial; see the repository `NOTICE.md`
and, for Monolith's terms, `shogosrc/EULA.txt`.
