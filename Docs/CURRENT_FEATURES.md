# Current Features

This document is features we want to implement for the current release, as future features are started they will be moved into here


## #5 - Dual pistols as two-handed weapons

### Goal
Modify dual pistols so that each pistol is held in its own hand (one per hand) rather than both on the dominant hand, while preserving the original Colt45 art. Support independent firing per hand, correct muzzle flashes per hand, and independent cooldowns.

### Affected files
- `game/VRStereo.h` / `game/VRStereo.cpp` - dual-pistol state, per-hand aiming/fire direction, second held gun, hand-aware muzzle flash, raw pose/trigger access
- `game/VRMenu.h` / `game/VRMenu.cpp` - optional toggle if needed (or auto-detect by weapon)
- `ShogoSource/ClientShellDLL/WeaponModel.cpp` - firing, animation keys, `SendFireMsg()`, muzzle flash handling
- `ShogoSource/ClientShellDLL/RiotClientShell.cpp` - update order, `UpdateWeaponModel()`, held gun updates
- `ShogoSource/ObjectDLL/RiotWeapons.cpp` - Colt45 weapon definitions (verify)
- `ShogoSource/ObjectDLL/PlayerObj.cpp` - server firing (`HandleWeaponFireMessage()`) - already accepts per-message origin/direction
- `ShogoSource/Shared/WeaponDefs.h` - weapon IDs, two-handed/melee checks

### Approach
1. **Held gun representation**: `UpdateHeldGun()` currently creates only one `m_hHeldGun`. Extend to support a second held gun object for dual pistols (one per hand). Reuse original Colt45 art for both copies.
2. **Per-hand poses/triggers (raw)**: Use raw pre-swap hand poses and raw trigger buttons (do not rely on `m_nRawButtons` being set after swap - store pre-swap copies). Left trigger should fire left pistol, right trigger fire right pistol in this mode.
3. **Aiming/fire direction**: `UpdateTwoHanded()` currently derives `m_vAimF` from the gun hand. For dual pistols, do **not** use two-handed support - use each hand's own forward direction for that hand's pistol. Each pistol needs independent fire direction.
4. **Firing/cooldown**: Each pistol needs its own cooldown before being able to fire again. Track per-hand fire timers/state. Fire events must originate from the correct hand with correct origin/direction.
5. **Muzzle flashes**: `GetMuzzleFlashWorld()` uses `W_FIRED`/`W_FIRED2` and `VRPistolSpread`. Extend to be hand-aware and support placement for both pistols (left and right). Update `UpdateHeldGun()` and flash placement to stay in sync.
6. **Network/messages**: `CWeaponModel::SendFireMsg()` currently sends one origin/direction based on weapon object pose. Server already accepts per-message origin/direction, so send separate messages per shot (or ensure correct per-hand origin/direction). Handle alternating barrels/flash states appropriately.

### Settings
- Possibly `Game/DualPistolsPerHand` (auto-enable when dual pistols equipped, or toggle). Auto-detect is preferred to avoid extra UI complexity.

### Edge cases / risks
- **Art reuse**: Must preserve original Colt45 art (two held-gun copies).
- **Handedness**: Works with `LeftHanded`; must use raw poses/triggers.
- **Cooldown independence**: Firing left shouldn't block right except weapon-level rules if any.
- **Muzzle flash sync**: Flash must follow correct hand and correct barrel/flash state.
- **Animation**: `CWeaponModel` animation keys and firing logic may assume single gun - need to adapt carefully.
- **Two-handed conflicts**: Disable/avoid two-handed aim logic while in dual-pistols-per-hand mode.

### Acceptance criteria
- [ ] When dual pistols equipped, one pistol appears in left hand and one in right hand (original art).
- [ ] Left trigger fires left pistol only, right trigger fires right pistol only.
- [ ] Each pistol has independent cooldown.
- [ ] Muzzle flashes appear at correct muzzles for each hand.
- [ ] Fire direction is correct per hand (no shared two-handed aim).
- [ ] Works correctly in right-handed and left-handed modes.


## #6 - Display upscaling

### Goal
Provide optional display upscaling (e.g. render at higher resolution and scale down for display) to improve visual quality, without making it a fixed default. Prefer leveraging existing renderer/supersampling approaches (e.g. dgVoodoo render-scale) with runtime/headset downscale handling.

### Affected files
- `launcher-xr/ShogoVR.cpp` - `BuildArgs()` resolution passing
- `launcher/` (reference) - dgVoodoo `[DirectX] Resolution = 2x` and `launcher/Common.h::SetDgVoodooDirectX`
- `game/VRStereo.h` / `game/VRStereo.cpp` - render resolution tracking, `CheckRenderer()`, `FitVRWindow()`, `FitGameWindow()`, interaction with `VRCapture.cpp`
- `game/VRCapture.cpp` - captures D3D11 backbuffer to shared texture and publishes dimensions
- `game/VRMenu.h` / `game/VRMenu.cpp` - optional upscaling setting
- dgVoodoo configuration files (launcher-xr integration)

### Approach
1. **Research integration**: Upstream `launcher/` has dgVoodoo `[DirectX] Resolution = 2x` and `SetDgVoodooDirectX`. `launcher-xr` currently has no equivalent. Need to add support for configuring dgVoodoo render-scale in launcher-xr similar to launcher.
2. **Resolution passing**: `BuildArgs()` currently passes desktop resolution via `+screenwidth/+screenheight`. For upscaling, we need to decide whether to:
   - Pass higher render resolution to engine and let renderer/dgVoodoo handle scaling, or
   - Use dgVoodoo's resolution scaling (e.g. 2x) and pass appropriate dimensions.
   Need to coordinate with dgVoodoo config.
3. **Runtime handling**: `VRCapture.cpp` copies actual D3D11 backbuffer to shared texture and publishes dimensions. `FitVRWindow()`/`CheckRenderer()` assume render size and window size normally match. With upscaling, render resolution differs from display/window size - need to ensure capture, window fitting, and VR display still work correctly.
4. **Setting**: Add optional setting in VR menu (e.g. "Display upscaling" with options Off/1.5x/2x or similar) or auto-detect. Make it optional and not a fixed default.
5. **Headset downscale**: Ensure the upscaled render is properly downscaled to the headset/display without breaking aspect ratio or capture sync.

### Settings
- `Game/DisplayUpscale` or similar (Off by default). Values could map to dgVoodoo resolution scaling factors.

### Edge cases / risks
- **Launcher integration**: Need to modify launcher-xr to write dgVoodoo config like launcher does.
- **Capture dimensions**: `VRCapture` must use actual backbuffer dimensions correctly; window fitting logic may need adjustment.
- **Performance**: Upscaling has GPU cost - must remain optional.
- **Compatibility**: Different headsets/displays may need different settings.
- **Renderer assumptions**: `CheckRenderer()` may reset if sizes don't match expectations - need to handle upscaled render size.

### Acceptance criteria
- [ ] Upscaling can be enabled/disabled via setting (off by default).
- [ ] When enabled, scene is rendered at higher resolution and scaled down correctly to display/headset.
- [ ] Window fitting, capture to shared texture, and VR display remain correct.
- [ ] No visual artifacts, aspect ratio preserved.
- [ ] Performance impact is acceptable when enabled.
