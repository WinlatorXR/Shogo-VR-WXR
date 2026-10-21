# ShogoSource/ClientShellDLL/RiotClientShell.cpp

**Goal:** mount the `g_VRStereo` (`CVRStereo`) module into the client-shell
lifecycle, expose a `VRRecenter` console command, feed head tracking / aim /
mech state / player body (including crouch) into the VR module, make
`RenderCamera`, the pause/menu screens and the splash stereo-aware, clone the
intro movies per eye, hand the hurt/explosion screen tint to the mod instead of
the engine camera light-add, drop WinlatorXR's leaked face-button keys, and
stamp the WinlatorXR frame-sync pixel.

All edits are additions except the `Update` menu-re-layout block, the
`RenderCamera` change, the `UpdateMenuState`/`UpdatePausedState` change, the
`ShowSplash` / `PlayIntroMovies` changes, the crouch argument in
`UpdateServerPlayerModel`, and the screen-tint branch. Code blocks show only the
mod's own added lines; the control-flow changes are described in prose so no
original source is reproduced.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/RiotClientShell.cpp`
(~9990 lines); your copy may differ by a few lines.

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
| 14. `ShowSplash`            | line 747 |
| 15. `PlayIntroMovies` / `UpdateMoviesState` | lines 6379 / 2292 (plus the constructor member init) |
| 16. `OnKeyDown`             | line 4565 |
| 17. `OnKeyUp`               | line 4900 |
| 18. `UpdateLoadingLevelState`| line 2515 |
| 19. `UpdateScreenTint` / `ClearScreenTint` / `HandleMPChangeLevel` | lines 3440 / 3493 / 9329 |

Edits 20–24 continue past the original list above (see their own sections below).

---

## 24. `CRiotClientShell::OnMessage()` — player mode signal (#42) — **revised to the correct test**

**Part of:** `case MID_PLAYER_MODECHANGE:` (pristine line 4949).

> **Status (20 Oct 2026).** The #42 grip handling is now a **`game/`-only** change: the right grip
> simply sends **both** `JUMP` and `DOUBLEJUMP` (see `Docs/BUGS_TO_FIX.md` #42), so no mech/vehicle
> mode is needed for it at all. This edit is kept because the mode signal it fixes is **correct in
> its own right** — the shell no longer passes `PM_MODE_KID` as a "is mech" test.
> `CRiotClientShell::SetMechMode` is still called (the mod records the mech/vehicle mode for the
> HUD), just from a correct expression.
>
> **The rule:** `PM_MODE_FOOT` (**0**) and `PM_MODE_KID` (**5**) are both
> **on foot** (the kid is cut content — a past-life version of the characters — and
> moves like a human). **Everything else is an MCA** (the walking mech that can
> also transform into a vehicle, i.e. the `PM_MODE_MCA_*` modes plus
> `PM_CURRENT_MCA` / `PM_MULTIPLAYER_MCA`). The old `m_nPlayerMode != PM_MODE_KID`
> expression is **not** the mech test: on foot the mode is `PM_MODE_FOOT`, not
> `KID`, so it reported "mech" everywhere on foot.

```cpp
// Tell the mod whether the player is on foot.  Both PM_MODE_FOOT
// (0) and PM_MODE_KID (5) are "on foot" (the kid is cut content, a
// past-life version of the characters, and moves like a human);
// everything else is an MCA - the walking mech that can also
// transform into a vehicle (its PM_MODE_MCA_* modes).  Note that
// "!= PM_MODE_KID" IS NOT the mech test: on foot the mode is
// PM_MODE_FOOT, not KID, so that expression reported "mech"
// everywhere on foot.
DBOOL bOnFoot = (DBOOL)(m_nPlayerMode == PM_MODE_FOOT || m_nPlayerMode == PM_MODE_KID);
g_VRStereo.SetMechMode((DBOOL)!bOnFoot, (DBOOL)pSettings->VehicleMode());
```

Notes:
- `IsOnFoot()` (`RiotClientShell.h:112`, `m_nPlayerMode == PM_MODE_FOOT`) is the
  game's own on-foot test, the same one `CMoveMgr.cpp:195` uses — but it is
  **narrower** than the rule above (it excludes `PM_MODE_KID`), so the call site
  spells the pairing out rather than using it.
- Verified offline against the full `PlayerModeTypes.h` list: `FOOT` and `KID` are
  the only on-foot modes; all MCA modes, `CURRENT_MCA`, `MULTIPLAYER_MCA` and
  `UNDEFINED` are mech. The reverted expression is shown side by side (it called
  `FOOT` "mech").
- With the #42 remap gone, nothing currently consumes this signal except the HUD
  log line in `CVRStereo::SetMechMode`; it is correct so that any future use — a
  revisited #42, or a mode-dependent HUD/behaviour — starts from the right value.
- Nothing else about the vehicle-mode handling in this branch changes.

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
if (g_VRStereo.ConsumeMenuLayoutChanged())
{
	m_menu.ScreenDimsChanged();		// VR menu size became available - re-lay out (#5)
}
g_VRStereo.CheckRenderer(DTRUE);
g_VRStereo.SetGameplayInput(m_nGameState == GS_PLAYING);
g_VRStereo.SetPlayerDead(IsPlayerDead());
g_VRStereo.UpdateInput();
```

