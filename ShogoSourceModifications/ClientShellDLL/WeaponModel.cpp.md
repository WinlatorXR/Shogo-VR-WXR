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
