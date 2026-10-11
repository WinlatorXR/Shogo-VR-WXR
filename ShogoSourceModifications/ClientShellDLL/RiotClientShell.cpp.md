# ShogoSource/ClientShellDLL/RiotClientShell.cpp

**Goal:** mount the `g_VRStereo` (`CVRStereo`) module into the client-shell
lifecycle, expose a `VRRecenter` console command, feed head tracking / aim /
mech state / player body into the VR module, and make `RenderCamera` stereo-aware
while stamping the WinlatorXR frame-sync pixel.

All edits are additions except the `RenderCamera` change and the
`UpdateMenuState`/`UpdatePausedState` change. Code blocks show only the mod's own
added lines; the control-flow changes are described in prose so no original
source is reproduced.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/RiotClientShell.cpp`
(~9700 lines); your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Include block            | after line 36 (`#include "SFXReg.h"`) |
| 2. `VRRecenterFn`           | beside `InitSoundFn` (line 216) |
| 3. Register console command | after line 824 (`RegisterConsoleProgram("InitSound", ...)`) |
| 4. Start VR module          | in `OnEngineInitialized` (starts line 785), after `m_sfxMgr.Init` (line 1177) |
| 5. `OnEngineTerm`           | line 1251 |
| 6. `OnExitWorld`            | line 1566 |
| 7. `Update`                 | line 1696 |
| 8. `CalculateCameraRotation`| line 2956 |
| 9. `UpdateWeaponModel`      | line 3398 |
| 10. `OnMessage`             | line 4949 (vehicle-mode branch) |
| 11. `UpdateServerPlayerModel`| line 8011 |
| 12. `RenderCamera`          | line 8043 |
| 13. `UpdateMenuState` / `UpdatePausedState` | lines 2395 / 2500 |

---

## 1. Include block

**Part of:** the include block at the top of the file (pristine lines 1–36).

Add, next to the other includes (for example after the `SFXReg.h` include on
line 36):

```cpp
#include "VRStereo.h"
#include "ModelFuncs.h"
```

`ModelFuncs.h` provides `PlayerModeToModelID`, used in edit 11.

---

## 2. New function `VRRecenterFn()`

**Part of:** a new free function at file scope. Place it next to the other
console callbacks (the `InitSoundFn` callback at pristine line 216 is a good
neighbour).

```cpp
void VRRecenterFn(int argc, char **argv)
{
	if( g_pRiotClientShell )
		g_VRStereo.RequestRecenter();
}
```

---

## 3. `CRiotClientShell::OnEngineInitialized()` — register the console command

**Part of:** `CRiotClientShell::OnEngineInitialized(...)` (starts pristine line
785), in the block that registers console programs at pristine lines 819–824
(the same block that registers `InitSound` on line 824).

Add:

```cpp
pClientDE->RegisterConsoleProgram("VRRecenter", VRRecenterFn);
```

---

## 4. `CRiotClientShell::OnEngineInitialized()` — start the VR module

**Part of:** `CRiotClientShell::OnEngineInitialized(...)` (starts pristine line
785), just after the special-effects manager is initialised (the `m_sfxMgr.Init`
call, pristine line 1177) and before the sound-error comment that follows it.

Add:

```cpp
g_VRStereo.Init(pClientDE);
```

---

## 5. `CRiotClientShell::OnEngineTerm()`

**Part of:** `CRiotClientShell::OnEngineTerm()` (pristine line 1251), near the
top, after the client-DE null check.

Add:

```cpp
g_VRStereo.Term();
```

---

## 6. `CRiotClientShell::OnExitWorld()`

**Part of:** `CRiotClientShell::OnExitWorld()` (pristine line 1566), after the
credits are torn down.

Add:

```cpp
g_VRStereo.OnExitWorld();
```

---

## 7. `CRiotClientShell::Update()`

**Part of:** `CRiotClientShell::Update()` (pristine line 1696), before the
tint-update section.

Add:

```cpp
g_VRStereo.CheckSettingsFile();
g_VRStereo.UpdateBridge();
g_VRStereo.CheckRenderer(DTRUE);
g_VRStereo.SetGameplayInput(m_nGameState == GS_PLAYING);
g_VRStereo.UpdateInput();
```

The last two lines let the VR module know whether we are actually playing (so it
takes over gameplay input) and give it one chance per frame to turn controller
edges into game commands / menu keys. See `Docs/BUGS_TO_FIX.md` #3.

---

## 8. `CRiotClientShell::CalculateCameraRotation()` — two insertions

**Part of:** `CRiotClientShell::CalculateCameraRotation()` (pristine line 2956).

First, at the end of the function (after the existing pitch clamp), add:

```cpp
g_VRStereo.ApplyHeadTracking(m_fYaw, m_fPitch);
```

Second, where the function feeds the joystick/axis movement into the move
manager (`m_MoveMgr->UpdateAxisMovement(...)`, around pristine line 3020),
replace that single call so that, while the controllers are live, the left
stick (via `GetMoveAxes`) drives movement instead of the keyboard/joystick axes:

```cpp
DFLOAT fVRFwd = 0.0f, fVRRight = 0.0f;
if (g_VRStereo.GetMoveAxes(fVRFwd, fVRRight))
	m_MoveMgr->UpdateAxisMovement(DTRUE, fVRFwd, 0.10f, DTRUE, fVRRight, 0.10f);
else
	m_MoveMgr->UpdateAxisMovement(<existing axis arguments>);
```

`<existing axis arguments>` stands for the four/five arguments already passed in
your copy.

---

## 9. `CRiotClientShell::UpdateWeaponModel()` — two insertions

