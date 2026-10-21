# BUGS_TO_FIX

Live issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed in code` (awaiting a device pass) / `Fixed` (verified) /
`Won't fix`.

This is the **0.8 cycle** tracker. The pre-release (0.7 and earlier) log is archived as
[`PreReleaseBugs/BUGS_TO_FIX.md`](PreReleaseBugs/BUGS_TO_FIX.md) (`#1`-`#39`); numbering here
**continues at `#40`** so the references in `Docs/PROJECT_PLAN.md` and the
`ShogoSourceModifications/` notes stay unambiguous. Note that `game/` files are in this repository
(the mod); `ShogoSource/` files are the git-ignored Monolith source (any edit there needs a
`ShogoSourceModifications/` note plus a `PROJECT_PLAN.md` entry).

## Status at a glance

All four items below were reported on device on **2026-10-19** against the 0.7 build. After several
passes (see the entries): **#40 and #43 are verified done**. **#41** and **#42** both looked
"unfixable" across five device passes, until the 6th pass's log showed the real cause: **the build
was stale** - `MSBuild /t:Rebuild` had been silently skipping recompilation, so the DLL on the
headset never contained the 5th revision's changes at all. A second fault was found in #41 at the
same time (`bHeld` gating). Both are fixed in code; `build/build-and-deploy.ps1` now refuses to
deploy a stale DLL. #41 and #42 await a device pass.

| #  | Issue | Status |
|----|-------|--------|
| 40 | Haptics fire once per trigger pull instead of while firing | **Fixed - verified on device, 2026-10-20** |
| 41 | Dual pistols: muzzle flashes sit at the back of / above the gun | Fixed in code (7th revision) - awaiting device pass |
| 42 | Right-grip "double jump" has no jump sound on foot | Fixed in code - awaiting device pass |
| 43 | Quicksave misses some levels, and re-saves right after a death quick-load | **Fixed - verified on device, 2026-10-20** |
| 44 | Left-stick movement only worked diagonally (dead-zone coupling) | Fixed in code (8th revision) - awaiting device pass |

## Third device pass (2026-10-20)

Owner's report against the second-round build:

- **#40 - PASS, "works great, fantastic haptics."** Done.
- **#41 - FAIL.** Both flashes still ended up **to the left of the left pistol**. On clarification
  the owner wanted **one flash per barrel** - left hand and right hand - and said the *default*
  position was the more correct one: the mod's error was collapsing both barrels onto one spot.
  The left/right value for the pistols should be the game's own default (one per barrel), with only
  forward/back and up/down corrected.
- **#42 - FAIL, and the mech double jump broke too.** The owner suspected the mod cannot actually
  tell on-foot from in-mech. That is exactly right - see the entry below.

## Second device pass (2026-10-20)

Owner's report against the first-round build:

- **#40 - PASS, but rejected on feel.** The vibration was tied to the held trigger, so an automatic
  weapon buzzed continuously for as long as the trigger was down. The owner asked for the pulse to
  follow the **ammo counter** instead (one kick per *shot*), and explicitly noted that a weapon
  switch should **not** buzz - the old weapon-change blip was there by accident and is now gone on
  purpose.
- **#41 - FAIL.** The alternating barrels no longer snapped to the same spot, but the fix had placed
  *both* flashes at the **same position, left of both guns**: the left/right component of the game's
  muzzle-offset table was being applied to a gun model it was never measured for. The owner wants the
  flash moved **only** down/up and forward/back - never left/right - sitting slightly lower and
  further out (away from the hand).
- **#42 - FAIL.** No jump sound on foot. The owner asked for the remap to happen at the **XrApi
  boundary** instead of inside the command handler: while **not** in a mech or vehicle, the right
  grip must report the **left** grip as true and its own bit as false.
- **#43 - PASS.**
- **#5 (value-based launcher update check) - PASS.**

## Items (as of 2026-10-19, second revisions 2026-10-20)

The four items come straight from the owner's notes; each records the report, what the code did, the
fix direction and what was changed, to be confirmed on the next device pass.

---

## 40. Haptics fire once per trigger pull, not while the weapon is firing

- **Status:** Fixed in code, second revision - awaiting device pass (reported 2026-10-19, revised
  2026-10-20)
