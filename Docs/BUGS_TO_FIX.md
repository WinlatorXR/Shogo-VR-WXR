# BUGS_TO_FIX

Live issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed` / `Won't fix`.

---

## 1. Head roll is reversed (lean left -> roll right)

- **Status:** Fixed (2026-10-11) - **verified on device**
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

- **Status:** Fixed (2026-10-11) - **verified on device**
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

- **Status:** Fixed (2026-10-11) - **verified on device**; *the left-stick
  movement half was broken and is now fixed - see #11*
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

## 5. Menu text is too small / unreadable at 2800x1400

- **Status:** Open (expanded 2026-10-11)
- **Priority:** Medium (readability/comfort)
- **Observed:** At 2800x1400 SBS, **all** menu text is somewhat small in the headset - not just
  the VR settings page, but every Options/pause menu. #6 (centring) is confirmed fixed on
  device, so what remains is purely text/panel **size**. Proposed: raise the default menu
  angular width/scale so text is legible, and/or split the VR page into **3 sub-menus** so less
  is on screen at once.
- **Refs:**
  - `game/VRMenu.cpp:40-80` — the current VR page item list (aim with, lean with head, menu
    screen size, etc.), each entry a `VRS_CHOICE`/`VRS_TOGGLE` bound to a VarTrack.
  - `game/VRStereo.cpp:248` — `m_vtMenuWidth.Init(... "VRMenuWidth", ... 80.0f)`; panel angular
    width fed by `ComputeHudXform` at `game/VRStereo.cpp:643-655`.
  - VR page lives in Shogo's Options menu (ShogoSource `OptionsMenu.cpp`), which is
    git-ignored; add new pages/sub-menus there and record them in
    `ShogoSourceModifications/` (prose-note policy).
- **Root cause (2026-10-11):** the whole menu tree is laid out for the **full SBS width**
  (2800), then the mod scales that canvas into the panel, so native font pixels shrink
  (~0.3x at `VRMenuWidth` 80 deg -> text a few px -> too small). `CRiotMenu::ScreenDimsChanged()`
  (ShogoSource `RiotMenu.cpp:212-217`) sizes every menu from `GetSurfaceDims(hScreen)`, which
  returns the real 2800 because `Hook_GetSurfaceDims` only substitutes the virtual size while
  `m_bVirtualHud` (the HUD) is set (`game/VRStereo.cpp:1505-1515`). Menu text is pre-rendered
  surfaces (`CTextHelper::CreateSurfaceFromString`) so it *does* scale with the xform - but from
  a 2800-wide layout the scale is ~0.3x. Raising `VRMenuWidth` scales panel + text together, so
  on its own it can't make text large enough (and enlarges the panel).
