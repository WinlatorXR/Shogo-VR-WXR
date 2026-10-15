# BUGS_TO_FIX

Live issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed in code` (awaiting a device pass) / `Fixed` (verified) /
`Won't fix`.

## Status at a glance

Verified on device: **#1, #2, #3, #4, #5, #6, #8, #9, #10, #11, #13, #14, #15, #16, #17**,
**#19** (zoom stays removed; the view fills normally), **#23/#31** (Right-A view toggle), **#27**
(shots track the visible gun), **#28** (the peripheral hurt/explosion flash - 2026-10-13), **#29**
(muzzle flash at the visible gun's muzzle; the on-foot dual pistols keep the game's own alternating
flashes), **#30** (VR menu trimmed to 7 rows), **#21** (the VR body no longer shows in
third-person / cutscene / death views - user-confirmed), **#34** (the gun no longer sits below the
shot origin), **#35** (dominant hand - user-confirmed) and **#36** (Right B transform). #4 and #9
were verified with the WinlatorXR XrApi UDP Tester rather than in the headset. **Removed /
cancelled:** #7 (supersampling not pursued), #18 (zoom re-select, folded into #19), #24/#25/#33 (the
autosave family - players can save/load manually if they hit an issue instead) and #32 (jump -
appears fixed on device). **Fixed in code, awaiting a device pass:** #12 (loading/bumper/credits
drawn per eye), #20 (crouch now lowers the eyes), #22 (per-axis left-stick dead zone) and #26 (intro
video moved down 1/8 + 50 px; the whole-window fallback that clipped its bottom is fixed).

