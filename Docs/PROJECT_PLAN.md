# Shogo VR -> WinlatorXR: Real Implementation Plan

Port the existing **Shogo: Mobile Armor Division VR mod** (PCVR, by BrandonW24) to
**WinlatorXR** by replacing its OpenXR headset bridge with the WinlatorXR **XrAPI**,
in-process inside `CShell.dll`, targeting a **Meta Quest 3**.

This document is the execution plan. It supersedes guesswork; every call site and
build fact below was verified against the source trees in this repository.

---

## 0. Goal

Make a **minimally changed** version of the SHOGO MAD PCVR mod run on **WinlatorXR**
using its own **XrAPI** instead of the original OpenXR bridge. Use the **same XrAPI
version as the Halo CE WinlatorXR mod (HWXR)** — 0.2, or 0.4 with the optional
fallback UDP port. Do **not** implement AER (alternate eye rendering) or non-VR mode;
do stereo VR if possible, otherwise monocular. Above all, **preserve 6DoF head and
hand-controller tracking, inputs and haptics**, and **avoid extra layers such as
dgVoodoo2**.

- XrAPI docs: https://winlatorxr.github.io/xrapi.html
- HWXR (reference mod): https://github.com/WinlatorXR/HaloCEWXR
- Far Cry VR WXR: https://github.com/SgtBilko76/Farcry_vrmod-WinlatorXR
- opentrack WXR: https://github.com/WinlatorXR/opentrackWXR
- SixDOFinator sample: https://github.com/WinlatorXR/SixDOFinator_SampleProject

(This section folds in the former `Docs/PROJECT_GOAL.md`, which was removed as
obsolete once its content was captured here.)

---

## 1. Objective & locked decisions

- Minimally change the Shogo MAD VR mod; swap only the headset transport.
- Use **XrAPI 0.4** (matching HWXR), including the optional **fallback UDP port 7873**.
- Fold the XrApi client **into the game DLL** (all in-process, like HWXR).
- **No dgVoodoo2** requirement.
- Preserve 6DoF head + hand tracking, inputs, and haptics.
- **Monocular VR first** (guaranteed baseline), then add full SBS stereo.
- Simplify the floating HUD panel / flat screen into the rendered frame.
- Skip AER and non-VR modes (out of scope for now).

Reference projects: HWXR (`WinlatorXR/HaloCEWXR`), Far Cry VR WXR,
`opentrackWXR`, `SixDOFinator_SampleProject`.
XrAPI docs: https://winlatorxr.github.io/xrapi.html

---

## 2. What we have, and the key blocker

### Repository
| Path | Contents |
|---|---|
| `game/` | The mod's own game-side sources compiled into `CShell.dll`: `VRStereo.cpp/.h`, `VRMenu.cpp/.h`, `VRCapture.cpp/.h`, `ShogoVRShared.h`, `dplobby.h`. |
| `bridge/` | Standalone 64-bit **OpenXR** bridge (`ShogoVRBridge.vcxproj`). Becomes obsolete for this port. |
| `launcher/` | Win32 launcher / settings / installer. Bridge-start logic to be dropped; `ShogoVR.ini` still used. |
| `tools/` | `leakcheck.py`, `makepayload.py`. |
| `ShogoSource/` | **Stock Monolith Shogo v2.2 source** (git-ignored, untracked). |
| `Docs/` | `PROJECT_PLAN.md`, this implementation plan. |

