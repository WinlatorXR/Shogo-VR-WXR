# Bugs_To_Fix_V0.9.md

Issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed in code` (awaiting a device pass) / `Fixed` (verified) /
`Won't fix`.

This is the **V0.9 cycle** tracker. The pre-release (0.7 and earlier) log is archived as
[`BUGS_TO_FIX.md`](BUGS_TO_FIX.md) (`#1`-`#39`); numbering here and beta release (0.8 and earlier) [`BUGS_TO_FIX_V0.7_to_V0.8.md`](BUGS_TO_FIX_V0.7_to_V0.8.md) (`#40`-`#44`); numbering here
**continues at `#45`** so the references in `Docs/Pre-LaunchDocs/PROJECT_PLAN.md` and the
`ShogoSourceModifications/` notes stay unambiguous. Note that `game/` files are in this repository
(the mod); `ShogoSource/` files are the git-ignored Monolith source (any edit there needs a
`ShogoSourceModifications/` note plus a `PROJECT_PLAN.md` entry).

## Status at a glance

This is the first entry in the new BUGS_TO_FIX.md, and existing files need to be updated to point to the previous document (Docs/Pre-LaunchDocs/BUGS_TO_FIX_V0.7_to_V0.8.md) where they currently point to this file (as it is a new file)

Any bugs from #1 to #44 are marked as closed and are to be ignored going forward unless specificially referenced by PROJECT_PLAN.md or FUTURE_FEATURES.md or the user suggests it directly

| # | Issue | Status |
|---|-------|--------|
| 45 | Jump fires from either grip; double jump only from right grip (with jump still firing simultaneously) | **Won't fix** (as of V1.0) |
| 46 | Pistol muzzle flashes too far forward / too low, and the global flash trims moved every other weapon's flashes off their original muzzle-table position | **Fixed** - verified on device (Oct 22) |
| 47 | Left-stick movement feels binary: full speed the moment the dead zone is crossed, and a slight off-axis tilt = full-speed strafe | **Fixed** - verified on device (Oct 22) |

**Device pass - Oct 22, 2026:** ran the new DLL (build `CShell.dll built Oct 22 2026 00:45:26`)
on device; `ShogoVR_game.log` is clean. Verdicts: **#46 good enough / verified**, **#47
perfect / verified**, **#45 did not work** - Won't fix as of V1.0.

---

## #45 - Jump should fire from BOTH grips; double jump only from RIGHT grip

**Status:** Won't fix (as of V1.0) - failed device pass (build `CShell.dll built Oct 22 2026 00:45:26`)

