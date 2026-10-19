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

All four items below are **open**, reported on device on **2026-10-19** against the 0.7 build. None
has a fix in code yet, so none is awaiting a device pass.

| #  | Issue | Status |
|----|-------|--------|
| 40 | Haptics fire once per trigger pull instead of while firing | Open |
| 41 | Dual pistols: muzzle flashes sit at the back of / above the gun | Open |
| 42 | Right-grip "double jump" has no jump sound on foot | Open |
| 43 | Quicksave misses some levels, and re-saves right after a death quick-load | Open |

## Open items (as of 2026-10-19)

Everything in this file is open - this is the fresh 0.8 list. The four items come straight from the
owner's notes; each records the report, what the code does today and a likely fix direction, to be
confirmed on the next device pass.

---

## 40. Haptics fire once per trigger pull, not while the weapon is firing

- **Status:** Open (reported on device, 2026-10-19)
- **Priority:** Medium (feedback only - shooting itself is unaffected)
- **Observed (device):** the controller buzzes **once** when the trigger is pulled and then goes
  quiet while the weapon keeps firing. It should keep vibrating for as long as a weapon is firing
  (automatic weapons, held fire).
- **Root cause:** the mod arms the pulse on the **fire button's rising edge**, not on the held firing
  state. `game/VRStereo.cpp:1996` tests `m_nPressed & SHOGOVR_BTN_FIRE`, and `m_nPressed` is the
  one-frame edge (`:1951` - `m_nButtons & ~m_nPrevButtons`). So a pull sets `m_fHapticR`
  (`m_fHapticL` when left-handed) to a 4-frame countdown exactly once; nothing re-arms it while the
  trigger stays down.
- **Fix direction:** re-arm from the **held** state instead of the edge - while the fire command is
  on (`VRCommandOn(COMMAND_ID_FIRING)`, or the `nHeld` form at `:2248`), top the countdown back up.
  Two things to settle on device: (a) a held trigger on a **single-shot** weapon fires one shot but
  would then rumble continuously - the ideal is to tie the pulse to an actual **shot**, which the mod
  cannot currently see; if a per-shot signal is wanted, have `CWeaponModel::Fire()` /
  `StartFlash()` call a new mod hook (a ShogoSource edit, so it needs a
  `ShogoSourceModifications/` note); and (b) keep a pulse cadence rather than one long countdown, so
  automatics thump instead of buzzing flat. `VRHaptics` (default 1) stays the master on/off.
- **Refs:** `game/VRStereo.cpp:1989-2004` (the haptics block), `:1951` (`m_nPressed` is the edge),
  `:2248-2251` (`nHeld` + `VRCommandOn(COMMAND_ID_FIRING)`), `:312` and `game/VRStereo.h:536`
  (`VRHaptics`), `game/VRStereo.h:401` (`m_fHapticL` / `m_fHapticR`).

---

## 41. Dual pistols: muzzle flashes sit at the back of / above the gun

- **Status:** Open (reported on device, 2026-10-19)
- **Priority:** Low-Medium (cosmetic, but visible on every pistol shot)
- **Observed (device):** with the on-foot **dual pistols** the muzzle flashes are *almost* right on
  the horizontal axis, but sit at the **back of the gun and above it** instead of **lower and
  forwards**, next to each barrel's muzzle.
- **Root cause:** the #29 visible-muzzle fix deliberately exempts the Colt 45 -
  `CVRStereo::GetMuzzleFlashWorld()` returns `DFALSE` for `GUN_COLT45_ID`
  (`game/VRStereo.cpp:4006-4011`), because the mod helper only knew **one** muzzle offset and pinning
  both alternating flashes to the same barrel looked wrong. With the helper out of the picture the
  pistols fall back to the **game's own** flash position (`m_vFlashDrawPos = m_vFlashPos`,
  `ShogoSource/ClientShellDLL/WeaponModel.cpp:372-375`), which is the **flat-screen** muzzle - a
  camera-relative spot at the old view weapon - so in VR it lands behind and above the hand-held gun.
  The horizontal axis looks close because that flat-screen position is roughly screen-centred while
  the two barrels differ mostly in X (`-6.2` vs `+5.8`; `ShogoSource/Shared/WeaponDefs.h:1130-1141`).