**Part of:** `CRiotClientShell::UpdateWeaponModel()` (pristine line 3398).

First, after the weapon model has been updated for this frame (the call that
returns the weapon state), add:

```cpp
HOBJECT hVRAimFilter[] = {hPlayerObj, m_MoveMgr->GetObject(), DNULL};
g_VRStereo.UpdateAimDepth(m_hCamera, hVRAimFilter);
```

Second, after the weapon model's object handle is fetched, add:

```cpp
g_VRStereo.UpdateHeldGun(hWeaponModel, m_weaponModel.GetId(), vPos);
```

---

## 10. `CRiotClientShell::OnMessage(DBYTE messageID, HMESSAGEREAD hMessage)`

**Part of:** `CRiotClientShell::OnMessage(...)` (pristine line 4949), inside the
vehicle-mode branch after the position floats have been read from the message.

Add:

```cpp
g_VRStereo.SetMechMode((DBOOL)(m_nPlayerMode != PM_MODE_KID), (DBOOL)pSettings->VehicleMode());
```

---

## 11. `CRiotClientShell::UpdateServerPlayerModel()`

**Part of:** `CRiotClientShell::UpdateServerPlayerModel()` (pristine line 8011),
at the end of the function.

Add:

```cpp
g_VRStereo.UpdateBody(hRealObj, (int)PlayerModeToModelID(m_nPlayerMode), DTRUE, DFALSE);
```

---

## 12. `CRiotClientShell::RenderCamera(DBOOL bDrawInterface)`

**Part of:** `CRiotClientShell::RenderCamera(DBOOL bDrawInterface)` (pristine
line 8043).

Originally this function starts the 3D scene, renders the single camera, draws
the 2D interface once if requested, then ends the 3D scene. Restructure it so it
drives the VR module instead, while still using the engine's existing render and
interface calls. There are no lines to copy from the original — this is a change
in control flow around calls you already have.

**Step A — decide the frame type, before starting the 3D scene.** Obtain the
current settings through the menu's settings accessor (the same `GetSettings()`
call other functions in this file already use) into a local pointer. Then add
these local values and the `BeginFrame` call (our code):

```cpp
DBOOL bFootScale = (DBOOL)(!pSettings || !pSettings->VehicleMode());

DBOOL bVRFlat = (m_nGameState == GS_MENU || m_nGameState == GS_PAUSED || m_nGameState == GS_LOADINGLEVEL ||
				 m_nGameState == GS_BUMPER || m_nGameState == GS_CREDITS || m_nGameState == GS_INTRO ||
				 m_nGameState == GS_DEMO_MULTIPLAYER || m_pMessageBox != DNULL || !m_playerCamera.IsFirstPerson());
g_VRStereo.BeginFrame(bVRFlat, bDrawInterface);
```

**Step B — render the scene.** After your existing `Start3D()` call, replace the
single-camera render with a branch: when `g_VRStereo.IsStereoFrame()` is true,
render through the VR module (our code); otherwise keep the original
single-camera `RenderCamera` call you already have.

```cpp
if (g_VRStereo.IsStereoFrame())
{
	g_VRStereo.RenderStereo(m_hCamera, DNULL, 0, m_fCurrentFovX, VR3D_IMMERSIVE, bFootScale);
}
```

**Step C — draw the interface per eye.** Where the function currently draws the
2D interface once inside `bDrawInterface`, loop once per eye instead and bracket
the existing interface draw with the VR 2D begin/end calls (our code; the
`<existing interface draw>` placeholder stands for the interface-drawing calls
you already have — keep them as they are):

```cpp
int nNumEyes = g_VRStereo.GetNumEyes();
for (int nEye = 0; nEye < nNumEyes; nEye++)
{
	// StartOptimized2D (existing)
	g_VRStereo.Begin2D(nEye, DTRUE);
	<existing interface draw>
	g_VRStereo.End2D();
	// EndOptimized2D (existing)
}
```

**Step D — frame-sync marker.** Immediately after your existing `End3D()` call,
add (our code):

```cpp
g_VRStereo.DrawXrSyncPixel();
```

---

## 13. `UpdateMenuState()` and `UpdatePausedState()` — per-eye menu rendering

**Part of:** `CRiotClientShell::UpdateMenuState()` (pristine line 2395) and
`CRiotClientShell::UpdatePausedState()` (pristine line 2500).

These two states draw a flat full-screen picture each frame (the polygrid in 3D,
the menu / "Game Paused" text in 2D) and they are reached from `Update()` without
going through `RenderCamera()`. In the XrApi bridge the runtime captures one
window and stays in SBS mode, so a flat full-screen picture was being split
across the two eyes. Both functions are restructured the same way, mirroring the
`RenderCamera` flow: call `g_VRStereo.BeginFrame`, and when `IsStereoFrame()` is
true render the polygrid through `RenderStereo` in `VR3D_PANEL_FLAT` mode (the
per-eye panel transform is identical to the 2D one, so the background lines up
with the menu), and draw the 2D content once per eye inside
`g_VRStereo.Begin2D(nEye, DFALSE)` / `End2D()`, finishing with
`g_VRStereo.DrawXrSyncPixel()`. When a stereo frame is not active the original
flat behaviour is unchanged.

There are no lines to copy from the original — this is a change in control flow
around calls you already have. The only mod lines are the `BeginFrame` /
`IsStereoFrame` / `RenderStereo` / `Begin2D` / `End2D` / `DrawXrSyncPixel`
calls, and the loop that brackets the existing `m_menu.Draw()` (or the
`m_hGamePausedSurface` blit) per eye.
