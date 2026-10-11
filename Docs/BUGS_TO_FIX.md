# BUGS_TO_FIX

Live issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed` / `Won't fix`.

---

## 1. Head roll is reversed (lean left -> roll right)

- **Status:** Fixed in code (2026-10-10) - **needs on-device verification**
- **Priority:** High (immediate comfort)
- **Observed:** Leaning the head to the left rolls the view to the right in-game; leaning right
  rolls it left. Yaw and pitch appear correct.
- **Refs:**
  - `game/XrApiUDP.cpp:42-49` — `XrQuatToOpenXR()` previously negated the raw quaternion's Z
    component (`(x, y, -z, w)`). That negation reversed the *roll* only: a pure yaw or pitch
    quaternion has Z = 0, so the negation could not touch those axes - exactly matching the
    symptom (roll wrong, yaw/pitch fine).
  - `game/XrApiUDP.cpp:442-462` — `ToGameSpace()` mirrors Z when converting the OpenXR frame
    into LithTech's left-handed frame; the (forward, up) pair carries the roll.
- **Fix applied:** dropped the Z-negation in `XrQuatToOpenXR()` (now an identity pass). Because
  the negation had Z=0 on yaw/pitch quaternions, yaw and pitch are numerically unchanged; roll
  is the only affected axis and its sign now matches the headset.
- **Still to verify:** on device, lean left/right and confirm the world rolls the correct way,
  and that yaw/pitch feel unchanged. Watch for any subtle roll-vs-yaw coupling at combined
  orientations.

---

## 2. Controllers / hands are upside down

- **Status:** Fixed in code (2026-10-10) - **needs on-device verification**
- **Priority:** High
- **Observed:** Hands render "upside down". Same issue as HWXR on Meta Quest 2 and Pico 4
  Ultra. Desired behaviour: a user-toggle that flips the hand 180 degrees about its roll
  (forward) axis, as HWXR offers. *`VRHandFlip` needs on-device testing; also re-check the
  hands after the #1 roll fix, since both come from the same quaternion path.*
- **Refs:**
  - `game/XrApiUDP.cpp:33-39` — the raw-quaternion Z-correction comment notes the *extra
    180-degree roll about Z* opentrackWXR keeps as hardware-specific; that is the precedent
    for a manual hand-roll flip.
  - `game/VRStereo.cpp:2032-2055` — `GetHandWorld()`: builds the hand/weapon basis
    (forward/up) in body space used by `AdjustWeaponPose()` (`AlignRotation`, line ~2071) and
    the gun model rotation (~line 3340).
- **Fix applied:** new `VRHandFlip` console/inifile setting (default 0 = off). When set, the
  hand/weapon **up** vector is negated in `GetHandWorld()` — a 180-degree rotation about the
  hand's forward axis (barrel), leaving the aim direction untouched. Exposed in the in-game
  VR settings page (`game/VRMenu.cpp`) as "flip hand roll (180)". Bumped `VRMENU_ITEMS` (and
  `MAX_GENERIC_ITEMS` in ShogoSource `BaseMenu.h`) to 27 to fit the new row.
- **Still to verify:** on device, toggle `VRHandFlip` and confirm the held gun/hands rotate
  180 degrees about the barrel without changing where the gun points; confirm it still works
  together with the #1 roll fix on this headset.

---

## 3. VR controller inputs are not used; only WinlatorXR keyboard/mouse reach the game

- **Status:** Fixed in code (2026-10-10) - **needs on-device verification**
- **Priority:** High (this is the whole point of the port)
- **Observed:** Gameplay reacted to WinlatorXR's injected keyboard/mouse, not to the Quest
  controllers. Goal: when the XrApi headset is connected, the mod drives gameplay **purely
  from XrApi**, ignoring the keyboard/mouse, while menus stay keyboard-driven (opened with
  Esc, navigated with arrows + Enter) and the controller emulates those menu keys.
- **Root cause:** the whole controller->game path was dead code. `SetGameplayInput()` was
  never called anywhere, so `m_bGameplayInput` stayed `DFALSE`, which disabled `VRCommandOn`
  (fire/jump/duck), `StickTurn`, and `GetMoveAxes`. `GetMoveAxes()` and
  `PollMenuDirection()` were never called, and nothing suppressed the real kb/mouse.