### The blocker
`ShogoSource/` is **stock Monolith v2.2** with **no VR glue**. The mod's `game/*`
files expect call sites inside Monolith's files (init/term, render loop, HUD draw,
weapon model, menus). The upstream repo **deliberately withheld** these connecting
edits (its `game/README.md` and root README say so: the source licence forbids
redistributing Monolith's source).

Therefore we must **reconstruct the glue**. It is bounded reverse-engineering: the
`game/` interface says *what* to call; the stock ClientShellDLL says *where*.

### Why the modern toolchain is safe (proven)
`game/VRCapture.cpp` includes `<d3d11.h>`/`<dxgi1_2.h>` and uses `__uuidof`, which
VC6 cannot compile. The shipped `CShell.dll` was thus already built with a modern
MSVC and loads on retail Shogo. The full CShell ↔ engine ABI (function-pointer
structs, default x86 packing, `DBOOL == char`, exports via `SETUP_CLIENTSHELL()`)
is already known to work with a modern compiler.

### The integration seam (this is the swap point)
`CVRStereo` talks to the bridge through **shared memory**
(`VRStereo.cpp: OpenShared/CloseShared/UpdateBridge/WriteGameBlock/RequestRecenter`,
`ShogoVRShared.h`). That is the one layer we replace with XrApi UDP; the rest of
`VRStereo` (stereo, HUD, head/hand pose use, inputs) stays as-is.

### Deployment model (confirmed against retail GOG)
- Retail `CShell.dll` lives **inside `SHOGO.REZ`**; there is no loose file.
- `Client.exe` loads `cshell`; `Shogo.exe` accepts `-rez <name>`.
- The mod wins by **overlay precedence**: files go into `<Shogo>\ShogoVR\`, and the
  game is launched with the base archives first and `ShogoVR` **last**:
  ```
  Client.exe -windowtitle Shogo -rez "SHOGO.REZ" -rez "SOUND.REZ" -rez "Custom"
             [ShogoP*.REZ ...] +multiplayer 0 -rez "ShogoVR"
  ```
  (`launcher/Common.h:429-465`.) Its `ShogoVR\CShell.dll` wins over the one in
  `SHOGO.REZ`.
- GOG install used for testing:
  `E:\GOG Galaxy\Games\Shogo - Mobile Armor Division`
- We only ever rebuild `CShell.dll`; GOG supplies the engine, renderers and REZ packs.

---

## 3. Build facts (from `ShogoSource\ClientShellDLL\RiotClientShell_60.dsp`)

- Output: `CShell.dll`, **Win32 / x86**, Dynamic Library.
- **89 `.cpp`** total: 85 in `ClientShellDLL\` + 4 in `ShogoSource\Shared\`
  (`DebrisTypes.cpp`, `ModelFuncs.cpp`, `RiotCommonUtilities.cpp`, `WeaponDefs.cpp`).
  `plasma2/3/4.cpp` exist but are **not** compiled.
- Include dirs (original): `..\appheaders`, `..\shared`.
- Defines: `WIN32;_WINDOWS;_MBCS;RIOT_BUILD;NDEBUG` (Release), `_DEBUG` (Debug).
- Runtime: `/MT` (static CRT). No external DirectX/Miles/GameSpy libs.
- Link libs: `kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32
  oleaut32 uuid odbc32 odbccp32`.
- Exports: `SETUP_CLIENTSHELL()` (`AppHeaders/clientshell_de.h:120`) provides
  `extern "C" __declspec(dllexport)` `GetClientShellFunctions`,
  `GetClientShellVersion` (returns `CLIENTSHELL_VERSION == 3`), `SetInstanceHandle`.
  No `.def`, no `/EXPORT`.
- `DBOOL` is `char` (`AppHeaders/basetypes_de.h:18`); `BEGIN_EXTERNC()` is `extern "C" {`.
- `NetStart.cpp:22` includes `dplobby.h` (DX6). Only a stub exists at `game/dplobby.h`;
  no DirectPlay API calls, so the stub is sufficient.

### Known modern-MSVC fixes
1. **for-scope leaks** (VC6 let `for(int i…)` escape): try `/Zc:forScope-`; if VS2022
   rejects it, fix the offending local Monolith sources case-by-case.
2. `min`/`max` redefinitions (`RiotClientShell.cpp:42`) — expect C4005; fix if it errors.
3. `dplobby.h` — satisfied by adding `..\game` to the include path.
4. `itoa/ltoa/strupr` — silenced by `_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_WARNINGS`.
5. Stock `ClientShellDLL.rc` includes `afxres.h` (MFC, absent) but defines no real
   resources -> **exclude the `.rc`** (or provide a tiny `build/CShell.rc` with
   `#include <winres.h>` if the engine turns out to need one).
6. Drop VC6 `/GX` -> `/EHsc`; drop `/YX` (auto-PCH) -> no PCH (`/Y-`).

---

## 4. Phases

### Phase 0 - Buildable stock base (VS2022, Win32/x86)
Deliverables (tracked in `build/`):
- `build/CShell.vcxproj` (Win32/x86 DynamicLibrary, `CShell.dll`)
- output `build/out/CShell.dll`
- optional `build/CShell.sln`; add `build/out/` to `.gitignore`

Project definition: the 89 `.cpp` above, include dirs
`..\ShogoSource\AppHeaders`, `..\ShogoSource\Shared`, `..\ShogoSource\ClientShellDLL`,
`..\game`; defines and link libs as in section 3; `/MT`, `/EHsc`, no PCH.

Steps:
- **0a** Build pure stock (no `game/` files) -> `build/out/CShell.dll`; fix build
  blockers until it links.
- **0a verify** Copy `CShell.dll` into the GOG `ShogoVR\` folder; launch `Client.exe`
  with the arg list from section 2; confirm the game reaches the menu and starts a
  level. Optionally `dumpbin /exports` and compare export names to a retail DLL.
- **0b** Add `game\VRStereo.cpp`, `VRMenu.cpp`, `VRCapture.cpp` + `..\game` include
  (still unreferenced/dead code) -> builds and still behaves stock. Proves the VR
  sources compile inside the project before any glue.

STATUS (Phase 0 DONE): `build/CShell.vcxproj` builds `build/out/CShell.dll`
(Win32/x86, VS2022 v143) and the result loads on retail GOG Shogo (see the Phase 1
smoke test).

### Phase 1 - Reconstruct the glue (`game/*` into stock ClientShellDLL)
Verbatim call sites are mapped. Highest-value anchors:

| VR API | Stock injection site | File:line |
|---|---|---|
| `Init(CClientDE*)` | `CreateClientShell` / `OnEngineInitialized` | `RiotClientShell.cpp:130` / `:785` |
| `Term()` | `OnEngineTerm` / destructor | `RiotClientShell.cpp:1251` / `:475` |
| `BeginFrame(bool,bool)` | top of `Update()` / `PreUpdate()` | `RiotClientShell.cpp:1696` / `:1648` |
| `RenderStereo(...)` | replace/augment `RenderCamera(m_hCamera)` | `RiotClientShell.cpp:8043` (call `:8064`), `DoRenderLoop:8293` |
| `Begin2D/End2D`, `BeginCrosshair` | around `DrawInterface()` | `RiotClientShell.cpp:8067` / `:8082` |
| `ApplyHeadTracking` | end of `CalculateCameraRotation()` / `UpdateCameraRotation()` | `RiotClientShell.cpp:3119` / `:3131` |
| `UpdateAimDepth` | `UpdateWeaponModel()` | `RiotClientShell.cpp:3398` |
| `AdjustWeaponPose`/`ScaleWeapon` | `CWeaponModel::UpdateWeaponModel` / `SetOffset` | `WeaponModel.cpp:238`, `WeaponModel.h:48` |
| `UpdateHeldGun` | `UpdateWeaponModel` / `CWeaponModel::SetVisible` | `RiotClientShell.cpp:3398`, `WeaponModel.cpp:403` |
| `SetMechMode` | `SetExternalCamera` / VehicleMode checks | `RiotClientShell.cpp:2708` / `:2729` |
| `UpdateBody`/`HideBody` | `UpdateServerPlayerModel` / `ShowPlayer` | `RiotClientShell.cpp:8011` / `:7986` |
| `OnExitWorld()` | `CRiotClientShell::OnExitWorld` | `RiotClientShell.cpp:1566` |
| `DrawOpeningNotice` | `ShowSplash` / notice blits | `RiotClientShell.cpp:738` |
| `VRMenu` | `COptionsMenu` (new member + entry) | `OptionsMenu.cpp:9`, `OptionsMenu.h:50` |
| `VRRecenter` console cmd | registration block | `RiotClientShell.cpp:819-824` |

Notes: stock has no `CInterfaceMgr`; the HUD is `CRiotClientShell::DrawInterface`,
menus are `CRiotMenu`/`CBaseMenu`, message boxes `CMessageBox`, dialogs `CPopupMenu`.

STATUS (Phase 1 DONE): all rows above are wired and build clean, deployed to
`E:\GOG Galaxy\Games\Shogo - Mobile Armor Division\ShogoVR\CShell.dll`. Smoke test =
101 modules, overlay `cshell.dll` loaded, mod log "Shogo VR started", stock behavior
with VR off, no crash. Notes on the chosen call sites: `BeginFrame` is called in
`RenderCamera` (not `Update`); `CheckSettingsFile`/`UpdateBridge`/`CheckRenderer` in
`Update`; body is drawn via `UpdateBody(hRealObj, PlayerModeToModelID(m_nPlayerMode), ...)`
in `UpdateServerPlayerModel`; mech mode via `SetMechMode` in `MID_PLAYER_MODECHANGE`;
the VR menu is a new `CVRMenu m_VRMenu` member of `COptionsMenu` (item 5, label literal
"vr", Back moved to item 6). Remaining: `DrawOpeningNotice` (optional).

### Phase 2 - Replace the bridge with XrApi 0.4 UDP (in-process)
- New `XrApiUDP.h/.cpp` adapted from HWXR (`WinXrApiUDP.cpp`, `WinXrApi.cpp/.h`).
- Wire protocol:
  - Out -> `localhost:7278`: `"<Lhaptic> <Rhaptic> <MODE_VR> <MODE_3D> <FOVH> <FOVV>"`
    (MODE_VR 1; MODE_3D 1 = SBS; 0 = monocular).
  - In <- `localhost:7872` (fallback `7873`):
    `"client <29 numbers> <19-char T/F string> [<9 floats> <2-char T/F>]"`.
  - 29 numbers = L quat(4)+stick(2)+pos(3), R quat(4)+stick(2)+pos(3),
    HMD quat(4)+pos(3), IPD, FOVH, FOVV, HMD_SYNC (index 28). Then the
    19-char bool string. 0.5's trailing floats (HMD_ALTITUDE + grips) and
    2-char Immersive/SBS string are ignored.
  - 19 bools (0-based) = L: grip(0), menu(1), stickPress(2), L/R/U/D(3-6),
    trigger(7), X(8), Y(9); R: A(10), B(11), grip(12), stickPress(13),
    L/R/U/D(14-17), trigger(18).
- Startup: check `Z:\tmp\xr\version`; create `Z:\tmp\xr\` (fallback `D:\xrtemp`);
  write `version` and `vr`; delete `vr` on exit; read `system` for make/model.
- Rewrite `UpdateBridge`/`WriteGameBlock`/`RequestRecenter` to XrApi; keep the same
  `m_vHeadF/m_vHeadU/m_vHeadP`, hand vectors, catch/button members.
- **Port the RH->LithTech pose + hand conversion** from `bridge/ShogoVRBridge.cpp`
  in-process; map the 19 bools to `SHOGOVR_BTN_*` and the move/turn axes.
- Draw a red **HMD_SYNC** pixel in the **top-left** corner each frame; hold submission
  until the sync/frame id changes.
- Remove `StartBridgeIfNeeded` / `RememberCommandLine` / the bridge-quit event and the
  `VRCapture` shared-texture path (no D3D11 capture; WinlatorXR reads the game window).

STATUS (Phase 2 code complete): `game/XrApiUDP.h/.cpp` written (in-process UDP client:
non-blocking `ioctlsocket(FIONBIO)` poll each frame, no thread; `WSAStartup`; recv bind
7872 -> 7873; send to `127.0.0.1:7278`; startup files `Z:\tmp\xr\` -> `D:\xrtemp`;
29-number + 19-bool parse tolerant of 0.5 trailing data; `QMul/QRotate/QYaw/YawOf` +
`ToGameSpace` ported from the bridge). `VRStereo.h/.cpp`: `CXrApiUDP* m_pXrApi` started in
`Init` (gated on `VRAutoBridge`), stopped/deleted in `Term`; `UpdateBridge` now polls XrApi
and fills the existing members (`m_vHeadF/U/P`, hands, `m_fHeadYaw/Pitch`, `m_fNeedTanX/Y`,
`m_fMove/TurnX/Y`, `m_nButtons`); `RequestRecenter` routed to XrApi; new
`DrawXrSyncPixel()`. `RiotClientShell.cpp` draws the red HMD_SYNC pixel after `End3D()`
(`:8142`). `build/CShell.vcxproj` gained `..\game\XrApiUDP.cpp` and `ws2_32.lib` (both
configs). Build clean (only pre-existing D9035/D9025), deployed (699392 bytes), smoke test
= 102 modules, overlay loaded, no crash; mod log shows `XrApi: listening on udp 7872/7873
(headset OCULUS PANTHER)` and `Headset connected (XrApi)` - the full receive/parse/convert
path runs against a runtime present on the dev machine. Note: the mandatory baseline is
"drain to newest pose + red sync pixel" every frame; an optional bounded HMD_SYNC hold
(see below) is layered on top.

STATUS (Phase 2 coordinate frame corrected): XrApi is **not** OpenXR - it delivers raw HMD
and hand quaternions/positions plus button inputs only. Both reference consumers agree the raw
quaternion's Z (roll) is inverted vs the right-handed frame the bridge used: HWXR does
`(x,y,-z,w)` (then a Halo-specific 180-degree roll about Z), opentrackWXR does `(x,y,-z,w)`
with no roll inversion. Raw positions are already right-handed (x right, y up, -z forward).
So `XrApiUDP.cpp` now applies `XrQuatToOpenXR()` (negate Z on head + both hands) before the
existing `ToGameSpace`, and `Recenter()` yaws off the corrected quaternion; positions pass
through unchanged. Using the official XrAPIUDPTester default sample (`L quat .21 .29 -.93 .04`,
`R quat .10 -.30 .95 -.08`, `HMD quat .15 -.07 .05 .99`, `IPD 0.0678 m`, `FOV 99/103.4`) the
corrected head is a near-identity forward gaze and left/right hands sit on their correct sides.
Build clean, deployed (699392 bytes), smoke test = 102 modules, no crash. Remaining risk: the
final axis mapping after `ToGameSpace` (yaw/pitch sign, hand-raise axis) still needs on-device
tuning.

STATUS (Phase 2 HMD_SYNC hold added): `CXrApiUDP::Poll` now has an optional bounded hold -
after draining it can wait (via `select()`, 4 ms slices, 24 ms cap) until the runtime reports
a new `HMD_SYNC` before returning, which is what keeps the app in step with the headset. It is
**off by default** (`VRSyncHold`, default 0) because it slows the game to the headset rate and
must be validated on-device; the default path stays "drain to newest pose + red sync pixel".

### Phase 3 - Controls, haptics, recenter, window fit
- Haptics: on button edges send L/R haptic values to port 7278.
- Recenter: left-stick click (`RequestRecenter`) -> XrApi recenter.
- Window fit: ensure the game window matches what the runtime expects; port the
  bridge's `PlanWindow`/`FixGameWindow` behavior; honor `VRFixRenderer`.

STATUS (Phase 3 code complete): **recenter** done - left-stick click and the
*Options -> vr settings -> recenter* item both route to `CXrApiUDP::RequestRecenter`
(`XrApiUDP.cpp:466`, `VRMenu.cpp:270`). **Haptics** done - `CVRStereo` keeps a
frame-duration pulse (`m_fHapticL/R`) that fires on the FIRE edge (and a short one
on weapon switch), decays every frame, and is sent as `L_HAPTICS`/`R_HAPTICS` in
`SendFrame` (`VRStereo.cpp` ~1620). Toggle with `VRHaptics` (default 1); the
runtime treats the value as a vibration duration in frames (XrAPI 0.5 spec).
**Window fit** done - `CVRStereo::FitGameWindow()` (`VRStereo.cpp`) resizes the
game window so its client area equals the render resolution (rate-limited, gives
up after 3 tries/min), gated on `VRFixRenderer` (default 1); this is the portable
core of the bridge's `PlanWindow`/`FixGameWindow` and lets `CheckRenderer` work.
Still needs on-device confirmation (the exact window/style the container presents).

STATUS (desktop window test, 10 Oct 2026): with the crash fixed, the desktop run
showed the window still had its title bar and the HMD_SYNC pixel never appeared.
Following HWXR (`HaloCEWXR/Game.cpp`: strip `WS_THICKFRAME|WS_BORDER|WS_CAPTION`
and `SetWindowPos` to the full screen, `HaloCEWXR/InGameRenderer.cpp`: a 10x10 red
square whose red channel is the frame ID at each eye's top-left), the game now has
`CVRStereo::FitVRWindow()` (borderless, fills the display, idempotent so it is
re-applied every frame if the engine rebuilds the window) gated on `VRFullscreen`
(default 1), and `DrawXrSyncPixel()` draws a 10x10 square at the top-left of the
frame plus the right-eye half when SBS is active. The runtime reads the pixel from
the capture's top-left, so the title bar was exactly what hid it. Needs a re-test:
title bar gone, red square visible at the true corner.

### Phase 4 - HUD/menus into the frame
- Drop the HUD strip / floating panel (per goal): `VRHudStrip = 0`, always-flat
  disabled, and remove the bridge-layer gate (`m_bFlatFrame` currently requires
  `SHOGOVR_BRIDGE_LAYERS`). HUD/menus render into the eye via the existing
  `Begin2D` path.

STATUS (Phase 4 code complete): the XrApi bridge no longer advertises
`SHOGOVR_BRIDGE_LAYERS` (`VRStereo.cpp`, in `UpdateBridge`), so `bLayers` is false
and `m_bFlatFrame` / `m_bHudStripAllowed` stay off. The HUD is laid out for the
smaller virtual screen and drawn once per eye via `ComputeHudXform` /
`Begin2D`; menus and cutscenes render into each eye in stereo instead of a
floating panel. (The old layer path is left in the code but is now unreachable
with XrApi - it still serves as the pre-XrApi fallback.)

STATUS (menus per eye, 10 Oct 2026): `UpdateMenuState` and `UpdatePausedState`
(`RiotClientShell.cpp`) reached their render from `Update()` without going through
`RenderCamera()`, so their flat full-screen picture was split across the SBS eye
halves (the "Game Paused" text straddled the frame centre line). Both now mirror
the `RenderCamera` flow: `BeginFrame`, polygrid via `RenderStereo` in
`VR3D_PANEL_FLAT`, and the menu / pause text drawn once per eye inside
`Begin2D(nEye, DFALSE)` / `End2D()`, finishing with `DrawXrSyncPixel()`. The 2D
and 3D panel transforms share `ComputeHudXform`, so the menu and its polygrid
background line up. Desktop non-VR rendering is unchanged (stereo frame not
active -> original flat path). Needs a re-test: pause menu fills each eye, text
readable, no split.

### Phase 5 - Packaging & test on Quest 3
- Package `CShell.dll` + `ShogoVR.ini` + `AUTHORS.txt` + `NOTICE.txt` into the
  container overlay folder (keeping the `ShogoVR` name so the trailing
  `-rez "ShogoVR"` argument and any settings file keep working), launched with the
  `-rez` list.
- Checklist: XrApi connects (`version`/`vr` files), 6DoF head, hands, buttons, fire,
  recenter, monocular baseline then SBS correct, no dgVoodoo.
- Logs: `ShogoVR\ShogoVR_game.log`, plus XrApi diagnostics.

STATUS (Phase 5 overlay ready, device run pending): `tools/package_overlay.py`
assembles `build/overlay/ShogoVR/` from the built `CShell.dll` + `AUTHORS.txt` +
`NOTICE.txt` (optionally a `ShogoVR.ini`). Copy that folder next to `Client.exe`
inside the container and launch with the `-rez` list. **No Quest 3 / WinlatorXR
run has happened yet** - only desktop smoke tests (see the Phase 2 STATUS notes).
This is the single largest unknown in the project.

STATUS (desktop test fix, 10 Oct 2026): first desktop level load crashed inside
the Options menu's surface cleanup (`COptionsMenu::UnloadAllSurfaces`,
`DeleteSurface` on a bogus handle). Root cause: `CVRMenu` holds 26 option rows
(`VRMENU_ITEMS`) but the base menu's `m_GenericItem[]` was sized by the stock
`MAX_GENERIC_ITEMS = 20`, so rows 21-26 overflowed into `CVRMenu`'s own
`m_Values[]` handles and the `CBaseMenu` position fields, double-freeing surfaces
on unload. Fixed by raising that constant to 26 (`BaseMenu.h`, now a sixth
modified Monolith file, see `ShogoSourceModifications/ClientShellDLL/BaseMenu.h.md`)
and adding a compile-time guard in `game/VRMenu.h`
(`#if VRMENU_ITEMS > MAX_GENERIC_ITEMS`). Build clean, deployed, smoke test ok
(102 modules); needs a repeated level-load check.

---

## 5. Risks / unknowns to resolve during work
- Exact XrAPI 0.4 details (docs now at 0.5); confirm the 28-float/19-bool layout and
  the fallback-port behavior against HWXR at the time we implement Phase 2.
- Whether the runtime captures the game window directly or needs a specific present
  path under Wine/Winlator (expected: window capture; HMD_SYNC pixel gates it).
- Retail Shogo engine (D3D6-era) running under WinlatorXR's Wine; validate the game
  launches at all before adding VR.
- Modern-MSVC fixes for the for-scope leaks could touch a number of local (untracked)
  Monolith files; keep edits minimal and outside the tracked repo.
- Export-name decoration differences between VC6 and VS2022 (verify with
  `dumpbin /exports`; add a `.def` only if needed).

---

## 6. Where we are, and the immediate next action

Progress: **Phases 0-4 are code-complete; Phase 5's overlay is assembled but the
port has never run on the target.** Everything so far has been built and smoke
tested on the desktop only, never on the Quest 3 - that is the remaining unknown.
After the 10 Oct fixes (level-load crash, and title bar + HMD_SYNC pixel via
`VRFullscreen`), the desktop run should show a borderless window that fills the
display with a red 10x10 sync square in its top-left corner; confirm that first.

The immediate next action is to get it onto the actual target and finish the parts
that can only be tuned there:

1. **Run on the target (Phase 5).** Copy `build\overlay\ShogoVR\` next to
   `Client.exe` inside a WinlatorXR container and launch on the Quest 3 (or, first,
   point the `XrApiUDPTester` at the in-game client to validate the wire path).
   Watch `ShogoVR\ShogoVR_game.log` for `Headset connected (XrApi)`.
2. **Tune Phase 2/3.** Confirm the post-`ToGameSpace` axis mapping (yaw/pitch sign,
   hand-raise axis), the window fit, and the haptics against the headset, and try
   the optional `VRSyncHold` (the bounded "hold until HMD_SYNC changes").
3. **Then polish** whatever the device run exposes (menus/HUD readability, scales).