The `ConsumeMenuLayoutChanged()` block keeps the options/menu tree laid out for
the small VR virtual screen: the menu is created before `g_VRStereo.Init` (so
its VarTracks aren't ready at menu-init time), and `UpdateBridge()` flags when
the headset becomes available; when the flag is set we re-run the menu's
`ScreenDimsChanged()` so it picks up `g_VRStereo.GetMenuLayoutSize()`. See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #5.

The middle three lines let the VR module know whether we are actually playing (so
it takes over gameplay input), whether the player is dead (so the left trigger
quick-loads the quicksave - see `Docs/PreReleaseBugs/BUGS_TO_FIX.md` #16), and give it one chance
per frame to turn controller edges into game commands / menu keys. See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #3.

---

## 8. `CRiotClientShell::CalculateCameraRotation()` — two insertions

**Part of:** `CRiotClientShell::CalculateCameraRotation()` (pristine line 2956).

First, at the end of the function, after the existing pitch clamp (pristine line
~3119), add:

```cpp
g_VRStereo.ApplyHeadTracking(m_fYaw, m_fPitch);
```

Second, where the function feeds the joystick/axis movement into the move
manager (`m_MoveMgr->UpdateAxisMovement(...)`, pristine line ~3030),
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

Add (see **note 24** for why this exact expression — `PM_MODE_FOOT` and
`PM_MODE_KID` are both on foot, everything else is an MCA):

```cpp
DBOOL bOnFoot = (DBOOL)(m_nPlayerMode == PM_MODE_FOOT || m_nPlayerMode == PM_MODE_KID);
g_VRStereo.SetMechMode((DBOOL)!bOnFoot, (DBOOL)pSettings->VehicleMode());
```

---

## 11. `CRiotClientShell::UpdateServerPlayerModel()`

**Part of:** `CRiotClientShell::UpdateServerPlayerModel()` (pristine line 8011),
at the end of the function.

Add:

```cpp
DBOOL bShowVRBody = (m_playerCamera.IsFirstPerson() && !m_bUsingExternalCamera &&
					 !IsPlayerDead() && !IsSpectatorMode());
g_VRStereo.UpdateBody(hRealObj, (int)PlayerModeToModelID(m_nPlayerMode), bShowVRBody, g_VRStereo.IsCrouchOn());
```

`bShowVRBody` is false for the **third-person (chase)** view, the **external /
cutscene** camera and **death / spectator** — in all of those the game renders its
own player model, so the mod's copy stays hidden and only one body shows. The
mod body is drawn only in normal first-person gameplay. The last argument is the
mod's crouch state (`IsCrouchOn()`), so the VR body plays its crouch animation
while the crouch controls are active (Shogo has no on-foot crouch physics, so this
is body feedback only). See `Docs/PreReleaseBugs/BUGS_TO_FIX.md` #21 and #20.

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

**Step D — post-render overlays.** Immediately after your existing `End3D()` call,
add (our code):

```cpp
g_VRStereo.DrawScreenFlashFrame();
g_VRStereo.DrawXrSyncPixel();
```

`DrawScreenFlashFrame()` paints the hurt/explosion vignette (#28) into each eye;
like the sync pixel it runs after the 3D and 2D passes, on the frame the runtime
captures.

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
`g_VRStereo.DrawScreenFlashFrame()` then `g_VRStereo.DrawXrSyncPixel()`. When a
stereo frame is not active the original flat behaviour is unchanged.

There are no lines to copy from the original — this is a change in control flow
around calls you already have. The only mod lines are the `BeginFrame` /
`IsStereoFrame` / `RenderStereo` / `Begin2D` / `End2D` / `DrawScreenFlashFrame` /
`DrawXrSyncPixel` calls, and the loop that brackets the existing `m_menu.Draw()`
(or the `m_hGamePausedSurface` blit) per eye.

---

## 14. `CRiotClientShell::ShowSplash()` — splash on the right half in stereo

**Part of:** `CRiotClientShell::ShowSplash()` (pristine line 747).

The function blits `interface/splash.pcx` and flips. Two small changes are needed.

First, the pristine function reuses the same width/height locals for both the screen
and the splash bitmap: it reads the screen size, builds the full-window destination
rectangle, then **overwrites** the same locals with the splash's own size before
building the source rectangle. Separate the splash's dimensions into their own
locals so the existing locals keep the screen size (our code):

```cpp
DDWORD nWidthSplash = 0, nHeightSplash = 0;
pClientDE->GetSurfaceDims (hSplash, &nWidthSplash, &nHeightSplash);
```

Build the **source** rectangle from the splash dimensions (`nWidthSplash` /
`nHeightSplash`) instead of the screen locals.

Second, in stereo the runtime reads the single SBS window, so a full-window splash
is seen split across the two eyes. Narrow the **destination** rectangle to the
**right half** while `g_VRStereo.IsStereoFrame()` is true, before the blit (our
code):

```cpp
if (g_VRStereo.IsStereoFrame())
{
	rcDst.left  = (int)nWidth / 2;
	rcDst.right = (int)nWidth;
}
```

Keep the existing clear / `Start3D` / `RenderCamera` / `StartOptimized2D` /
`ScaleSurfaceToSurface` / `EndOptimized2D` / `End3D` / `FlipScreen` sequence
otherwise unchanged.

---

## 15. `CRiotClientShell::PlayIntroMovies()` / `UpdateMoviesState()` — movies cloned per eye

**Part of:** `CRiotClientShell::PlayIntroMovies(...)` (pristine line 6379),
`CRiotClientShell::UpdateMoviesState()` (pristine line 2292), and the
constructor's member initialisation list (pristine line ~303).

The engine's Smacker player lives in `Client.exe` and decodes straight into the
window buffer (at native size, or fullscreen), centered; the mod cannot
re-project the decode from `CShell.dll`. It therefore clones the finished frame
into both eye halves. A new `CRiotClientShell` member `m_bMovieShiftRight`
tracks whether a VR-shifted movie is playing (its declaration is a separate edit
in `RiotClientShell.h`, see that note).

**Constructor member init** (our code, beside the other `DBOOL` member
initialisations near pristine line 303):

```cpp
m_bMovieShiftRight = DFALSE;
```

**In `PlayIntroMovies()`** — two additions:

1. Near the top of the function, before it advances its static "next movie" counter,
   reset the VR movie detector so the new movie's picture is found again on its
   first frame (our code):

```cpp
g_VRStereo.ResetMovieSource();
```

2. Where the function sets the movie-playback flags to `PLAYBACK_FULLSCREEN`,
   replace that value so VR starts the movie at native size, remember whether the
   shift is active, and log it (our code):

```cpp
m_bMovieShiftRight = g_VRStereo.IsActive();
DDWORD nFlags = m_bMovieShiftRight ? 0 : PLAYBACK_FULLSCREEN;
g_VRStereo.Log("PlayIntroMovies: VR=%d flags=%d", (int)(m_bMovieShiftRight ? 1 : 0), (int)nFlags);
```

`nFlags` replaces the fullscreen flag already passed to the movie-play call: `0`
in VR, `PLAYBACK_FULLSCREEN` otherwise.

**In `UpdateMoviesState()`** — after the engine's per-frame `UpdateVideo()` while
a movie is playing, add (our code):

```cpp
if (m_bMovieShiftRight) g_VRStereo.ShiftMovieToRightHalf();
```

`ShiftMovieToRightHalf()` (in `game/VRStereo.cpp`) captures the window after
`UpdateVideo()`, auto-detects the video rectangle, blanks the whole window to
engine-black (`SetupColor1(0,0,0,DFALSE)` + `FillRect`), and re-blits the video
into the middle of each half, so each eye sees one clean native-size copy. See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #10.

---

## 16. `CRiotClientShell::OnKeyDown(int key, int rep)` — drop the leaked face-button keys

**Part of:** `CRiotClientShell::OnKeyDown(int key, int rep)` (pristine line
4565), immediately after the client-DE null check at the top of the function.

WinlatorXR passes the Quest face buttons through as the keyboard letters
X/Y/B/A on top of the XrApi feed, so the game acts on those keys. The VR module
now decides when to drop them; the engine should then cancel the key before it
becomes a command (the same trick the `GS_BUMPER` branch already uses with
`ClearInput()`). Add (our code):

```cpp
if (g_VRStereo.ShouldSwallowFaceKey(key))
{
	// Cancel the stray key so the engine doesn't act on it.  But the letter
	// B is the game's ChaseView key: the leaked-B collision is already
	// handled by the OnCommandOn guard (#31), and ClearInput() wipes the
	// *whole* input state - including the vehicle-transform key the same
	// button injects a moment later, so B needed repeated taps (#36).
	if (key != 'B') pClientDE->ClearInput();
	return;
}
```

`ShouldSwallowFaceKey` is true only while a controller is live, gameplay input
is active and the player is alive, for `VK` `A`/`B`/`X`/`Y`. See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #17 and #36.

**Why `B` is excluded from `ClearInput()`:** `ClearInput()` takes no key
argument, so it clears the *entire* input state. Right B is the only controller
control that both injects a key (the `VehicleModeToggle` bind, letter `V`, via
`SendInput`) *and* arrives as a leaked face letter (`B`). When the leaked `B`'s
`ClearInput()` landed after the mod queued `V`, it cancelled the transform
command, so B needed repeated taps (#36). The leaked `B` no longer needs
`ClearInput()` because the key-driven `ChaseView` is already ignored by the
`OnCommandOn` guard (#31) and the `IsCommandOn` hook. `X` (bound to `CursorTog`)
and `Y` keep it.

---

## 17. `CRiotClientShell::OnKeyUp(int key)` — drop the matching key-up

**Part of:** `CRiotClientShell::OnKeyUp(int key)` (pristine line 4900),
immediately after the client-DE null check at the top of the function.

Swallow the matching key-up so the engine never sees a release for a key it was
never allowed to press. Add (our code):

```cpp
if (g_VRStereo.ShouldSwallowFaceKey(key)) return;
```

---

## 18. `CRiotClientShell::UpdateLoadingLevelState()` — tell the mod a level is playable

**Part of:** `CRiotClientShell::UpdateLoadingLevelState()` (pristine line 2515),
in the block that flips to the playing state once the world and player object
exist.

When the level has finished loading and the game becomes playable, tell the VR
module so it can quicksave once the opening cutscene is over (the left trigger
then quick-loads it on death). Add (our code):

```cpp
g_VRStereo.SetLevelLoaded();
```

See `Docs/PreReleaseBugs/BUGS_TO_FIX.md` #16.

---

## 19. Screen tint -> VR flash (`UpdateScreenTint`, `ClearScreenTint`, `HandleMPChangeLevel`)

**Part of:** `CRiotClientShell::UpdateScreenTint()` (pristine line 3440),
`CRiotClientShell::ClearScreenTint()` (pristine line 3493), and the tint reset
inside `CRiotClientShell::HandleMPChangeLevel()` (pristine line 9329).

The hurt/explosion/pickup "screen flash" is this shell tint, applied with
`pClientDE->SetCameraLightAdd()`. Under the stereo render the engine's light-add
flash comes out as a broken full-screen quad, so while VR is active we hand the
tint colour to the mod and it draws a clean per-eye edge vignette instead. See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #28.

In `UpdateScreenTint()`, split the early-out so the mod flash is cleared when no
tint is active, then branch the final light-add. Our code:

```cpp
	if (!m_bTintScreen)
	{
		g_VRStereo.ClearScreenFlash();
		return;
	}
```

and, where the function ends:

```cpp
	if (g_VRStereo.IsActive())
		g_VRStereo.SetScreenFlash(vLightAdd.x, vLightAdd.y, vLightAdd.z);
	else
		pClientDE->SetCameraLightAdd(m_hCamera, &vLightAdd);
```

`ClearScreenTint()` gains (our code), before its existing `m_bTintScreen` block:

```cpp
	g_VRStereo.ClearScreenFlash();
```

and `HandleMPChangeLevel()` gains the same line in its "Clear screen tint"
section. Leave every other original line (the ramp maths, the intersection test,
the non-VR `SetCameraLightAdd` calls) unchanged.

The mod draws the tint on its **post-render** path: `g_VRStereo.DrawScreenFlashFrame()`
is called once after `End3D()` in `RenderCamera()`, `UpdateMenuState()` and
`UpdatePausedState()` (edits 12 and 13 above), beside `DrawXrSyncPixel()`. Drawing
the flash there - rather than inside the optimized-2D HUD block - is what makes it
land on the frame the runtime captures. The flash itself is two opaque rectangles per
frame: a band down the outer edge of the left eye and of the right eye (a
peripheral-vision flash), so it is cheap enough for the headset. `VRFlashTest` (a mod
CVar, default 0) forces a red flash every frame for testing.

---

## 20. `CRiotClientShell::Update()` - Right A toggles the view directly

**Part of:** `CRiotClientShell::Update()`, immediately after the existing
`g_VRStereo.UpdateInput();` call (pristine around line 1742).

The game binds the first/third-person view (**ChaseView**,
`COMMAND_ID_CHASEVIEWTOGGLE`) to the keyboard letter **`B`**
(`defkeybd.cfg`/`autoexec.cfg`: `rangebind ... "##48" ... "ChaseView"`). The mod used
to fire Right A by injecting that bound key, but WinlatorXR also leaks the raw face
buttons as the letters X/Y/B/A, so an injected `B` was swallowed by the face-key
filter (A did nothing) while the leaked `B` still toggled the view from the B button.
The mod now performs the toggle itself, once per frame, from the A button request.
Add (our code):

```cpp
	// Right A toggles the first/third-person view.  Done here rather than by
	// injecting the game's bound ChaseView key: Shogo binds that view to the
	// keyboard letter B (defkeybd.cfg "##48"), which is one of the raw face keys
	// WinlatorXR also leaks, so an injected B is swallowed by the face-key
	// filter (A did nothing) and the leaked B toggled the view from B (#23/#31).
	if (g_VRStereo.ConsumeViewToggle() && m_ePlayerState == PS_ALIVE && !m_bZoomView)
	{
		SetExternalCamera(m_playerCamera.IsFirstPerson());
	}
```

`ConsumeViewToggle()` (`game/VRStereo.cpp`) is a one-shot flag set when the XrApi A
button edge arrives; the shell performs the same `SetExternalCamera()` call the
`COMMAND_ID_CHASEVIEWTOGGLE` command case uses. See `Docs/PreReleaseBugs/BUGS_TO_FIX.md` #31/#23.

---

## 21. `CRiotClientShell::OnCommandOn()` - ignore the leaked key-driven ChaseView

**Part of:** the existing `case COMMAND_ID_CHASEVIEWTOGGLE :` block in
`CRiotClientShell::OnCommandOn()` (pristine around line 4428).

WinlatorXR's leaked face letters can still reach the engine's command **edge** path
(the mod's `IsCommandOn` hook only suppresses the **held** path), so the letter `B`
would fire ChaseView while a controller is live. The A button now drives the view
directly (edit 20 above), so ignore the key-driven toggle in that state. Add (our
code) at the top of the case:

```cpp
			// While a controller is live in gameplay the A button drives the
			// view (see Update()).  WinlatorXR also leaks the raw face letters
			// into the command system, and the letter B is bound to ChaseView,
			// so a leaked B would still toggle the view from the B button.
			// Ignore the key-driven toggle in that state - the mod owns it
			// (#23/#31).
			if (g_VRStereo.IsControllerActive() && g_VRStereo.IsGameplayInput()) break;
```

Leave the original `if (m_ePlayerState == PS_ALIVE && !m_bZoomView)
SetExternalCamera(...)` below it unchanged, so a real keyboard still toggles the view
when no controller is live. `IsGameplayInput()` is a new accessor in
`game/VRStereo.h`. See `Docs/PreReleaseBugs/BUGS_TO_FIX.md` #31/#23.

---

## 22. Loading / bumper / credits drawn per eye (`UpdateLoadingLevelState`, `UpdateBumperState`, `UpdateCreditsStereo`)

**Part of:** `CRiotClientShell::UpdateLoadingLevelState()` (pristine line 2515),
`CRiotClientShell::UpdateBumperState()` (pristine line 2467), the credits/intro
state updates (`UpdateCreditsState`, `UpdateIntroState`, `UpdateGameOver`,
`UpdateDemoMultiplayerState`), and a new helper `CRiotClientShell::UpdateCreditsStereo()`
(its declaration is a separate edit in `RiotClientShell.h`).

The level **loading** screen, the **bumper** ("press any key") screen and the
**credits** / intro text are flat 2D drawn straight to the full SBS window, so in
VR the runtime reads one picture and each eye sees half of it (mono / split).
They now go through the same per-eye treatment as the menus (#5/#6).

`UpdateLoadingLevelState()` and `UpdateBumperState()` mirror `UpdateMenuState()`:
begin the frame, render the animated polygrid through the panel in stereo, then
draw the 2D art once per eye between `Begin2D(nEye, DFALSE)` / `End2D()`. The
loading blit in `UpdateLoadingLevelState()` becomes (our code):

```cpp
	g_VRStereo.BeginFrame(DTRUE, DFALSE);
	DBOOL bStereo = g_VRStereo.IsStereoFrame();
	... Start3D + (bStereo ? RenderStereo(polygrid) : UpdateMenuPolygrid()) ...
	for (int nEye = 0; nEye < (bStereo ? g_VRStereo.GetNumEyes() : 1); nEye++)
	{
		pClientDE->StartOptimized2D();
		if (bStereo) g_VRStereo.Begin2D(nEye, DFALSE);
		UpdateLoadingLevel();
		if (bStereo) g_VRStereo.End2D();
		pClientDE->EndOptimized2D();
	}
	...
	g_VRStereo.DrawScreenFlashFrame();
	if (bStereo) g_VRStereo.DrawXrSyncPixel();
```

The bumper state does the same, with its `m_hPressAnyKey` blit moved inside the
per-eye loop. The credits / intro states, which just called
`pClientDE->Start3D(); UpdateMenuPolygrid(); m_credits.Update(); pClientDE->End3D();`,
now call the new helper `UpdateCreditsStereo()`, which performs the same per-eye
wrap around `m_credits.Update()`.

`Begin2D` installs the 2D hooks and makes `GetSurfaceDims(hScreen)` report the
mod's small virtual screen (`Hook_GetSurfaceDims`), so the existing centred-layout
math in `UpdateLoadingLevel()` and the credits lands inside the virtual canvas and
is then scaled per eye. The credits themselves are laid out on that virtual canvas
by a matching change in `Credits.cpp` (see `Credits.cpp.md`). See
`Docs/PreReleaseBugs/BUGS_TO_FIX.md` #12.

---

## 23. `CRiotClientShell::OnKeyDown()` — a bumper level is playable (#43)

**Part of:** `CRiotClientShell::OnKeyDown(int key, int rep)` (pristine line 4565),
in the `GS_BUMPER` branch that flips to `GS_PLAYING` when the player dismisses the
"press any key" screen putting the level back into play.

Section 18 calls `g_VRStereo.SetLevelLoaded()` when `UpdateLoadingLevelState()`
reaches playable. A level that shows the **bumper** screen leaves
`GS_LOADINGLEVEL` inside `OnEnterWorld()` before that code can run, so it never
armed the level-start quicksave. Arm it at this second transition too. Our code,
right after the state change:

```cpp
	m_nGameState = GS_PLAYING;
	PauseGame (DFALSE);

	g_VRStereo.SetLevelLoaded();
```

`SetLevelLoaded()` ignores the call while the player has just died (the mod's
`PlayerJustDied` flag, set from the existing `SetPlayerDead(IsPlayerDead())` call
in `Update()`), so a death quick-load that happens to pass through a bumper screen
does not overwrite the level-start save. See `Docs/BUGS_TO_FIX.md` #43.

---

