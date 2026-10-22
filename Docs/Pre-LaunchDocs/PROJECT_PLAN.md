# Shogo VR -> WinlatorXR: Real Implementation Plan

Port the existing **Shogo: Mobile Armor Division VR mod** (PCVR, by BrandonW24) to
**WinlatorXR** by replacing its OpenXR headset bridge with the WinlatorXR **XrAPI**,
in-process inside `CShell.dll`, targeting a **Meta Quest 3**.

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

STATUS (Phase 5 overlay ready): `tools/package_overlay.py`
assembles `build/overlay/ShogoVR/` from the built `CShell.dll` + `AUTHORS.txt` +
`NOTICE.txt` (optionally a `ShogoVR.ini`). Copy that folder next to `Client.exe`
inside the container and launch with the `-rez` list. (Superseded: this was the
single largest unknown until the first device run - see the device-run status
below.)

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

STATUS (device run, 10-14 Oct 2026): the port **has run on the target** - Meta
Quest 3 via WinlatorXR, launched from the custom `ShogoVR.exe` (launcher-xr), with
the game at **2800x1400 SBS** (1400/eye). The on-device sessions exposed a set of
comfort / render / input issues; those are logged, root-caused and almost all fixed
in `Docs/PreReleaseBugs/BUGS_TO_FIX.md` (#1-#39; archived 19 Oct 2026 when the 0.8 tracker took over
at `Docs/PreReleaseBugs/BUGS_TO_FIX_V0.7_to_V0.8.md`, then #40 to #44; archived 20 Oct 2026 when the 1.0 tracker took over). As of the 14 Oct 2026 headset pass the list was
nearly closed - the original 10-11 Oct batch (head roll #1, hand roll #2/#8, the
controller input path #3, fire #4, menu sizing/centring #5/#6, the stale-data hang
#9, the stereo splash/movies #10, left-stick movement #11, render resolution #13,
crosshair default #14, the control layout #15, respawn-on-death #16, the face-button
leak #17) plus the later zoom removal (#19), the Right-A/B view + transform toggles
(#23/#31/#36), the VR body hidden outside first-person (#21), shots-vs-model (#27), the
hurt / explosion flash (#28), the muzzle flash at the visible gun (#29), the trimmed VR
menu (#30), the lower-than-origin weapon model (#34) and dominant hand (#35) are all
verified. Supersampling (#7) is dropped, #18 folds into the #19 zoom removal, and the
autosave family (#24/#25/#33) plus the intermittent jump flag (#32) are
**removed/cancelled** - players save/load manually. No bug is still **open**, and none awaits a
device pass: #20 (crouch lowers the eyes and the held gun), #22 (single 15% left-stick move dead
zone), #37 (right-stick weapon-swap dead zone), #38 (the Keyboard / Mouse / Joystick pages are
hidden from Options) and #39 (Quit exits immediately) are all **fixed and verified on device
(17 Oct)** - the dead zones are live settings on the VR screen and in the config; #12
(loading/bumper/credits) and #26 (intro-video y-offset + 50 px) are **fixed and verified
on device (14 Oct)**.
On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`; `#4` (fire) and `#9`
(sync hold) were verified on the desktop with the XrApi UDP Tester. **For that work the live
issue tracker was `Docs/PreReleaseBugs/BUGS_TO_FIX.md` (#1-#39), not the phase statuses below;
the previous 0.8 tracker was 'Docs/BUGS_TO_FIX_V0.7_to_V0.8.md'
the current 1.0 tracker is `Docs/BUGS_TO_FIX.md`.**

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

The V1.0 launch is almost ready now, we have the few final tweak suggestions from the user before deploy

The text below for sub-point number 6 is old information, pre-1.0 release and is only kept for historical purposes:

Progress: **Phases 0-5 are code-complete and the port has run on the target**
(Meta Quest 3 via WinlatorXR, launched from `ShogoVR.exe`, 2800x1400 SBS). The
10-13 Oct 2026 on-device sessions produced a set of comfort / render / input bugs
that are now the **live issue log in `Docs/PreReleaseBugs/BUGS_TO_FIX.md` (#1-#39)** - that file,
not the phase statuses above, was the source of truth for the pre-release work (the 0.8 cycle opened a
fresh `Docs/BUGS_TO_FIX.md` on 19 Oct 2026). As of the 17 Oct
2026 pass **no bug is open and none is awaiting a device pass**: #20 (crouch lowers the eyes **and
the held gun**), #22 (the single 15% left-stick move dead zone), #37 (right-stick weapon-swap dead
zone), #38 (the Keyboard / Mouse / Joystick pages are hidden from Options) and #39 (Quit exits
immediately) were all **verified on device** and joined the verified set. The dead zones are live
settings (`VRMoveDeadZone`, `VRWeaponSwapDead`) on the VR screen and in `ShogoVR.ini`. #12
(loading/bumper/credits per eye) and #26 (intro-video offset + 50 px) are **fixed and verified on
device**. `#4` (fire) and `#9` (sync hold) were verified on the desktop with the XrApi UDP Tester.

The 11 Oct 2026 session added six more in-code fixes (all since verified on device):

- **#13 render resolution** - `launcher-xr\ShogoVR.cpp` now passes
  `+screenwidth W +screenheight H` from the desktop resolution (and `build.cmd`
  links `user32.lib`), instead of the engine's 640x480 default.
- **#14 crosshair off by default** - the mod aim helper (`VRAimStyle`) defaults to
  none, and the game's own crosshair is gated by a new `VRGameCrosshair` setting
  (default off) via a `BeginCrosshair`/`EndCrosshair` pair wired in
  `PlayerStats.cpp`. A "game crosshair" row was added to the in-game VR menu
  (`VRMenu.cpp`, capacity 27 -> 28 in `VRMenu.h`/`BaseMenu.h`); the VR menu's
  action-row index constants were also corrected (they had been off by one since
  the "aim helper" row was added).
- **#15 controller mapping (revised)** - `XrApiUDP.cpp` `ButtonBits()` and
  `VRStereo.cpp` `VRCommandOn`/`UpdateInput` implement the requested layout
  (left stick move, left stick click crouch-while-held, X crouch toggle, left grip
  jump, left Y zoom, left trigger nothing while alive, right trigger fire, right
  grip double jump, A view toggle, B transform, right stick turn + weapon cycle,
  right stick click recentre). New `SHOGOVR_BTN_CROUCH_HOLD` bit; the
  `SHOGOVR_BTN_PREV_WEAPON` bit was renamed to `SHOGOVR_BTN_LEFT_TRIGGER` in both
  `ShogoVRShared.h` copies, and the prev/next-weapon buttons were dropped.
- **#16 autosave/quicksave on death** - `RiotClientShell.cpp` feeds
  `g_VRStereo.SetPlayerDead(IsPlayerDead())` each frame and calls
  `g_VRStereo.SetLevelLoaded()` when a level becomes playable; the mod taps F6
  (quicksave) once ~2 s into gameplay, and the left trigger taps F9 (quickload)
  while dead (replacing the dead-end Enter path). Documented as edit 18 in
  `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`.
- **#17 face-button key leak** - WinlatorXR also sends the controller face
  buttons as the keyboard letters X/Y/B/A; `VRStereo.cpp`
  `ShouldSwallowFaceKey()` plus `RiotClientShell.cpp` `OnKeyDown`/`OnKeyUp`
  (`ClearInput()` + early return) drop them while playing and alive. **#36
  (14 Oct):** `ClearInput()` is now skipped for the letter `B` - it cleared the
  *whole* input state and cancelled the `VehicleModeToggle` key the same button
  injects (`V`), so Right B needed repeated taps; the leaked `B`-driven
  `ChaseView` is already ignored by the #31 `OnCommandOn` guard.
- **#18 Left-Y weapon zoom (removed)** - no zoom command exists; zoom was toggled
  by re-selecting the equipped weapon (`VRStereo.cpp` cached the held weapon id in
  `UpdateHeldGun` and `UpdateInput` tapped `GetCommandId(...)`). This is removed with
  the #19 zoom removal.

This brings the tracked ShogoSource notes to **eight** files
(`ShogoSourceModifications/` now also documents `PlayerStats.cpp`).

The 12 Oct 2026 session added a fresh device bug list (now archived at
`Docs/PreReleaseBugs/BUGS_TO_FIX.md`, then #1-#36) and worked several of them in code; the 13 Oct 2026 headset pass
confirmed the input / render / menu work (#1-#6, #8-#11, #13-#17) and ruled out the
#19 zoom approach, while re-opening the camera half of #20 and #28:

- **#19 weapon zoom (was a release blocker) - removed in code (13 Oct).** The
  pin-FOV half is kept (`VRStereo.cpp` `RenderStereo()` pins the eye FOV to the
  headset base and leaves the eye separation at the full IPD, so the presented
  scale and the reported frame FOV can never change - the narrow FOV had made the
  runtime present a tiny centre image that could stick). The **scope-cursor
  overlay failed on device**, so zoom is gone: the `ZOOM` mapping / `SHOGOVR_BTN_ZOOM`
  bit, the Left-Y re-select, `DrawScopeOverlay()`, `m_bScoped` and the scope
  surfaces are all removed (with #18). True magnification needs the offscreen
  second pass if it is ever wanted.
- **#5 menu virtual layout (finished)** - `VRStereo.cpp` `ComputeLayout()` sets
  `m_bMenuLayoutDirty` when the menu virtual size changes and `UpdateBridge()`
  sets it on the bridge-alive transition; `GetMenuLayoutSize()` is implemented;
  `End2D()` clears `m_bVirtual2D`; `Hook_GetSurfaceDims` substitutes the virtual
  size for the HUD *and* menu passes. On the ShogoSource side,
  `CRiotMenu::ScreenDimsChanged()` now asks `g_VRStereo.GetMenuLayoutSize()`
  (new prose note `RiotMenu.cpp.md`) and `RiotClientShell::Update()` re-runs it
  when `ConsumeMenuLayoutChanged()` fires (edit 7, `RiotClientShell.cpp.md`).
  Menus now lay out on a 640x480 virtual canvas that the per-eye xform scales up
  into the panel. **Verified on device 13 Oct** - menu text is readable.
- **#20 crouch (body feedback + eye height)** - new `CVRStereo::IsCrouchOn()` returns the
  same crouch state `VRCommandOn(DUCK)` uses (the `m_bCrouchOn` toggle or
  `SHOGOVR_BTN_CROUCH_HOLD` held). `RiotClientShell.cpp`
  `UpdateServerPlayerModel()` passes it into `g_VRStereo.UpdateBody(...)`, so
  the VR body plays its crouch animation while crouching. The 13 Oct device pass
  confirmed the animation but showed the **camera never moved**.
  **Camera half fixed in code (14 Oct):** the mod now lowers the HMD world position while
  crouched - `CVRStereo::UpdateCrouchOffset()` eases `m_fCrouchAmt` toward `IsCrouchOn()`, and
  `RenderStereo()` subtracts `m_fCrouchAmt * VRCrouchDrop` (new setting, default 0.6 m) along the
  body's up axis from the eye base position, so the stereo view drops smoothly and lifts on release.
  **Held-gun follow-up (14 Oct):** the camera object is restored to its standing spot after the eyes
  are drawn, so the gun (and muzzle flash / aim) still saw the un-crouched camera and stayed up;
  `GetHandWorld()` now subtracts the same eased drop from the hand position, so the gun drops with
  the eyes too. **Verified on device (2026-10-17):** the eyes and the held gun drop together while
  crouched. The body showing in third-person / cutscene views moved to #21.
- **#28 hurt / explosion flash** - the shell now hands the tint colour to the mod
  (`g_VRStereo.SetScreenFlash`) instead of using the engine camera light-add (whose
  full-screen quad broke under the stereo render), and the mod draws a clean
  per-eye **edge vignette** (`DrawScreenFlash`) in the HUD pass. Opaque nested
  frames, since the 2D API has no alpha; the centre of vision stays clear.
  Documented as edit 19 in `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`.
  **Re-fixed 13 Oct:** the first version drew inside the HUD's optimized-2D block and did not reach
  the captured frame. The draw now runs on the **post-render path, beside the WinlatorXR sync
  pixel** (`CVRStereo::DrawScreenFlashFrame()` after `End3D()` in `RenderCamera()`,
  `UpdateMenuState()` and `UpdatePausedState()`), and the flash is now just **two rectangles per
  frame** - a full-height band down the outer edge of each eye (left eye's left side, right eye's
  right side), a peripheral-vision "tunnel vision" flash. The earlier nested-frame vignette was ~20
  fills per frame and too heavy on device; this is 2. It logs on/off and its first draw, and a
  `VRFlashTest` CVar forces a red flash for testing; on the PC that lights the two outer bands.
  **Verified on device (2026-10-13):** the player confirms the peripheral flash works and is not too
  expensive.
- **#29 muzzle flash moved to the visible gun** - the game positions its flash sprite from the
  *flat-screen* muzzle (`CWeaponModel::m_vFlashPos` = camera + weapon offset + muzzle offset), so
  with VR holding the gun in the hand the flash sat above and behind the barrel. The mod now
  supplies the **visible** gun's muzzle each frame via
  `CVRStereo::GetMuzzleFlashWorld(nWeaponId, vCamPos, bFootScale, vOut)` (mirroring either
  `AdjustWeaponPose`'s first-person pose or `UpdateHeldGun`'s held pose, then offsetting by the
  game's own `GetWeaponMuzzleOffset()` scaled by the gun scale). The shell fills a new
  `CWeaponModel::m_vFlashDrawPos` from it (`UpdateWeaponModel`) and draws the sprite there
  (`UpdateFlash`). `m_vFlashPos` - the **shot origin** sent in `MID_WEAPON_FIRE` - is left alone, so
  aiming is not disturbed; when VR isn't posing the gun by hand the mod returns `DFALSE` and the draw
  falls back to `m_vFlashPos`. Documented as edit 3 in
  `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md`. Build clean; **verified on device
  (2026-10-14)**.
  **Follow-up (13 Oct):** the on-foot **dual pistols** (Colt 45, `GUN_COLT45_ID`) fire from two
  barrels alternately (`W_FIRED` / `W_FIRED2`), but the helper only knows the one `W_FIRED` muzzle,
  so it pinned both flashes to the same barrel. `GetMuzzleFlashWorld()` now returns `DFALSE` for that
  weapon, so it keeps the game's own alternating flashes; every other weapon keeps the fix.
- **#30 in-game VR menu trimmed** - the settings page went from 28 rows to 7
  (`game/VRMenu.cpp` `s_Settings`, `VRMENU_ITEMS` in `VRMenu.h`): dominant hand,
  hand roll flip (now reading **off**/**on**), turning (default smooth), smooth turn
  speed, recentre view, reset to defaults, back. The removed options are pinned to
  their listed defaults, correcting `VRSnapTurn` -> smooth, `VRMoveDir` -> where you
  look, `VRTwoHand` -> off and `VRGunScaleMCA` -> 1.5x in the `CVRStereo` VarTrack
  `Init` calls. Build clean; **verified on device (2026-10-14)**. **Follow-up (14 Oct):** the
  left/right, forward/back and weapon-swap dead zones were added back as three rows
  (`VRMoveDeadZoneX`/`Y`, `VRWeaponSwapDead`) at the owner's request. **Follow-up (15 Oct):** the
  split left/right and forward/back rows collapsed into a single "move dead zone" (`VRMoveDeadZone`,
  15%), so the page is now 9 rows.
- **#35 dominant hand now works** - `Controls/LeftHanded` was read only by the PCVR bridge, so on
  the WinlatorXR path the toggle was inert. `CVRStereo` now reads it itself
  (`LoadSettingsFile`, logged as `VR: dominant hand left/right`) and, when left-handed, swaps the two
  hand poses (so the gun models follow the other controller) and swaps **only** the trigger/grip
  actions - left trigger fires, left grip double-jumps - leaving the sticks and face buttons alone.
  Build clean; **verified on device (2026-10-14, user-confirmed)**.
- **#21 VR body hidden outside first-person (14 Oct)** - `UpdateServerPlayerModel()`
  now passes `bShow = m_playerCamera.IsFirstPerson() && !m_bUsingExternalCamera && !IsPlayerDead() &&
  !IsSpectatorMode()` to `g_VRStereo.UpdateBody(...)`, so the mod's copy of the player body is drawn
  only in normal first-person gameplay. In third-person (chase), cutscene (external camera) and
  death / spectator views the game's own model is the one shown, so the "two mechs / two players"
  overlap is gone. Build clean; **verified on device (2026-10-14, user-confirmed)**.
- **#22 single left-stick move dead zone (fixed and verified, 14-17 Oct; RADIAL restored 20 Oct)** -
  `GetMoveAxes()` used one radial
  `0.15` for both axes; it now uses a forward/back dead zone of `0.08` and a strafe dead zone of
  `0.20` (20%, the 14 Oct device request), rescales each axis from its own edge, then caps the
  combined magnitude at 1, so a mostly-forward push no longer drifts into a diagonal. **Follow-up
  (14 Oct):** both thresholds are now live settings
  (`VRMoveDeadZoneX` = 0.20, `VRMoveDeadZoneY` = 0.08, clamped to `0..0.95`), tunable in
  `ShogoVR.ini` and on the VR screen. **Follow-up (15 Oct):** the split was dropped in favour of a
  **single** `VRMoveDeadZone` (default 0.15 = 15%, clamped to `0..0.95`) applied to both axes; the two
  per-axis settings and their VR rows were removed. The per-axis application is kept, so a
  mostly-forward push still cannot drift sideways. **Verified on device (17 Oct, user-confirmed):** the
  single 15% move dead zone ("confirmed for now").
- **#22 follow-up (20 Oct) - the per-axis split was WRONG on device.** The per-axis dead zone above
  felt broken in play: movement only happened "diagonally", as if an axis needed the stick past the
  dead zone on **both** X and Y. The owner asked for the dead zone to stop coupling the axes and
  pointed at the upstream Shogo-VR implementation. It is now restored **verbatim**
  (`https://github.com/BrandonW24/Shogo-VR/blob/main/game/VRStereo.cpp`): a single **radial** dead
  zone - `fLen = sqrt(moveX^2 + moveY^2)`, `DFALSE` if `fLen <= VRMoveDeadZone`, otherwise one common
  rescale factor applied to the whole vector for `fForward`/`fRight`. The dead zone stays live
  (`VRMoveDeadZone`, default `0.15`). This is `game/`-only - no `ShogoSourceModifications/` note.
  See `Docs/BUGS_TO_FIX.md` #44. Build clean; awaiting a device pass.
- **#37 right-stick up/down weapon-swap dead zone (fixed and verified, 14-17 Oct)** - `PollWeaponCycle()`
  cycled weapons as soon as `|turnY| > 0.7`, so a light up/down nudge could change weapons. It now
  requires a deliberate push (`fSwapPress = 0.80`) and re-arms only below `fSwapReset = 0.45`
  (hysteresis). **Follow-up (14 Oct):** the press threshold is
  now the live setting `VRWeaponSwapDead` (0.80; the reset stays 0.35 below it), tunable in
  `ShogoVR.ini` and on the VR screen. **Verified on device (17 Oct, user-confirmed).**
- **#12 loading / bumper / credits per eye (fixed and verified on device, 14 Oct)** - these screens are
  flat 2D blitted straight to the full SBS window, so each eye saw half of one picture. They now go
  through the same per-eye treatment as the menus (#5): `UpdateLoadingLevelState()` and
  `UpdateBumperState()` mirror `UpdateMenuState()` (begin the frame, render the polygrid through the
  panel in stereo, then draw the 2D art once per eye between `Begin2D(nEye)`/`End2D()`), and the
  credits / intro states now call a new `UpdateCreditsStereo()` helper that wraps `m_credits.Update()`
  the same way. `Credits.cpp` lays the text out on the mod's small virtual screen
  (`g_VRStereo.GetMenuLayoutSize()`) so the per-eye remap centres it in each eye and picks the larger
  font. Documented as edit 22 in `RiotClientShell.cpp.md`, edit 2 in `RiotClientShell.h.md`, and a new
  `Credits.cpp.md`. Owner confirmed the loading screen and credits ("great"); the bumper shares the
  path.
- **#26 intro video lowered (fixed and verified on device, 14 Oct)** - `ShiftMovieToRightHalf()` now
  blits the per-eye movie clones at `y = screenHeight / 8 + 50`, clamped so a full-height frame still
  lands fully on-screen, instead of at `y = 0` (the `+ 50` is the owner's 14 Oct "about 50 px lower"
  follow-up). Follow-up: the deeper offset made the movie clip at the **bottom** - the
  not-detected **fallback** blits the **whole window**, which can't be shifted down without losing
  its bottom edge, so it now stays at `y = 0`. `DetectMovieSource()` was also made robust
  (vertical extent first, then a superset of the old rows) so the offset still applies whenever the
  frame is found. Confirmed by the owner: "the video is perfect now".
- **#38 Options menu trimmed (fixed and verified on device, 15-17 Oct)** - the Keyboard, Mouse and
  Joystick binding pages are hidden from the Options menu (the controllers own input): `COptionsMenu`
  now lists only Screen, Sound, vr and Back. The rows were removed from `LoadSurfaces()`, the matching
  `Return()` branches deleted, and the item-count loops reduced from seven to four. Documented as
  section 6 in `ShogoSourceModifications/ClientShellDLL/OptionsMenu.cpp.md`.
  **Verified on device (17 Oct, user-confirmed).**
- **#39 Quit exits immediately (fixed and verified, 15-17 Oct)** - the Exit item used to show an "are you
  sure?" box that waits for the Y key, which the controllers cannot easily send. `CMainMenu::Return()`
  now calls `ExitGame(DTRUE, 0)` directly, so the box is never created and Quit is a single action.
  Documented in the new `ShogoSourceModifications/ClientShellDLL/MainMenu.cpp.md`.
  **Verified on device (17 Oct, user-confirmed).**

This brings the tracked ShogoSource notes to **eleven** files
(`ShogoSourceModifications/` now also documents `RiotMenu.cpp`, `Credits.cpp` and `MainMenu.cpp`).

The immediate next actions are:

1. **Device verification is up to date.** The fixed set is now verified on device
   (13-14 Oct): the 10-11 Oct batch (#1-#6, #8-#11, #13-#17), the zoom removal
   (#19), the VR body hidden outside first-person (#21), shots-vs-model (#27), the hurt
   / explosion flash (#28), the muzzle flash at the visible gun (#29), the trimmed VR
   menu (#30), the lower-than-origin weapon model (#34), dominant hand (#35) and the
   Right-A/B view + transform toggles (#23/#31/#36, B confirmed 14 Oct). The 17 Oct
   headset pass cleared the last five: #20, #22, #37, #38 and #39.
2. **Clean-up pass - verified.** Zoom is removed (#19, which also drops the #18
   Left-Y re-select), the in-game VR menu is trimmed to a short list (9 rows, including the
   move and weapon-swap dead zones; the move dead zones collapsed to one on 15 Oct), the
   removed options are pinned to their defaults (#30), and the hurt / explosion flash is
   re-drawn on the post-render path (#28). #7 (supersampling) is dropped and not
   pursued.
3. **Removals (14 Oct).** The autosave family - on-foot autosave (#24), autosave
   re-arm (#25) and the in-level area-load autosave (#33) - is **cancelled**; the
   owner decided players can save/load manually if they hit an issue. The
   intermittent jump flag (#32) is **removed** as it appears fixed on device.
   **#36 (Right B transform, 14 Oct):** the leaked face-letter `B`'s `ClearInput()`
   was wiping the `V` key the transform injects from the same press; `ClearInput()`
   is now skipped for `B` (the leaked `B` is already ignored by the #31 guard) and a
   single B press transforms both ways.
4. **Device verification is complete - nothing is left in code only.** No tracked bug is open
   and none awaits a device pass: #20 (crouch lowers the eyes **and the held gun**), #22 (single
   15% left-stick dead zone), #37 (right-stick weapon-swap dead zone), #38 (Keyboard / Mouse /
   Joystick pages hidden) and #39 (Quit exits immediately) were all **verified on device
   (17 Oct, user-confirmed)**.
   The move and weapon-swap dead zones are live settings (`VRMoveDeadZone`, `VRWeaponSwapDead`)
   on the VR screen and in `ShogoVR.ini`, so they can be tuned in the headset. #12 (loading /
   bumper / **credits** screen per eye) and #26 (intro-video y-offset + 50 px) are **fixed and
   verified on device (14 Oct)**.
5. **Keep the live tracker `Docs/BUGS_TO_FIX.md` current** - it now covers the **0.8** cycle
   (`#40` onward); the pre-release items `#1`-`#39` are archived in
   `Docs/PreReleaseBugs/BUGS_TO_FIX.md`. Note each fix's device-verification status and drop the
   matching prose note under `ShogoSourceModifications/` plus a `PROJECT_PLAN.md` entry for any
   ShogoSource change.
6. **Auto-update (new - see section 7).** Done on both sides: the latest stable build, its
   version number and a short changelog live in `AutoUpdate/` (`CShell.dll`, `version.txt`,
   `notes.txt`), and `ShogoVR.exe` (`launcher-xr/`) now reads the online `version.txt`,
   prompts **YES/NO** when it differs from the local copy, and on YES stages `version.txt` +
   `CShell.dll` in `ShogoVRUpdate` before installing them. **The 0.7 release is published**
   (19 Oct 2026 - `AutoUpdate/version.txt` and `launcher-xr/version.txt` were bumped to `0.7` and
   pushed, so the updater offers it): the rebuilt `ShogoVR.exe` and the byte-identical `CShell.dll`
   are deployed to the GOG game folder and to `Release/`, and the launcher's update / no-answer /
   YES-install / NO-launch paths were re-checked against the deployed bytes. Running the updater
   itself inside the container under Wine/WinlatorXR is still untested on device.

**0.8 cycle opened (19 Oct 2026).** With 0.7 published, the owner returned with the first four items
for 0.8: continued-fire haptics (**#40**), the dual-pistol muzzle-flash position (**#41**), the
right-grip jump sound on foot (**#42**) and the level quicksave logic (**#43** - the earlier autosave
family, archived `#24`/`#25`/`#33`, is picked up again now that the death-recovery save matters). All
four are fixed in code (19 Oct) and await a device pass; the root cause and fix direction for each are
recorded in `Docs/BUGS_TO_FIX.md`, and the pre-release log moved to
`Docs/PreReleaseBugs/BUGS_TO_FIX.md`.

**0.8 fixes in code (19 Oct 2026), build clean:**

- **#40 haptics while firing** - `VRStereo.cpp` `UpdateBridge()` now sets the gun hand's
  haptic to **1 every frame** while `SHOGOVR_BTN_FIRE` is held (gameplay input + controller
  live), instead of a 4-frame pulse on the fire edge; WinlatorXR handles the fade-out. The
  weapon-switch blip is unchanged.
- **#41 dual-pistol muzzle flashes** - `GetMuzzleFlashWorld()` no longer exempts
  `GUN_COLT45_ID`; it takes the fired `WeaponState` (`W_FIRED` / `W_FIRED2`) and uses the
  matching `GetWeaponMuzzleOffset`, so each alternating barrel gets its own flash. The shell
  passes `eState` through at the `UpdateFlash(eState)` call site - a ShogoSource edit,
  documented as edit 3 in `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md`.
- **#42 right-grip jump sound on foot** - on foot, `VRCommandOn(COMMAND_ID_DOUBLEJUMP)` now
  answers `DFALSE`; `COMMAND_ID_JUMP` already covers both grip bits, so the right grip
  performs a regular jump and plays its sound. MCA / vehicle keep the separate double jump.
- **#43 quicksave** - the mod tracks `m_bPlayerJustDied` (set from the existing per-frame
  `SetPlayerDead(IsPlayerDead())` call); `SetLevelLoaded()` clears it without arming the save
  when set, and arms normally otherwise, so a death quick-load no longer overwrites the
  level-start save. Bumper levels, which leave `GS_LOADINGLEVEL` in `OnEnterWorld()` and so
  never reached `UpdateLoadingLevelState()`, are now armed at the `GS_BUMPER -> GS_PLAYING`
  transition in `OnKeyDown()` - documented as edit 23 in
  `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`.

**0.8 second device pass (20 Oct 2026).** The owner tested the 19 Oct build on the Quest 3:
**#43 PASS** and the **#5 value-based launcher update check PASS**; **#40, #41 and #42 came back
FAIL**, each for a specific reason, and all three were revised the same day (build clean):

- **#40 - PASsed on behaviour, rejected on feel.** Vibrating for as long as the trigger is held
  is wrong for a *shot*: a shotgun or charged shot should be one kick, not a shake, and the
  weapon-switch blip was unintended. The pulse now comes from
  `CVRStereo::ShotFired()`, called from the **one** place ammo is actually spent,
  `CWeaponModel::Fire()` (ShogoSource edit - documented as edit 4 in
  `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md`), so it fires exactly once per
  shot and never on a switch, a dry trigger hold or an idle frame. `VRHaptics` still gates it and
  `LeftHanded` still picks the hand.
- **#41 - both flashes had been placed at the same spot, left of both guns.** The muzzle-offset
  table's left/right value only exists to pick *which* barrel of a two-barrel weapon fires and was
  measured for a different gun model, so it was meaningless sideways displacement in the hand.
  For the held gun the lateral component is now a **single constant** rather than the table's
  per-barrel value, the vertical is set below the barrel and the depth is pushed out past the
  muzzle - three new live CVars, `VRFlashOffset` (default 3, out), `VRFlashLower` (default 3,
  down) and `VRMuzzleLateral` (default 3, onto the pistols), so the feel can be tuned without a
  rebuild. Barrel alternation stays in the shot, where the engine reads it.
- **#42 - the command-level `DFALSE` did not produce an audible jump.** Per the owner's
  direction the remap moved to the **XrApi boundary**: in `CVRStereo::UpdateInput()`, where the
  runtime's button word is latched, a right-grip (double-jump) bit on foot is folded onto the left
  grip before the edge sets or any command sees it. `m_nRawButtons` keeps the pre-remap word.
  `COMMAND_ID_JUMP` answers on the jump bit alone and `COMMAND_ID_DOUBLEJUMP` on its own bit
  alone. A level load resets the known player mode (`ResetMechModeKnown()` from the shell at
  `MID_PLAYER_MODECHANGE` - edit 24 in
  `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`) and an unknown mode counts as
  on foot, the safe direction. Checked offline against a table of the button bits for on-foot
  (known and unknown mode), mech/vehicle, idle, inactive-controller and weapon-switch cases.

`CShell.dll` was rebuilt and redeployed to `Release/ShogoVR/CShell.dll` and the GOG
`ShogoVR/` folder (identical bytes). The launcher is unchanged by the second pass.

**0.8 third device pass (20 Oct 2026).** Owner's report on the second-round build:

- **#40 - PASS, "works great, fantastic haptics."** Done and signed off.
- **#41 - FAIL.** Both flashes still landed **left of the left pistol**. The owner again asked for
  only forward/back and up/down to change, and said the game's own default beats the broken result.
- **#42 - FAIL, and the mech double jump broke with it.** The owner suspected the mod cannot tell
  on-foot from in-mech - correct.

**Third-pass fixes (20 Oct 2026, build clean):**

- **#41** - the mod had collapsed both pistol barrels onto one spot; the addendum clarified the
  intent is **one flash per barrel** (left hand / right hand) with the game's own left/right value.
  The Colt 45 now **keeps** the table's per-barrel left/right (`-6.2` left / `+5.8` right), so each
  barrel flashes in its own hand, while forward/back (`VRFlashOffset`) and up/down (`VRFlashLower`)
  are still corrected for **all** held weapons. Single-barrel weapons use the constant
  `VRMuzzleLateral` (default back to `0`, on the axis). Verified offline against the real table
  values: opposite sides, the game's 12-unit separation, single-barrel on the axis.
- **#42** - **the root cause was a wrong on-foot test in the shell.** The call site passed
  `SetMechMode(m_nPlayerMode != PM_MODE_KID, ...)`, but `PM_MODE_FOOT` is **0**, `PM_MODE_KID` is
  **5**, and `IsOnFoot()` is `== PM_MODE_FOOT` - so on foot the shell reported "mech", the grip fold
  was skipped and the MCAs saw an unreliable mode (breaking the mech double jump). The shell now
  passes `SetMechMode(!IsOnFoot(), ...)`; the mod's foot test is one named helper, `OnFoot()`
  (unknown mode counts as on foot). Re-checked offline with the corrected signal - the mech case now
  keeps the right grip as `COMMAND_ID_DOUBLEJUMP`.

`CShell.dll` was rebuilt and redeployed to `Release/ShogoVR/CShell.dll` and the GOG `ShogoVR/`
folder (identical bytes).

**0.8 fourth device pass (20 Oct 2026).** Owner's report on the third-round build:

- **#41 - close.** One flash per hand, alternating correctly; the two flashes are simply **a little
  too wide apart**. Owner: bring them **just a tiny bit closer together** and the pistols are good.
- **#42 - FAIL, "badly" - the double jump does not work at all.** Owner's call: **stop trying**,
  restore the grips to a **plain 1:1 mapping** (left grip = Jump, right grip = Double jump) and
  leave **#42 as a known bug** for now.

**Fourth-pass changes (20 Oct 2026, build clean):**

- **#41** - the dual pistols keep the game's own per-barrel left/right (barrel 1 left, barrel 2
  right - one flash per hand), but it is now **scaled about the centre** by a new live CVar
  `VRPistolSpread` (default **0.7**): `-6.2 -> -4.34` and `+5.8 -> +4.06`, so the span narrows from
  the game's `12.0` to `8.4` while the barrels stay on **opposite sides** (they are never given the
  same x - that was the original bug). `1.0` reproduces the game's own spread exactly. Forward/back
  (`VRFlashOffset`) and up/down (`VRFlashLower`) are unchanged for all held weapons; single-barrel
  weapons keep the constant `VRMuzzleLateral` (`0`). Verified offline against the real table values.
- **#42** - **all remap attempts removed; the plain 1:1 grip mapping is restored** and the item is a
  **known, deferred bug** (see `Docs/BUGS_TO_FIX.md` #42). In `game/` the `UpdateInput()` fold, the
  `OnFoot()` helper, `ResetMechModeKnown()` and `m_bModeKnown` are gone; `VRCommandOn` answers
  `COMMAND_ID_JUMP` on the jump bit and `COMMAND_ID_DOUBLEJUMP` on its own bit, 1:1. The symptom
  (silent jump on foot) is the game's own design: a human's double jump performs a normal jump but
  deliberately makes no sound.
- **Mode signal kept correct (owner's rule).** The shell no longer uses `PM_MODE_KID` as an "is
  mech" test. `PM_MODE_FOOT` (**0**) and `PM_MODE_KID` (**5**) are **both on foot** (the kid is cut
  content - a past-life version of the characters - and moves like a human); **every other mode is an
  MCA** (the walking mech that transforms into a vehicle). The call site computes
  `bOnFoot = (m_nPlayerMode == PM_MODE_FOOT || m_nPlayerMode == PM_MODE_KID)` and passes `!bOnFoot`
  (edit 24 in `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`). It is correct so any
  future use starts from the right value; with the remap gone only the HUD log consumes it. Verified
  offline against the full `PlayerModeTypes.h` list.

`CShell.dll` was rebuilt and redeployed to `Release/ShogoVR/CShell.dll` and the GOG `ShogoVR/`
folder (identical bytes).

**0.8 fifth device pass (20 Oct 2026).** Owner's report on the fourth-round build:

- **#41 - still a little too far apart.** Owner: bring the two flashes **horizontally closer
  together still**, towards the middle, **by about a tenth of the distance**.
- **#42 - solved.** The owner's insight: **send both `JUMP` and `DOUBLEJUMP` from the right grip at
  the same time**, so a regular jump (with its sound) *always* happens and the game still gets the
  double jump in a mech. No mode detection needed, and nothing is ever taken away.

**Fifth-pass changes (20 Oct 2026, build clean):**

- **#41** - `VRPistolSpread` default lowered from `0.7` to **`0.63`** (a further 10% off the span:
  `0.7 * 0.9`). Barrel 1 `-6.2 -> -3.91`, barrel 2 `+5.8 -> +3.65`, span `8.4 -> 7.56`; still on
  opposite sides of the axis, everything else unchanged. Live CVar, so it can still be nudged
  towards `1` if it is now too close. Verified offline against the real table values.
- **#42** - **the right grip now sends both jump bits.** In `CVRStereo::UpdateInput()`, after the
  left-handed swap, `if (m_nButtons & SHOGOVR_BTN_DOUBLEJUMP) m_nButtons |= SHOGOVR_BTN_JUMP;`.
  `VRCommandOn()` is back to a plain 1:1 answer on each command's own bit, so when the right grip is
  held **both** commands are true: the regular jump plays its sound, and the engine still sees the
  double jump where it means something (MCA). No remap, no mode gate, and the double-jump bit is
  never cleared. This supersedes the three failed remaps - **replacing** one grip's action can never
  be right; both actions have to happen. Verified offline across button-bit cases (each grip, both,
  idle, non-jump buttons, left-handed). **This is the 0.8 fix for #42** - the "known bug" state of
  the fourth pass is over.
- **Mode signal** stays as corrected in the fourth pass (`PM_MODE_FOOT` and `PM_MODE_KID` are both
  on foot; the shell passes `bOnFoot = (mode == PM_MODE_FOOT || mode == PM_MODE_KID)`). The fix no
  longer needs it, but it is correct.

`CShell.dll` was rebuilt and redeployed to `Release/ShogoVR/CShell.dll` and the GOG `ShogoVR/`
folder (identical bytes).

**0.8 sixth device pass (20 Oct 2026).** Owner: **neither #41 nor #42 is fixed**, and the supplied
`ShogoVR_game.log` explained why in one line - it read `CShell.dll built Oct 20 2026 22:22:16` while
the source had been edited at `22:34`:

- **The build was stale.** `MSBuild /t:Rebuild` had been **silently skipping recompilation** (the
  object files were seen as newer than the edited sources), so the DLL on the headset contained
  **neither** the 5th revision **nor** the #42 change. Five device passes had been testing a build
  with none of those fixes in it. **This is the root cause of the whole cycle's "nothing works".**
- **#41's offsets were still gated on `bHeld`.** `VRGunModel` defaults to `0`, so on device the mod
  poses the **first-person model with arms**; the corrections never ran. The log's
  `(first-person gun; x/y/z offset -6.2,-1.3,12.2 ...)` - raw table values - proved it.

**Sixth-pass changes (20 Oct 2026, build clean):**

- **#41** - the offset corrections (`VRFlashOffset`, `VRFlashLower`, `VRMuzzleLateral`,
  `VRPistolSpread`) are **moved out of `if (bHeld)`**, so they apply in **both** branches (held gun
  *and* first-person model). The per-weapon x handling is unchanged. The log line now tags the
  pistols in either branch, so a device log shows which path ran.
- **#42** - logic unchanged and confirmed correct (right grip sets `JUMP | DOUBLEJUMP`); the owner
  pointed out it is simply "send the left grip's jump at the same time", which the left grip already
  proves works through the same command path. `UpdateInput` now logs **one line per grip press**
  (`VR: grip jump - bits after mapping JUMP=.. DOUBLEJUMP=..`) so the next pass is unambiguous.
- **New build script `build/build-and-deploy.ps1`** - clears the object tree, rebuilds from scratch,
  and **refuses to deploy** a DLL whose `built <date>` stamp is older than the newest `game/`
  source. Deploys to both targets and hash-checks them, then verifies `AutoUpdate/`,
  `Release/version.txt` and `Release/ReadMe.txt` are untouched. **Use this instead of a bare
  `MSBuild /t:Rebuild`** - that is what let the stale DLL through.

`CShell.dll` was rebuilt from scratch and redeployed to `Release/ShogoVR/CShell.dll` and the GOG
`ShogoVR/` folder (identical bytes).

**0.8 seventh device pass (20 Oct 2026).** First pass on a genuinely fresh build - and the fixes
finally showed:

- **#41 - progress.** The dual-pistol flashes are now correctly **in front of** their barrels, one
  per hand, alternating. Two small trims asked for: the sprites are **slightly too far ahead**
  (about **half a foot**) and **slightly too low** (about **3 inches**).
- **#42 - the owner's own fix.** In `game/XrApiUDP.cpp`, `ButtonBits()` now sets the jump bit for the
  **right grip as well as the left** (`if (XB(0) || XB(12))`), in addition to the right grip's
  double-jump bit - the fix at the source of the runtime word, which is the cleanest place.

**Seventh-pass changes (20 Oct 2026, build clean):**

- **#41** - trimmed both nudges: `VRFlashOffset` (forward) `3.0 -> 2.0` and `VRFlashLower` (down)
  `3.0 -> 2.4`, forward by more because its error was about twice the vertical one (both live CVars).
- **#42** - kept the owner's `XrApiUDP.cpp` fix and **removed the now-redundant `UpdateInput` line**
  so the intent lives in one place. **Left-handed mode now swaps only the triggers, not the grips**,
  so the right grip is "jump + double jump" in both handedness modes and the source-level fix covers
  left-handed play too. The 8-case offline check passes (the earlier 6-case one failed on
  left-handed-left-grip, which is what exposed the swap problem).
- **Grip press log** (`VR: grip jump - bits after mapping ...`) kept, so the next device log proves
  what the right grip produces.

`CShell.dll` was rebuilt from scratch and redeployed to `Release/ShogoVR/CShell.dll` and the GOG
`ShogoVR/` folder (identical bytes).

**0.8 eighth revision (20 Oct 2026) - movement dead zone (owner's report).** The owner reported that
the left stick only moved the player **diagonally**, as if an axis needed the stick past the dead
zone on **both** X and Y. The per-axis dead zone added to `GetMoveAxes()` for #22 (15 Oct) was the
cause - it coupled the two directions. It is now **restored to the upstream Shogo-VR radial dead
zone verbatim** (`fLen = sqrt(moveX^2 + moveY^2)`, one common rescale for the whole vector, `DFALSE`
if inside `VRMoveDeadZone`). The dead zone stays live (default `0.15`). `game/`-only change; tracked
as `Docs/BUGS_TO_FIX.md` #44. Build clean; awaiting a device pass.

---

## 7. Auto-update (launcher-xr / `ShogoVR.exe`)

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

### Status

- **Repository side: done.** `AutoUpdate/CShell.dll` (714240 bytes - the stable 15 Oct build,
  byte-identical to the 0.7 release build), `AutoUpdate/version.txt` (hand-bumped to `0.7`) and
  `AutoUpdate/notes.txt` are in place, and the stale `CShell.dll` line was
  removed from `.gitignore` so the release DLL is tracked (commits `b4fa06a`, `05c9e75`). Until
  the `0.7` bump is committed and pushed, the copy the updater reads on `main` is still `0.6`.
- **Launcher side: done (17 Oct).** `launcher-xr/ShogoVR.cpp` runs `TryUpdate()` before the
  game starts: a WinHTTP `GET` of `version.txt` + `notes.txt` (5 s resolve/connect and
  15/20 s send/receive timeouts, and *no* proxy lookup, so an offline headset fails fast
  instead of stalling), `MessageBoxW(MB_YESNO)` for the prompt, the `ShogoVRUpdate` staging
  folder and `CopyFileW` for the install. Every run appends to
  `ShogoVR_update.log` next to the EXE. (The updater links `winhttp.lib` via a
  `#pragma comment` in the source, so `launcher-xr/build.cmd` is unchanged.)
- **Verified on the desktop (17 Oct).** Every branch was exercised against the live release
  URL with the prompt answered through `WM_COMMAND`: up-to-date (no prompt); missing local
  `version.txt` with both **NO** (nothing touched) and **YES** (installs); the launcher
  sitting *inside* the `ShogoVR` overlay (installs into that folder); a faulty
  `version.txt` URL (HTTP 404 -> skipped, no prompt, the other URLs are never fetched, ~0.4 s);
  a faulty `CShell.dll` URL after a good prompt (nothing installed, staged file kept); the
  retry after that failure (installs); and a failed `CShell.dll` copy (old `version.txt`
  restored, staged files kept). The copy-failure branch is easy to hit: **install assumes
  `<game>\ShogoVR\` already exists** (it is part of every release package), so if the folder is
  missing the DLL copy fails - the rollback then restores/removes the local `version.txt`, keeps
  the staged files and re-offers the update next launch (i.e. it fails safe, never half-installed).
- **Re-checked against the deployed 0.7 bytes (17 Oct).** The rebuilt `ShogoVR.exe` was driven a
  second time in a throwaway folder: the un-answered prompt **blocks indefinitely** (no update is
  ever installed without an explicit YES); **NO** launches `Client.exe` with the game folder as the
  working directory and the expected command line, and the launcher waits for it; **YES** downloads
  and installs `version.txt` plus a `ShogoVR\CShell.dll` that is **byte-identical** to
  `AutoUpdate/CShell.dll` (SHA256 `5EE4A7C4...`, 714240 bytes), then empties `ShogoVRUpdate`.
- **Value-based compare (20 Oct 2026), built clean.** `ShogoVR.cpp` now decides with
  `ParseVersion` / `IsNewer` instead of `local == online`, so only a **strictly newer** online
  version is offered (section 7, step 3). Exercised against the live `0.7` channel with the
  prompt never answered (so nothing could be installed): installed `0.7` -> *"online is not
  newer, no prompt"*; `0.8-beta` and `1.0` -> no prompt; older `0.6` -> prompt (then *"declined
  by the player"*); missing local file -> prompt. That last pair is the old fallback rule and is
  deliberately unchanged. Deployed to the GOG folder and `Release/` as `Release/ShogoVR.exe`.
- **Still to try on device:** the updater's HTTPS under Wine/WinlatorXR (a desktop pass cannot
  prove the container's WinHTTP path).
- **Release step (each stable release, by hand):** rebuild `CShell.dll`, copy it to
  `AutoUpdate/CShell.dll`, rewrite `AutoUpdate/notes.txt`, then bump `AutoUpdate/version.txt`
  **and** `launcher-xr/version.txt` to the same new number, and **commit + push** - the version
  bump is what actually publishes the release, and nothing reaches users until it is on `main`.
- **Deploying a release (by hand):** put the rebuilt **`ShogoVR.exe`** and the **`ShogoVR`
  sub-folder** (`CShell.dll`, plus `AUTHORS.txt` / `NOTICE.txt` in the packaged release) into
  exactly two places - the GOG Shogo directory (`E:\GOG Galaxy\Games\Shogo - Mobile Armor
  Division\`) and the git release directory (`Release/`) - and nowhere else. `Release/` also holds
  the human-written `ReadMe.txt`, the human-edited `version.txt` and past packaged zips under
  `Release/Releases/`; those are not touched by a deploy. An automated deploy may write **only**
  `Release/ShogoVR.exe` and `Release/ShogoVR/CShell.dll` inside `Release/` - never
  `Release/version.txt`, and never anything under `AutoUpdate/`.
- **Packaging (by hand):** the public release ZIPs (`Release/Releases/…-vNN.zip` - alphas and betas
  included) are assembled by the owner, not by any script or tooling here. Built executables are
  **never tracked in git** (`*.exe` is ignored): the repo holds the source, the docs and the
  `AutoUpdate/` release artifacts (a DLL and text files), never a built `ShogoVR.exe` or a zip.
- **Staged for the 0.7 release (17 Oct):** `CShell.dll` rebuilt and confirmed **byte-identical** to
  the device-verified / hand-picked DLL (`5EE4A7C4...`), `ShogoVR.exe` rebuilt, and both copied into
  the GOG folder and `Release/` (`Release/ShogoVR.exe` and the GOG copy are the same bytes as
  `launcher-xr/ShogoVR.exe`). Every tracked device bug (#20, #22, #37, #38, #39) was cleared in the
  same 17 Oct headset pass, so this build is the first with **no open issues**.