- **Refs (wiring):**
  - `game/XrApiUDP.cpp:425-440` — `ButtonBits()` maps XrApi bool indices to
    `SHOGOVR_BTN_*`: right trigger->FIRE, right grip->NEXT_WEAPON, A->JUMP, B->CROUCH,
    left grip/Y->WEAPONS, X->LOG, left menu->MENU, right stick click->TRANSFORM.
  - `game/VRStereo.cpp:1563-1582` — `m_nButtons`/edges per frame.
  - `game/VRStereo.cpp:1806-1825` — `Hook_IsCommandOn`/`VRCommandOn` (fire/jump/duck).
  - `game/VRStereo.cpp:1834-1868` — `GetMoveAxes()` (left stick) and stick-turn.
  - `game/VRStereo.cpp:2870-2878` — `VRSendScanCode()` *injects* keyboard scancodes via
    `SendInput` (used for the vehicle toggle, now also for menus and weapon selection).
  - `ShogoSource/AppHeaders/client_de.h:237` — `(*GetAxisOffsets)(DFLOAT*)`: the mouse
    look/strafe entry point; `:737` — `(*IsCommandOn)(int)`.
- **Fix applied (gameplay-only split):**
  1. **Mouse suppressed.** Hooked `pClientDE->GetAxisOffsets` (`Hook_GetAxisOffsets`,
     installed/uninstalled in `CVRStereo::Init`/`Term`) -> reports zero when
     `IsControllerActive()`, so the mouse cannot turn/strafe while a controller is live.
  2. **Keyboard suppressed.** `Hook_IsCommandOn` now ignores the engine's real state when
     `IsControllerActive()`, answering only `VRCommandOn`; the WinlatorXR keyboard can no
     longer move/fire the player.
  3. **Input mode wired (ShogoSource).** `CRiotClientShell::Update()` calls
     `g_VRStereo.SetGameplayInput(m_nGameState == GS_PLAYING)` and
     `g_VRStereo.UpdateInput()` each frame.
  4. **Movement wired (ShogoSource).** `CalculateCameraRotation()` feeds
     `GetMoveAxes()` (left stick, analog) into `m_MoveMgr->UpdateAxisMovement(...)`; turning
     is the existing right-stick `StickTurn()` via `ApplyHeadTracking()`.
  5. **Buttons/menus.** `UpdateInput()` drives gameplay edges by tapping the command's
     **bound key** via `SendInput` (so the engine's own `OnCommandOn` runs): menu button->Esc,
     right grip->next weapon, left grip/Y->previous weapon, right-stick up/down also cycles
     weapons, and the vehicle toggle/crouch-tap logic is unchanged. In a menu
     (`!m_bGameplayInput`) the sticks act as the arrow keys, fire/A->Enter, crouch/B->Backspace,
     menu->Esc. (`FindTransformKey` was generalised to `FindCommandKey`.)
- **Not done yet:** weapon *zoom* toggles by double-tapping the same weapon-slot key, and the
  melee/"special move" double-tap - these still need their bound keys and a double-tap helper
  (see #4 and the input note). Confirm on device which actions still feel missing.
- **Note:** The menu/pause flow (`UpdateMenuState`/`UpdatePausedState`) must keep working with
  controller-only input, including on the per-eye menu fix in #5/#6 (`UpdateInput()` handles it
  with injected menu keys).
- **Still to verify:** on device - no keyboard/mouse leakage in gameplay, left stick moves
  (analog) and right stick turns, fire/jump/crouch work, weapon switch works, Esc opens the
  menu and the sticks/buttons navigate it, then close it and confirm no stuck/stray inputs.

---

## 4. Right trigger vibrates but does not fire

- **Status:** Fixed (2026-10-11) - **verified with the XrApi UDP Tester** (shooting works)
- **Priority:** High
- **Observed:** Right trigger produced the expected vibration (haptics send works) but firing
  never happened.
- **Refs:**
  - `game/XrApiUDP.cpp:430` — `if (XB(18)) b |= SHOGOVR_BTN_FIRE; // right trigger`.
  - `game/VRStereo.cpp:1817-1824` — fire requires `m_nButtons & SHOGOVR_BTN_FIRE`, plus
    `m_bGameplayInput` and `IsControllerActive()`.
  - `game/XrApiUDP.cpp:416-418` — first bool char is the left stick click; index 2 used for
    recenter.
  - **Tester cross-check (2026-10-11):** the XrAPIUDPTester bool string is
    0=LGRIP, 1=LMENU, 2=LTHUMBCLICK, 3..6=L thumb dirs, 7=LTRIGGER, 8=LX, 9=LY, 10=RA,
    11=RB, 12=RGRIP, 13=RTHUMBCLICK, 14-17=R thumb dirs, **18=RTRIGGER**, 19=IS_IMMERSIVE,
    20=IS_SBS. So **index 18 is confirmed correct** and `ButtonBits()`'s mapping matches.
- **Hypotheses (in order of likelihood):**
  1. `m_bGameplayInput` was false while firing (the fire command is queried through
     `IsCommandOn`, which the dead-input bug in #3 disabled). **Most likely fixed by #3** -
    re-test on device.
  2. The game polls fire via a command other than `COMMAND_ID_FIRING` in this context (e.g. a
    different command while `m_bAimByHand` is on, or in a mech).
  3. ~~Wrong bool index~~ - **eliminated** by the tester (index 18 is correct).
- **Next steps (PC, no headset needed):** with the XrApi UDP Tester's "Read Key Input" mode on,
  map `R_TRIGGER` to `3` and confirm on PC; add a temporary log of raw bools string +
  `m_nButtons` + `nHeld` + `m_bGameplayInput` + `IsControllerActive()` around a press if it
  still fails. Re-test fire on foot and in a mech.

---

## 5. "VR" menu is too small / text unreadable

- **Status:** Open
- **Priority:** Medium
- **Observed:** The VR settings page in the Options menu is too small to read in the headset.
  Proposed: break the VR page into **3 sub-menus** so less is on screen at once.
- **Refs:**
  - `game/VRMenu.cpp:40-80` — the current VR page item list (aim with, lean with head, menu
    screen size, etc.), each entry a `VRS_CHOICE`/`VRS_TOGGLE` bound to a VarTrack.
  - `game/VRStereo.cpp:248` — `m_vtMenuWidth.Init(... "VRMenuWidth", ... 80.0f)`; panel angular
    width fed by `ComputeHudXform` at `game/VRStereo.cpp:643-655`.
  - VR page lives in Shogo's Options menu (ShogoSource `OptionsMenu.cpp`), which is
    git-ignored; add new pages/sub-menus there and record them in
    `ShogoSourceModifications/` (prose-note policy).
- **Proposed fix design:**
  - Split the VR page into 3 sub-pages, e.g. *Tracking* (aim mode, lean with head, hand flip,
    move direction), *Display* (menu screen size, HUD width/layout, supersampling, stereo),
    *Advanced/Controls* (mapping, haptics, sync, toggles like easy ladders).
  - Sub-pages mean fewer rows each; also raise the default angular width and/or give the menu
    its own (per-eye) virtual resolution so text is legible at 2800x1400 (couples with #6).
  - Keep one deep-link entry ("VR ...") that opens the first sub-page and standard
    back/forward nav driven by the controller (#3) rather than keyboard.
- **Next steps:** sketch the 3 groups with their VarTracks, then implement in ShogoSource +
  `VRMenu.cpp`, document the ShogoSource diffs, build, deploy, smoke test on PC (menus per eye)
  then on device.

---

## 6. Menu not centred at stereo 2800x1400 (shows ~2/3 of the screen per eye)

- **Status:** Open
- **Priority:** High (uncomfortable, needs verification at the target resolution)
- **Observed:** At 2800x1400 (SBS, 1400/eye) the pause/VR menu is not centred per eye; roughly
  two-thirds of the menu canvas is visible in each eye with a wrong/severe stereo offset, which
  is uncomfortable.
- **Refs:**
  - `game/VRStereo.cpp:637-665` — `ComputeHudXform`: in-headset menus use `m_fVirtW =
    m_nScreenW` (the full 2800px SBS canvas) and an angular width via `fTanW * fFocalX /
    fVirtW`; treat the whole canvas, not the per-eye half.
  - `game/VRStereo.cpp:963-1037, 1039-1047` — `Begin2D(nEye)`/`End2D()` per-eye 2D path
    (menu/hud draws). Non-bridge branch uses `GetHalfRect()` (`game/VRStereo.cpp:672`); the
    in-headset branch does not clamp to the eye half.
  - Menu/pause call sites (ShogoSource `RiotClientShell.cpp`, git-ignored) that call
    `Begin2D(nEye, DFALSE)` + `m_menu.Draw()`.
- **Hypothesis:** The panel is sized/positioned from the full SBS canvas width, so each eye's
  camera sees a slice that is the full panel (2/3 because the panel is ~1.5x an eye's width),
  and the per-eye x-offset is not being applied/divided correctly, producing a confusing
  double image rather than a fixed point at one depth.
- **Proposed direction:**
  1. Reproduce on PC at 2800x1400 SBS and confirm the menu overlaps the centre seam.
  2. Give the menu its own virtual resolution and centre/clip it to each eye's half-rect
     (`GetHalfRect`), i.e. same treatment the HUD strip gets (`m_bHudStrip`,
     `game/VRStereo.cpp:994-1014`) but per eye.
  3. Verify the per-eye horizontal offset (stereo separation) so the menu sits on a stable
     plane (like the flat panel in #5); then re-check on device at 2800x1400.
  4. Combine with #5 (sub-menus + legible sizing) so sizing and centring are solved together.

---

## 7. Supersample: render at 2x the window resolution

- **Status:** Open
- **Priority:** Medium (quality/performance trade-off)
- **Observed request:** default the game to render at 2x the window resolution and
  supersample the image for the headset.
- **Context:** The window *is* the SBS canvas (e.g. 1440x900 desktop; 2800x1400 on device with
  1400/eye). XrApi/WinlatorXR samples that canvas as the source for both eyes, so raising the
  game's internal render resolution directly upsamples the texture the headset reads.
- **Refs:**
  - `game/VRStereo.cpp:1448` — startup log `"VR: game resolution %dx%d, drawing at %dx%d,
    window %dx%d"` (render size currently equals the window/mode size).
  - `game/VRStereo.cpp:2094-2109` — `CheckRenderer()` assumes drawing resolution equals window
    resolution (any mismatch is treated as a broken renderer state).
  - `FitVRWindow()` (borderless full-screen) and `DrawXrSyncPixel()` — window/resolution
    plumbing.
- **Proposed direction:**
  1. Add a `VRSupersample` choice (1x / 2x, default 1x; expose in the VR menu, ties to #5).
  2. When >1x and the headset is live, set the game's internal render resolution to
     `windowW * factor` x `windowH * factor` (via the same `SetMode`/renderer path the game
     uses) and let the runtime downscale the SBS canvas.
  3. `CheckRenderer()` and the resolution bookkeeping (`m_nRenderW/H`, `m_nSurfW/H`) must
     treat the supersampled size as the expected render size; re-centre per-eye windows to
     the halves of the larger canvas (#6 relevant).
  4. Verify on device: measure visual sharpness vs. frame rate; make sure menus/HUD stay
     legible and DoF shadows etc. don't break.

---

## 8. Hands are still upside down by default

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** High (comfort / aim correctness)
- **Observed:** On device (and with the XrApi UDP Tester's default pose) the hands/gun render
  upside down out of the box; the `VRHandFlip` option from #2 only corrected it when set to 1.
- **Refs:**
  - `game/VRStereo.cpp:2082-2086` — `GetHandWorld()`: builds the hand/weapon basis from the
    right/left hand pose; the flag negates the **up** vector (`vU`) only - a 180-degree roll
    about the hand's forward (barrel) axis.
  - `game/VRStereo.cpp:282` — `m_vtHandFlip.Init(..., "VRHandFlip", NULL, 0.0f)`.
  - `game/VRMenu.cpp:54` — the "hand roll flip" row.
  - `game/XrApiUDP.cpp:42-49` / `ToGameSpace()` — the same quaternion path used by the HMD;
    the #1 roll fix changed only the HMD orientation, not the hand frame.
- **Fix applied:** the flag's meaning is **inverted** - `0` (the default) now applies the
  180-degree roll correction and `1` leaves the hand as the runtime reported it. This makes
  upside-down hands correct with no config (and keeps existing `VRHandFlip 0` inifiles
  working). The menu row at `game/VRMenu.cpp:54` is now a choice: "flipped" (0) / "as
  reported" (1), so the on-screen wording matches the new default rather than reading "off".
- **Still to verify:** default pose (no config) shows right-side-up hands; toggling to "as
  reported" flips them back; the gun still points where the hand points (roll-only, not a
  mirrored aim). If the aim is also wrong the hand *basis* needs fixing (see background
  below), not this flag.
- **Background (why this is a roll fix, not a basis fix):**
  1. The default was simply wrong for this headset, so inverting the flag makes hands
     right-side-up out of the box while keeping the row for the other way.
  2. If the hand roll turns out to be baked into the hand *quaternion* conversion (the same
     path that needed the #1 roll fix), negating only `vU` rolls the *model* but not the
     underlying aim/hand frame - the hand would look right while the muzzle/grab stays
     mirrored. In that case fix the basis in `GetHandWorld()` / the hand frame in
     `XrApiUDP::ToGameSpace()` against the HMD basis (known-good after #1), not the flag.
  3. If "upside down" is the hand being pitched rather than rolled, a `vU` negation cannot
     correct it and the basis must be fixed.

---

## 9. Game keeps running when the headset stops sending XrApi data

- **Status:** Fixed (2026-10-11) - **verified with the WinlatorXR XrApi UDP tester tool**
- **Priority:** Medium-High (safety / feel; the player can be hurt on a stale view)
- **Observed:** When WinlatorXR stops delivering fresh XrApi packets (headset off/backgrounded,
  runtime stalled), the game kept simulating and rendering as if nothing changed - the view
  froze on the last pose but the world kept advancing. HWXR instead blocks the game until
  the compositor consumes a frame.
- **Decision (owner):** a **hard hang is acceptable**. WinlatorXR runs the game in its
  container/sandbox, so if the runtime is gone for good the whole sandbox (and the hung
  `Client.exe`) can be torn down. So the hold is unbounded by design rather than a bounded
  watchdog.
- **Fix applied:**
  - `game/XrApiUDP.cpp:337-351` - `Poll()` now **blocks (sleeping in `select`, no CPU)** until
    the runtime reports a fresh `HMD_SYNC`; the previous `XR_SYNC_HOLD_MAX_MS` cap is removed.
    It only kicks in once a packet has actually been seen (`m_bHaveRaw && m_bHavePrevSync`), so
    startup with no headset never blocks. `SocketReadable()` now returns the raw `select()`
    result so a socket error bails instead of spinning.
  - `game/VRStereo.cpp:291` - `VRSyncHold` now defaults to **1 (on)**.
  - Ref drops the "bounded" wording (`game/XrApiUDP.h`, `game/VRStereo.h`).
- **Still to verify:** on device - take the headset off / background the runtime and confirm
  the game holds its last frame with no further simulation, then resumes when tracking returns.
  Note: a true app death requires killing the sandbox (as intended); there is no in-process
  recovery by design.
- **Superseded reasoning (kept for context):** a bounded watchdog + auto-resume was considered
  (avoids a wedged session and any compositor mutual-wait), but the owner chose the hard hang.

---

## 10. Intro logo and movie play in a single frame instead of stereo

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** Medium (first impression; also proves the SBS path for 2D content)
- **Observed:** The intro logo (`ShowSplash`) and the intro movies (`Logo.smk`, `Intro.smk`)
  are drawn full-window on the SBS canvas, so the runtime sees one image split across the
  eyes rather than the same image centered in each eye (like the menu panels).
- **Refs:**
  - `RiotClientShell.cpp:746-781` — `ShowSplash()` blits `interface/splash.pcx` scaled to the
    full screen surface.
  - `RiotClientShell.cpp:6367-6437` — `PlayIntroMovies()` calls `pClientDE->StartVideo(path,
    PLAYBACK_FULLSCREEN)` for `Logo.smk` / `Intro.smk`; `:2282-2295` `UpdateMoviesState()`
    drives the engine's `UpdateVideo()`/`IsVideoPlaying()`.
- **Fix applied:**
  1. **Splash rendered per-eye.** `ShowSplash()` now calls `g_VRStereo.BeginFrame(DTRUE, DFALSE)`,
     detects `IsStereoFrame()`, and wraps the 2D blit in `Begin2D(nEye)/End2D()` for each eye
     (same pattern as `UpdateMenuState`), followed by `DrawXrSyncPixel()` when in stereo.
  2. **Movies skipped in VR.** `PlayIntroMovies()` now jumps straight to the menu when
     `g_VRStereo.IsActive()` (or when movies are disabled). Since the engine's Smacker player
     draws directly to the window (not through the mod's per-eye hooks), re-projecting the
     decoded video into each eye is not practical from `CShell.dll`. Skipping the cinematics
     avoids the single-frame/split artifact and gets to the game cleanly. This is the lowest
     risk approach; the splash still renders correctly in each eye.
- **Still to verify:** splash appears centered in both eyes (not split), intro movies don't
  flash the split frame and you land in the main menu; loading/bumper transitions remain
  correct. If the cinematic is wanted later, a different path (engine-side video draw path)
  will be needed.
- **Note:** The engine video player lives in `Client.exe` outside `CShell.dll`, so per-eye
  video is not feasible without engine changes.

---

## Cross-cutting notes

- **Basis / handedness convention (#1, #2, #8):** both HMD and hands flow through
  `XrQuatToOpenXR` -> `ToGameSpace` (`game/XrApiUDP.cpp`). The HMD basis is now known-good
  after #1; use it as the reference when fixing the hand/weapon basis in #8 rather than
  treating `VRHandFlip` as the fix.
- **Input path (#3, #4):** reconcile every XrApi bool index used in `ButtonBits()` against the
  on-device bool string, then expand `VRCommandOn` / inject handled events for the remaining
  actions; verify on foot and in a mech, in menus and in-game. See #9 for what should happen
  when the input feed itself stalls.
- **Frame pacing / headset stall (#9):** `VRSyncHold` (now on by default) hard-blocks
  `Poll()` on a fresh `HMD_SYNC`; an unbounded hang is accepted because the WinlatorXR sandbox
  can be torn down if the runtime is gone for good.
- **2D content per eye (#5, #6, #10):** menus, splash and movies must all be centered in each
  eye; the engine video player runs in `Client.exe` and is the one case that can't be
  re-projected from `CShell.dll`.
- **Testing:** PC smoke test first (windowed, borderless, sync pixel, menus per eye), then on
  device (Quest 3, WinlatorXR, 2800x1400 SBS). Grep the desktop build a log line with each
  changed behaviour (e.g. "VR: dropped the title bar") to confirm deployment.
  - **XrApi UDP Tester** (`D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\...`, repo
    `WinlatorXR/XrAPIUDPTester`): drives the game's XrApi input on PC. TX 7872 / listens
    7278, auto-increments the OpenXR frame id (= `HMD_SYNC`), and its **"Read Key Input"**
    mode (toggle `]`; `[` = always-on-top) maps the keyboard to the controller:
    `WASD`→left stick, `IJKL`→right stick, `1/2/3/4`→L-Trigger/L-Grip/R-Trigger/R-Grip,
    `5/6`→L/R stick click, `7/8/9/0`→LX/LY/RB/RA, `Y`→L-Menu. Use it to exercise #3/#4 without
    a headset. It also has an InputAPI RX on 7728 that the mod does not use.
- **Repo policy:** ShogoSource is git-ignored; every change there needs a prose note under
  `ShogoSourceModifications/` and a matching entry in `Docs/PROJECT_PLAN.md`.