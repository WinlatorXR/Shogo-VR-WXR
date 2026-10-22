# Shogo VR -> WinlatorXR: Real Implementation Plan

Port the existing **Shogo: Mobile Armor Division VR mod** (PCVR, by BrandonW24) to
**WinlatorXR** by replacing its OpenXR headset bridge with the WinlatorXR **XrAPI**,
in-process inside `CShell.dll`, targeting a **Meta Quest 3** running inside a container within the **WinlatorXR** app, but also supporting other WinlatorXR compatible devices like:
Meta Quest 2, Meta Quest Pro, Meta Quest 3 S, Pico 4 Ultra, and more.

This document is the execution plan. It supersedes guesswork; every call site and
build fact below was verified against the source trees in this repository.

---

## Hard rule: release-owned files are read-only

`AutoUpdate/` and every `version.txt` inside `Release/` are **human-approved release
artifacts**. Automated edits - including AI agents, scripts, build steps and deploy
commands - must **never create, modify, overwrite, move or delete anything in
`AutoUpdate/`**, and must **never touch any `version.txt` file inside `Release/`**.
Reading them for verification is fine; writing is not.

The reason: `AutoUpdate/` holds the stable, hand-picked builds that the launcher
publishes, and changing a version number there is a release decision. `Release/` is
where a build is staged for on-device testing and packaging into the GitHub release,
so its metadata is hand-managed for the same reason.

Inside `Release/`, the **only** files an automated pass may write are:

- `Release/ShogoVR.exe`
- `Release/ShogoVR/CShell.dll`

Everything else under `Release/` (`version.txt`, `ReadMe.txt`, `Releases/…`, and any
other packaged files) is off-limits. If a deploy seems to need a change there, stop
and ask the owner; do not make it.

## Hard rule: Look but don't touch the Pre-LaunchDocs folder

Documents that existed before this final push to V1.0 are in **Docs/Pre-LaunchDocs** and may be read but **should not** be taken as final truths anymore, they are no longer accurate and **will not be updated**

## 2. What we have, and the key blocker

### Repository
| Path | Contents |
|---|---|
| `game/` | The mod's own game-side sources compiled into `CShell.dll`: `VRStereo.cpp/.h`, `VRMenu.cpp/.h`, `VRCapture.cpp/.h`, `ShogoVRShared.h`, `dplobby.h`. |
| `bridge/` | Standalone 64-bit **OpenXR** bridge (`ShogoVRBridge.vcxproj`). Becomes obsolete for this port. |
| `launcher/` | Win32 launcher / settings / installer. Bridge-start logic to be dropped; EXE not used at all anymore; `ShogoVR.ini` still used. |
| `tools/` | `leakcheck.py`, `makepayload.py`. |
| `ShogoSource/` | **Stock Monolith Shogo v2.2 source** (git-ignored, untracked). |
| `Docs/` | `PROJECT_PLAN.md`, this implementation plan. |
| 'launcher-xr' | The launcher for the ShogoVR.exe for WinlatorXR |

