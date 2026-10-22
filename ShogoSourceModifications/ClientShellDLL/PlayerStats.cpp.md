# ShogoSource/ClientShellDLL/PlayerStats.cpp

**Goal:** let the VR module hide (or place) the game's own first-person
crosshair, so it can be turned off by default and, when enabled, drawn at the
aimed-at depth instead of the flat HUD plane.

**Why this matters:** the crosshair is blitted as ordinary 2D content
(`DrawSurfaceToSurfaceTransparent` on the screen surface). The VR module maps
2D blits either onto the flat HUD plane or, for the crosshair, onto a plane at
the aim distance (the `m_xfCross` transform). It knows which is which only
while a marker is set, and it must also be able to drop the blit entirely when
the "game crosshair" setting is off. Both are exposed through a begin/end pair
on `g_VRStereo` that simply brackets the crosshair's two draw sites.

All edits are additions. Line numbers below refer to the pristine
`shogosrc/ClientShellDLL/PlayerStats.cpp`; your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Include `VRStereo.h` | the include block at the top of the file |
| 2. `DrawOnFoot` crosshair markers | the on-foot crosshair blit |
| 3. `DrawEnforcer` crosshair markers | the Enforcer (mech) crosshair blits |

---

## 1. Include `VRStereo.h`

**Part of:** the include block at the top of the file.

Add, beside the other includes:

```cpp
#include "VRStereo.h"
```

---

## 2. `CPlayerStats::DrawOnFoot()` — mark the crosshair pass

**Part of:** `CPlayerStats::DrawOnFoot(...)`, the block that draws the
single on-foot crosshair (the one guarded by the crosshair-enabled and
crosshair-level checks).

Immediately **before** that crosshair `if` block, add (our code):

```cpp
g_VRStereo.BeginCrosshair();
```

and immediately **after** it add (our code):

```cpp
g_VRStereo.EndCrosshair();
```

Leave the existing drawing calls between the two markers exactly as they are.

---

## 3. `CPlayerStats::DrawEnforcer()` — mark the crosshair pass

**Part of:** `CPlayerStats::DrawEnforcer(...)`, the block that draws the
Enforcer crosshair levels (the left/middle/right pieces and the zoomed
crosshair).

Bracket that block the same way as edit 2: add `g_VRStereo.BeginCrosshair();`
just before it and `g_VRStereo.EndCrosshair();` just after it. Leave the
existing drawing calls in between unchanged.

---

## Notes

- `BeginCrosshair()` / `EndCrosshair()` are inline setters on `CVRStereo`
  (`game/VRStereo.h`). With the marker set, the VR module's 2D remap uses the
  aim-distance transform and can skip the blit entirely when the "game
  crosshair" setting (`VRGameCrosshair`, default off) is 0 - see
  `Docs/Pre-LaunchDocs/BUGS_TO_FIX.md` #14.
- No crosshair-drawing code is changed, only bracketed, so behaviour with stereo
  off is identical.