- **Priority:** Medium (feedback only - shooting itself is unaffected)
- **Observed (device, 1st pass):** the controller buzzes **once** when the trigger is pulled and then
  goes quiet while the weapon keeps firing. It should keep vibrating for as long as a weapon is
  firing (automatic weapons, held fire).
- **Root cause (1st pass):** the mod armed the pulse on the **fire button's rising edge**, not on the
  held firing state. `game/VRStereo.cpp:1996` tested `m_nPressed & SHOGOVR_BTN_FIRE`, and
  `m_nPressed` is the one-frame edge (`:1951` - `m_nButtons & ~m_nPrevButtons`). So a pull set
  `m_fHapticR` (`m_fHapticL` when left-handed) to a 4-frame countdown exactly once; nothing re-armed
  it while the trigger stayed down.
- **1st fix:** send haptic value `1` every frame the trigger was held, letting WinlatorXR fade it
  out on release.
- **Result (device, 2nd pass):** PASS - the vibration now lasts as long as firing - **but rejected on
  feel.** A held trigger buzzes continuously for the whole pull, which is wrong for the *shot*
  (a shotgun or a charged shot should be one kick, not a shake), and the old weapon-change blip was
  an unintended extra.
- **Decision / fix direction (owner's call, 2nd pass):** drive the vibration from the **ammo counter**
  instead of the trigger - detect that an actual shot was fired (something other than a weapon
  switch) and pulse once per shot. Weapon change should produce **no** vibration, deliberately.
- **Fix (in code, 2026-10-20):** the trigger test is gone. The pulse now comes from
  `CVRStereo::ShotFired()`, called from **one** place: `CWeaponModel::Fire()`
  (`ShogoSource/ClientShellDLL/WeaponModel.cpp`), immediately after the ammo count is read and before
  the ammo/`W_FIRING_NOAMMO` branches. That is the single point where ammo is actually spent, so it
  can tell "trigger held" apart from "actually fired" by construction: it runs exactly once per shot,
  it cannot fire on a switch or an idle frame, and it needs no ammo-delta bookkeeping anywhere else.
  `ShotFired()` still respects `VRHaptics` (`0` = off) and picks the gun hand under `LeftHanded`; the
  weapon-change blip is removed from `UpdateBridge()` on purpose. A dry click (empty clip) also
  buzzes, which matches "the ammo counter drives it".
- **Refs:** `game/VRStereo.cpp` (`ShotFired`, the haptics block in `UpdateBridge`),
  `game/VRStereo.h` (`ShotFired`), `ShogoSource/ClientShellDLL/WeaponModel.cpp` (`Fire()` - note 4 in
  `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md`), `game/VRStereo.h` (`VRHaptics`,
  `m_fHapticL` / `m_fHapticR`).

---

## 41. Dual pistols: muzzle flashes sit at the back of / above the gun

- **Status:** Fixed in code, fourth revision - awaiting device pass (reported 2026-10-19, revised
  2026-10-20)
- **Priority:** Low-Medium (cosmetic, but visible on every pistol shot)
- **Observed (device, 1st pass):** with the on-foot **dual pistols** the muzzle flashes are *almost*
  right on the horizontal axis, but sit at the **back of the gun and above it** instead of **lower
  and forwards**, next to each barrel's muzzle.
- **Root cause (1st pass):** the #29 visible-muzzle fix deliberately exempted the Colt 45
  (`CVRStereo::GetMuzzleFlashWorld()` returned `DFALSE` for `GUN_COLT45_ID`), because the mod helper
  only knew **one** muzzle offset and pinning both alternating flashes to the same barrel looked
  wrong. With the helper out of the picture the pistols fell back to the **game's own** flash position
  (`m_vFlashDrawPos = m_vFlashPos`), which is the **flat-screen** muzzle - a camera-relative spot at
  the old view weapon - so in VR it landed behind and above the hand-held gun.
- **1st fix:** stop exempting the pistols and give the helper the **fired barrel** (`W_FIRED` /
  `W_FIRED2`, already tracked by `CWeaponModel::Fire()` and distinguished by
  `GetWeaponMuzzleOffset(id, eState)`), so each alternating barrel got its own offset.
- **Result (device, 2nd pass):** FAIL - both flashes are now at the **same position, to the left of
  both guns**. Alternation works, but the sprite is off to one side.
- **Root cause (2nd pass):** the game's muzzle-offset table carries a **left/right component**
  (`-6.2` / `+5.8` for the Colt 45) whose only purpose is to **pick which barrel of a two-barrel
  weapon fires**. It was measured for the flat game's own gun model, which is not the mod's
  third-person model, so on the hand-held gun it is meaningless sideways displacement. Applying it
  put both barrels at the same wrong lateral spot.
- **Decision / fix direction (owner's call, 2nd pass):** alter **only** the forward/backward and
  up/down position - never left/right. The flash should sit **slightly lower** and **further
  forwards** (away from the hand).
- **Result (device, 3rd pass):** FAIL - still **both flashes to the left of the left pistol**. The
  owner reiterated that only forward/back and up/down should change, and said the **game's own
  default** is preferable to the broken result if that is all that can be adjusted.
- **Clarified intent (owner, 2026-10-20):** the Colt 45 should have **one flash per barrel - left
  hand and right hand - depending on which one fires**. The *default* behaviour was **more
  correct**: the mod's mistake was collapsing both barrels onto the same position. So the
  left/right value for this weapon should be **the game's own default** (the pre-mod value, `-6.2` /
  `+5.8`, one per barrel), not a single constant.
- **Fix (in code, 2026-10-20, 3rd revision):** `GetMuzzleFlashWorld()` now special-cases the dual
  pistols. For `GUN_COLT45_ID` the table's left/right component is **kept**, so barrel 1 fires on the
  left and barrel 2 on the right - one flash per hand, matching the game. For **every other**
  (single-barrel) weapon the table's left/right is only barrel-selection noise, so it is replaced by
  the constant `VRMuzzleLateral` (default **0**, back to its pre-modding value). Forward/back
  (`z += VRFlashOffset`, default `3`) and up/down (`y = -VRFlashLower`, default `3`) are applied to
  **all** held weapons, which is what fixed the original "behind and above the gun" complaint.
- **Result (device, 4th pass):** PASS on the arrangement - one flash per hand, alternating correctly
  - but the two flashes are now **a little too wide apart**. The owner asked for them to be **just a
  tiny bit closer together**, after which the pistols are good.
- **Fix (in code, 2026-10-20, 4th revision):** the pistols' left/right is **scaled about the centre**
  by a new live CVar `VRPistolSpread` (default **0.7**), applied only to `GUN_COLT45_ID`. Barrel 1
  goes from `-6.2` to `-4.34` and barrel 2 from `+5.8` to `+4.06`, so the span narrows from the
  game's `12.0` to `8.4` while the two barrels stay on **opposite sides** of the axis (they are never
  given the same x - that was the original bug). `1.0` reproduces the game's own separation exactly.
  Every other weapon is unchanged. All four CVars (`VRFlashOffset`, `VRFlashLower`,
  `VRMuzzleLateral`, `VRPistolSpread`) are live, and the applied x/y/z offset + scale is logged per
  weapon (the pistols' line is tagged `[pistols: spread-scaled]`).
- **Result (device, 5th pass):** still **a little too far apart**. Owner: bring them **horizontally
  closer together still, towards the middle by maybe a tenth of the distance**.
- **Fix (in code, 2026-10-20, 5th revision):** `VRPistolSpread`'s default is lowered from `0.7` to
  **`0.63`** - a further **10%** off the current span (`1 - (1 - 0.7) * 0.9`, i.e. `0.7 * 0.9`).
  Barrel 1 goes to `-3.91` and barrel 2 to `+3.65`, span `7.56` (from `8.4`). The barrels stay on
  **opposite sides**; nothing else changes. Raise `VRPistolSpread` back towards `1` (or set it live)
  if it is now too close.
- **Verification (offline, real table values):** 3rd revision - barrel 1 at `-6.2` (left), barrel 2
  at `+5.8` (right), opposite sides, 12-unit separation, versus the broken revision's identical
  `+3.0`; single-barrel weapons on the axis. 4th revision - `-4.34` / `+4.06`, span `8.4`. 5th
  revision - `-3.91` / `+3.65`, span `7.56`, still opposite sides, single-barrel unchanged, and
  `VRPistolSpread = 1` reproduces `12.0`. All cases pass.
- **Result (device, 6th pass - the decisive one):** still unchanged, **and the device log explained
  why.** Every flash line read `(first-person gun; x/y/z offset -6.2,-1.3,12.2 ...)` - the **raw
  table values**, with no spread scaling at all. Two separate faults:
  1. **The build was stale.** `ShogoVR_game.log` showed the game loading
     `CShell.dll built Oct 20 2026 22:22:16`, but the source had been edited at `22:34`. `MSBuild
     /t:Rebuild` had been **silently skipping recompilation** (the object files looked newer than
     the source), so the DLL on the headset **contained neither the 5th revision nor the #42
     change**. This is why five passes saw no movement. **Now guarded** by
     `build/build-and-deploy.ps1`, which clears the object tree, rebuilds from scratch, and refuses
     to deploy a DLL whose `built <date>` stamp is older than the newest source file.
  2. **The corrections were gated on `bHeld`.** `VRGunModel` defaults to **`0`**, and no `.ini` in
     the tree sets it, so on device the mod poses the **first-person model with arms**
     (`bHeld == false`). The offsets were inside `if (bHeld)`, so they never ran. The log's
     `first-person gun` tag and raw `-6.2,-1.3,12.2` confirm it.
- **Fix (in code, 2026-10-20, 6th revision):** the offset corrections are **moved out of
  `if (bHeld)`** so they apply in **both** branches (mod-held gun *and* first-person model) - the
  flash is drawn on the gun the player sees either way. The per-weapon x handling is unchanged
  (Colt 45 keeps its per-barrel left/right, scaled by `VRPistolSpread`; every other weapon uses
  `VRMuzzleLateral`). The per-weapon log line now tags the pistols in either branch
  (`[pistols: spread-scaled, first-person]` / `..., held]`) so a device log proves which path ran.
- **Result (device, 7th pass):** **progress** - the flashes are now correctly **in front of** their
  barrels, and the per-hand alternation is right. Two small trims asked for: the sprites are
  **slightly too far ahead** of the barrels (looks like about **half a foot**) and **slightly too
  low** (about **3 inches**).
- **Fix (in code, 2026-10-20, 7th revision):** both forward/back and up/down nudges are trimmed.
  `VRFlashOffset` (**forward**) goes `3.0 -> 2.0` and `VRFlashLower` (**down**) goes `3.0 -> 2.4` -
  forward by more, because its error was judged about twice the vertical one. On foot these are
  multiplied by `VRGunScale` (1.5), so `VRFlashOffset=2` now moves the sprite 3.0 model units out
  and `VRFlashLower=2.4` moves it 3.6 down. Both stay **live CVars**, so either can be nudged on
  device without a rebuild (`VRFlashOffset 2.2`, etc.).
- **Refs:** `game/VRStereo.cpp` (`GetMuzzleFlashWorld`, the `VRFlashOffset` / `VRFlashLower` /
  `VRMuzzleLateral` / `VRPistolSpread` CVars), `game/VRStereo.h` (the four `VarTrack`s);
  `ShogoSource/ClientShellDLL/WeaponModel.cpp:372` (forwards `eState`),
  `ShogoSource/Shared/WeaponDefs.h:1119-1150, 1326-1350` (the table, incl. the Colt 45's `-6.2` /
  `+5.8`); `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md` (note 3); archived: #27, #29,
  #34.

---

## 42. Right-grip "double jump" has no jump sound on foot

- **Status:** Fixed in code - awaiting device pass (reported 2026-10-19; the 6th pass showed the
  earlier builds were stale, see below)
- **Priority:** Low (a missing sound on foot; movement always worked)
- **Observed (device):** the right grip is the "double jump", which only the MCA can do. On foot,
  pressing it gives a jump but **no jump sound**; the left grip's normal jump does play the sound.
- **Cause (confirmed by the owner, 2026-10-19):** the double jump is a **separate in-game control**,
  and the developers chose that for a **human character** it still performs an ordinary jump but
  deliberately **makes no jump sound** - the sound belongs to the normal jump path. So the right grip
  is faithfully asking for the double-jump control and getting exactly what the game does with it on
  foot: the silence is by design, not a mod or mapping fault. The engine already downgrades a
  double jump to `BC_CFLG_JUMP` on foot (`ShogoSource/ClientShellDLL/CMoveMgr.cpp:192-203`), which is
  why the jump itself works.
- **Failed attempts (all removed):** three different *remaps* of the right grip were tried - a
  command-level fold (`VRCommandOn(COMMAND_ID_DOUBLEJUMP)` returning `DFALSE` on foot), an
  input-level fold in `UpdateInput()` gated on the mech/vehicle mode, and that same fold with a
  corrected mode signal (the shell had passed `m_nPlayerMode != PM_MODE_KID`, which reports "mech" on
  foot because `PM_MODE_FOOT` is `0` while `PM_MODE_KID` is `5`). The first failed outright; the
  second and third **broke the mech double jump**. The lesson is that **replacing** one grip's
  action with the other can never be right - both the sound *and* the double jump have to happen.
- **Owner's solution (2026-10-20, 5th pass, confirmed as the direction):** **send BOTH `JUMP` and
  `DOUBLEJUMP` while the right grip is held.** That way a regular jump *always* happens (which is
  what plays the sound) and the game additionally sees the double jump where it means something
  (in an MCA). It does not need to know the player mode at all. On foot: ordinary jump **with** its
  sound. In a mech: the double jump, with the jump sound. Because both bits are simply *added*,
  nothing is ever taken away.
- **Fix (in code, 2026-10-20) - the owner's, in `game/XrApiUDP.cpp`:** the cleanest place is the
  **source of the runtime word**, `CXrApiUDP::ButtonBits()`. The right grip's line is changed from
  `if (XB(0))` to **`if (XB(0) || XB(12))`** - so the **left grip and the right grip both set
  `SHOGOVR_BTN_JUMP`**, and the right grip *also* keeps `SHOGOVR_BTN_DOUBLEJUMP`. The right grip
  therefore looks like a genuine jump to everything downstream - which is what plays the sound on
  foot - while the double jump bit stays for the MCA. `VRCommandOn()` stays a plain 1:1 answer on
  each command's own bit. Nothing is remapped and the double-jump bit is never cleared. A one-line
  **grip press log** (`VR: grip jump - bits after mapping ...`, in `UpdateInput`) makes a device log
  prove it. (An earlier `UpdateInput` line that OR'd the jump bit in has been **removed** - the
  source-level fix supersedes it; one place, one intent.)
- **Left-handed mode (owner's rule, 2026-10-20):** left-handed mode now swaps **only the two
  triggers** (fire / left-trigger); the **grips are not swapped**. That keeps the left grip = jump
  and the right grip = jump + double jump in *both* handedness modes, so the source-level fix above
  covers left-handed play too. (Previously the swap traded the grip bits, which would have moved the
  double jump to the left controller and made it silent on foot again - the 6-case offline check
  caught exactly that, which is what prompted this rule.)
- **Verification (offline, 7th revision):** the full pipeline - `ButtonBits()` -> `UpdateInput()`
  (trigger-only swap) -> `VRCommandOn` - over 8 grip cases. Right-handed and left-handed: right grip
  -> `JUMP` **and** `DOUBLEJUMP` both true; left grip -> `JUMP` only; both grips -> both; no grip ->
  neither. All cases pass. (The previous 6-case version **failed** on left-handed-left-grip, which
  is what exposed the swap problem.)
- **Mode signal (owner's rule, 2026-10-20 - kept correct even though this fix no longer needs it):**
  `PM_MODE_FOOT` (**0**) and `PM_MODE_KID` (**5**) are **both on foot** - the kid is cut content, a
  past-life version of the characters, and moves like a human. **Every other mode is an MCA** (the
  walking mech that transforms into a vehicle). `PM_MODE_KID` must **never** be used as the "is mech"
  test: the original `m_nPlayerMode != PM_MODE_KID` reported "mech" **everywhere on foot**. The shell
  call site computes `bOnFoot = (m_nPlayerMode == PM_MODE_FOOT || m_nPlayerMode == PM_MODE_KID)` and
  passes `!bOnFoot` (edit 24 in
  `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md`). The mode tracking that existed
  only for the old remap (`OnFoot()`, `ResetMechModeKnown()`, `m_bModeKnown`) stays **removed**;
  `m_nRawButtons` / `GetRawButtons()` stay as a plain record of the runtime's word.
- **Verification (offline):** a table of button-bit cases through the real `UpdateInput`-then-
  `VRCommandOn` path. Right grip -> `JUMP` **and** `DOUBLEJUMP` both true; left grip -> `JUMP` only
  (unchanged); both grips -> both true; idle and a non-jump button -> neither; left-handed mode ->
  the swap moves which physical grip is "the double jump", and the addition follows it correctly.
  All cases pass.
- **Result (device, 6th pass):** the right grip still played no sound - **but the log showed the
  reason: the DLL on the headset was stale** (`built ... 22:22:16`, source edited `22:34`), so the
  both-bits fix had **never actually run on device**. `MSBuild /t:Rebuild` had been silently
  skipping recompilation. With `build/build-and-deploy.ps1` (which clears the object tree and
  refuses to deploy a DLL older than the newest source) the fix is now genuinely in the build.
  `UpdateInput` also now logs **one line per grip press** - `VR: grip jump - bits after mapping
  JUMP=.. DOUBLEJUMP=..` - so the next device pass proves what the right grip produces, alongside
  the build stamp at the top of the log.
- **Why "just send the left grip's jump too" is exactly right (owner, 6th pass):** the *left* grip's
  jump already works - sound and all - through this same `IsCommandOn` path, so adding the jump bit
  to the right grip's frame is all that is needed. Nothing about the jump key or its binding is
  touched.
- **Refs:** `game/XrApiUDP.cpp` (`CXrApiUDP::ButtonBits()` - **the fix**), `game/ShogoVRShared.h`
  (`SHOGOVR_BTN_JUMP` / `SHOGOVR_BTN_DOUBLEJUMP`),
  `game/VRStereo.cpp` (`UpdateInput` - the grip log, and the trigger-only left-handed swap;
  `VRCommandOn`), `game/VRStereo.h` (`m_nRawButtons`, `GetRawButtons`);
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp` (`MID_PLAYER_MODECHANGE`),
  `ShogoSource/ClientShellDLL/RiotClientShell.h:112` (`IsOnFoot`),
  `ShogoSource/Shared/PlayerModeTypes.h` (`PM_MODE_FOOT` = 0, `PM_MODE_KID` = 5, `PM_MODE_MCA_*`),
  `ShogoSource/ClientShellDLL/CMoveMgr.cpp:183-204` (the engine-side downgrade - background);
  `ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md` (note 24); archived: #15
  (controller mapping), #32 (jump stopped working).

---

## 43. Quicksave misses some levels, and re-saves right after a death quick-load

- **Status:** **Fixed - verified on device, 2026-10-20** (reported 2026-10-19, fixed 2026-10-19,
  passed the second device pass)
  the old "autosave family" the owner had cancelled on 2026-10-14 (archived #24 / #25 / #33) being
  picked up again, because the level-start save is worth having right.
- **Priority:** Medium-High (without a level-start save, the left-trigger death recovery - archived
  #16 - has nothing sound to restore)
- **Observed (device):** two related problems with the level-start quicksave:
  1. **Some levels never quicksave at all** - so the left trigger has nothing to reload on death.
  2. After **dying and pressing the left trigger** (quick-load / F9), the level loads and then
     **quicksaves again** ~2 s later, overwriting the level-start save with the post-respawn state -
     so a second death reloads the wrong place. The owner's ask: **do not quicksave again when the
     load we just did came from that death quick-load**.
- **Root cause:** the mod arms and fires the save in `CVRStereo::UpdateInput`
  (`game/VRStereo.cpp:3546-3563`): `m_bAutosavePending` (armed by `SetLevelLoaded()`,
  `game/VRStereo.h:150`) fires `InjectCommandTap(COMMAND_ID_QUICKSAVE)` once after 120 frames (~2 s)
  of live, alive gameplay. `SetLevelLoaded()` is called from **one** place - the tail of the loading
  path, once `m_bInWorld` and the player object exist
  (`ShogoSource/ClientShellDLL/RiotClientShell.cpp:2644-2652`). So:
  (a) a level entered via the **bumper / "press any key"** screen plays through
  `GS_BUMPER -> GS_PLAYING` (`RiotClientShell.cpp:4831-4844`) and never reaches that call - hence the
  levels that do not save; and
  (b) a quick-load re-opens the world through `DoLoadWorld(..., LOAD_RESTORE_GAME)`
  (`RiotClientShell.cpp:7578`; the flags are in `ShogoSource/Shared/ClientServerShared.h:145-147`),
  which passes the same loading path, so `SetLevelLoaded()` arms it again and the ~2 s save fires
  over the level-start one.
- **Fix direction:** Use a `PlayerJustDied` flag approach. Set this flag when player dies. 
  When `SetLevelLoaded()` is called, if `PlayerJustDied` is true, clear the flag but do **not** 
  trigger a quicksave. If `PlayerJustDied` is false (fresh level load), trigger a quicksave 
  as before. Also arm on bumper transition (`UpdateBumperState()`) when transitioning to 
  `GS_PLAYING` to catch levels that miss the current load path. The death quick-load should 
  set up state so that after loading, we don't re-save over the level-start save. These are 
  ShogoSource-side changes (mod needs to track the flag; shell call sites may need adjustment), 
  so they need `ShogoSourceModifications/` notes.
- **Fix (in code, 2026-10-19):** the mod tracks `m_bPlayerJustDied` (`game/VRStereo.h`), set by
  `SetPlayerDead()` (the shell already calls it every frame with `IsPlayerDead()` -
  `RiotClientShell.cpp:1753`). `SetLevelLoaded()` clears the flag and skips arming the save when it
  is set, otherwise arms `m_bAutosavePending` as before. The shell now also calls
  `SetLevelLoaded()` at the bumper transition - note that the actual `GS_BUMPER -> GS_PLAYING`
  transition lives in `OnKeyDown()` (the `GS_BUMPER` branch), not in `UpdateBumperState()` (which
  only draws the screen while the state is `GS_BUMPER`). Documented as section 23 in
  `RiotClientShell.cpp.md`; `PROJECT_PLAN.md` updated. Known edge (inherent in the owner's design,
  worth a device check): if the player dies and then loads a save from the **menu** instead of the
  left trigger, that load also consumes the flag and does not quicksave; the next fresh level load
  arms normally again.
- **Process (confirmed by the owner, 2026-10-19):** editing ShogoSource here is expected and fine -
  the standing rule is simply that every ShogoSource change comes with its `ShogoSourceModifications/`
  prose note and a `PROJECT_PLAN.md` entry, and that rule is followed. It is not a reason to avoid the
  change, and it does not change the fix direction above.
- **Refs:** `game/VRStereo.cpp:3546-3563` (arm + fire), `:3575-3577` (left trigger while dead ->
  `COMMAND_ID_QUICKLOAD`), `game/VRStereo.h:150,412`;
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp:2644-2652` (`SetLevelLoaded` call), `:4831-4844`
  (`GS_BUMPER -> GS_PLAYING`), `:7578` (`DoLoadWorld ... LOAD_RESTORE_GAME`),
  `ShogoSource/Shared/ClientServerShared.h:145-147`; archived: #16, #24, #25, #33.

---

## 44. Left-stick movement only worked diagonally (dead-zone coupling)

- **Status:** Fixed in code (2026-10-20, 8th revision) - awaiting device pass
- **Priority:** High (movement is the core interaction)
- **Observed (device, 2026-10-20):** the left stick only moves the player **diagonally**. It feels
  as if an axis only registers once the stick is past the dead zone on **both** X and Y - a straight
  forward push does nothing, and only a diagonal push moves you.
- **What the code was doing:** `CVRStereo::GetMoveAxes()` dead-zoned **each axis separately**
  (`fx` from `|moveX|`, `fy` from `|moveY|`, each rescaled from its own edge), then capped the
  combined magnitude at 1. This was a deliberate change (see PROJECT_PLAN #22, 14-17 Oct, "single
  15% move dead zone... the per-axis application is kept, so a mostly-forward push still cannot
  drift sideways") meant to stop a straight push from drifting sideways. On device it felt wrong -
  the per-axis thresholds coupled the two directions into a diagonal-only feel.
- **Fix (in code, 2026-10-20):** restored the **upstream Shogo-VR** `GetMoveAxes()` dead zone
  **verbatim** - a single **radial** dead zone. Take the length of the whole stick vector
  (`fLen = sqrt(moveX^2 + moveY^2)`); if it is past the dead zone, rescale the **whole vector** by
  one common factor and use that for both `fForward` and `fRight`; if not, report centred
  (`DFALSE`). This preserves the push direction exactly and starts movement gently at the edge of
  the zone. The dead zone stays the **live** `VRMoveDeadZone` CVar (default `0.15`, clamped
  `0..0.95`), so it can still be tuned in `ShogoVR.ini` / Options > VR without a rebuild. The
  rotate-to-aim block (`VRMoveDir`) is unchanged.
  Upstream reference: `https://github.com/BrandonW24/Shogo-VR/blob/main/game/VRStereo.cpp`
  (`CVRStereo::GetMoveAxes`).
- **Note:** this is a `game/`-only change (the mod), so it needs **no** `ShogoSourceModifications/`
  note and no `PROJECT_PLAN.md` ShogoSource entry - it is recorded here and in the PROJECT_PLAN
  device-pass history.
- **Refs:** `game/VRStereo.cpp` (`CVRStereo::GetMoveAxes`), `game/VRStereo.h`
  (`m_vtMoveDeadZone`). Consumed by `game/`'s `VRCommandOn` (FORWARD/REVERSE/STRAFE_* answers) and
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp` (`CalculateCameraRotation` ->
  `CMoveMgr->UpdateAxisMovement`).

---

## Cross-cutting notes

- **The pre-release history is in `PreReleaseBugs/BUGS_TO_FIX.md`.** Its "Cross-cutting notes" still
  hold - handedness/basis, the input path (`ButtonBits()` vs the on-device bool string), the 2D
  per-eye layout, `VRSyncHold` frame pacing, and the repo policy that every `ShogoSource/` change
  gets a `ShogoSourceModifications/` note plus a `PROJECT_PLAN.md` entry.
- **Input path (this list: #40, #42, #43, #44):** anything driven from the pad starts in
  `game/XrApiUDP.cpp` (`ButtonBits()` maps the runtime's bool indices to `SHOGOVR_BTN_*`) and is
  read through `CVRStereo::VRCommandOn()` / the edge sets in `UpdateInput()`. Reconcile each bool
  index against the on-device bool string before trusting a mapping. #42 turned out **not** to be a
  mapping fault - the game's double-jump control simply makes no sound for a human character, so the
  fix is a deliberate mod-side remap while on foot (see #42).
- **Weapon effects (this list: #41):** the mod hides the game's real first-person weapon model and
  draws its own at the hand pose, so anything the engine anchors to the hidden model (the muzzle
  flash) has to be re-placed by `CVRStereo::GetMuzzleFlashWorld()`. That helper applies the engine's
  per-weapon muzzle offset in the **gun model's own frame** - keep the offset and the placement
  together when fixing #41.
- **Save / load (this list: #43):** the level-start quicksave exists only to back the left-trigger
  death recovery. Any change here must keep that guarantee: exactly **one** save per fresh level,
  protected from restore loads. See archived #16 (the recovery) and #24/#25/#33 (why it was
  previously dropped).
- **Testing:** PC smoke test first (windowed, borderless, sync pixel, menus per eye), then on device
  (Quest 3, WinlatorXR, 2800x1400 SBS). Grep the desktop build log for a line naming each changed
  behaviour to confirm deployment. The **XrApi UDP Tester** (repo `WinlatorXR/XrAPIUDPTester`) drives
  the pad on PC: `1/2/3/4` -> L-Trigger / L-Grip / R-Trigger / R-Grip, `5/6` -> L/R stick click,
  `7/8/9/0` -> LX/LY/RB/RA, `Y` -> L-Menu - useful for #40 (haptics need the runtime, but the fire
  edge vs held state can be logged) and #42 (R-Grip is index 12).