- **Fix direction (modelled on the HUD, #5/#6):** give menus their own small virtual layout
  (~640x480, the resolution Shogo's menus were designed for) and have the menu tree lay out in
  that space; the existing per-eye xform then scales it *up* into the panel, so 12px fonts land
  at ~16px+ in the eye. Concretely: expose the menu virtual size from the mod, have
  `CRiotMenu::ScreenDimsChanged()` use it when VR is active (ShogoSource `RiotMenu.cpp`, needs a
  new prose note), and set `m_fVirtW/H` to the same size in `Begin2D` for the non-HUD pass.
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

- **Status:** Fixed on device (2026-10-11) - user confirmed ("seems fixed now")
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
- **Resolved (2026-10-11):** confirmed fixed on device at 2800x1400; the menu now sits centred
  per eye. Remaining readability complaint is text **size**, tracked separately in #5.

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

- **Status:** Fixed (2026-10-11) - **verified on device**
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

- **Status:** Fixed in code (2026-10-11) - **movies confirmed on device (user: "basically
  fixed"); splash still to verify**
- **Priority:** Medium (first impression; also proves the SBS path for 2D content)
- **Observed:** The intro logo (`ShowSplash`) and the intro movies (`Logo.smk`, `Intro.smk`)
  were drawn full-window on the SBS canvas, so the runtime saw one image split across the eyes.
  The movie also left a stale video "background" (the engine's own full-window draw looping
  behind/around the per-eye copies).
- **Refs:**
  - `RiotClientShell.cpp:747-780` — `ShowSplash()` plain 2D blit; gated to the right half of
    the SBS window when `g_VRStereo.IsStereoFrame()`.
  - `RiotClientShell.cpp:2300-2301` — `UpdateMoviesState()` calls
    `g_VRStereo.ShiftMovieToRightHalf()` after `UpdateVideo()` while a movie is shifted.
  - `RiotClientShell.cpp:6393-6396` — `PlayIntroMovies()`: `m_bMovieShiftRight =
    g_VRStereo.IsActive()` and `nFlags` drops `PLAYBACK_FULLSCREEN` in VR so the video keeps
    its native size.
  - `game/VRStereo.cpp:1088-1180` — `ShiftMovieToRightHalf()`: captures the window into a
    scratch surface, auto-detects the video rect (`DetectMovieSource`, `:1185`), paints the
    whole screen black, then blits the video into the middle of each half (both eyes see the
    same native-size movie). `ResetMovieSource()` re-detects at each new movie/resize.
- **Fix applied:**
  1. **Splash** is a plain 2D blit; in stereo it draws only into the right half of the SBS
     window.
  2. **Movies are cloned per-eye.** In VR the movie starts at native size (no fullscreen),
     `UpdateVideo()` decodes into the window, and `ShiftMovieToRightHalf()` re-blits the
     detected video into the middle of both halves after blanking the window to black, so each
     eye sees one clean movie with no split or stale looping background.
- **Abandoned approaches (kept for context):** per-eye via the hooked surface table doesn't
  reach the Smacker player (it draws in `Client.exe`); a whole-window black surface filled
  with `SetPixel` hung the game (reverted); `ClearScreen` and `FillRect(0)` don't paint on
  this driver - real engine black is `SetupColor1(0,0,0,DFALSE)`.
- **Still to verify:** on device at 2800x1400 - splash centered (or accepted right-side),
  clean black background behind both movie copies, no hang/regression in the movie->menu
  transition.

---

## 11. Motion-controller movement doesn't actually move the player

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** High (this is the other half of #3 - the stick should walk you around)
- **Observed:** Reported today with #3's on-device testing: the left stick pushes the analog
  axes through but the player never moves. This is what #3's "movement wired" step was
  supposed to deliver and it doesn't work yet.
- **Refs / wiring already in place:**
  - `RiotClientShell.cpp:3128-3135` — `CalculateCameraRotation()` feeds the mod's left-stick
    axes into `m_MoveMgr->UpdateAxisMovement(DTRUE, fVRFwd, 0.10f, DTRUE, fVRRight, 0.10f)`.
  - `game/VRStereo.cpp:2024-` — `GetMoveAxes(fForward, fRight)`: analog stick with dead-zone;
    source of `fVRFwd/fVRRight`.
  - `CMoveMgr.cpp:123-157` — `UpdateAxisMovement()` only **stores** the axis values; the block
    that used to `SetCommandOn(COMMAND_ID_FORWARD/REVERSE/STRAFE_LEFT/STRAFE_RIGHT)`
    (lines 132-156) is **commented out**.
- **Root cause analysis:**
  1. There is **no `SetCommandOn` in the `CClientDE` interface** (`ShogoSource/AppHeaders/client_de.h`
     has only `IsCommandOn`, line 737). The commented block in `UpdateAxisMovement` is stale
     and could never compile, so it is **not** the fix to re-enable.
  2. Movement flags are built by `CMoveMgr::UpdateControlFlags()` (`CMoveMgr.cpp:206-244`)
     entirely from `m_pClientDE->IsCommandOn(COMMAND_ID_FORWARD / REVERSE / LEFT / RIGHT /
     STRAFE_LEFT / STRAFE_RIGHT / ...)`. Those queries go through the mod's
     `Hook_IsCommandOn` (`game/VRStereo.cpp:1981-1988`), which while a controller is live
     returns only `VRCommandOn()` - and `VRCommandOn()` (`VRStereo.cpp:2003-2015`) answers
     **only FIRING / JUMP / DUCK**. So all movement commands read false, `m_dwControlFlags`
     never gets `BC_CFLG_FORWARD` etc., and the player never moves.
  3. The **analog speed cap is already wired and fine**: `CMoveMgr.cpp:539/570/750` read the
     stored `m_fAxisForwardBackwardVel`/`m_fAxisLeftRightVel` when
     `m_bUseAxisForwardBackward || m_bUseAxisLeftRight` is set, capping walk/run speed by
     stick deflection. What's missing is the direction flags, not the speed.
  4. The keyboard is intentionally dead while a controller is live (so a digital
     "tap the W key via `SendInput`" hack is out - it would be indistinguishable from the
     real keyboard and would break the no-leakage guarantee).
- **Proposed fix (no ShogoSource changes needed):**
  1. In the VR command path (`VRStereo.cpp`), while `m_bGameplayInput && IsControllerActive()`,
     also answer the **movement commands** from the left-stick axes (reuse `GetMoveAxes()` with
     the same dead-zone): `FORWARD` when `fForward > dead`, `REVERSE` when `< -dead`,
     `STRAFE_LEFT/RIGHT` (and/or `LEFT/RIGHT`) from `fRight`. This makes
     `UpdateControlFlags()` build the right `BC_CFLG_*`, and the existing analog cap gives
     proportional speed.
  2. Verify `COMMAND_ID_LEFT/RIGHT` vs `STRAFE_LEFT/STRAFE_RIGHT` semantics (keyboard turn vs
     strafe) so the stick strafes rather than turns (turning is already the right stick via
     `StickTurn()` in `ApplyHeadTracking`).
  3. PC-test with the **XrApi UDP Tester** ("Read Key Input", WASD maps to the left stick) to
     confirm the player walks before taking it to the device.
- **Fix applied (2026-10-11):** `CVRStereo::VRCommandOn()` (`game/VRStereo.cpp:2003-2034`) now
  handles `COMMAND_ID_FORWARD`/`REVERSE`/`STRAFE_LEFT`/`STRAFE_RIGHT` by reading the left-stick
  axes from `GetMoveAxes()` (dead-zoned + aim-rotated). `COMMAND_ID_LEFT/RIGHT` are deliberately
  left unanswered so the stick strafes (turn stays on the right stick via `StickTurn()`).
  No ShogoSource change was required. Build + deploy + smoke (`mods=102`) done.
- **Still to verify:** PC test via the XrApi UDP Tester (WASD on the left stick walks forward/
  back/strafe, right stick turns), then on device; confirm speeds feel proportional and there is
  no drift when the stick is centred.

---

## 12. Loading screen renders mono

- **Status:** Open
- **Priority:** Medium (same flat-2D-per-eye class as #5 / #6 / #10)
- **Observed:** The level **loading** screen (and the bumper screen) is drawn once, centered, on
  the full SBS canvas. With SBS active the runtime reads one image, so each eye sees half of one
  picture at the seam (mono / split) instead of a per-eye copy. Reported on device 2026-10-11.
- **Root cause:** `CRiotClientShell::UpdateLoadingLevel()` blits `m_hLoading` /
  `m_hBumperText` / `m_hLoadingWorld` straight to the screen surface centered
  (`DrawSurfaceToSurfaceTransparent(hScreen, ..., (nScreenWidth - w) / 2, (nScreenHeight - h) / 2, ...)`).
  It never goes through `RenderCamera()` / `BeginFrame()` / `Begin2D()`, so it is not stereo-aware
  - the same class of bug as the splash/menu/movie issues in #5/#6/#10.
- **Refs:**
  - `RiotClientShell.cpp:7134-7179` — `UpdateLoadingLevel()`: centered blits to the full screen
    surface, no per-eye offset.
  - `RiotClientShell.cpp:1510-1513` — `OnEnterWorld()` calls it while `GS_BUMPER` /
    `GS_LOADINGLEVEL`.
  - `RiotClientShell.cpp:2514-2525` — `UpdateLoadingLevelState()` calls it every frame while loading.
  - `RiotClientShell.cpp:1190` — `m_hLoading` loaded from `interface/loading.pcx`;
    `m_hLoadingWorld` / `m_hBumperText` created from `IDS_BUMPER_LOADING` (`RiotClientShell.cpp:7310`).
  - `game/VRStereo.cpp:963-1047` — `Begin2D(nEye)` / `End2D()` per-eye 2D path used by the
    menu/pause fix (#5/#6) to reuse.
  - `game/VRStereo.cpp:1088-1180` — `ShiftMovieToRightHalf()` clone approach used for #10.
  - `RiotClientShell.cpp:747-792` — `ShowSplash()` right-half gate (#10) is the simplest precedent.
- **Proposed direction:** one of two, mirroring the existing fixed cases:
  1. **Per-eye (preferred):** bracket the blits in
     `g_VRStereo.Begin2D(nEye, DFALSE)` / `End2D()` for both eyes, like
     `UpdateMenuState`/`UpdatePausedState` do, so the loading art is centered inside each eye
     half. `UpdateLoadingLevel()` is pure 2D (no camera/3D), so it needs no `RenderStereo` or
     polygrid - just the per-eye copy.
  2. **Right-half clone (quick):** like `ShowSplash` (#10), draw the loading art into the right
     half of the SBS window while stereo is active; the left eye then shows the cleared
     background. Less correct than per-eye but a one-place change.
- **Still to verify:** on device at 2800x1400 - loading/bumper text centered and legible in both
  eyes with no seam-split; no regression in the loading -> level transition.

---

## 13. Render resolution forced to 640x480

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** High (the game renders tiny; everything is soft/scaled)
- **Observed:** On device the game comes up at 640x480 regardless of the display, so the stereo
  image is low-resolution and the window does not fill the screen.
- **Root cause:** The engine seeds its default `RMode` from the console variables `ScreenWidth` /
  `ScreenHeight` (`ShogoSource/AppHeaders/clientshell_de.h:29-30`) and `OnEngineInitialized` passes
  that mode to `SetRenderMode` (`RiotClientShell.cpp:1037`); there is a hard 640x480 fallback at
  `RiotClientShell.cpp:1046`. Nothing set those variables, so the default (640x480) won.
- **Fix applied (2026-10-11):** `launcher-xr\ShogoVR.cpp` `BuildArgs()` now appends
  `+screenwidth W +screenheight H` (from `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`) to the
  `Client.exe` command line, before the `-rez ShogoVR` entry. `launcher-xr\build.cmd` now links
  `user32.lib` for `GetSystemMetrics`.
- **Still to verify:** on device, the game starts at the display resolution (log line
  `Resolution WxH, drawing at WxH` in `ShogoVR\ShogoVR_game.log`); no black bars / stretched image.

---

## 14. Crosshair is on by default

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** Medium (clutter; the aim helper already shows where you point)
- **Observed:** Both the game's own first-person crosshair and the mod's aim helper show by
  default.
- **Root cause:** The game crosshair is always drawn (`PlayerStats.cpp:655-662`
  on foot, `:758-785` in the Enforcer) from `m_bCrosshairEnabled` (default `DTRUE`), and the mod
  aim helper `VRAimStyle` defaulted to 2 (reticle).
- **Fix applied (2026-10-11):**
  - Aim helper: `VRAimStyle` default is now 3 = none (`game/VRStereo.cpp` Init, and the VR menu
    row default in `game/VRMenu.cpp`). Still user-selectable (laser / dot / reticle / none).
  - Game crosshair: new `VRGameCrosshair` setting (default 0). `PlayerStats::DrawOnFoot` and
    `DrawEnforcer` now wrap their crosshair blits in `g_VRStereo.BeginCrosshair()` /
    `EndCrosshair()`, and `CVRStereo::SkipThisDraw()` skips that pass when `VRGameCrosshair` is 0.
    Exposed as a "game crosshair" toggle row in the in-game VR menu (`game/VRMenu.cpp`), which
    needed `VRMENU_ITEMS`/`MAX_GENERIC_ITEMS` raised 27 -> 28 (`game/VRMenu.h`,
    `ShogoSource/ClientShellDLL/BaseMenu.h`).
- **Side note:** while adding the menu row, the action-row index constants `VRM_RECENTER` /
  `VRM_DEFAULTS` / `VRM_BACK` in `game/VRMenu.cpp` were found to be **off by one** (they had not
  been bumped when the "aim helper" row was added). Corrected to 25 / 26 / 27.
- **Still to verify:** on device, no crosshair on foot or in a mech by default; toggling
  "game crosshair" on draws it at aim depth (head aim) and still hides it when aiming by hand.

---

## 15. Default controller mapping doesn't match the intended layout

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** High (core controls)
- **Requested default mapping (revised 2026-10-11):**
  - Left stick: move
  - Left stick click: crouch (**hold**)
  - Left X: crouch (**toggle**)
  - Left grip: jump (regular, a single jump)
  - Left Y: zoom toggle (re-select the currently equipped weapon - see #18)
  - Left trigger: **F9 (quick load) while dead**; nothing while alive
  - Right trigger: fire
  - Right grip: double jump (a single jump when on foot)
  - Right A: toggle first/third person camera
  - Right B: toggle vehicle / mech mode (the transform)
  - Right stick: turn (left/right) and change weapon (up/down) - the engine's own weapon
    cycling, so **no** button is mapped to next/previous weapon
  - Left menu: menu / Esc (unchanged)
  - Right stick click: recentre the view (unchanged - not listed in the revision; **confirm
    whether to keep**)
- **Fix applied (2026-10-11):**
  - `game\ShogoVRShared.h` + identical `bridge\ShogoVRShared.h`: added
    `SHOGOVR_BTN_CROUCH_HOLD` (0x4000) and renamed `SHOGOVR_BTN_PREV_WEAPON` ->
    `SHOGOVR_BTN_LEFT_TRIGGER`; comments on the other bits updated.  (The bridge's
    `k_fnButton[]` only uses FIRE/JUMP/CROUCH/TRANSFORM/NEXT_WEAPON/WEAPONS/LOG/MENU, all
    unchanged, so it still binds correctly.)
  - `game\XrApiUDP.cpp` `ButtonBits()` remapped to the raw XrApi indices: 0 (L grip) -> JUMP,
    2 (L stick click) -> CROUCH_HOLD, 7 (L trigger) -> LEFT_TRIGGER, 8 (X) -> CROUCH, 9 (Y) ->
    ZOOM, 10 (A) -> VIEWTOGGLE, 11 (B) -> TRANSFORM, 12 (R grip) -> DOUBLEJUMP, 13 (R stick
    click) -> RECENTER.
  - `game\VRStereo.cpp`: `VRCommandOn` answers `COMMAND_ID_JUMP` from JUMP|DOUBLEJUMP (left
    grip jumps; the right grip double-jumps and also jumps on foot) and `COMMAND_ID_DUCK` from
    the latched toggle **or** the held `CROUCH_HOLD`; `UpdateInput` drops the previous-weapon
    button (the right stick still cycles weapons via `PollWeaponCycle`) and leaves the left
    trigger doing nothing while alive.
- **Still to verify:** on device - each binding does what the list says, on foot and in a mech,
  and no action fires from a button it shouldn't.  The left trigger's dead behaviour is #16.

---

## 16. Can't respawn when dead (the Enter key does nothing)

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** Medium-High (you get stuck after dying)
- **Observed:** While dead, tapping **Enter** from the controller does nothing - the game's
  respawn / load prompt does not accept it. This is a bug in the game itself, so Enter is a
  dead end.
- **New approach:** use the game's own save/load cycle instead of Enter:
  1. **Quicksave on level start.** The mod sends **F6 (quicksave)** once per level, always,
     as soon as the level is playable - i.e. after the intro cutscene has ended and
     `GS_PLAYING` is active (not during the loading screen, a movie, or any menu/pause).
  2. **Quick load on death.** While dead, the **left controller trigger** sends **F9
     (quick load)**; while alive the left trigger does nothing.
- **Why:** F6/F9 are the game's own quicksave/quickload bindings and reliably restore the
  level-start state, sidestepping the broken Enter path.
- **Open questions / to verify:**
  - Confirm F6/F9 are bound to quicksave/quickload in the shipped default config (the mod
    taps the *bound* keys, so an unbound key would do nothing).
  - The quicksave is taken **once per level**: the shell arms it when the level finishes
    loading, and it fires only after ~2 s of gameplay while alive, so opening/closing a
    menu (which drops gameplay input) does not trigger it.
  - Right trigger still sends `COMMAND_ID_FIRING` (the game's respawn handler uses it as a
    secondary path).
- **Fix applied (2026-10-11):**
  - `ShogoSource\ClientShellDLL\RiotClientShell.cpp`: `UpdateLoadingLevelState()` calls
    `g_VRStereo.SetLevelLoaded()` when the world + player object exist and it flips to
    `GS_PLAYING` (so multiplayer never arms it).  Documented in
    `ShogoSourceModifications\ClientShellDLL\RiotClientShell.cpp.md` edit 18.
  - `game\VRStereo.h/.cpp`: new `SetLevelLoaded()` arms `m_bAutosavePending`; `UpdateInput`
    fires `InjectCommandTap(COMMAND_ID_QUICKSAVE)` once after **120 frames (~2 s)** of
    continuous gameplay input while alive (the settle window so an opening cutscene ends, and
    it resets if play stops), then disarms.  The left trigger while dead now calls
    `InjectCommandTap(COMMAND_ID_QUICKLOAD)` instead of tapping Enter.
- **Still to verify:** on device - a level start produces one quicksave (watch for the
  "Quicksaving" message) not repeated when merely closing a menu; dying and pressing the left
  trigger reloads that save.  If the save lands before an opening cutscene, lengthen the 120
  frame settle; if it never fires, confirm F6 is bound to quicksave in Options > Keyboard.

---

## 17. Controller face buttons leak the keyboard letters X/Y/B/A into the game

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** Medium (stray keystrokes trigger unintended actions while playing)
- **Observed:** WinlatorXR passes the Quest face buttons through as the keyboard letters
  X/Y/B/A **in addition to** the XrApi feed, so the game reacts to those keys while playing.
- **Root cause:** WinlatorXR injects the letters as real key events; the engine turns them
  into game commands (held state via `IsCommandOn`, edges via `OnCommandOn`). The mod's
  `Hook_IsCommandOn` already blocks the held path while a controller is live, but **edge**
  commands still fire from the raw keystroke.
- **Fix applied (2026-10-11):** new `CVRStereo::ShouldSwallowFaceKey(int vk)`
  (`game/VRStereo.{h,cpp}`) returns true while a controller is live, gameplay input is
  active, and the player is **not** dead, for `VK` `A`/`B`/`X`/`Y`. `CRiotClientShell::OnKeyDown`
  now calls `pClientDE->ClearInput()` and returns for those keys (the same "cancel the key"
  trick the `GS_BUMPER` path already uses), and `OnKeyUp` returns early, so the stray keys
  never reach the engine's command system.
- **Scope:** only while playing and alive. Menus and the dead state are untouched, so the
  keyboard still works there and the mod's own injected keys (Esc, the weapon keys, and the
  F6/F9 saves of #16) are unaffected.
- **Still to verify:** on device - while alive the face buttons do nothing on their own;
  the mod's mapped actions (fire / jump / crouch / weapon / view / etc.) still work; and
  when dead or in a menu the keyboard still behaves as before.

---

## 18. Weapon zoom toggle must re-select the currently equipped weapon

- **Status:** Fixed in code (2026-10-11) - **needs on-device verification**
- **Priority:** Medium (zoom is a core weapon feature)
- **Requirement:** the **left controller Y** button toggles zoom. Zoom is toggled by
  *re-selecting the already-equipped weapon* (the engine flips zoom when the same weapon is
  selected again - `CWeaponModel::HandleInternalWeaponChange`, `WeaponModel.cpp:1674`). The
  user reports that on foot **both weapon 5 and weapon 6** zoom, while in MCA mech mode only
  **weapon 6** does. So the button must send the weapon key matching the **currently equipped**
  weapon, not a fixed slot.
- **How to track the current weapon:** the shell already exposes it -
  `CRiotClientShell::m_weaponModel.GetId()` returns the current `RiotWeaponId`, and the mod
  already receives it every frame (`g_VRStereo.UpdateHeldGun(hWeaponModel, m_weaponModel.GetId(),
  vPos)`, `RiotClientShell.cpp:3585`). Convert id -> weapon-slot command with
  `GetCommandId(nWeaponId)` (`ShogoSource/Shared/WeaponDefs.h:249`) and have the mod inject
  that command's **bound key** on Left Y. Reading `GetId()` is more reliable than remembering
  the last weapon command, because the right stick changes weapons via
  `ChangeToNextWeapon()`/`ChangeToPrevWeapon()` (not via the numbered commands).
- **Engine facts to reconcile with the report:** `CanWeaponZoom()` (`Shared/WeaponDefs.h:1959`)
  returns true only for `GUN_ASSAULTRIFLE_ID` and `GUN_SNIPERRIFLE_ID`. `GetWeaponId()` maps
  on foot `COMMAND_ID_WEAPON_4` -> Assault Rifle and mech `COMMAND_ID_WEAPON_5` -> Sniper
  Rifle; on foot `COMMAND_ID_WEAPON_5` = Energy Grenade and `COMMAND_ID_WEAPON_6` = Kato
  Grenade (neither zoomable). So the user's "weapon 5 / 6" is probably the in-game weapon
  list/labels, not `COMMAND_ID_WEAPON_n` - **confirm on device which slots are meant**, but
  the track-and-re-select approach works either way.
- **When the equipped weapon can't zoom:** re-selecting it is harmless -
  `CWeaponModel::ChangeWeapon` returns right after its `CanWeaponZoom` check - so no
  special-casing is needed.
- **Fix applied (2026-10-11):** `game\VRStereo.cpp` now includes `WeaponDefs.h`; the mod
  caches the equipped weapon id (`m_nWeaponId`, added to `VRStereo.h`, initialised to -1) in
  `UpdateHeldGun` each frame.  On Left Y, `UpdateInput` maps the id with `GetCommandId()` and
  taps that weapon-slot command (re-selecting the equipped weapon toggles its zoom); if the id
  is unknown it does nothing.
- **Still to verify:** on device - Left Y toggles zoom for the zoomable weapons on foot and
  in a mech, and does nothing for the rest (confirm which in-game slots you meant by "5 / 6").

---

## Cross-cutting notes

- **Basis / handedness convention (#1, #2, #8):** both HMD and hands flow through
  `XrQuatToOpenXR` -> `ToGameSpace` (`game/XrApiUDP.cpp`). The HMD basis and the hand
  roll (`VRHandFlip` inverted so `0` = corrected) are now confirmed on device. If the
  hand *aim* is ever found wrong, fix the hand frame in `GetHandWorld()` /
  `ToGameSpace()` against the (known-good) HMD basis rather than re-using the flag.
- **Input path (#3, #4, #15, #16, #17, #18):** the controller->game path (#3) and firing (#4)
  are verified on device. #15 (revised mapping), #16 (auto-quicksave at level start +
  quick-load on the left trigger when dead), #17 (the face-button keystrokes A/B/X/Y no
  longer leak) and #18 (Left-Y re-selects the equipped weapon to toggle zoom) are fixed in
  code and need on-device verification. Also reconcile each XrApi bool index in
  `ButtonBits()` against the on-device bool string. See #9 for what should happen when the
  input feed itself stalls.
- **Frame pacing / headset stall (#9):** `VRSyncHold` (now on by default) hard-blocks
  `Poll()` on a fresh `HMD_SYNC`; an unbounded hang is accepted because the WinlatorXR sandbox
  can be torn down if the runtime is gone for good.
- **2D content per eye (#5, #6, #10, #12):** menus, splash, movies and the loading/bumper
  screens must all be centered in each eye; the engine video player runs in `Client.exe` and is
  the one case that can't be re-projected from `CShell.dll`.
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