| #  | Issue | Status |
|----|-------|--------|
| 1  | Head roll reversed | Fixed - verified |
| 2  | Hands upside down (`VRHandFlip`) | Fixed - verified |
| 3  | Controller input not used | Fixed - verified (left stick: #11) |
| 4  | Right trigger doesn't fire | Fixed - verified (tester) |
| 5  | Menu text too small | Fixed - verified (2026-10-13) |
| 6  | Menu not centred | Fixed - verified |
| 7  | Supersample 2x | Removed (not pursued) |
| 8  | Hands upside down by default | Fixed - verified |
| 9  | Game runs on when headset stalls | Fixed - verified (tester) |
| 10 | Intro logo/movie mono | Fixed - verified (intro offset: #26) |
| 11 | Motion-controller movement | Fixed - verified |
| 12 | Loading/bumper/credits screens mono | Fixed in code (2026-10-14) - per-eye; needs a device pass |
| 13 | Render resolution 640x480 | Fixed - verified |
| 14 | Crosshair on by default | Fixed - verified |
| 15 | Controller mapping | Fixed - verified (individual controls: #22) |
| 16 | Can't respawn when dead | Fixed - verified |
| 17 | Face buttons leak X/Y/B/A | Fixed - verified (A/B leak: #31) |
| 18 | Weapon zoom re-select | Removed - superseded by #19 |
| 19 | Zoom shrinks view (blocker) | Fixed - verified (2026-10-14; zoom removed) |
| 20 | Crouch doesn't lower the VR camera | Fixed in code (2026-10-14) - eyes lower; needs a device pass |
| 21 | Body shown in 3rd-person / cutscenes | Fixed - verified (2026-10-14, user-confirmed) |
| 22 | Left-stick deadzone | Fixed in code (2026-10-14) - per-axis; needs a device pass |
| 23 | Right A/B intermittent/swapped | Fixed - verified (2026-10-14) |
| 24 | On-foot autosave | Removed / cancelled (2026-10-14) - manual save/load |
| 25 | Autosave re-arms after load | Removed / cancelled (2026-10-14) - manual save/load |
| 26 | Intro video too high | Fixed in code (2026-10-14) - moved down 1/8 + 50 px; bottom-clip fallback fixed; needs a device pass |
| 27 | Shots vs weapon model | Fixed - verified (2026-10-14) |
| 28 | Hurt/explosion flash not visible | Fixed - verified (2026-10-13) |
| 29 | Muzzle flash centred on the view | Fixed - verified (2026-10-14); dual pistols excluded |
| 30 | Trim the in-game VR menu | Fixed - verified (2026-10-14) |
| 31 | Right B toggle + A/B keystrokes leak | Fixed - verified (2026-10-14) |
| 32 | Jump intermittently stops working | Removed - appears fixed (2026-10-14) |
| 33 | Area load doesn't re-trigger autosave | Removed / cancelled (2026-10-14) - manual save/load |
| 34 | Weapon model lower than shot origin | Fixed - verified (2026-10-14) |
| 35 | Left/right dominant hand does nothing | Fixed - verified (2026-10-14, user-confirmed) |
| 36 | Right B (transform) needs repeated taps | Fixed - verified (2026-10-14) |

## Open items (as of 2026-10-14)

No tracked bug is still **open**: everything is either fixed/verified or fixed in code awaiting a
device pass, and a few items were removed/cancelled.

**Fixed in code, awaiting a device pass:**

- **#12 - Loading / bumper / credits screens render mono** (text unreadable, split across both
  eyes). Now drawn per eye like the menus (#5); the credits are laid out on the mod's small virtual
  screen so their text stays centred and legible in each eye.
- **#20 - Crouch doesn't lower the VR camera**. The eyes now drop while the crouch controls are on
  (eased), using a new `VRCrouchDrop` amount (default 0.6 m).
- **#22 - per-axis left-stick dead zone** (forward/back 0.08, strafe 0.20).
- **#26 - intro video moved down ~1/8 of the screen plus 50 px.** The whole-window fallback (used
  when the movie frame isn't detected) no longer applies that offset, which was clipping the bottom;
  detection was also made more robust so the offset still applies when the frame is found.

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
- **Verified on device (2026-10-13):** lean left/right rolls the correct way; yaw/pitch unchanged,
  with no noticeable roll-vs-yaw coupling.

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
- **Verified on device (2026-10-13):** the flip rotates the hands 180 deg about the barrel without
  changing where the gun points, and works with the #1 roll fix.

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
- **Verified on device (2026-10-13):** no keyboard/mouse leakage in gameplay; left stick moves
  (analog) and right stick turns; fire/jump/crouch work; weapon switch works; Esc opens the menu
  and the sticks/buttons navigate it with no stuck/stray inputs.

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

- **Status:** Fixed - **verified on device (2026-10-13): menu text is readable in VR.**
- **Device result (2026-10-13):** confirmed working - "The menu fix is great, readable text now in
  VR." No further work needed. (See #30: the VR page is now being trimmed to a short list.)
- **Priority:** Medium (readability/comfort)
- **Code state (2026-10-12): finished.** The virtual-layout members/VarTrack are in
  (`game/VRStereo.h/.cpp`: `m_vtMenuLayout` / `VRMenuLayout` default 640, `m_fMenuVirtW/H`,
  `m_bVirtual2D`, `m_bMenuLayoutDirty`, `GetMenuLayoutSize()` declared, `ConsumeMenuLayoutChanged()`
  inline). Completed this session:
  - `game/VRStereo.cpp`: `ComputeLayout()` now sets `m_bMenuLayoutDirty` when the menu virtual size
    changes; `UpdateBridge()` sets it on the bridge-alive transition (VR menu size becomes active);
    `GetMenuLayoutSize()` **is implemented** (fills the menu virtual size and returns DTRUE only
    while VR is active and the headset is bridged, else DFALSE); `End2D()` now clears
    `m_bVirtual2D`; `Hook_GetSurfaceDims` now substitutes the virtual size when `m_bVirtual2D` is
    set - so it works for the HUD pass *and* the non-HUD menu pass.
  - `ShogoSource` (prose notes in `ShogoSourceModifications/`): `CRiotMenu::ScreenDimsChanged()`
    now asks `g_VRStereo.GetMenuLayoutSize()` and falls back to the real screen size
    (`RiotMenu.cpp`, new prose note); `CRiotClientShell` polls
    `g_VRStereo.ConsumeMenuLayoutChanged()` right after `UpdateBridge()` and re-runs
    `m_menu.ScreenDimsChanged()` when set (`RiotClientShell.cpp`, note edit 7).
  Menus now lay out on a 640x480 virtual canvas and the existing per-eye xform scales them *up*
  into the headset panel - the same trick that fixed the HUD (#6 confirmed fixed on device).
- **Still to verify (device):** every Options/pause page is legible at 2800x1400; no page draws
  ~4x too big or clipped; the default 640 reads well (else lower `VRMenuLayout` or split the VR
  page into sub-pages - see the design notes below).
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
- **Remaining work (optional, after device check):** if 640 is still too small, lower
  `VRMenuLayout` and/or split the VR page into the 3 sub-pages sketched above; both are mod-side
  / menu-source changes only.

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

- **Status:** Removed (2026-10-13) - supersampling is not being pursued.
- **Context:** the game already renders at the display resolution (#13, verified), and that SBS
  canvas is what WinlatorXR samples for both eyes; raising it further was judged not worth the
  sharpness/performance trade-off. Dropped from the tracker.

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
- **Verified on device (2026-10-13):** the default pose shows right-side-up hands; the gun still
  points where the hand points (roll-only, not a mirrored aim).
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
- **Verified with the XrApi UDP tester:** stalling the feed holds the last frame with no further
  simulation and it resumes when tracking returns. (A true app death still requires killing the
  sandbox by design; no in-process recovery.)
- **Superseded reasoning (kept for context):** a bounded watchdog + auto-resume was considered
  (avoids a wedged session and any compositor mutual-wait), but the owner chose the hard hang.

---

## 10. Intro logo and movie play in a single frame instead of stereo

- **Status:** Fixed - **verified on device (2026-10-13)** (splash and movies both render in
  stereo; the remaining intro-video vertical offset is #26).
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
- **Verified on device (2026-10-13):** splash and movies both render cleanly in stereo with no
  split or stale background; the only remaining intro issue is the vertical offset in #26.

---

## 11. Motion-controller movement doesn't actually move the player

- **Status:** Fixed - **verified on device (2026-10-13)**.
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
- **Verified on device (2026-10-13):** the left stick walks/strafes and the right stick turns;
  speeds feel proportional with no drift when centred. (Left/right dead-zone drift is #22.)

---

## 12. Loading / bumper / credits screens render mono (text unreadable, split across both eyes)

- **Status:** Fixed in code (2026-10-14) - awaiting device verification (mirrors #5)
- **Priority:** Medium (same flat-2D-per-eye class as #5 / #6 / #10)
- **Observed:** The level **loading** screen (and the bumper screen) is drawn once, centered, on
  the full SBS canvas. With SBS active the runtime reads one image, so each eye sees half of one
  picture at the seam (mono / split) instead of a per-eye copy. Reported on device 2026-10-11 and
  **again 2026-10-13**: the loading screens are 2D, so their text is unreadable, split between
  both eyes. The user wants "a fix here similar to the menus" (#5). The **credits screen** has the
  same problem (reported 2026-10-14).
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
     polygrid - just the per-eye copy. **Mirror #5:** lay the loading/bumper art out on a small
     virtual canvas (like the menu's 640x480) so the per-eye xform scales the text *up* legibly,
     and reuse/extend the #5 virtual-size plumbing (`GetMenuLayoutSize()` and the
     `Hook_GetSurfaceDims` substitution) rather than only centring.
  2. **Right-half clone (quick):** like `ShowSplash` (#10), draw the loading art into the right
     half of the SBS window while stereo is active; the left eye then shows the cleared
     background. Less correct than per-eye but a one-place change.
- **Still to verify:** on device at 2800x1400 - loading/bumper art centered **and its text legible
  in both eyes** with no seam-split; no regression in the loading -> level transition.
- **Fix applied (2026-10-14, option 1 - per-eye, mirror #5):**
  - `RiotClientShell.cpp` `UpdateLoadingLevelState()` and `UpdateBumperState()` now mirror
    `UpdateMenuState()`: begin the frame (`g_VRStereo.BeginFrame(DTRUE, DFALSE)`), render the
    animated polygrid through the panel in stereo (`RenderStereo` of `m_pMenuPolygrid`), then draw
    the 2D loading art / press-any-key blit once per eye between
    `g_VRStereo.Begin2D(nEye, DFALSE)` / `End2D()`, and finish with `DrawScreenFlashFrame()` /
    `DrawXrSyncPixel()`.
  - The credits / intro states (`UpdateCreditsState`, `UpdateIntroState`, `UpdateGameOver`,
    `UpdateDemoMultiplayerState`) now call a new helper `UpdateCreditsStereo()`, which wraps
    `m_credits.Update()` in the same per-eye pass.
  - `Credits.cpp` now lays the credits out on the mod's small virtual screen
    (`g_VRStereo.GetMenuLayoutSize()`), so the per-eye `Hook_GetSurfaceDims` substitution +
    `Hook_DrawStringToSurface` remap put the text centred in each eye and even pick the larger
    font. No `RenderStereo` is needed for the 2D art itself - the per-eye 2D xform handles it.
  - Notes: `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md` (edit 22),
    `RiotClientShell.h.md` (edit 2), `Credits.cpp.md` (new). Mod side is `game/VRStereo.cpp`
    `Begin2D` / `End2D` / `Hook_GetSurfaceDims` (already present from #5).

---

## 13. Render resolution forced to 640x480

- **Status:** Fixed - **verified on device (2026-10-13)**.
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
- **Verified on device (2026-10-13):** the game starts at full display resolution with no black
  bars and no stretched image.

---

## 14. Crosshair is on by default

- **Status:** Fixed - **verified on device (2026-10-13)**.
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
- **Verified on device (2026-10-13):** no crosshair by default on foot or in a mech.

---

## 15. Default controller mapping doesn't match the intended layout

- **Status:** Fixed - **verified on device (2026-10-13)** (individual controls still pending
  #22/#23/#31/#32).
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
- **Verified on device (2026-10-13):** the revised layout matches the list. Remaining looseness is
  tracked separately: left-stick dead-zone #22, A/B taps #23, the B transform / A/B keystroke leak
  #31, and the intermittent jump #32.

---

## 16. Can't respawn when dead (the Enter key does nothing)

- **Status:** Fixed - **verified on device (2026-10-13)**.
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
- **Verified on device (2026-10-13):** a level start quicksaves (see the "Quicksaving" message)
  and the left trigger reloads it after death. (Autosave gaps/edge cases moved to #24/#25/#33.)

---

## 17. Controller face buttons leak the keyboard letters X/Y/B/A into the game

- **Status:** Fixed - **verified on device (2026-10-13)** (X/Y clean; the remaining A/B leak is
  #31).
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
- **Device result (2026-10-13):** X/Y are clean; the remaining **A/B keystroke leak** is tracked
  separately in #31 (block A/B everywhere except the pause menu), so #17 itself is closed.

---

## 18. Weapon zoom toggle must re-select the currently equipped weapon

- **Status:** Removed (2026-10-13) - superseded by #19: weapon zoom is being removed for now, so
  the Left-Y re-select toggle is dropped. See #19 for the zoom history.

---

## 19. Weapon zoom shrinks the whole view to a tiny square and never restores (RELEASE BLOCKER)

- **Status:** **Fixed - verified on device (2026-10-14)** - zoom stays removed; the view always fills
  normally (no tiny centre square, no stuck FOV).
- **Priority:** Blocker
- **Fix applied (2026-10-13) - option 3 (remove zoom):** dropped the `ZOOM` mapping in
  `game/XrApiUDP.cpp` `ButtonBits()` and the `SHOGOVR_BTN_ZOOM` bit (both `ShogoVRShared.h`
  copies); removed the Left-Y re-select block in `CVRStereo::UpdateInput`, the scope overlay
  (`DrawScopeOverlay()`, `m_bScoped`, the `m_hScopeOnFoot`/`m_hScopeMech` surfaces) and the
  now-unused `m_nWeaponId` tracking. The pin-FOV guard in `RenderStereo()` is kept (the eye FOV
  stays at the headset base) and the camera FOV ratio is now only logged. Build clean; deployed;
  PC smoke ok (new stamp `built Oct 13 2026 21:24:09`, intro runs).
- **Device result (2026-10-13):** the pin-FOV + scope-cursor approach (option 1 below) **does not
  work** in the headset ("the new zoom method doesn't work"). Decision: **remove zoom for now**
  (option 3). The pin-FOV guard is kept (the eye FOV is never multiplied by the zoom ratio): it is
  what stops the tiny-square / stuck-FOV presentation bug. Zoom is now unbindable, so the camera
  never zooms and nothing scope-related is drawn.
- **Observed (device, 2026-10-12):** Left Y zoom (re-select the equipped weapon, #18) is hard to
  trigger and sometimes doesn't fire; when it does, the **entire display collapses to a small
  square in the middle** of the view and **never restores** afterwards.
- **Root cause (suspected):** the mod couples the eye render FOV to the game's camera FOV.
  `RefreshFrame()` derives `fZoom = fOrigFovX / m_fLastBaseFovX` from the live camera FOV
  (`game/VRStereo.cpp:797-799`), shrinks the eye FOV by it (`:876`, `:884`), then reports the
  shrunken `m_fEyeFovX/Y` to WinlatorXR (`game/VRStereo.cpp:1842-1844`). Shogo's zoom slams the
  camera FOV to 10 deg (`ShogoSource/ClientShellDLL/RiotClientShell.cpp:59` `FOV_ZOOMED`,
  `UpdateCameraZoom` `:3288-3352`). Feeding a ~10 deg FOV at the SBS canvas makes the runtime
  present a tiny central image; "never restores" fits a stuck zoom state (the re-select toggle is
  unreliable) plus the retained narrow FOV. It also touches real screen presentation, which we do
  not want.
- **Requirement:** a zoom that does **not** change the resolution / presented FOV at all - keep the
  eye render FOV pinned to the headset base FOV - **or remove zoom entirely**.
- **Options:**
  1. **Pin the FOV + scope cursor (preferred if legible):** stop deriving `fZoom` from the camera
     FOV (drop the coupling at `:797-884`); keep `m_fEyeFovX/Y` at the base forever. Show zoom
     feedback as a mod-drawn **scope cursor / vignette** per eye so the player knows zoom is on,
     with no change to the world's rendered scale. (The game already has a zoom scope surface,
     `Cross_Zoom.pcx`, `ShogoSource/ClientShellDLL/PlayerStats.cpp:496/552/632`.)
  2. **True magnification without an FOV change:** render the eye at the narrow FOV into an
     offscreen surface and present it magnified inside the scope overlay (a second pass per eye).
     Most complex; only if option 1 is not enough.
  3. **Remove zoom:** unbind Left Y (drop the `ZOOM` mapping in `ButtonBits()`
     `game/XrApiUDP.cpp:434` and the `UpdateInput` tap) and mark won't-fix. The user's stated
     fallback.
- **Refs:** `game/VRStereo.cpp:790-800, 876-884, 1842-1844`;
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp:59, 3288-3352`; #18.
- **Fix applied (2026-10-12) - option 1 (pin FOV + scope cursor):**
  - `game/VRStereo.cpp` `RenderStereo()`: the eye FOV is no longer multiplied by the zoom ratio -
    `fEyeFovY = ClampFov(BaseEyeFovY(m_fLastBaseFovX))` and the eye separation is left at the full
    `fIpdWorld` (the `* fZoom` and zoom-shrunk IPD are gone). The reported frame FOV
    (`m_fReportTanX/Y`) therefore stays at the base too, so zoom **never changes the presented
    scale or the headset FOV**. `fZoom` is still computed (and logged) purely to detect zoom.
  - New `m_bScoped` flag (set from `fZoom < 0.97f`) drives a **zoom cursor**:
    `CVRStereo::DrawScopeOverlay(nEye)` lazily loads the game's own scope art
    (`interface/OnFoot/Cross_Zoom.pcx` / `interface/Enforcer/Cross_Zoom.pcx`, colour-keyed black
    transparent) and blits it, fit + centred, into each eye half. It is called at the end of
    `Begin2D(nEye, bHud=TRUE)` so it draws over the world and under the HUD, once per eye. The
    surfaces are deleted in `Term()`.
  - Deliberate trade-off: this removes true magnification - zoom now shows the world at normal
    scale plus a scope overlay. If real magnification is wanted later, do it as an offscreen
    second pass (option 2) rather than by narrowing the reported FOV.
- **Still to verify (device):** Left Y does nothing and no scope art appears; the view keeps its
  normal scale in all weapon/vehicle modes (no tiny centre image, no stuck FOV).

---

## 20. Crouch doesn't lower the VR camera (Left X toggle, Left stick click hold)

- **Status:** Fixed in code (2026-10-14) - awaiting device verification (crouch now drops the eyes).
- **Priority:** Medium (dead inputs; may matter on later levels / low gaps)
- **Observed (device, 2026-10-12):** Left X (crouch toggle) and Left stick click (crouch hold)
  give no visible crouch.
- **Observed (device, 2026-10-13):** crouch does **not** raise or lower the camera in VR - it only
  animates the third-person body model (which, per #21, should not even be visible). So in VR the
  player still gets no crouch effect at all.
- **Root cause:** Shogo has **no on-foot crouch**. `COMMAND_ID_DUCK` only sets `BC_CFLG_DUCK`,
  which (a) halves move accel (`CMoveMgr.cpp:725-728`) and (b) only forces a downward vertical
  accel while **swimming / free-moving** (`CMoveMgr.cpp:835-838`). It never lowers the camera or
  plays a stance animation. The mod's own body anim was also never told to crouch:
  `UpdateServerPlayerModel()` called `g_VRStereo.UpdateBody(..., DTRUE, DFALSE)` with `bCrouching`
  hard-coded `DFALSE` (`ShogoSource/ClientShellDLL/RiotClientShell.cpp:8179`), even though
  `UpdateBody()` accepts it and the crouch anim exists (`game/VRStereo.cpp:3482`).
- **Fix applied (2026-10-12, option 1 - body crouch only):** new `CVRStereo::IsCrouchOn()` returns
  the same state `VRCommandOn(COMMAND_ID_DUCK)` uses (`m_bCrouchOn` toggle OR
  `SHOGOVR_BTN_CROUCH_HOLD` held, gated on gameplay input + an active controller), and
  `RiotClientShell.cpp` now passes it:
  `g_VRStereo.UpdateBody(..., DTRUE, g_VRStereo.IsCrouchOn())`. So crouching now drives the VR
  body's crouch animation (`CR` / `CWR*`). **The player camera is not lowered yet** - that is the
  other half of option 1, still open.
- **Device result (2026-10-13):** only the third-person body model reacts to crouch - the camera
  stays put, so VR shows no crouch effect. (That body also shows in third-person / cutscene views
  and is missing geometry - both tracked in #21.)
- **Fix applied (2026-10-14, camera eye-height):** the mod now lowers the HMD world position while
  crouched. New `CVRStereo::UpdateCrouchOffset()` (called at the top of `UpdateInput()`, so it runs
  every frame) eases `m_fCrouchAmt` (0 standing .. 1 ducked) toward `IsCrouchOn()` at ~8/s. In
  `RenderStereo()`, when the frame is a tracked immersive one, that amount is subtracted along the
  body's up axis from `vBasePos`:
  `fDrop = m_fCrouchAmt * VRCrouchDrop * fWorldScale` (new `VRCrouchDrop` setting, default `0.6` m).
  So the whole stereo view (and the held weapon / aim, which are anchored to the eyes) drops
  smoothly and lifts back on release. No ShogoSource change - entirely in `game/VRStereo.cpp` /
  `game/VRStereo.h`.
- **Refs:** `game/XrApiUDP.cpp:435-436`; `game/VRStereo.cpp:2131-2141`
  (`VRCommandOn`/`COMMAND_ID_DUCK`), `game/VRStereo.h:373` (`m_bCrouchOn`);
  `ShogoSource/ClientShellDLL/CMoveMgr.cpp:178-181, 725-728, 835-838`;
  `RiotClientShell.cpp:8185`; `game/VRStereo.cpp:3467-3482`.

---

## 21. The VR body shows in third-person / cutscene views (and is missing geometry)

- **Status:** **Fixed - verified on device (2026-10-14, user-confirmed)** - the VR body no longer
  shows in third-person / cutscene / death views ("completely done").
- **Priority:** Medium (visual; third-person cutscenes read as "two mechs / two players")
- **Observed:**
  - (device, 2026-10-12) during cutscenes two MCA mechs / player bodies appear in third person.
  - (device, 2026-10-13) the mod's VR body should **not appear at all** in third-person and
    cutscene views, but it does; the user also noted the body is missing some geometry ("the extra
    bits").
- **Root cause:** the mod draws its own client-side copy of the player body for VR
  (`CVRStereo::UpdateBody`, `game/VRStereo.cpp:3580`), and the game's cutscene / third-person camera
  also renders the real player model. `UpdateServerPlayerModel()` always passed `bShow = DTRUE`, so
  whenever the game was on a non-first-person camera the mod copy was drawn on top of the game's own
  model.
- **Fix applied (2026-10-14):** the `UpdateBody()` call site now passes
  `bShow = m_playerCamera.IsFirstPerson() && !m_bUsingExternalCamera && !IsPlayerDead() &&
  !IsSpectatorMode()` (edit 11, `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`).
  The mod body is therefore hidden for the **third-person (chase)**, **external / cutscene** and
  **death / spectator** views, where the game's own model is the one shown; it is still drawn in
  normal first-person gameplay (where the game hides its model). Build clean (712704 bytes),
  deployed; **verified on device (2026-10-14, user-confirmed)**.
- **Refs:** `ShogoSource/ClientShellDLL/RiotClientShell.cpp` (`m_bUsingExternalCamera`,
  `IsPlayerDead`, `IsSpectatorMode`, `UpdateServerPlayerModel`);
  `game/VRStereo.cpp:3564,3580` (`HideBody` / `UpdateBody`).

---

## 22. Left-stick deadzone too small / not split between strafe and forward-back

- **Status:** **Fixed in code (2026-10-14)** - needs a device pass to confirm.
- **Priority:** Medium (unintended diagonal movement)
- **Observed (device, 2026-10-12):** the left stick drifts into diagonals too easily; want **at
  least 15% deadzone on left/right** so diagonal only happens on purpose, and **5-10% on
  forward/back** (the most common input).
- **Observed (device, 2026-10-13):** still not fixed - the left/right deadzone "isn't applied", so
  it is still very hard to walk straight without drifting diagonally.
- **Root cause:** `GetMoveAxes()` applies a single **radial** dead zone of `0.15` to both axes
  (`game/VRStereo.cpp:2096-2100`), so strafe and forward share one threshold; a small sideways
  component survives whenever the stick is mostly forward.
- **Fix applied (2026-10-14) - per-axis (anisotropic) dead zone:** `GetMoveAxes()` no longer uses the
  single radial `0.15`.  Each axis now has its own threshold and is rescaled from its edge back to
  `0..1`: forward/back **0.08** (5-10%) and strafe left/right **0.20** (>=15%).  The combined vector
  is then capped at magnitude 1 so a diagonal is no longer faster than a straight push, and the
  rotate-to-aim transform is unchanged.  Net effect: a mostly-forward push no longer drifts
  sideways, while deliberate diagonals still work. Build clean (712704 bytes), deployed; needs a
  device pass.
- **Refs:** `game/VRStereo.cpp:2260-2286` (`GetMoveAxes`), `:2230-2241` (`VRCommandOn` movement),
  #11.

---

## 23. Right A (view toggle) / Right B (transform) intermittent and sometimes swapped

- **Status:** **Fixed - verified on device (2026-10-14)** - the Right-A view toggle works every
  press; the transform half was the separate #36, now verified too.
- **Priority:** Medium (core controls feel unreliable)
- **Resolution (2026-10-13):** the "sometimes swapped / does nothing" behaviour was the game's own
  **`B` = ChaseView** binding colliding with the face-key leak (cause 3 below). The fix is in #31:
  Right A now toggles the view **directly** (`CVRStereo::ConsumeViewToggle` ->
  `CRiotClientShell::SetExternalCamera`) instead of injecting the bound key, and the leaked raw
  `B`'s key-driven `ChaseView` is ignored while a controller is live in gameplay. Right B (transform)
  still injects `V`. See #31 for the code paths.
- **Observed (device, 2026-10-12):** camera first/third person (A) and vehicle/mech change (B)
  work only some of the time and occasionally do the **opposite** of what was pressed.
- **Suspected causes:**
  1. Both actions are delivered by **injecting the bound keyboard key**
     (`InjectCommandTap` / `RequestTransform`), which only fires while the game window is
     foreground (`InjectScanTap`, `game/VRStereo.cpp:3253`) - if focus is elsewhere the tap is
     silently dropped.
  2. The injected key uses a **single slot** (`m_nInjScan`); a second tap releases the first
     early, and `RequestTransform` / `ReleaseTransformKey` (`:3241-3259`) run on their own clock,
     so pressing A and B close together can clobber each other.
  3. **Face-key leak:** WinlatorXR also sends the raw letters A/B/X/Y (see #17).
     `ShouldSwallowFaceKey` only swallows them while `IsControllerActive() && m_bGameplayInput &&
     !m_bPlayerDead` (`game/VRStereo.cpp:2015-2022`), so in the gaps (just before gameplay input
     is flagged, in menus, or dead) the raw letter reaches the game and can fire the
     *keyboard-bound* action - the "opposite of expected".
  4. The transform press has a release/debounce window; a fast re-press is ignored
     (`RequestTransform` guards on `m_bTransformKeyDown`).
- **Fix direction:** serialise/debounce the injected taps (don't share one slot) and make the
  face-key swallow independent of the gameplay/dead state for A/B/X/Y (or route view/transform
  through commands the engine polls, if a real keypress is not required). Confirm the raw bool
  indices (10 = A, 11 = B) against the on-device bool string.
- **Refs:** `game/XrApiUDP.cpp:432-433`; `game/VRStereo.cpp:2015-2022, 3215-3266`; #17.

---

## 24. On-foot missions never auto-quicksave on level start

- **Status:** Removed / cancelled (2026-10-14) - the owner decided players can **save/load manually**
  if they hit an issue here, so the autosave family (#24/#25/#33) is dropped.
- **Priority:** Medium-High (breaks the #16 death-recovery on those levels)
- **Observed (device, 2026-10-12):** an on-foot mission did not quicksave on load, so the #16
  quick-load-on-death has nothing to restore.
- **Root cause:** the autosave is only armed by `CVRStereo::SetLevelLoaded()`, and the shell only
  calls that from `UpdateLoadingLevelState()`
  (`ShogoSource/ClientShellDLL/RiotClientShell.cpp:2538`), which runs while the state is
  `GS_LOADINGLEVEL`. Levels that show the **bumper / "press any key"** screen enter play via the
  `GS_BUMPER -> GS_PLAYING` transition (`RiotClientShell.cpp:4691-4703`) on a keypress, which
  never calls `SetLevelLoaded()`. So any level entered through the bumper screen (the on-foot
  missions appear to) never arms the autosave.
- **Fix direction:** also arm the autosave on the `GS_BUMPER -> GS_PLAYING` transition (call
  `g_VRStereo.SetLevelLoaded()` there), or, more robustly, arm on **any** transition into
  `GS_PLAYING` from a non-playing state **that is a fresh level** (see #25 for the save-load
  exclusion).
- **Refs:** `RiotClientShell.cpp:2515-2540` (`UpdateLoadingLevelState`), `:4691-4704`
  (`GS_BUMPER`); `game/VRStereo.h:135` (`SetLevelLoaded`); #16, #33.

---

## 25. Auto-quicksave re-arms after loading a save / quick-loading

- **Status:** Removed / cancelled (2026-10-14) - see #24; the owner opted for **manual save/load**.
- **Priority:** Medium-High (the level-start save can be overwritten by a mid-level state)
- **Observed (device, 2026-10-12):** the on-load quicksave fires again after the player loads a
  save file or quick-loads, not only at the very start of a level.
- **Root cause:** a save load goes `LoadGame()` -> `DoLoadWorld(..., LOAD_RESTORE_GAME)` ->
  `GS_LOADINGLEVEL` -> `UpdateLoadingLevelState()` -> `SetLevelLoaded()`
  (`ShogoSource/ClientShellDLL/RiotClientShell.cpp:7430-7440, 7301, 2538`). The autosave can't
  tell a fresh level from a restored save, so it re-arms and fires ~2 s later - overwriting the
  level-start quicksave with the current mid-level state and defeating #16's death-recovery.
- **Fix direction:** only arm the autosave for a **fresh** level. The load flag is available at
  `DoLoadWorld` (`LOAD_NEW_GAME` = 1 / `LOAD_NEW_LEVEL` = 2 = fresh; `LOAD_RESTORE_GAME` = 3 =
  save/quick-load; `ShogoSource/Shared/ClientServerShared.h:145-147`). Either set a "this load was
  a restore" flag in `LoadGame()` / `DoLoadWorld` and have the `SetLevelLoaded()` call honour it,
  or only arm where the load is not a restore.
- **Refs:** `ShogoSource/ClientShellDLL/RiotClientShell.cpp:7430-7440` (`LoadGame`), `:7301`
  (`DoLoadWorld` -> `GS_LOADINGLEVEL`), `:2538`; `ShogoSource/Shared/ClientServerShared.h:145-147`;
  #16, #24, #33.

---

## 26. Intro video sits slightly too high; move it down ~1/8

- **Status:** **Fixed in code (2026-10-14)** - needs a device pass to confirm.
- **Priority:** Low (comfort / polish)
- **Observed (device, 2026-10-12):** the intro movie is a little too high on screen; the user
  wants it about **1/8 of the screen height lower** from the top.
- **Root cause / refs:** `CVRStereo::ShiftMovieToRightHalf()` clones the finished Smacker frame
  into the middle of each eye half at **y = 0** (`game/VRStereo.cpp:1224`), so the cloned frame sits
  at the top of the screen.
- **Fix applied (2026-10-14):** the two clones (and the not-detected fallback) are now drawn at
  `y = screenHeight / 8`, clamped to `screenHeight - frameHeight` so a full-height frame still lands
  fully on-screen. Build clean (712704 bytes), deployed; needs a device pass.
- **Follow-up (2026-10-14):** the user asked for a further **~50 px lower**, so the offset is now
  `y = screenHeight / 8 + 50` (still clamped to `screenHeight - frameHeight`; when the film is taller
  than half the screen the clamp keeps it valid). Build clean (713728 bytes), deployed; needs a
  device pass.
- **Follow-up 2 - "cut off on the bottom" (2026-10-14):** the deeper offset made the movie clip at
  the **bottom**. Root cause: only the *detected* branch is clamped (`yOff + vh <= screenHeight`);
  the **fallback** branch (used when `DetectMovieSource()` fails) blits the **whole window** at
  `screenHeight / 8 + 50`, and shifting a whole-window clone down necessarily clips the bottom by
  that many pixels - the `+50` made it 50 px worse. `DetectMovieSource()` failed because it only
  sampled **three fixed rows** (`h/4, h/2, 3h/4`) horizontally, so a short video sitting between
  them was never seen. Fix: (a) the fallback now draws at **y = 0** (a whole-window clone can't be
  lowered without clipping); (b) `DetectMovieSource()` now finds the **vertical extent first** down
  five columns, then scans six rows (the old three **plus** three inside the found video) so the
  result is a strict superset of the old box on the same frame and can't regress. Build clean
  (713728 bytes), deployed, PC smoke ok (`movie source detected` logged, no crash). Needs a device
  pass.

---

## 27. Shots don't always appear to come from the weapon model

- **Status:** **Fixed - verified on device (2026-10-14).**
- **Priority:** Low-Medium (cosmetic; aim/impact are unaffected)
- **Observed (device, 2026-10-12):** sometimes muzzle flash / shots don't line up with the visible
  weapon model.
- **Root cause:** the mod **hides the game's real first-person weapon model** (all nodes hidden),
  but that hidden model still drives the muzzle flash and the shot origin, while the **visible**
  gun is the mod's own copy positioned/oriented at the hand pose (`UpdateHeldGun`,
  `game/VRStereo.cpp:3618-3629`). The two transforms diverge (especially while the hand moves), so
  the flash/origin leaves from where the game's hidden gun is, not where the visible one is.
- **Fix direction (options):**
  1. Nudge/align the mod gun so its muzzle node matches the game's hidden muzzle each frame (feed
     the game model's muzzle node world transform into the visible gun) - tightest, but per-model.
  2. Draw a small "muzzle marker" at the game's real muzzle position so the flash reads as coming
     from a consistent point.
  3. Accept it (cosmetic) - impacts follow the game's origin regardless.
- **Refs:** `game/VRStereo.cpp:3586-3597` (VR gun table), `:3600-3635` (`UpdateHeldGun`).

---

## 28. Hurt / explosion screen flash is not visible at all (comfort)

- **Status:** **Fixed - verified on device (2026-10-13)** - the peripheral flash works well; the
  player confirms it (see the re-fix below).
- **Priority:** Medium-High (no damage feedback; the player can't tell they're being hit)
- **Device result (2026-10-13):** the player sees **no red (hurt) or white (explosion) flash at
  all** - the broken quad is gone, but so is the feedback. The tint is not reaching the draw, or the
  draw happens in a frame/state the HUD pass misses (see candidates below).
- **Observed (device, 2026-10-12):** the red (hurt) / white (explosion) full-screen flash showed
  **vertical line patterns down the bottom-left** and a **diagonal solid-transparent block in the
  top-right** - a classic broken full-screen quad.
- **Root cause:** every hurt/explosion/pickup flash funnels through
  `CRiotClientShell::TintScreen()` -> `UpdateScreenTint()` -> `pClientDE->SetCameraLightAdd()`. The
  shell draws no quad, so the broken two-triangle block is the **engine renderer's** implementation
  of the camera light-add flash under the two-eye stereo layout (the shell's 2D API has no alpha:
  `SetupColor1(...,bTransparent)` is a colour key, so a smooth translucent fill isn't drawable
  either).
- **Fix applied (2026-10-12):**
  - `ShogoSource` (`RiotClientShell.cpp`, prose note edit 19): `UpdateScreenTint()` no longer calls
    `SetCameraLightAdd` while `g_VRStereo.IsActive()` - it feeds the current tint colour to the mod
    via `g_VRStereo.SetScreenFlash(r,g,b)`, and `ClearScreenFlash()` is called when no tint is
    active, in `ClearScreenTint()`, and in `HandleMPChangeLevel()`. Non-VR still uses the engine
    path unchanged.
  - `game/VRStereo.cpp/.h`: new `SetScreenFlash()` / `ClearScreenFlash()` store the tint, and
    `DrawScreenFlash(nEye)` draws a clean **per-eye edge vignette** (nested opaque frames whose outer
    band is the full tint and whose inner bands ramp down, so the colour fades into the picture; the
    centre of vision is never covered). Strength and edge thickness scale with the tint's brightest
    channel. **This first version drew inside the HUD 2D pass and did not reach the frame - see the
    2026-10-13 re-fix.**
- **Diagnosis (2026-10-13):** the tint *does* reach the mod - removing `SetCameraLightAdd` (which
  alone killed the broken quad) only happens on the `g_VRStereo.IsActive()` branch, so
  `SetScreenFlash()` was being fed. The miss was therefore the **draw location**: the vignette was
  painted deep inside the HUD's optimized-2D block (`Begin2D(nEye, DTRUE)`), where the raw
  `FillRect` fills did not reach the captured frame.
- **Re-fix applied (2026-10-13):**
  - Moved the draw onto the **same post-render path as the WinlatorXR sync pixel**, which is known
    to land on the captured frame. New `CVRStereo::DrawScreenFlashFrame()` is called once after
    `End3D()` in `RenderCamera()`, `UpdateMenuState()` and `UpdatePausedState()`, immediately before
    `DrawXrSyncPixel()`; the old `Begin2D` call is removed. (`RiotClientShell.cpp` prose note edits
    12, 13 and 19.)
  - `DrawScreenFlash()` now fetches the screen surface itself (instead of relying on the 2D-pass
    `m_hScreen`) and `DrawScreenFlashFrame()` falls back to `p->FillRect` if no stereo pass has
    cached the original yet.
  - Simplified to a **peripheral-vision flash**: `DrawScreenFlash()` now paints **two rectangles per
    frame** - a full-height band down the *outer* edge of each eye (the left eye's left side, the
    right eye's right side), 30% of the eye width. The earlier nested-frame vignette cost ~20
    `FillRect` calls per frame, which was too heavy on device; this is 2. The centre of vision stays
    clear, and the band reads as tunnel vision while being hit.
  - Diagnostics: `SetScreenFlash()` logs on/off transitions with the colour; `DrawScreenFlash()`
    logs the first draw (eye rect, colour). New `VRFlashTest` CVar (default 0) forces a red flash
    every frame for testing (`VRFlashTest=1` under `[Game]` in `ShogoVR.ini`).
  - **Verified on the PC:** with `VRFlashTest=1` each outer band reads saturated red (48,600 sampled
    px; left band rect `0,0 - 216,900`); with it 0, none. Build clean, deployed.
- **Verified on device (2026-10-13):** the player confirms the peripheral red (hurt) / white
  (explosion) flash "works great" and is not too expensive.
- **Refs:** `ShogoSource/ClientShellDLL/RiotClientShell.cpp` `UpdateScreenTint`/`ClearScreenTint`/
  `HandleMPChangeLevel`; `WeaponFX.cpp:606-689` (explosion tint), `:6690` (hurt), `:9814` (pickup);
  `game/VRStereo.cpp` `SetScreenFlash`/`ClearScreenFlash`/`DrawScreenFlash`/`DrawScreenFlashFrame`;
  `game/VRStereo.h`.

---

## 29. Muzzle flash is centred on the view instead of attached to the visible gun

- **Status:** **Fixed - verified on device (2026-10-14)** - the flash sits at the visible gun's muzzle;
  the on-foot dual pistols keep the game's own alternating flashes.
- **Priority:** Medium (immersion; the flash reads as coming from the player's face)
- **Observed (device, 2026-10-13):** the muzzle flash appeared **centred in the player's view** /
  above and behind the barrel, rather than at the muzzle of the visible 3D gun.
- **Root cause:** the game positions the flash sprite from the **flat-screen** muzzle -
  `CWeaponModel::m_vFlashPos = cameraPos + weaponOffset + muzzleOffset` (`WeaponModel.cpp`) - but VR
  poses the gun in the hand (`AdjustWeaponPose` / `UpdateHeldGun`), so the flash sat where the
  flat-screen gun would be: near the camera, above and behind the real barrel.
- **Fix (2026-10-13):** the **visible** gun's muzzle is now computed each frame and used for the
  sprite. The mod supplies it: `CVRStereo::GetMuzzleFlashWorld(nWeaponId, vCamPos, bFootScale, vOut)`
  mirrors whichever gun is on show (`AdjustWeaponPose`'s first-person pose, or `UpdateHeldGun`'s
  held pose) and offsets by the game's own `GetWeaponMuzzleOffset()` (scaled by the gun scale, since
  the offset is in the model's local frame). In `CWeaponModel::UpdateWeaponModel()` the shell fills a
  new **`m_vFlashDrawPos`** from it and `UpdateFlash()` draws the sprite there. `m_vFlashPos` - the
  **shot origin** (sent in `MID_WEAPON_FIRE`) - is deliberately **left unchanged**, so aiming is not
  disturbed. When VR is not posing the gun by hand the mod returns `DFALSE` and the draw position
  falls back to `m_vFlashPos` (flat game unchanged).
  - One log line per weapon (`VR muzzle flash: weapon N -> x,y,z (held|first-person gun)`) confirms
    it engaged on device.
  - **Dual pistols excluded (2026-10-13 follow-up):** the on-foot **Colt 45** (`GUN_COLT45_ID`) fires
    from **two barrels alternately** (`W_FIRED` / `W_FIRED2`, `GetWeaponMuzzleOffset` returns a
    different muzzle for each), but this helper only knows the one `W_FIRED` offset - so it pinned
    both flashes to the same barrel ("weird behaviour"). `GetMuzzleFlashWorld()` now returns `DFALSE`
    for that weapon, so it keeps the game's own alternating flashes and every other weapon still gets
    the visible-muzzle fix.
- **Still to verify (device):** fire each weapon and confirm the flash sits at the barrel tip on the
  first-person gun (the default) and on the held gun (`VRGunModel 1`). The held-gun muzzle uses the
  first-person offset table, which may be a little off if the powerup model's origin differs.
- **Refs:** `game/VRStereo.cpp` `GetMuzzleFlashWorld` / `AdjustWeaponPose` / `UpdateHeldGun` / VR gun
  table; `ShogoSource/ClientShellDLL/WeaponModel.cpp` `UpdateWeaponModel` / `UpdateFlash`;
  `ShogoSource/Shared/WeaponDefs.h` `GetWeaponMuzzleOffset`; `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md`;
  #27, #34.

---

## 30. Trim the in-game VR menu and pin the removed options to fixed defaults

- **Status:** **Fixed - verified on device (2026-10-14)** - the VR menu is trimmed to 7 rows.
- **Priority:** Medium (clean-up; removes options that no longer do anything useful)
- **Fix applied (2026-10-13):** `game/VRMenu.cpp` `s_Settings` trimmed 28 -> 7 rows
  (`game/VRMenu.h` `VRMENU_ITEMS` 28 -> 7): the kept rows are dominant hand, hand roll flip,
  turning, smooth turn speed, recentre view, reset to defaults and back (the action-row constants
  `VRM_RECENTER`/`VRM_DEFAULTS`/`VRM_BACK` renumbered to 4/5/6). "hand roll flip" now reads
  **off** (0) / **on** (1). The removed settings keep their pinned defaults: `VRSnapTurn`
  (turning) -> 0 (smooth), `VRMoveDir` -> 1 (where you look), `VRTwoHand` -> 0 (off) and
  `VRGunScaleMCA` -> 1.5 were corrected in the `CVRStereo` VarTrack `Init` calls; the rest
  already matched. Build clean; deployed; PC smoke ok (new stamp `built Oct 13 2026 21:24:09`).
- **Requested:** remove most of the in-game VR settings rows and hard-default them, leaving only a
  short list. The internal menu **virtual layout** from #5 is not a user row and stays as it is.
- **Remove these rows, defaulting each to the value shown:**
  - two-handed aiming -> OFF
  - aim with -> GUN HAND
  - walk towards -> WHERE YOU LOOK
  - comfort vignette -> 0%
  - lean with head (6dof) -> ON
  - stereo depth -> 64 mm (ignored; we use WinlatorXR XrApi's IPD instead)
  - aim helper -> NONE
  - game crosshair -> OFF
  - show body -> ON
  - body arms -> HIDDEN
  - gun model -> first person (detailed)
  - gun arms -> SHOWN
  - easy ladders -> ON
  - hud width -> 60 DEG
  - hud distance -> 3.5 m
  - sharp hud panel -> AUTOMATIC
  - menu screen size -> 80 deg
  - gun size on foot -> 1.5x
  - gun size in mech -> 1.5x
  - sharpening -> 50%
  - upscaling -> ON
- **Keep these rows (default in brackets):**
  - dominant hand [right-handed]
  - hand roll flip [off] - **relabel** the row: the current default already corrects a Quest 3's
    roll, so it should read **off** (not "flipped"), since a Quest 3 is the norm.
  - turning [smooth]
  - smooth turn speed [150 deg/s]
  - recentre view
  - reset to defaults (restores the defaults above and here)
- **Implementation notes:**
  - Rows live in `game/VRMenu.cpp` (`VRMENU_ITEMS` array, currently 28) mapped to `CVRStereo`
    VarTracks. Removing rows means dropping the entries and lowering `VRMENU_ITEMS` (and the
    `MAX_GENERIC_ITEMS` capacity in ShogoSource `BaseMenu.h` if it can be lowered - or just leave
    headroom).
  - Keep the **defaults** in the VarTrack `Init(...)` calls (or the settings array) at the values
    above, so a fresh config and "reset to defaults" land on them.
  - Confirm the "menu screen size" row maps to the angular width (`VRMenuWidth`, 80 deg) and not to
    the internal virtual layout from #5 (`VRMenuLayout`, 640).
- **Refs:** `game/VRMenu.cpp` (item array), `game/VRStereo.cpp` (`m_vt*` VarTrack defaults),
  `ShogoSource/ClientShellDLL/BaseMenu.h` (`MAX_GENERIC_ITEMS`); #5.

---

## 31. Right B doesn't toggle mech walking / vehicle mode (raw A/B keystrokes leak)

- **Status:** **Fixed - verified on device (2026-10-14)** - the Right-A view toggle works every
  press, the leaked key-driven B no longer toggles the view, and the transform action (#36) works too.
- **Priority:** Medium (core mech action)
- **Observed (device, 2026-10-13):** pressing **B** toggles **third person**, and **A** does
  nothing. Expected: **A** = first/third-person view, **B** = vehicle/mech transform (#15's
  mapping, which is correct and unchanged).
- **Root cause found (2026-10-13) - the game itself binds the view to `B`:**
  - `defkeybd.cfg` / `autoexec.cfg` bind **ChaseView** (first/third person,
    `COMMAND_ID_CHASEVIEWTOGGLE`) to scan code `##48` = the **letter `B`**, and
    **VehicleModeToggle** to `##47` = the letter `V`.
  - The mod's **Right A -> VIEWTOGGLE** was implemented as `InjectCommandTap(COMMAND_ID_CHASEVIEWTOGGLE)`,
    i.e. it **injected the letter `B`** via `SendInput`. That injected key goes through the
    engine's `OnKeyDown`, where the mod's own `ShouldSwallowFaceKey` (#17) drops `B` - so **A did
    nothing**.
  - WinlatorXR also leaks the raw face buttons as the letters X/Y/B/A. The leaked **`B`** reaches
    the engine's command system as an **edge** command (the `IsCommandOn` hook only suppresses the
    **held** path), so pressing **B** fired **ChaseView** -> third person, while the mod's intended
    transform (`RequestTransform` -> injects `V`) was masked.
- **Fix applied (2026-10-13) - route the view toggle directly, block the leaked key-driven one:**
  - `game/VRStereo.{h,cpp}`: Right A no longer injects a key. It sets a one-shot request
    (`m_bWantViewToggle`) exposed as `ConsumeViewToggle()`; also added `IsGameplayInput()`.
  - `CRiotClientShell::Update()`: after `UpdateInput()`, `if (g_VRStereo.ConsumeViewToggle() &&
    m_ePlayerState == PS_ALIVE && !m_bZoomView) SetExternalCamera(m_playerCamera.IsFirstPerson());`
    - the same action the `COMMAND_ID_CHASEVIEWTOGGLE` case performs, but without a key press.
  - `CRiotClientShell::OnCommandOn()` `COMMAND_ID_CHASEVIEWTOGGLE`: ignore the command while
    `g_VRStereo.IsControllerActive() && g_VRStereo.IsGameplayInput()` (the leaked raw `B`), so the
    key can no longer toggle the view out from under the controller. The keyboard still drives it
    when no controller is live.
  - **Right B -> TRANSFORM** is unchanged (`RequestTransform` injects `V`, which is neither
    leaked nor swallowed), so it reaches `ToggleVehicleMode` as before.
- **Note:** the OnKeyDown face-key swallow (#17) is kept; it still covers any state where the leak
  does pass through the window-proc path. The decisive fix for the edge command is the
  `OnCommandOn` guard above.
- **Refs:** `game/VRStereo.cpp` `UpdateInput` / `ConsumeViewToggle`; `game/XrApiUDP.cpp:432-433`
  (A -> VIEWTOGGLE, B -> TRANSFORM); `ShogoSource/ClientShellDLL/RiotClientShell.cpp`
  `Update()` / `OnCommandOn()` / `OnKeyDown`; `defkeybd.cfg` / `autoexec.cfg`
  (`ChaseView ##48`, `VehicleModeToggle ##47`); #15, #17, #23.

---

## 32. Jump intermittently stops working (an input flag appears to stick)

- **Status:** Removed - appears fixed (2026-10-14).
- **Priority:** High (core movement; "sometimes unable to jump again randomly")
- **Observed (device, 2026-10-13):** the player can, at random, no longer jump; the user suspects an
  input boolean is being held down when it should have been cleared.
- **Suspected cause:** jump is answered by `VRCommandOn(COMMAND_ID_JUMP)` from the `JUMP |
  DOUBLEJUMP` bits (left grip / right grip). A "held" flag that is never cleared (a dropped release
  never clears `m_nButtons`, a stalled feed leaves the bit set, or the shared `m_nInjScan` /
  `InjectScanTap` slot latches) would make the game keep seeing jump down and ignore the next press.
  Related: #23's shared injection slot and #9's stalled-feed handling.
- **Fix direction:** audit the jump path - make sure the button bit and any injected key are cleared
  when the XrApi release is (or should be) seen, and that a stalled/disconnected feed resets held
  booleans (see #9). Add a log line on jump-down / jump-up transitions to catch a stuck state on
  device.
- **Refs:** `game/XrApiUDP.cpp` (`ButtonBits`), `game/VRStereo.cpp` `VRCommandOn` / `UpdateInput` /
  `InjectScanTap`; #9, #15, #23.

---

## 33. Auto-quicksave doesn't fire when loading into a new area of a level

- **Status:** Removed / cancelled (2026-10-14) - see #24; the owner opted for **manual save/load**.
- **Priority:** Medium (same death-recovery family as #24 / #25)
- **Observed (device, 2026-10-13):** entering a new **area** within a level does not re-trigger the
  quick save.
- **Context:** the same autosave mechanism as #24 (a level entered via the bumper screen never arms
  it) and #25 (it re-arms after a save load). Here the trigger is an in-level **area** load rather
  than a level start / restore; the autosave appears to fire once per level and is not re-armed for
  later areas.
- **Fix direction:** decide the intended policy (save on every fresh area load?), then arm the
  autosave on the area-load transition as well, honouring the #25 "fresh load only" exclusion.
- **Refs:** `RiotClientShell.cpp:2515-2540` (`UpdateLoadingLevelState`), `:7301` (`DoLoadWorld`);
  `game/VRStereo.h:135` (`SetLevelLoaded`); #16, #24, #25.

---

## 34. Weapon model sits lower than the shot origin (aiming is off)

- **Status:** **Fixed - verified on device (2026-10-14)**.
- **Priority:** Medium-High (shots don't line up with the visible gun)
- **Observed (device, 2026-10-13):** the 3D weapon models appear **lower** than where their
  projectiles actually leave from, so it is hard to aim - the visible gun and the shot origin are
  vertically misaligned.
- **Root cause (suspected):** the mod hides the game's real first-person weapon model and draws its
  own at the hand pose (`UpdateHeldGun`), while the game still spawns the projectile from the
  **hidden** model's muzzle / camera origin. If the drawn gun sits lower than the hidden model, the
  shot tracks the hidden model, not the visible one - the same mismatch behind #27 (shots vs
  weapon model) and #29 (muzzle flash on the visible gun).
- **Fix direction:** fix together with #27 / #29 - drive both the visible gun's placement and the
  shot origin from the same muzzle node (or offset the drawn gun to match the hidden model's
  muzzle) so the gun, muzzle flash and projectile origin coincide. Verify the vertical offset on a
  known weapon on device.
- **Refs:** `game/VRStereo.cpp` `UpdateHeldGun`; `ShogoSource/ClientShellDLL/RiotClientShell.cpp`
  (weapon model / muzzle origin); #27, #29.

---

## 35. Left/right dominant-hand setting does nothing (gun stays on the right hand)

- **Status:** **Fixed - verified on device (2026-10-14, user-confirmed)**.
- **Priority:** Medium-High (left-handed players can't aim with their dominant hand)
- **Observed (device, 2026-10-13):** changing **dominant hand** to **left-handed** (the in-game VR
  menu, the desktop Settings window, or `[Controls] LeftHanded` in `ShogoVR.ini`) changed nothing -
  the gun still followed the right controller and the right trigger still fired.
- **Root cause:** `Controls/LeftHanded` was read only by the PCVR **bridge**
  (`bridge/ShogoVRBridge.cpp`, which swaps the pose, sticks and bindings) and the launcher's bindings
  UI. The WinlatorXR path runs entirely in `CShell.dll` from the XrApi UDP feed, and `CVRStereo`
  never read the setting, so on Quest the toggle was inert.
- **Fix applied (2026-10-13):** the game now reads `[Controls] LeftHanded` itself in
  `CVRStereo::LoadSettingsFile()` (re-read live when the ini changes; logs
  `VR: dominant hand left/right`). In `CVRStereo::UpdateBridge()`, when it is on:
  - the two hand poses are **swapped** (`m_vRight*` <-> `m_vLeft*`), so every gun model
    (`AdjustWeaponPose`, `UpdateHeldGun`, the muzzle-flash helper) and two-handed aiming follow the
    **other** controller;
  - **only the triggers and grips** change action - `FIRE` <-> `LEFT_TRIGGER` (left trigger fires,
    right trigger is the dead-state quick-load) and `JUMP` <-> `DOUBLEJUMP` (left grip double-jumps,
    right grip jumps). Every other control (sticks, face buttons, crouch, recentre, menu) is
    unchanged;
  - the fire haptic moves to the left controller.
- **Deliberately not changed:** the sticks stay as they are (left = move, right = turn) - the request
  was triggers/grip plus position/rotation only.
- **Verify on device:** with dominant hand = left, the gun sits in the left hand and points where it
  points; left trigger fires; left grip double-jumps; right grip jumps; everything else unchanged.
  The log shows `VR: dominant hand left (...)` on startup / on change.
- **Refs:** `game/VRStereo.cpp` `LoadSettingsFile` / `UpdateBridge` (pose + button swap) / haptics;
  `game/VRMenu.cpp` "dominant hand" row (`Controls` / `LeftHanded`); `game/XrApiUDP.cpp`
  `ButtonBits`; `game/ShogoVRShared.h` button bits; `bridge/ShogoVRBridge.cpp` (legacy PCVR swap).

---

## 36. Right B (transform mech/vehicle) needs repeated taps

- **Status:** **Fixed - verified on device (2026-10-14)** - a single B press transforms, both ways.
- **Priority:** Medium (core mech action)
- **Observed (device, 2026-10-13/14):** with the Right-A view toggle now working every press, **Right
  B** still needed rapid taps to transform; the user suspected the injected signal was firing too
  fast and toggling on/off.
- **Root cause (2026-10-14):** B is the only control whose XrApi button also arrives as a **leaked
  WinlatorXR face-letter** `B` (#17). `CRiotClientShell::OnKeyDown` swallowed that letter with
  `pClientDE->ClearInput()`, and `ClearInput()` takes **no key argument** - it wipes the **whole**
  input state. The transform is requested from the *same* button press (`RequestTransform()` injects
  the bound `VehicleModeToggle` key, `V`, via `SendInput`), so whichever arrives second wins: often
  the leaked `B`'s `ClearInput()` cancelled the just-queued `V`, so the command never reached
  `CPlayerObj::ToggleVehicleMode()`. The race is timing-dependent, so repeated taps eventually land.
  Right A is unaffected because it is now performed **directly** (it injects nothing, #31), which is
  exactly why A worked and B did not.
  - **Separate, game-side:** `CPlayerObj::ToggleVehicleMode()` also silently does nothing when
    leaving vehicle mode without room for the bipedal model (`!m_bBipedal` + the `SetDims` room
    check, `PlayerObj.cpp`). That can make an individual press "do nothing" too, independent of the
    race above.
- **Fix applied (2026-10-14):**
  - `ShogoSource/ClientShellDLL/RiotClientShell.cpp` `OnKeyDown`: the leaked face key is still
    returned (swallowed), but `ClearInput()` is **skipped for the letter `B`**. The leaked `B`-driven
    `ChaseView` is already neutralised by the `OnCommandOn` guard (#31) and the `IsCommandOn` hook,
    so nothing else is lost; X/Y keep their `ClearInput()`.
  - `game/VRStereo.cpp` `RequestTransform()`: logs `VR: transform key down (scan 0x..)` so a device
    log confirms the B edge and the injected key.
- **Verify on device:** a single B press transforms (both directions, given room); the log shows one
  `VR: transform key down` per press.
- **Fallback if still flaky:** request the transform on the B **release** edge
  (`m_nReleased & SHOGOVR_BTN_TRANSFORM`) so the injected `V` can never race the press-edge
  `ClearInput()`.
- **Refs:** `game/VRStereo.cpp` `RequestTransform` / `ReleaseTransformKey` / `UpdateInput`;
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp` `OnKeyDown` / `OnCommandOn`;
  `ShogoSource/ObjectDLL/PlayerObj.cpp` `ToggleVehicleMode`; #17, #23, #31.

---

## Cross-cutting notes

- **Basis / handedness convention (#1, #2, #8):** both HMD and hands flow through
  `XrQuatToOpenXR` -> `ToGameSpace` (`game/XrApiUDP.cpp`). The HMD basis and the hand
  roll (`VRHandFlip` inverted so `0` = corrected) are now confirmed on device. If the
  hand *aim* is ever found wrong, fix the hand frame in `GetHandWorld()` /
  `ToGameSpace()` against the (known-good) HMD basis rather than re-using the flag.
- **Input path (#3, #4, #15, #16, #17, #18, #20, #22, #23, #31, #32, #35, #36):** the controller->game path (#3)
  and firing (#4) are verified on device. #15 (revised mapping), #16 (auto-quicksave at level start
  + quick-load on the left trigger when dead) and #17 (the face-button keystrokes A/B/X/Y no longer
  leak) are fixed in code and need on-device verification. #18 (Left-Y re-selects the equipped
  weapon to toggle zoom) is being **removed** with #19. #20 (crouch toggle/hold) - the VR body
  plays its crouch animation, but the camera never moves, so it is still **In progress**; the body
  showing in third-person / cutscene views moved to #21. #23 (A/B view/transform) and #31 (Right B
  didn't transform; the view toggled instead) are **fixed in code (2026-10-13)**: the game binds
  ChaseView to the letter `B` (`defkeybd.cfg`), so the mod's old `InjectCommandTap(CHASEVIEWTOGGLE)`
  injected a key the face-key swallow ate (A did nothing) while the leaked raw `B` fired ChaseView
  (B went third-person). Right A now drives the view **directly**
  (`CVRStereo::ConsumeViewToggle` -> `CRiotClientShell::SetExternalCamera`) and the key-driven
  `ChaseView` is ignored while a controller is live in gameplay; Right B still injects `V` for the
  transform, but its leaked-face-key `ClearInput()` no longer cancels that injected key (#36,
  2026-10-14). #32 (jump intermittently stops working, as if an input flag is stuck) is still open
  (2026-10-13). #22 (left-stick deadzone) still isn't
  applied on device (2026-10-13) and needs a per-axis split. Also reconcile each XrApi bool index
  in `ButtonBits()` against the on-device bool string. See #9 for what should happen when the input
  feed itself stalls. **#35 (dominant hand) is now read by the game** (`Controls/LeftHanded`):
  left-handed swaps the two hand poses and the trigger/grip actions, leaving the sticks and buttons
  alone.
- **Zoom (#18 -> #19, was RELEASE BLOCKER):** the pin-FOV half stays (the eye FOV is never
  multiplied by the zoom ratio, so the presented scale/FOV can't blow up), but the **scope-cursor
  zoom failed on device (2026-10-13)** and is being **removed for now**: drop the `ZOOM` mapping
  and the Left-Y toggle and delete the scope overlay. True magnification needs the offscreen
  second pass (option 2) if it is ever wanted.
- **Autosave / save-load (#16, #24, #25, #33):** the death-recovery autosave is armed only from
  `UpdateLoadingLevelState` (`GS_LOADINGLEVEL`), so (a) levels entered via the **bumper screen**
  (`GS_BUMPER -> GS_PLAYING`) never arm it (#24), (b) a save/quick-load also passes through that
  path and re-arms it (#25), and (c) an in-level **area** load does not re-trigger it once the
  level-level save has fired (#33). Arm on the bumper transition and on fresh area loads, but only
  for a **fresh** load - use the `DoLoadWorld` flag (`LOAD_RESTORE_GAME` = save/quick-load).
- **Player body (#20, #21):** the mod's VR body is a separate client-side copy. Its crouch
  animation works, but crouch does **not** move the VR camera (#20, reopened) - and the body
  should not be visible in third-person / cutscene views anyway (#21), so crouch currently has no
  useful VR effect. Hide the body on any non-first-person camera (`m_bUsingExternalCamera` or the
  chase view), add a mod-side eye-height offset for a real duck, and check why the body is missing
  geometry.
- **Weapon model / effects (#27, #29, #34):** the mod hides the game's real first-person weapon
  model and draws its own at the hand pose, so effects the game anchors to the hidden model (the
  **muzzle flash**) used to appear at the hidden gun / camera rather than the visible gun. **#29 is
  fixed in code (2026-10-13):** the flash sprite is drawn at the visible gun's muzzle
  (`CVRStereo::GetMuzzleFlashWorld` + `CWeaponModel::m_vFlashDrawPos`), while the shot origin
  (`m_vFlashPos`) is left alone - so aiming is untouched. Still open: the visible gun sits **lower
  than the shot origin** (#34) and shot origins can diverge (#27); if the flash's new spot proves
  slightly off on device, the same visible-muzzle helper is the place to nudge.
- **Hurt / explosion flash (#28):** the engine camera light-add was replaced by a mod-drawn flash
  (opaque fills; the 2D API has no alpha). The first version drew inside the HUD 2D pass and **did
  not reach the captured frame** (invisible on device, 2026-10-13), so the draw moved onto the
  **post-render path beside the sync pixel** (`DrawScreenFlashFrame()` after `End3D()`), and the
  flash is now just **two rectangles per frame** - a full-height band down the outer edge of each eye
  (a peripheral-vision flash), instead of the earlier ~20-fill nested vignette that was too heavy on
  device. **Verified on device (2026-10-13):** the player confirms it works and is cheap enough.
- **2D content per eye (#5, #6, #10, #12):** menus, splash, movies and the loading/bumper
  screens must all be centered in each eye; the engine video player runs in `Client.exe` and is
  the one case that can't be re-projected from `CShell.dll`. **#5's menu virtual-layout is
  verified on device (2026-10-13) - text is readable.** **#12 (loading / bumper screen) is still
  mono/split with unreadable text (2026-10-13)** - fix it the same way as #5 (small virtual canvas +
  per-eye `Begin2D`). #26 (intro movie y-offset) is the same movie path as #10. See #30 for the menu
  trim.
- **Settings menu (#30, #5):** with the menu now readable (#5, verified) the in-game VR page is
  being **trimmed** to a short list (dominant hand, hand roll flip, turning, smooth turn speed,
  recentre, reset) with most rows removed and the removed options pinned to the fixed defaults
  listed in #30. The internal menu virtual layout from #5 is not a user row and stays. The kept
  **dominant hand** row now actually does something - see #35 (the game reads `Controls/LeftHanded`).
- **Frame pacing / headset stall (#9):** `VRSyncHold` (now on by default) hard-blocks
  `Poll()` on a fresh `HMD_SYNC`; an unbounded hang is accepted because the WinlatorXR sandbox
  can be torn down if the runtime is gone for good.
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