### The blocker
`ShogoSource/` is **stock Monolith v2.2** with **no VR glue**. The mod's `game/*`
files expect call sites inside Monolith's files (init/term, render loop, HUD draw,
weapon model, menus). The upstream repo **deliberately withheld** these connecting
edits (its `game/README.md` and root README say so: the source licence forbids
redistributing Monolith's source).

Therefore we must **reconstruct the glue**. It is bounded reverse-engineering: the
`game/` interface says *what* to call; the stock ClientShellDLL says *where*.

## Auto-update (launcher-xr / `ShogoVR.exe`)

New feature (17 Oct 2026). `ShogoVR.exe` (`launcher-xr/`) is the thin launcher the
WinlatorXR container runs instead of a desktop shortcut; besides launching the game it now
checks GitHub for a newer mod build and offers to install it. This is a
**launcher-side** feature only - `CShell.dll` itself is unchanged by it.

### Release artifacts (tracked, branch `main`)

The latest confirmed **stable** build of the mod lives in `AutoUpdate/` at the repo root:

| File | Purpose |
|------|---------|
| `AutoUpdate/CShell.dll` | the stable mod DLL (the exact bytes that ship) |
| `AutoUpdate/version.txt` | the version number of that build (hand-edited release gate - `0.7` for the current release) |
| `AutoUpdate/notes.txt` | one or two sentences describing what changed |

**`AutoUpdate/` is human-owned.** Its three files are hand-picked / hand-written for each stable
release: no build step, script or tool writes them, and they must never be created, edited,
moved or deleted by an automated pass (see the hard rule at the top of this document).
`Release/ReadMe.txt` (the release blurb) and every `version.txt` in the tree
(`AutoUpdate/`, `launcher-xr/`, `Release/`) are human-edited the same way - a version number is a
release decision, so if a human gets one wrong, a human corrects it.

Public URLs the launcher reads (`raw`, branch `main`):

- latest release DLL:
  https://github.com/WinlatorXR/Shogo-VR-WXR/raw/refs/heads/main/AutoUpdate/CShell.dll
- release notes:
  https://raw.githubusercontent.com/WinlatorXR/Shogo-VR-WXR/refs/heads/main/AutoUpdate/notes.txt
- release version:
  https://raw.githubusercontent.com/WinlatorXR/Shogo-VR-WXR/refs/heads/main/AutoUpdate/version.txt

The launcher ships its **own** `version.txt` next to `ShogoVR.exe`
(`launcher-xr/version.txt`, hand-edited to the same number - `0.7` for the current release) -
that is the locally "installed" version it compares against. The **installed** copy of that
file is the one in the game folder next to `ShogoVR.exe`
(`<game>\version.txt`, e.g. the GOG directory); the launcher reads and rewrites the one in its
own directory, so a desktop build and a release install each carry their own.

`version.txt` is the **release gate**, and it is edited **by hand, for stable, human-approved
releases only** - nothing bumps it automatically, and no build step reads or writes it.
Because the launcher's decision is based on that number alone, dropping a new `CShell.dll`
(or new notes) into `AutoUpdate/` does *not* offer the update to anyone: the release goes live
only when `AutoUpdate/version.txt` is bumped by hand. That makes prerelease playtesting safe -
the new DLL can sit on `main` ahead of its version number.

### Update flow

1. On start, if the container has internet access, the launcher downloads
   `AutoUpdate/version.txt` and compares it with the local `version.txt` next to the EXE.
2. If the local `version.txt` is **missing**, the online file is assumed to be the newest
   (first run / fresh install) and it prompts. If the online version is **not strictly
   newer** than the installed one, it skips straight to launching the game.
3. **The compare is by value, never equality** (`ShogoVR.cpp`: `ParseVersion` / `IsNewer`).
   A dotted version is parsed into numeric fields (`0.7`, `1.2.3`; a tag after the numbers,
   such as `0.8-beta`, is ignored and sorts *below* its release). Missing fields count as
   zero, so `0.8` and `0.8.0` are the same version. The update is offered only when the
   online version is **greater** - so a local prerelease build (say `0.8-beta`, or a plain
   `0.8` sitting ahead of its bump) is left alone by the published `0.7`: no per-launch
   prompt, and no risk that answering YES replaces a newer DLL with the older published one.
   Older or equal installs are still offered the update, exactly as before.
   If the versions **differ and the online one is newer**, a simple **YES/NO message box**
   appears - every time it detects the newer version - reading roughly:
   `A new update is available, download now? Version [version_number]: [notes]`
4. **NO** - launch the game as normal.
5. **YES** - create (or empty the contents of) a temporary folder `ShogoVRUpdate` (next to
   the EXE) and download `version.txt` and `CShell.dll` into it.
6. If both downloads succeed: copy-and-overwrite the downloaded `version.txt` next to the
   EXE, then copy-and-overwrite `CShell.dll` into the `ShogoVR` sub-folder (the overlay that
   wins over the copy inside `SHOGO.REZ` - see the deployment model in section 2).
7. Only when **both** copies succeed, delete the *contents* of `ShogoVRUpdate` (keep the
   folder itself), then continue.

No internet, or any download / copy failure, is non-fatal: the launcher falls through to a
normal launch with whatever DLL is currently installed.

## Build / Tooling / Misc. Features

- Full-rebuild deploy script (`build/build-and-deploy.ps1`) with a freshness guard, so a
  stale `CShell.dll` can't be deployed.
- Frame capture built into the shell (`game/VRCapture.cpp`).

## Core Features

- Side-by-side stereo rendering with full 6DoF head tracking.
- XrApi 0.4 client running in-process inside `CShell.dll` (UDP), so no external bridge
  process is needed.
- HUD, menus and the game's UI drawn into each headset eye.
- In-headset VR menu with live settings (Options > vr settings) plus `ShogoVR.ini` for
  tuning: movement deadzone, turning, handedness, gun scale/position, muzzle-flash offsets
  and more - no rebuild needed to adjust them.
- Left stick to move (radial dead zone with a per-axis cut) and right stick to turn;
  rotate-to-aim so you walk where you look.
- Controller grips, triggers, sticks and face buttons mapped to the game's actions (jump,
  fire, crouch, view toggle, transform, recenter HMD position, weapon cycle).
- Left-handed mode: the triggers swap hands while the grips stay put.
- Held gun model posed to the controller, with first-person (arms) or third-person gun view
  (VRGunModel).
- Per-weapon muzzle-flash placement tuned for the held models, including one flash per
  barrel on the dual pistols.
- Quicksave/quickload support: a level-start autosave, and the left trigger quick-loads
  while you're dead.
