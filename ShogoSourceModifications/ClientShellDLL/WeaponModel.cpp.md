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
| 3. Muzzle flash follows the gun (#29) | `WeaponModel.h` line 112, ctor line 54, `UpdateWeaponModel` line 361, `UpdateFlash` line 529 |

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

## 3. Muzzle flash follows the visible gun (#29)

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
if (!g_VRStereo.GetMuzzleFlashWorld(m_nWeaponId, vPos, bVRFootScale, m_vFlashDrawPos))
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
- `GetMuzzleFlashWorld()` returns `DFALSE` when VR is not posing the gun by hand
  (head aim, or non-VR), so `m_vFlashDrawPos` falls back to `m_vFlashPos` and the
  flat game is unchanged.
