# Complete Features

Features from `CURRENT_FEATURES.md` that are implemented and shipped, most recent first.

## #8 - Two-handed grip for on-foot weapons

Completed 28 Oct 2026 (in code; awaiting a device pass).

Build: `CShell.dll built Oct 28 2026 22:19:09`, SHA-256 `A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`.

Reach your off hand out along the barrel and pull its trigger to take hold of a
two-handed weapon with both hands; pull it again to let go. While held, the gun
aims along the line between your hands and firing is felt in both controllers.

Covers the **shotgun, submachine gun, assault rifle, both grenade launchers and
the TOW**. The Colt 45 is not one of these - it is two separate guns, one per
hand (#5) - so the off trigger goes on firing the off pistol exactly as before
and the two features never overlap. Mech weapons are out of scope.

### One hand or two

- **One-handed (default):** the weapon's own off hand is taken off the gun and
  put on the off controller, which is #7 generalised from the submachine gun to
  all six weapons. No borrowed hand is needed - every one of them carries an
  off-hand group in its own model, so each uses its own art.
- **Two-handed:** that split stands down, so the model draws both hands on the
  gun itself, which is what being braced should look like.

### Taking hold

The gate is the geometry the older proximity two-handed aim already used -
hands 0.12 to `VRGripDist` metres apart, off hand within `VRGripAngle` of the
barrel - so it feels as it always did; it just no longer happens by accident.

**Letting go never needs the gesture.** Only taking hold does, so a grip can
always be dropped however the hands happen to be held. It is also dropped
automatically when the weapon is put away or a controller stops tracking, so it
can never be left stuck on.

Aim while held ignores those thresholds entirely: the player has said they are
holding the gun, so it must not slip out of two-handed aim because they moved
their hands a little.

### Sharing the off trigger

- **Quick load while dead keeps precedence** over everything else on this
  button. Losing a save reload to a grip toggle would be a bad trade.
- The dual pistols are not a conflict: the Colt 45 is never a two-handed weapon,
  so the toggle never applies while it is equipped.
- Suppression applies, so the trigger that dismissed a menu does not also change
  the grip on the way back in.

### The hand tables

The two-handed weapons use **two** naming conventions and both are tabulated:
15, 16, 17 and 19 use `palm`/`palm3`, while 14 and 18 use the
`Right_Palm`/`R_Arm` form from #7 (hyphen typo and mixed suffixes included).

These tables carry **only hands, no gun parts** - which matters, because
weapons 15, 16, 17 and 19 have gun parts named `cube1`, `cyl1` and `extru1`,
and those are also Colt 45 *pistol* parts. Reusing #5's tables here would have
classified half a rifle as a hand.

### Settings

`VRTwoHandGrip` (on), `VRGripDist`, `VRGripAngle`, plus `VROffHandSwap` and the
`VROffHand*` trims shared with #7.

All three are rows on the in-game **VR Advanced** page
(*Options -> vr settings -> Advanced settings*) as "Two-handed grip", "Grip
reach" and "Grip angle", so the gate can be tuned in the headset without
leaving the game.

### Still to verify on device

- Taking hold and letting go, and that the gate feels right.
- Both hands appear on the gun when held, one when not.
- Aim follows the line between the hands; both controllers buzz on firing.
- Quick load still works when dead; the off pistol still fires with the Colt 45.
- Reloading plays in full and the grip survives it.
- Both handedness modes; mech weapons untouched.


## #7 - Free off arm on the submachine gun

Completed 28 Oct 2026 (in code; awaiting a device pass).

Build: `CShell.dll built Oct 28 2026 22:19:09`, SHA-256 `A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`.

The submachine gun's first-person model carries a **second arm that holds
nothing**: the gun sits in the dominant hand and the spare arm goes wherever the
animation puts it, unattached to the player's real off controller. That arm now
follows the off controller instead.

**Visual only.** Nothing here touches firing, rate of fire, ammo, aim, the muzzle
flash or anything the server sees, and - unlike the dual pistols (#5) - it needs
**no `ShogoSource/` edit at all**. The gun stays whole in the dominant hand.

### The model

- Weapon 18 (`GUN_MAC10_ID`) is 50 parts in three blocks: 17 for the arm holding
  the gun, 15 for the gun, 17 for the free arm. Only the free arm is moved; the
  gun block is never touched.
- The arm is a named node (`R_Arm`, `R_Arm3`), so the whole group *can* be
  moved - but the **forearm is not drawn by default** (`VRHandArm`). It is
  modelled running back to the shoulder, and once the hand is pinned to a
  controller the arm points wherever that hand is rotated instead of back
  towards the player: on device it read as a large slab hanging in mid-air.
  Nothing can aim it, because the engine has no way to write a node's
  transform, so the hand is drawn alone. `VRHandArm 1` restores the forearm.
- The node names are listed verbatim, with three traps that would each break a
  rule-based match: `R-thumb_middle` uses a **hyphen** where every sibling uses
  an underscore (an art typo, in both arms); the second group mixes suffixes
  (`R_Arm3` but `Right_Palm4` and `4` on every finger), so a single-suffix rule
  finds the fingers and loses the arm; and **both** arms are named `R_`/`Right_`,
  so which one is free cannot be read from the names at all - hence
  `VROffHandSwap`, which only the headset can settle.
- Falls back to the stock single model when a group does not carry at least 12
  recognised parts, so replacement art cannot cost the player an arm.

### The reload takes the hand back

The arm is free only while the gun is idle or firing. **Reloading drives that
same hand to the magazine well to seat the clip**, and select/deselect swing the
whole model - so hand control is handed back for the duration and taken again
afterwards. This is wanted behaviour, accepted by the owner, not a defect.

`CWeaponModel` looks its animations up by name, so the mod resolves the same
names once per model (`GetAnimIndex`) and compares each frame against
`GetModelAnimation`. `Idle_1`, `Idle_2`, `Fire`, `Fire2`, `Start_fire`,
`End_fire` and `Fire_zoom` leave the arm free; **anything not on that list counts
as busy**, so an unaccounted-for animation degrades to stock behaviour rather
than to an arm torn out of it.

There is a visible pop as control changes hands. The engine exposes no
frame-level animation control, so there is no way to blend.

### Shared with the dual pistols

- The same mod-owned copy object serves both features - they are different
  weapons, so they can never both want it - and the same lessons apply: the copy
  **follows the game's animation** (nothing else drives it, and a free-running
  copy tumbles), and it is **anchored** by its own palm node so the hand sits on
  the controller whatever the animation does.
- Both pistols are measured from the **real camera**. `AdjustWeaponPose` is
  handed the camera plus the weapon's placement and bob, and those offsets are
  built from the gun hand's rotation - so that origin feeds the gun hand's
  movement into every hand measured from it. It cost two faults on device: the
  pair sat a constant few units apart (only one of them used that origin), and
  then, once both did, the off-hand pistol was visibly dragged about by the gun
  hand. Taking the camera from the renderer and measuring every hand from it
  fixes both, and drops the weapon bob, which in VR should come from the
  player's hand and nothing else.
- Stands down while the pistols are split, while `VRGunModel=1` (the
  third-person gun has no second arm), and while `VRGunArms` is hiding the arms
  anyway.

### Borrowed hands for weapons that are missing one

The same donor idea solves an older, wider gap: three on-foot weapons are drawn
with only one hand, so in VR the other controller has nothing on it.

| weapon | drawn | missing | borrowed |
|---|---|---|---|
| Shotgun (14) | the *fore* hand, on the pump | the hand on the rear grip | group 0, closed round a grip |
| Tanto (22) | the knife hand | the off hand | group 1, open |
| Squeaky toy (21) | the hand holding it | the off hand | group 1, open |

Each is lent a hand from the submachine gun's model, which carries both a hand
closed round a grip and an open one, posed to its controller from the same
origin and scale as the weapon itself so the two move together. The weapons'
own art is untouched.

The list is deliberately explicit rather than "any weapon with one hand":
*which* hand a model is missing cannot be read from the node names - both are
called `R_`/`Right_` - so each weapon is listed only after being looked at. The
shotgun is the case that proves the point, since the one hand it draws is the
off hand, not the dominant one.

Because the donor comes from a *different* model there is no animation of the
equipped weapon to follow, so it is pinned to a single looping idle at creation
and left there. A copy left to free-run tumbles, which is what #5's first device
run showed.

`VRDomHand` and `VRFreeHand` turn them off; `VRDomHandX/Y/Z`, `VRDomHandRoll`,
`VRFreeHandX/Y/Z` and `VRFreeHandRoll` seat them.

### Which hand stays on the gun is measured, not configured

The part names cannot say which hand is the trigger hand, and the device log
settled why: the assault-rifle family carries `palm`/`palm3` and the
submachine gun carries `Right_Palm`/`Right_Palm4`, and **the two families order
their hands oppositely**. One global `VROffHandSwap` cannot satisfy both - it
held the SMG correctly while leaving the rifle gripped by its fore-end with
nothing on the trigger.

So the mod measures it. Both palms are read off the posed model and projected
onto the gun's own forward axis; the one further along the barrel is the fore
hand and goes to the off controller, and whatever is left is the trigger hand
and stays on the gun. No table, no per-weapon default, and it holds for models
this mod has never seen. `VROffHandAuto` (on) controls it; `VROffHandSwap` is
the manual override for when the measurement cannot decide - a weapon with only
one hand group, such as the shotgun.

### The rifle family's forearms

The hand tables stop at the wrist, so moving a hand to the off controller left
its forearm behind on the gun, attached to nothing - visible on the assault
rifle, both grenade launchers and the TOW. Those forearms are `tetra1` +
`cube1_3` for the first hand and `tetra3` + `cube1_4` for the second, and the
trap is that **the TOW pairs `tetra4`/`tetra5` the opposite way round** from the
other three, so they cannot be assigned per hand by name. Since forearms are
hidden by default anyway (`VRHandArm`), all six are hidden together while the
split is active. Taking the grip stands the split down, so the model draws its
own arms again and they return - which is the wanted behaviour: an arm only
while two-handed.

### The lent hands come from the Colt 45

The engine cannot mirror a model, so a right hand stays a right hand however it
is posed - a left-handed player needs a genuine left hand to exist in some
model's art. The submachine gun was tried first and does carry two hands, but
they are a gripping hand and an **open** one (it holds a magazine), so lending
the second to a left-handed player put a flat, open hand on the gun.

The Colt 45 is the one on-foot model with a mirrored **pair** of hands both
closed round a pistol grip - which is the whole point of #5 - and a rear grip
is exactly what that shape wants, in either handedness. Half B is the right
hand, by #5's settled `VRPistolSwapHalves=1`; `VRLentHandSwap` flips the pair.

#### Seating it took three attempts, and the first two failed the same way

The offset was applied along the **hand's** axes, which turn with the wrist, so
numbers found against the desktop tester's synthetic poses landed somewhere
else once a real wrist held the gun at a real angle - on device the hand was
thrown clear of the weapon entirely. Two things made that worse:

- The test packets' quaternions **were not unit length** (the left controller's
  was 4.7% long, the right's 1.4%), and `QRotate` is a pure rotation only for a
  unit quaternion - fed a longer one it rotates *and* scales by `|q|²`. The hand
  bases were therefore stretched, by a different amount per hand. They are
  normalised on parse now, which also guards against a real runtime's
  quaternions drifting with float error over a session.
- The trims were one shared set of CVars across both handedness modes, so
  tuning one mode silently overwrote the other.

The hand is seated **in the gun's frame** now (`PoseLentHandOnGun`): same
origin, orientation and scale as the weapon, offset along the weapon's own
axes. A hand on a grip does not move when the wrist turns - the whole weapon
turns with it - so the offset is a property of the model and transfers between
desktop and device. The hand also rolls with the gun rather than the wrist,
which is how a hand actually holds a shotgun.

Seated at **right `5.6 / -5.4 / -3.0`**, **left `-7.5 / -4.2 / -2.2`**.

**Open question.** In the gun's frame both modes should want the *same* offset,
and they do not. The gun's basis logs clean - orthonormal to 2e-4 and
right-handed, `F = R x U` - so the frame itself is sound. The left-handed basis
has not been captured yet; equipping the shotgun in each mode and comparing the
two logged lines is what would settle it. Until then the two sets are two
measurements that work rather than one rule that explains them.

### Settings

`VRSplitOffHand` (on), `VROffHandAuto` (on), `VROffHandSwap`, `VROffHandAnchor` (on),
`VROffHandX/Y/Z`, `VROffHandRoll`, plus `VRDomHand`, `VRDomHandX/Y/Z`,
`VRDomHandRoll`, `VRFreeHand`, `VRFreeHandX/Y/Z` and `VRFreeHandRoll` for the
borrowed hands - all live, tunable on device without a
rebuild.

Everything but the X/Y/Z seating trims is a row on the in-game **VR Advanced**
page (*Options -> vr settings -> Advanced settings*), so the choices that
actually had to be found in the headset - which arm is the free one, whether
the anchor drift is wanted, the roll, whether a hand is lent at all - can be
changed there. The X/Y/Z trims stay ini-only: they are millimetre work that
nobody wants to do on a thumbstick.

### Still to verify on device

- The free arm follows the off controller, and it is the correct arm
  (`VROffHandSwap` if not).
- The reload plays in full with the hand seating the magazine, and the arm
  returns afterwards.
- Seating and roll of the arm in the hand (`VROffHandX/Y/Z`, `VROffHandRoll`).
- Both handedness modes; every other weapon unaffected.


## #5 - Dual pistols as two-handed weapons

Completed 28 Oct 2026 - **verified on device by the owner**.

A first run in the headset showed the dual-wield itself working - two pistols,
one per controller, each trigger firing its own - with two faults, both now
fixed: the off-hand pistol rolled and tumbled continuously, and the gun-hand
pistol swung round to point at whatever the off hand had just shot. See
*Driving the copy* and *The aim* below.

Build: `CShell.dll built Oct 28 2026 22:19:09`, SHA-256 `A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`.

### The models

- The Colt 45's first-person model is **one** object holding both pistols and
  both of Sanjuro's hands as two mirrored halves, so a second copy of it would
  show four pistols. The halves are split by part name instead: one stays on
  the game's own model (which the mod already poses to the gun hand) and the
  other is shown on a single mod-owned copy posed to the off hand. The art is
  the game's own, unmodified, from `GetPVModelName`/`GetPVModelSkin`.
- **Shogo animates the pistols heavily, and the animation carries them a long
  way from the model's origin** - so simply placing each object at a controller
  leaves the visible pistol somewhere else entirely. Each copy is therefore
  placed by *anchoring*: the mod reads where that half's `palm` node has
  actually ended up (`GetModelNodeTransform`) and shifts the whole object by
  the opposite amount, so the grip stays in the player's hand through the whole
  animation. There is no node-transform *write* in the engine's interface, so
  cancelling the animation out at the object is the only way to do it.
  `VRPistolAnchor` turns it off.
- The off-hand half is a **mirrored** copy of the gun-hand half inside the one
  model, which raised the question of whether it needs the opposite roll to the
  gun hand. It does not: device testing settled it - the mirrored geometry and
  the mirrored hand pose already cancel out, so the gun hand's own correction
  (`VRHandFlip`, applied in `GetHandWorldFrom` to undo the OpenXR-to-LithTech
  handedness flip) is right for both halves. Applying a second flip is what put
  the off-hand pistol upside down. `VRPistolMirror` keeps that extra 180-degree
  roll available but **defaults to off**; `VRPistolOffRoll` is the fine trim.

### Driving the copy

The off-hand pistol is a separate object, so **nothing in the game drives its
animation**. Left alone it free-ran on whatever animation it was created on
while the game played the real weapon's idle, fire and reload animations on its
own model - and because Shogo swings the pistols a long way in those animations,
an unsynchronised copy reads as the off-hand pistol tumbling and rolling on its
own, never settling. That was the most visible fault in the first device run.

The copy now follows the game's model: every frame it reads
`GetModelAnimation`/`GetModelLooping` off the real weapon and, whenever the
animation changes, sets the same one on the copy and restarts it. The interface
has no frame-level animation control - there is no `SetModelPlaybackState` - so
restarting on change is what keeps the two in phase; both then tick at the same
rate from the same frame. The alternating fire animations change index on every
shot, so firing resynchronises them for free, and `ShotFired` forces a restart
as well to cover the case where the same fire animation replays twice running.

### The aim

Two-handed aim turns itself off while the pair is split - bracing one gun with
the other hand is exactly what you are not doing with a pistol in each hand.

Each pistol is posed from **its own hand's** direction, and in particular the
gun-hand model no longer follows `m_vAimF`. That matters because `m_vAimF` also
steers the player's view: taking it for the model meant the gun-hand pistol
rotated to wherever the view had swung, which in the headset looked like the
right pistol being yanked off its controller to aim at whatever the left pistol
had just shot. The shot direction already comes from the firing hand
(`ApplyPistolShotPose`), so nothing was gained by it.

`VRPistolAimSnap` - easing the player's *view* across to whichever pistol last
fired - therefore **defaults to off** and should stay off. It is kept only as an
opt-in for anyone who wants it.

### The firing

- **One shared cadence, not two.** The game's own fire animation remains the
  only thing deciding *when* a shot happens, exactly as before - so the pair
  fires at the stock rate between them, the server sees the stock stream of
  `MID_WEAPON_FIRE` messages, and ammo drains at the stock rate. The mod only
  decides *which* pistol owns each shot. Nothing in `CWeaponModel`'s firing
  state machine is overridden.
- Only one pistol fires at a time. With both triggers held the two
  **alternate**, so the dominant hand fires first and the other follows one
  cadence later. With one trigger held, that pistol takes every shot.
- **Nothing is queued.** The triggers are read at the instant the shot happens
  and nowhere earlier, so a pull that begins and ends inside a pistol's
  cooldown is never seen: to fire that pistol again you have to be holding its
  trigger when the next shot comes round.
- Each shot is sent from the pistol that fired - direction down that hand,
  origin at that pistol's muzzle (`VRPistolShotOrigin`, default on). The server
  already honoured a per-message origin and direction, so nothing there
  changed.
- The muzzle flash is drawn at the firing pistol's own muzzle. It no longer
  follows `W_FIRED`/`W_FIRED2`, which track the fire *animation* rather than
  the player's hands. `VRPistolSpread` now applies only to the stock pair.
- The shot is assigned to a hand inside `CVRStereo::ShotFired()`, the one call
  that happens exactly once per shot. It has to be there and not in
  `ApplyPistolShotPose`, because `Fire()` runs before the block that sends the
  shot - deciding later would buzz the hand that fired the *previous* shot.
- Haptics buzz the hand that actually fired.

### Behaviour and limits

- Works in right- and left-handed modes with no extra bookkeeping: the triggers
  are read after the existing handedness swap, so whichever trigger is pulled,
  the pistol the player can see in that hand is the one that fires.
- Falls back to the stock pair when `VRGunModel=1` (the third-person held gun
  is a single pickup model with nothing to split) and when the model does not
  carry the expected part names, so a mod replacing `Colt45_pv.abc` is safe.
- In third person the server replaces the shot origin with
  `HandHeldWeaponFirePos()`, so only the direction stays per-hand there.
- Tunable on device without a rebuild: `VRDualPistols`, `VRPistolSwapHalves`,
  `VRPistolMirror`, `VRPistolAnchor`, `VRPistolAimSnap`, `VRPistolOffX/Y/Z`,
  `VRPistolOffRoll`, `VRPistolShotOrigin`. Every one of them is a row on the
  in-game **VR Advanced** page (*Options -> vr settings -> Advanced settings*),
  which is how the two that had to be settled in the headset -
  `VRPistolSwapHalves` and `VRPistolMirror` - should be tried in future.
- `VRPistolSwapHalves` **defaults to 1**, settled on device. With it off each
  controller held the other hand's half of the model. That reads as correct at a
  glance - a pistol in each hand, pointing the right way - and is only visible
  once you look at which way the fingers and thumb go, which is why it survived
  an earlier run: that run confirmed the *controls* were on the right hands, not
  the geometry. Swapping changes only which half is drawn on which object; each
  is still posed from its own controller, so the angles are unaffected.


## #1 - Add an additional mod selector to the ShogoVR launcher in launcher-xr

Completed 22 Oct 2026.

- The launcher looks for a `Custom` subfolder inside the Shogo MAD game directory. If it
  doesn't exist, it skips this feature and launches normally.
- `Custom` is **no longer mounted automatically**. If it holds `.rez` files, the launcher
  shows a checkbox list of those files and loads only the checked ones.
- If `Custom` also holds files other than `.rez` (or subfolders), the player is first asked
  whether to load the whole folder, pick individual `.rez` files, or continue without
  loading anything. The stock `Custom\custom.txt` is ignored for this check.
- Nothing is remembered between launches. A shortcut can instead pass an exact,
  comma-separated list of filenames from `Custom` with
  `ShogoVR.exe -mods "coop.rez,someothermod.rez"`, which skips the popup and loads that
  list automatically. `ShogoVR` is always added by the launcher.
- Mods that should always load go in an `AlwaysLoad` folder in the game root. The launcher
  mounts it whenever it exists, with no prompt.
- Load order is the base game and map packs, then `AlwaysLoad`, then the Custom selection,
  then `ShogoVR` last (so its `CShell.dll` wins).


## #2 - Multiplayer support

Confirmed to be working to some extent as of 22 Oct 2026. No further work needed

**Multiplayer support** - We can look into seeing if we can get MP working. For example: It would be amazing if the [Shogo Co-op mod](https://www.moddb.com/mods/shogo-coop) worked alongside this one. This one might need zero code changes at all in reality, just review to ensure it's possible


## #3 - Physical motion melee attacks

Completed 25 Oct 2026.

- Dominant-hand slash gesture (any direction) while holding a melee weapon triggers a shot via `COMMAND_ID_FIRING` like a normal trigger pull; no dedicated melee button/command.
- Weapon tracked from shell/held-gun; gesture only active for melee weapons.
- VR menu: single `Gesture melee sens.` (OFF, 10%, 15%, 25%, 30%, 40%, 60%, 80%), default 15%. Higher % = easier/smaller swing. Behind scenes maps to velocity threshold and cooldown.
- Works in right/left-handed modes, respects existing trigger firing for ranged weapons, haptics on actual shot.


## #4 - Physical motion weapon swapping

Completed 26 Oct 2026.

- Reach over shoulder with left hand behind HMD/shoulder area → previous weapon, right hand → next weapon (independent of `LeftHanded`), cooldown ~0.6s, haptics on the swapping hand.
- Sends `COMMAND_ID_PREV_WEAPON` / `COMMAND_ID_NEXT_WEAPON` via `PollWeaponCycle()`.
- VR menu toggles "Physical wep swap" and "Wep Swap Via RS" (thumbstick) in VR section; both can be on, cannot both be off (thumbstick auto-enables).

## #6 - Render scale (supersampling)

Abandoned 28th Oct 2026 in favor of just upscaling the container resolution instead

### Goal

Let the player trade frame rate for sharpness: render the game at a higher
resolution than the headset actually displays and let the downscale do the
anti-aliasing. The same control should scale *down* as well, since a weaker
headset buying frame rate is the same knob in the other direction. Off (1.0x)
by default.

### Can this even be done from inside `CShell.dll`?

**Not the way the old draft assumed, and not the way it is usually done.**

The engine gives the mod exactly one way into the 3D renderer:
`ClientDE::RenderCamera(hCamera)`, which draws into the rectangle set by
`SetCameraRect` **on the back buffer**. There is no render-to-texture, no
render target, no `RenderToSurface` anywhere in `client_de.h`. So "render
internally to a 2x buffer, then scale it down" has no direct expression - there
is no internal buffer to render into. This is how the stereo view already
works: `RenderStereo` sets a half-screen rect per eye and renders twice into
the one back buffer.

The previous draft was written around **dgVoodoo's `[DirectX] Resolution = 2x`**
and around `VRCapture.cpp` copying a D3D11 backbuffer. Neither applies now.
dgVoodoo survives only in the legacy PCVR `launcher/` and `bridge/`, and
`VRCapture` is the old shared-texture path that the WinlatorXR port does not
use - it reads the game window instead. Nothing in `launcher-xr` touches
dgVoodoo and it must stay that way.

**But supersampling does not require an internal buffer.** It only requires
rendered pixels to outnumber displayed pixels. The mod controls the first and
the headset controls the second, so the feature is real - it is just plumbing
rather than rendering:

- The engine's back buffer is sized by `+screenwidth` / `+screenheight`, which
  `launcher-xr/ShogoVR.cpp::BuildArgs()` currently sets to the desktop
  resolution.
- WinlatorXR reads the game window and presents it; its compositor samples that
  image down to the headset panel.

Render at 1.5x the panel and the compositor's downsample *is* the supersample.
No wrapper, no DX11, no readback, no engine edit.

### The one thing that has to be proven first

Everything above rests on an assumption I cannot check from the desktop:

> **Does WinlatorXR accept a game window larger than the headset panel, and
> does it filter when it samples it down?**

If it point-samples, a 2x window will look *worse* than 1x, not better - and
the whole feature collapses. If it clamps the window to panel size, there is
nothing to sample down from and the feature does nothing at all.

So step one is not code. It is: launch at 1.5x the usual device resolution by
hand, look at a high-contrast edge (a railing, the HUD text), and compare
against 1.0x. Build nothing until that answer is known.

### The simplest version needs no code at all

`BuildArgs()` sizes the engine from `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`,
so **a larger WinlatorXR container is already a larger back buffer**. Telling
players to run a container at 1.5x or 2x gets the entire feature today, with no
build.

It is also the *safer* route, not merely the cheaper one. `CheckRenderer()`
resets the screen mode whenever the surface, render and client sizes disagree -
that is the "world shrinks, field of view balloons" bug it was written for. A
`VRRenderScale` setting would have to move all three in lockstep by hand, and
getting that subtly wrong is easy. A bigger container moves them together by
construction, through the same code path that already ships and is already
tested, rather than a second path that only engages when a setting is on.

What it gives up is small: no in-game adjustment - though a render scale needs
a relaunch either way - and no way to scale the 3D separately from the HUD,
which is drawn into the same buffer and so benefits too.

**Answered on device, 29 Oct: it works.** A container of **4200x2100** - 1.5x
each axis over the usual 2800x1400, so 2.25x the pixels - looks markedly
better, and WinlatorXR does filter when it samples the oversized window down to
the panel. The 1998 renderer took the mode without complaint.

So the primary route is confirmed and needs no code. What remains for #6 is
documentation: the recommended container size and its frame-rate cost go in
`README.md`. Fallbacks A and B below stay unbuilt unless something new asks
for them.

### Fallback A: `VRRenderScale` in the mod

If container sizing turns out to be awkward to explain, or players want it
switchable, the same thing can be done in `BuildArgs()` by multiplying the
resolution before passing it. The requirement is Approach 2 above: raise the
surface, render and client sizes **together**, or `CheckRenderer()` will fight
it once a second and lose.

### Fallback B: per-eye oversample inside the back buffer

Only if both routes above fail. Render each eye into the **full** back buffer
rather than its half, copy it off with `DrawSurfaceToSurface`, and
`ScaleSurfaceToSurface` it down into its half. The surface API has every call
this needs, and the movie fix already copies the screen into an offscreen
surface, so the shape of it is proven for 2D.

Two reasons this is an experiment to measure, not a plan:

- It needs a **full-resolution readback per eye per frame** through LithTech's
  system-memory surfaces, under Box64/Wine on a mobile chip. Very likely far
  too slow, and slow in the worst way - a steady frame-time cost that no
  setting recovers.
- Whether a screen-surface readback even returns 3D-rendered pixels is
  unproven. The movie path copies 2D blitted content; the 3D path may come
  back blank.

Measure it with a throwaway build before writing anything real around it.

### Affected files

- `launcher-xr/ShogoVR.cpp` - `BuildArgs()` resolution passing
- `game/VRStereo.cpp` / `.h` - `CheckRenderer()`, `FitGameWindow()`,
  `FitVRWindow()`, the `m_nScreenW/H`, `m_nRenderW/H`, `m_nSurfW/H`,
  `m_nClientW/H` family
- `game/VRMenu.cpp` / `.h` - the setting row, if it lives in the menu

Explicitly **not** in scope: `game/VRCapture.*`, `bridge/`, `launcher/`, and
anything dgVoodoo. Those are the legacy PCVR path.

### Settings

None, for the primary route - the container size is the setting, and it lives
in WinlatorXR rather than in the mod.

Fallback A only: `VRRenderScale`, default 1.0, applied at launch, so its menu
row would have to say a restart is needed.

### Edge cases / risks

- **The compositor's filtering is the whole feature.** See above.
- **A 1998 renderer at modern resolutions.** The engine may refuse unusual
  modes or misbehave well above what it shipped against. Test the actual
  numbers, not just the ratio.
- **Fill rate.** Mobile GPUs under translation; 2x is 4x the pixels.
- **`CheckRenderer()` fighting the change**, per Approach 2.
- **Aspect ratio.** The scale must be uniform, or the SBS halves stop matching
  the headset's expectation.

### Acceptance criteria

- [ ] The compositor question above is answered, in writing, before any code.
- [ ] Default behaviour is unchanged from today.
- [ ] At 1.5x+, edges are visibly cleaner than 1.0x on device - confirmed by a
      screenshot pair, not by assertion.
- [ ] Window fitting and stereo rendering stay correct; `CheckRenderer()` does
      not trigger.
- [ ] Aspect ratio preserved; no stretching in either eye.
- [ ] The recommended container size and its frame-rate cost are written
      down in `README.md`.
- [ ] Nothing in the dgVoodoo / `VRCapture` legacy path is revived.
