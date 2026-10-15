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
in `Docs/BUGS_TO_FIX.md` (#1-#36). As of the 14 Oct 2026 headset pass the list is
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
**removed/cancelled** - players save/load manually. No bug is still **open**: #12
(loading/bumper/credits screens mono), #20 (crouch now lowers the eyes), #22 (per-axis
stick dead zone) and #26 (intro-video y-offset + 50 px) are all **fixed in code
(14 Oct)**, awaiting a device pass.
On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`; `#4` (fire) and `#9`
(sync hold) were verified on the desktop with the XrApi UDP Tester. **The live
issue tracker is now `Docs/BUGS_TO_FIX.md`, not the phase statuses below.**

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

Progress: **Phases 0-5 are code-complete and the port has run on the target**
(Meta Quest 3 via WinlatorXR, launched from `ShogoVR.exe`, 2800x1400 SBS). The
10-13 Oct 2026 on-device sessions produced a set of comfort / render / input bugs
that are now the **live issue log in `Docs/BUGS_TO_FIX.md` (#1-#36)** - that file,
not the phase statuses above, is the current source of truth. As of the 14 Oct
2026 pass no bug is still **open**: #21 was confirmed verified by the user, and
#12 (loading/bumper/credits per eye), #20 (crouch lowers the eyes), #22 (per-axis
stick dead zone) and #26 (intro-video offset + 50 px) are **fixed in code**,
awaiting a device pass. `#4` (fire) and `#9` (sync hold) were verified on the
desktop with the XrApi UDP Tester.

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

The 12 Oct 2026 session added a fresh device bug list (`Docs/BUGS_TO_FIX.md`,
now #1-#36) and worked several of them in code; the 13 Oct 2026 headset pass
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
  body's up axis from the eye base position, so the whole stereo view (and the held gun) drops
  smoothly and lifts on release. Build clean; awaiting a device pass. The body showing in
  third-person / cutscene views moved to #21.
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
  `Init` calls. Build clean; **verified on device (2026-10-14)**.
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
- **#22 per-axis left-stick dead zone (fixed in code, 14 Oct)** - `GetMoveAxes()` used one radial
  `0.15` for both axes; it now uses a forward/back dead zone of `0.08` and a strafe dead zone of
  `0.20`, rescales each axis from its own edge, then caps the combined magnitude at 1, so a
  mostly-forward push no longer drifts into a diagonal. Build clean; awaiting a device pass.
- **#12 loading / bumper / credits per eye (fixed in code, 14 Oct)** - these screens are flat 2D
  blitted straight to the full SBS window, so each eye saw half of one picture. They now go through
  the same per-eye treatment as the menus (#5): `UpdateLoadingLevelState()` and `UpdateBumperState()`
  mirror `UpdateMenuState()` (begin the frame, render the polygrid through the panel in stereo, then
  draw the 2D art once per eye between `Begin2D(nEye)`/`End2D()`), and the credits / intro states now
  call a new `UpdateCreditsStereo()` helper that wraps `m_credits.Update()` the same way. `Credits.cpp`
  lays the text out on the mod's small virtual screen (`g_VRStereo.GetMenuLayoutSize()`) so the
  per-eye remap centres it in each eye and picks the larger font. Documented as edit 22 in
  `RiotClientShell.cpp.md`, edit 2 in `RiotClientShell.h.md`, and a new `Credits.cpp.md`. Build clean;
  awaiting a device pass.
- **#26 intro video lowered (fixed in code, 14 Oct)** - `ShiftMovieToRightHalf()` now blits the
  per-eye movie clones (and the not-detected fallback) at `y = screenHeight / 8 + 50`, clamped so a
  full-height frame still lands fully on-screen, instead of at `y = 0` (the `+ 50` is the owner's
  14 Oct "about 50 px lower" follow-up). Build clean; awaiting a device pass.

This brings the tracked ShogoSource notes to **ten** files
(`ShogoSourceModifications/` now also documents `RiotMenu.cpp` and `Credits.cpp`).

The immediate next actions are:

1. **Device verification is up to date.** The fixed set is now verified on device
   (13-14 Oct): the 10-11 Oct batch (#1-#6, #8-#11, #13-#17), the zoom removal
   (#19), the VR body hidden outside first-person (#21), shots-vs-model (#27), the hurt
   / explosion flash (#28), the muzzle flash at the visible gun (#29), the trimmed VR
   menu (#30), the lower-than-origin weapon model (#34), dominant hand (#35) and the
   Right-A/B view + transform toggles (#23/#31/#36, B confirmed 14 Oct).
2. **Clean-up pass - verified.** Zoom is removed (#19, which also drops the #18
   Left-Y re-select), the in-game VR menu is trimmed to the 7-row short list with the
   removed options pinned to their defaults (#30), and the hurt / explosion flash is
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
4. **Device-verify the last in-code fixes.** No tracked bug is still open: #12
   (loading / bumper / **credits** screen per eye - fix like #5), #20 (crouch now
   lowers the eyes), #22 (per-axis stick dead zone) and #26 (intro-video y-offset
   + 50 px) are all **fixed in code (14 Oct)** and await a device pass.
5. **Keep `Docs/BUGS_TO_FIX.md` current** - it is the single live tracker; note
   each fix's device-verification status and drop the matching prose note under
   `ShogoSourceModifications/` plus a `PROJECT_PLAN.md` entry for any ShogoSource
   change.