**Device pass result - Oct 22: did not work.** The new DLL ran and `ShogoVR_game.log`
is clean, but the contract below did not hold on device; the #45 proof log is commented
out (owner's choice), so the failure left no input trace. **Decision: Won't fix as of
V1.0** - the candidate fix in the watch item below (hold `COMMAND_ID_DOUBLEJUMP` back
~2 frames after the right-grip press edge) was considered and not pursued.

Both controller grips must always trigger **JUMP** to the game engine, whichever hand presses
them (left grip or right grip). **Double jump** must only fire when the **RIGHT grip** is
pressed - and when it does, JUMP must still fire simultaneously from that same press (the
engine sees a jump and a double jump together, so the right grip alone performs a double jump).

Desired end state: left grip = jump only, right grip = jump + double jump simultaneously,
in every player mode.

**Implemented (owner's, in this build):**

- `game/XrApiUDP.cpp` `CXrApiUDP::ButtonBits()` - the two bits are now sourced one grip each:
  `if (XB(0)) b |= SHOGOVR_BTN_JUMP;` (left grip only) and
  `if (XB(12)) b |= SHOGOVR_BTN_DOUBLEJUMP;` (right grip only). Indices re-checked against
  the XrApi runtime's bool order (index 0 = `L_GRIP`, index 12 = `R_GRIP`; the left-handed
  swap only touches the triggers).
- `game/VRStereo.cpp` `CVRStereo::VRCommandOn()` - `COMMAND_ID_JUMP` answers from **either**
  bit: `((nHeld & SHOGOVR_BTN_JUMP) || (nHeld & SHOGOVR_BTN_DOUBLEJUMP))`, while
  `COMMAND_ID_DOUBLEJUMP` stays on the double-jump bit alone. Net effect per press:
  - left grip -> JUMP only -> a regular jump (with sound);
  - right grip -> JUMP + DOUBLEJUMP together -> a regular jump and a double jump
    simultaneously - a double jump from that one grip, in every player mode.
- **Review note (caught in this cycle, fixed by the owner before this build):** the first
  version of that line used `||` *between the two bit constants*
  (`SHOGOVR_BTN_JUMP || SHOGOVR_BTN_DOUBLEJUMP`), which the compiler folds to the constant
  `1` - i.e. the FIRE bit - so jump would have answered on the right trigger and never on any
  grip. The line above is the corrected two-mask form, verified in source for this build.
- **Diagnostics disabled by the owner for this pass:** the #45 proof log
  (`VR: input - runtime bools [...]`) in `UpdateBridge()` and the screen-flash toggle log in
  `SetScreenFlash()` are both commented out, so device logs will not carry those lines.

**Engine sound path (researched for this entry):** the jump sound is played server-side by
`CBaseCharacter::UpdateInAir()` (`ShogoSource/ObjectDLL/BaseCharacter.cpp`) - only while
airborne with upward velocity and `m_bJumping && !m_bJumped`, where `m_bJumping` is set from
`BC_CFLG_JUMP || BC_CFLG_DOUBLEJUMP`. `CPlayerObj::UpdateControlFlags()`
(`ShogoSource/ObjectDLL/PlayerObj.cpp`) folds `COMMAND_ID_DOUBLEJUMP` back into `BC_CFLG_JUMP`
on foot, but uses a real `BC_CFLG_DOUBLEJUMP` in the MCA.

**Device-pass watch item - right grip sound:** the right grip presents JUMP and DOUBLEJUMP to
the engine in the very same frame. If the right grip double-jumps but is silent while the left
grip sounds normally, that same-frame arrival is the prime suspect; the candidate fix is to
hold `COMMAND_ID_DOUBLEJUMP` back ~2 frames after the grip's press edge so a clean JUMP frame
reaches the engine first.

**Device pass checklist:**
- left grip (on foot and in the MCA) -> one regular jump, with sound;
- right grip -> double jump in the MCA; on foot, jump with sound (the watch item above);
- right trigger -> fires the weapon and must **not** jump (regression check for the `||` typo).

## #46 - Pistol muzzle flashes too far forward/low; other weapons' flashes moved with the trims

**Status:** Fixed - verified on device (build `CShell.dll built Oct 22 2026 00:45:26`)

**Verified on device - Oct 22: good enough.** The muzzle flash still sits in front of the
pistols, but it looks fine as long as you don't aim the pistols at a goofy sideways angle.
Accepted as-is: the sideways-aim case is a known cosmetic limitation, not worth further
tuning.

Two complaints carried by this entry:

- **Dual pistols**: flashes sit too far forward (out past the muzzle along the barrel depth
  axis) and too low relative to the gun model.
- **Every other weapon**: the *global* flash trims made while chasing the pistol problem also
  moved their flashes. Those flashes must sit at the game's original muzzle-table position -
  what the game draws with no correction at all - which is what every non-pistol weapon looked
  like before this tuning.

**Implemented (owner's, in this build):**

1. `CVRStereo::Init()` (`game/VRStereo.cpp`) - both defaults trimmed again:

   | CVar | Original | Previous build | Now |
   |------|----------|----------------|-----|
   | `VRFlashOffset` (forward past the muzzle) | `2.0` | `1.5` | `0.5` |
   | `VRFlashLower` (drop below the barrel) | `2.4` | `2.16` | `1.16` |

2. `CVRStereo::GetMuzzleFlashWorld()` - the correction is now split per weapon:
   - **Dual pistols** (`GUN_COLT45_ID`): scaled down at the use site -
     `y = -(VRFlashLower * 0.5)`, `z += VRFlashOffset * 0.3`. With the new defaults and the
     on-foot gun scale (1.5) that is **0.225 model units past the table and 0.87 below it**
     (previous build: 2.25 / 3.24). Left/right is untouched: `VRPistolSpread` still scales the
     game's own per-barrel values and the `W_FIRED` / `W_FIRED2` barrel alternation is
     unchanged.
   - **Every other weapon**: the correction is skipped entirely - the else-case simply leaves
     the muzzle table's own y/z in place, so the flash sits exactly where the game puts it
     (a 0 / 0 correction), the same as before any of this tuning.

**Gap closure (owner-approved follow-up, in this build):** the else-branches originally
spelled their fallbacks as `GetFloat(0.0f)` - which looked like a no-op but is not, because
`VarTrack::GetFloat` only uses a fallback when the CVar was never registered, and `Init()`
registers both - so the full live values (1.16 / 0.5) were silently applied to every weapon.
That gap, flagged in the earlier review, is now closed: the y/z correction moved into a
dual-pistols-only `if` block, and every other weapon keeps the game's table position
untouched. The old global correction stays in the source as commented reference lines.

**Also in this build:** the per-weapon muzzle-flash diagnostic log line and the screen-flash
toggle log are commented out (owner's choice) - no flash logging this pass.

**Device pass checklist:** dual pistols - flash position and height against the barrels
(0.225 past / 0.87 under the table is the new intent); per-barrel left/right alternation
intact; spot-check rifle/shotgun/etc. - their flashes must now sit exactly at the game's
own muzzle-table position (the primary check for the gap closure above).

## #47 - Left-stick movement feels binary (WASD): full speed and phantom strafe

**Status:** Fixed - verified on device (build `CShell.dll built Oct 22 2026 00:45:26`)

**Verified on device - Oct 22: perfect.** Movement ramp, dead-zone onset and the
no-phantom-strafe behavior all confirmed by the owner.

The left stick does not ramp. The moment it crosses the dead zone, movement is at full speed,
and a slightly-off-straight push also strafes at full speed - it feels like digital WASD.
Worst case: *any* thumbstick value passing the dead zone seemed to send forward/backward
**and** left/right at once.

**Root cause - in the engine, not the mod.** Movement is consumed as binary commands:
`VRCommandOn()` answers `COMMAND_ID_FORWARD/REVERSE/STRAFE_*` from the **sign** of the analog
value, and `CMoveMgr::UpdateControlFlags()` turns that into `BC_CFLG_*` flags. The velocity
cap in `ShogoSource/ClientShellDLL/CMoveMgr.cpp` (the `OverrideForwardBackward` /
`OverrideLeftRight` logic) then **skips** an axis's analog cap (`fMaxVel * m_fAxis*Vel`)
whenever that axis's `BC_CFLG_*` flag is set. So the gentle ramp the radial dead zone produces
never reaches the player: any nonzero axis value means full speed on that axis.

**Fix - two gates, both reading `VRMoveDeadZone` (default 0.15; tunable in Options > vr
settings or `ShogoVR.ini`):**

1. `CVRStereo::GetMoveAxes()` (gate 1): the radial dead zone for the stick's shape, plus a
   per-axis cut that zeroes any axis whose **own raw** deflection is still inside the dead
   zone.
2. `CVRStereo::VRCommandOn()` (gate 2 - the owner's new change this pass, at "the level where
   commands are sent to the game"): once `GetMoveAxes()` returns the (post rotate-to-aim)
   axes, each movement command re-checks **its own axis** against the dead zone on the scaled
   value - `fFwd > fDead` forwards, `fFwd < -fDead` backwards, `fRight > fDead` right,
   `fRight < -fDead` left. An axis must clear the dead zone twice - raw, then scaled - before
   its command answers true, and only the axis(es) that do are sent to the engine.

**Device-pass expectations (the two gates share one CVar):**

- A straight push starts moving at roughly **28% of raw stick length** (0.15 at gate 1, then
  0.15 on the rescaled value at gate 2); a pure diagonal needs roughly **33%**. If that feels
  too stiff, lowering `VRMoveDeadZone` moves both gates together.
- Mostly-forward push with a slight tilt must **not** strafe - the reported worst case, which
  gate 2 catches by rejecting any small second-axis value gate 1 let through.
- When both axes end up inside the gates, `GetMoveAxes()` still returns TRUE with zeros on
  purpose, so the keyboard/joystick axes stay ignored (no ghost keyboard movement).

**Device pass checklist:** gentle forward push walks only once the stick is clearly past the
zone (and never sprints from a nudge); slight tilt on a forward push -> no strafe; pure
diagonal -> moves; onset feels right at ~28% straight / ~33% diagonal (both tunable).

---

**Build policy (also from this change):** every deploy now comes from `build/build-and-deploy.ps1`,
which wipes `build/obj` **and** `build/out` and runs a from-scratch `/t:Rebuild` every time, then
refuses to deploy unless the DLL's embedded `CShell.dll built ...` stamp parses to a timestamp
at least as new as the newest `.cpp`/`.h` in **all three** compiled trees (`game/`,
`ShogoSource/ClientShellDLL/`, `ShogoSource/Shared/`). Stale-DLL builds are no longer possible
through that script.