- **Fix direction:** stop exempting the pistols and give the helper the **fired barrel**. The engine
  already tracks both: `CWeaponModel::Fire()` returns `W_FIRED` or `W_FIRED2` depending on which fire
  animation is playing (`ShogoSource/ClientShellDLL/WeaponModel.cpp:747-758`), and
  `GetWeaponMuzzleOffset(id, eState)` already returns a different offset for `W_FIRED` vs `W_FIRED2`
  (`ShogoSource/Shared/WeaponDefs.h:1119-1150`; `GetWeaponMuzzleOffset2` at `:1326-1350` is the
  second-gun variant). So: add the state to the helper's signature, use it in the
  `GetWeaponMuzzleOffset` call (`game/VRStereo.cpp:4062` currently hard-codes `W_FIRED`), drop the
  `GUN_COLT45_ID` early-out, and forward `eState` from the `UpdateFlash(eState)` call site
  (`WeaponModel.cpp:372`) - which is a ShogoSource edit, so it needs a
  `ShogoSourceModifications/ClientShellDLL/WeaponModel.cpp.md` note (check for an existing one from
  #29 and extend it).
- **Refs:** `game/VRStereo.cpp:4002-4077` (`GetMuzzleFlashWorld`; the bail-out at `:4011`, the
  hard-coded `W_FIRED` at `:4062`), `game/VRStereo.h:175`;
  `ShogoSource/ClientShellDLL/WeaponModel.cpp:364-378` (call site), `:513-544` (`UpdateFlash` moves
  the flash object to `m_vFlashDrawPos`), `:715-759` (`Fire()` picks `W_FIRED` / `W_FIRED2`),
  `ShogoSource/Shared/WeaponDefs.h:1119-1150, 1326-1350`; archived: #27, #29, #34.

---

## 42. Right-grip "double jump" has no jump sound on foot

- **Status:** Open (reported on device, 2026-10-19)
- **Priority:** Low (a missing sound; movement still works)
- **Observed (device):** the right grip is the "double jump", which only the MCA can do. On foot,
  pressing it gives a jump but **no jump sound**; the left grip's normal jump does play the sound.
  The owner's suggested fix - make both grips the same jump while on foot - becomes the decision below.
- **Cause (confirmed by the owner, 2026-10-19):** the double jump is a **separate in-game control**,
  and the developers chose that for a **human character** it still performs an ordinary jump but
  deliberately **makes no jump sound** - the sound belongs to the normal jump path. So the right grip
  is faithfully asking for the double-jump control and getting exactly what the game does with it on
  foot: the silence is by design, not a mod or mapping fault. (Mechanically the mod keeps the two
  controls apart - `COMMAND_ID_JUMP` for either grip, `COMMAND_ID_DOUBLEJUMP` for the right only
  (`game/VRStereo.cpp:2252-2253`) - and the shell already downgrades a double jump to `BC_CFLG_JUMP`
  on foot (`ShogoSource/ClientShellDLL/CMoveMgr.cpp:192-203`), which is why the jump itself works.)
- **Decision / fix direction (owner's call):** fix it **mod-side, not in the game's core code** -
  while on foot, make the **right grip also send the regular "jump" input**, so both grips behave
  identically and the jump sound plays. The mod already knows the mode (`m_bMech`, set by
  `SetMechMode` - `game/VRStereo.cpp:3367-3373`, `game/VRStereo.h:450` - from the shell at
  `ShogoSource/ClientShellDLL/RiotClientShell.cpp:5485`), so `VRCommandOn(COMMAND_ID_DOUBLEJUMP)` can
  return `DFALSE` while on foot, folding the right grip into `COMMAND_ID_JUMP` (which already covers
  both bits at `:2252`). Keep the separate double jump in MCA mode. Because this stays in `game/`,
  it needs **no** `ShogoSourceModifications/` note.
- **Refs:** `game/XrApiUDP.cpp:424-441` (`ButtonBits`), `game/ShogoVRShared.h:37,45`,
  `game/VRStereo.cpp:2244-2254` (`VRCommandOn`), `:3367-3373` (`SetMechMode`);
  `ShogoSource/ClientShellDLL/CMoveMgr.cpp:183-204` (the engine-side downgrade - background for the
  explanation, not part of the fix), `ShogoSource/ClientShellDLL/RiotClientShell.cpp:5485`;
  archived: #15 (controller mapping), #32 (jump stopped working).

---

## 43. Quicksave misses some levels, and re-saves right after a death quick-load

- **Status:** Open (reported on device, 2026-10-19). This is the old "autosave family" the owner had
  cancelled on 2026-10-14 (archived #24 / #25 / #33) being picked up again, because the level-start
  save is worth having right.
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
- **Fix direction:** (a) also arm on the bumper transition - or, more robustly, on **any** transition
  into `GS_PLAYING` that is a **fresh** level; and (b) **never** arm on a restore: the load flag is
  available (`LOAD_RESTORE_GAME` = 3 = save / quick-load), so gate the `SetLevelLoaded()` call (or
  the arming inside the mod) on the load not being a restore. Confirm the death quick-load itself
  reads as a restore (it should - it goes through `LoadGame`/`DoLoadWorld`). These are ShogoSource-side
  changes (and/or the mod's arming rule), so they need `ShogoSourceModifications/` notes.
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

## Cross-cutting notes

- **The pre-release history is in `PreReleaseBugs/BUGS_TO_FIX.md`.** Its "Cross-cutting notes" still
  hold - handedness/basis, the input path (`ButtonBits()` vs the on-device bool string), the 2D
  per-eye layout, `VRSyncHold` frame pacing, and the repo policy that every `ShogoSource/` change
  gets a `ShogoSourceModifications/` note plus a `PROJECT_PLAN.md` entry.
- **Input path (this list: #40, #42, #43):** anything driven from the pad starts in
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
