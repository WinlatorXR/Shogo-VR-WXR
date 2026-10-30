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
- Free off arm on the submachine gun (#7): weapon 18's first-person model carries a
  second arm that holds nothing, and it now follows the off controller instead of the
  animation.  The whole arm moves (the art names it, `R_Arm`/`R_Arm3`), the gun is never
  touched, and the arm is handed back whenever the gun's own animation needs it - the
  reload seats the magazine with that hand.  Entirely mod-side: **no `ShogoSource/` edit**,
  so there is no modification note to match.
- Dual pistols held one per hand (#5): the Colt 45 first-person model is split into its
  two mirrored halves by part name, one left on the game's own model in the gun hand and
  one shown on a mod-owned copy in the off hand.  Because Shogo animates the pistols well
  away from the model's origin, each copy is anchored by reading its own `palm` node's
  animated transform (`GetModelNodeTransform`) and shifting the object by the opposite
  amount.  The game's fire animation stays the single cadence - the pair fires at the stock
  rate between them and alternates hands when both triggers are held - so the only engine
  change is one edit in `ShogoSource/ClientShellDLL/WeaponModel.cpp` (`UpdateWeaponModel`:
  rewrite the shot origin/direction before `SendFireMsg`), noted in
  `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md` as edit 5.
- Quicksave/quickload support: a level-start autosave, and the left trigger quick-loads
  while you're dead.
- Multiplayer deaths respawn automatically in VR after the stock wait; the trigger is read
  as a held command, so the game's usual "press fire" edge never arrives in VR.
- Custom mod selector in the launcher (`launcher-xr`): a checkbox list for `.rez` files in
  the game's `Custom` folder (the folder is no longer mounted blindly), an `AlwaysLoad`
  folder that is mounted whenever it exists, and a `-mods "coop.rez,other.rez"` shortcut
  argument that skips the popup for unattended launches. `ShogoVR` always loads last.

## Weapon models and coordinates: what we learned the hard way

Notes for anyone posing hands, guns or effects on Shogo's first-person weapon
models. Every point below cost at least one wrong fix before it was understood.
A full part dump for each on-foot weapon is in `Docs/GameLogs/2026-10-28/ShogoVR_models.txt`.

### Hand groups are named by two different conventions, ordered oppositely

Every two-handed on-foot weapon carries two hand groups of 16-17 nodes each,
but under two different naming schemes - and the schemes disagree about which
hand comes first:

| naming | weapons | group A | group B |
|---|---|---|---|
| `palm` / `palm3` | 13 Colt 45, 15 assault rifle, 16 + 17 grenade launchers, 19 TOW | **fore** hand | trigger hand |
| `Right_Palm` / `Right_Palm4` | 14 shotgun, 18 MAC-10, 21 squeaky toy | trigger hand | **fore** hand |

Everything is named `R_`/`Right_` whatever hand it actually is, so the names
cannot tell you which is which. One global "swap the hands" setting cannot
satisfy both conventions: the value that held the submachine gun correctly left
the assault rifle gripped by its fore-end with nothing on the trigger.

**Measure it instead.** Read both palms off the posed model and project them
onto the gun's own forward axis; the one further along the barrel is the fore
hand. That rule needs no table and holds for models the mod has never seen
(`CVRStereo::ForwardHandGroup`).

### The arms are not in the hand groups, and their names are not stable

The hand node lists stop at the wrist. The `Right_Palm` family carries its
forearm as `R_Arm`/`R_Arm3`, but the `palm` family uses generic names -
`tetra1` + `cube1_3` for the first hand, `tetra3` + `cube1_4` for the second -
and **the TOW pairs `tetra4`/`tetra5` the opposite way round** from the assault
rifle and both grenade launchers. They cannot be assigned per hand by name.

### Weapons 15, 16 and 17 are the same rig three times

The assault rifle and both grenade launchers carry identical gun part names.
Anything written for one covers the other two unchanged.

### Some weapons have only one hand in the model

The shotgun (14) has a single group, and it is the **fore** hand on the pump -
its dominant hand was never modelled. The knife (22) and squeaky toy (21) are
single-handed too. Code that walks hand groups must cope with a group that
returns zero nodes rather than assuming a pair.

### Offsets must be measured in the frame they will be used in

This is the big one. An offset applied along the **hand's** axes turns with the
player's wrist, so a value found with the desktop UDP tester's synthetic poses
lands somewhere else entirely once a real wrist holds the weapon at a real
angle - on device the lent hand was thrown clear of the gun. Anything attached
to a weapon belongs in the **gun's** frame (`CVRStereo::PoseLentHandOnGun`):
same origin, orientation and scale as the weapon, offset along the weapon's own
axes. A hand on a grip does not move when the wrist turns; the whole weapon
turns with it.

Corollary: a trim tuned per handedness mode needs per-mode storage. One shared
set of CVars means tuning the second mode silently overwrites the first.

### The model's origin is not the grip

`GetModelNodeTransform` is read-only and there is no node-transform write, so a
mod-owned copy of a model cannot be posed by its nodes. Anchor it instead by
reading the node you care about, taking its offset from the object origin, and
shifting the whole object by the opposite amount (`CVRStereo::GetNodeDrift`).
Shogo animates weapons well away from the origin, so without this the copy
drifts visibly as the animation plays.

### Check your quaternions are unit length

`QRotate` in `XrApiUDP.cpp` - and the usual `v + 2w(u x v) + 2u x (u x v)` form
generally - is a pure rotation **only** for a unit quaternion. Fed a longer one
it rotates *and* scales by `|q|²`. Hand-authored test packets are normalised
only by luck (ours were 4.7% and 1.4% long, which stretched the two hand bases
by different amounts), and a real runtime's quaternions drift with float error
over a session. The failure is silent and shows up as small asymmetries in
things that should be symmetric. Normalise on parse.

### A screenshot cannot identify a weapon

The shotgun and the assault rifle read as the same gun at the wrong angle, and
two fixes were aimed at the wrong weapon because of it. The mod logs one line
per weapon change naming the model file and every decision made about that
weapon's hands; trust that over the picture.
