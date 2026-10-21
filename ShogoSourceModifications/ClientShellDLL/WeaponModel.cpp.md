# ShogoSource/ClientShellDLL/WeaponModel.cpp

**Goal:** let the VR module reposition and rescale the first-person weapon so it
sits in the tracked hand, then apply the final transform (the original code only
set the object's position).

Code blocks show only the mod's own added lines.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/WeaponModel.cpp`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Include                       | after line 20 (`#include "ClientServerShared.h"`) |
| 2. `UpdateWeaponModel` (commit)  | function starts line 238; final commit at line 353 |
| 3. Muzzle flash follows the gun (#29, #41) | `WeaponModel.h` line 112, ctor line 54, `UpdateWeaponModel` line 361, `UpdateFlash` line 529 |

| 4. Shot haptics (#40)            | `CWeaponModel::Fire()` line 715, after `GetAmmoCount` at line 724 |

---

## 1. Include

**Part of:** the include block at the top of the file (pristine lines 1–20).

Add, next to the other game includes (for example after the `ClientServerShared.h`
include on line 20):

```cpp
#include "VRStereo.h"
```

---

## 2. `CWeaponModel::UpdateWeaponModel()`

**Part of:** `CWeaponModel::UpdateWeaponModel(DRotation rRot, DVector vPos, DBOOL bFire)`
(pristine function starts line 238).

At the point where the function currently commits the final weapon position — the
position-setting `SetObjectPos` call at pristine line 353 — replace that
single position-setting call with a VR pose/scale adjustment followed by a call
that sets **both** position and rotation (so the VR-applied rotation is not
discarded). Do not confuse it with the earlier `SetObjectPosAndRotation` at line
258, which stays as-is.

Our replacement code:

```cpp
// Let the VR module put the weapon in the tracked hand (no-op without VR)...
DBOOL bVRFootScale = (DBOOL)(!pSettings->VehicleMode());
g_VRStereo.AdjustWeaponPose(vNewPos, rRot, bVRFootScale);
g_VRStereo.ScaleWeapon(m_hObject, bVRFootScale);

m_pClientDE->SetObjectPosAndRotation(m_hObject, &vNewPos, &rRot);
```

Notes:
- `pSettings` is the settings pointer already available in this function.
- The former call set only the position; the new call must set position **and**
  rotation.

---

## 3. Muzzle flash follows the visible gun (#29, #41)

**Part of:** the same two functions, plus a new member in
`ShogoSource/ClientShellDLL/WeaponModel.h`.

The game positions the muzzle-flash sprite from the *flat-screen* muzzle —
`m_vFlashPos`, near the camera — but VR holds the gun in the hand, so the sprite
ended up above and behind the barrel. `m_vFlashPos` is also the **shot origin**
(sent in `MID_WEAPON_FIRE`), so it must not move: add a second vector for the
**drawn** position only.

**Header (`WeaponModel.h`),** next to `m_vFlashPos` (pristine line 112):

```cpp
DVector		m_vFlashDrawPos;	// where the flash sprite is drawn (VR: the visible muzzle)
```

**Constructor (`CWeaponModel::CWeaponModel`,** next to `VEC_INIT(m_vFlashPos)` at
pristine line 54):

```cpp
VEC_INIT(m_vFlashDrawPos);
```

**`UpdateWeaponModel()`** — in the "Update the muzzle flash..." block (pristine
line 361), before the existing `UpdateFlash(eState)` call, ask the mod where the
visible barrel ends; keep `m_vFlashPos` as the shot origin:

```cpp
if (!g_VRStereo.GetMuzzleFlashWorld(m_nWeaponId, vPos, bVRFootScale, m_vFlashDrawPos, eState))
{
	VEC_COPY(m_vFlashDrawPos, m_vFlashPos);
}
```

**`UpdateFlash()`** — the `SetObjectPos(m_hFlashObject, &m_vFlashPos)` call
(pristine line 529) now uses the drawn position:

```cpp
m_pClientDE->SetObjectPos(m_hFlashObject, &m_vFlashDrawPos);
```

Notes:
- `vPos` here is the camera position (unchanged by `AdjustWeaponPose`, which works
  on `vNewPos`), the same value passed to `g_VRStereo.UpdateHeldGun()`.
- `eState` is the weapon state the flash is for: `W_FIRED` or `W_FIRED2` depending
  on which of the dual pistols' barrels is firing (`CWeaponModel::Fire()` returns
  it). Passing it through lets the mod use the matching muzzle offset, so the
  on-foot **Colt 45** gets each alternating flash on its own barrel (#41). Every
  other weapon only ever uses `W_FIRED`, so its result is unchanged.
- The mod **corrects only the offset's forward/back and up/down components** for the held gun
  (`z += VRFlashOffset` out past the muzzle, `y = -VRFlashLower` below the barrel); the table's own
  value drives the rest. This is what fixed the original "behind and above the gun" complaint.
  Defaults: `VRFlashOffset` **2.0** (out) and `VRFlashLower` **2.4** (down) - trimmed from 3.0 once
  a device pass confirmed the sprite was in front of the barrel but a touch too far out and too low.
  **These corrections apply in BOTH branches of `GetMuzzleFlashWorld()`** — the mod's third-person
  held gun (`VRGunModel=1`) *and* the first-person model with arms (`VRGunModel=0`, the **default**).
  A device log showed `VRGunModel` is `0` on the headset, and the corrections used to sit inside
  `if (bHeld)`, so they never ran: the flash kept the raw table offset. Do not re-gate them.
- **Left/right is per weapon.** The dual pistols (Colt 45) are the only two-barrel weapon and keep
  the table's left/right value — that is the game's own default and it puts barrel 1 on the left and
  barrel 2 on the right, so **each hand gets its own flash, matching which barrel fires**
  (`W_FIRED` vs `W_FIRED2`, passed through from here). That spread is then **scaled about the
  centre** by `VRPistolSpread` (default `0.63`) — on device the mod's held model put the two flashes
  a little too far apart, so `-6.2`/`+5.8` become `-3.91`/`+3.65` (span `12.0 -> 7.56`) while the
  barrels stay on opposite sides; `1.0` reproduces the game's own spread. For every other
  (single-barrel) weapon the table's left/right was only barrel-selection noise, so it is replaced
  by one constant (`VRMuzzleLateral`, default `0`, i.e. on the barrel axis). All four values
  (`VRFlashOffset`, `VRFlashLower`, `VRMuzzleLateral`, `VRPistolSpread`) are live CVars, and
  `GetMuzzleFlashWorld()` logs the applied x/y/z offset and scale per weapon (the pistols' line is
  tagged `[pistols: spread-scaled, first-person]` or `..., held]`) so they can be tuned on-device.
  The pistols are **never** given the same x for both barrels — collapsing them onto one spot was
  the original bug.
- `GetMuzzleFlashWorld()` returns `DFALSE` when VR is not posing the gun by hand
  (head aim, or non-VR), so `m_vFlashDrawPos` falls back to `m_vFlashPos` and the
  flat game is unchanged.

---

## 4. Shot haptics (#40)

**Part of:** `CWeaponModel::Fire()` (pristine function starts line 715), right
after the ammo count is read (pristine line 724) and **before** the
ammo/`W_FIRING_NOAMMO` branches.

```cpp
// Haptics (#40): this is the one place a shot is really fired - the ammo
// count is decremented right here and nowhere else - so it is the only
// place that can tell "held the trigger" apart from "actually fired".  The
// mod buzzes the gun hand in step with the counter (a dry click buzzes
// too, but a weapon switch and an idle frame do not).
g_VRStereo.ShotFired();
```

Notes:
- The device pass on the first attempt reported continuous vibration while the
  trigger was held and an unwanted buzz on weapon switch. Tying the pulse to this
  single function fixes both: it runs exactly once per shot, so a slow weapon, a
  shotgun or the railgun gives one kick per shot, and nothing at all happens on a
  switch or an idle frame.
- The mod owns the intensity and duration (`CVRStereo::ShotFired`, which also
  respects the `VRHaptics` setting and picks the gun hand when `LeftHanded` is on),
  so nothing else in this file needs to change.
- Called before the branches, so an empty clip (the dry click) also buzzes — that
  matches "the ammo counter is what drives the vibration".

