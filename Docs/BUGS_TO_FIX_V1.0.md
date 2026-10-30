# BUGS_TO_FIX_V1.0.md

Issue log for the Shogo-VR-WXR port (XrApi 0.4, in-process UDP client in `CShell.dll`).
Context: Meta Quest 3 via WinlatorXR, side-by-side stereo, on-device session initiated from
`ShogoVR.exe` (launcher-xr). On-device logs land in `<game>\ShogoVR\ShogoVR_game.log`.

Legend: `Open` / `In progress` / `Fixed in code` (awaiting a device pass) / `Fixed` (verified) /
`Won't fix`.

This is the **V1.0 cycle** tracker. Earlier logs are archived as
[`Pre-LaunchDocs/BUGS_TO_FIX.md`](Pre-LaunchDocs/BUGS_TO_FIX.md) (`#1`-`#39`),
[`Pre-LaunchDocs/BUGS_TO_FIX_V0.7_to_V0.8.md`](Pre-LaunchDocs/BUGS_TO_FIX_V0.7_to_V0.8.md)
(`#40`-`#44`) and [`Pre-LaunchDocs/Bugs_To_Fix_V0.9.md`](Pre-LaunchDocs/Bugs_To_Fix_V0.9.md)
(`#45`-`#47`); numbering here **continues at `#48`**. Note that `game/` files are in this
repository (the mod); `ShogoSource/` files are the git-ignored Monolith source (any edit there
needs a `ShogoSourceModifications/` note plus a `PROJECT_PLAN.md` entry).

## Status at a glance

| # | Issue | Status |
|---|-------|--------|
| 48 | Multiplayer death never respawns in VR without a mouse click / fire-button press | **Fixed** - verified on device (Oct 22) |
| 49 | VR menu's Recentre / Reset / Back act on the wrong rows | **Fixed in code** - awaiting a device pass |
| 50 | The two physical-weapon-swap settings reset on every launch | **Fixed in code** - awaiting a device pass |
| 51 | Intro movie shows four part-frames side by side instead of two | **Fixed in code** - awaiting a device pass |

---

## #48 - Multiplayer death needs a fire-button edge to respawn

**Status:** **Fixed - verified on device (22 Oct 2026)**

**Device pass:** owner confirmed the automatic multiplayer respawn works on device.

**Observed (device):** in multiplayer the player dies and then stays dead unless the
"shoot" button is pressed (which the owner suspected was really a mouse click reaching the
game). Single player is fine.

**Root cause:** the stock shell only respawns a dead multiplayer player from
`CRiotClientShell::OnCommandOn(COMMAND_ID_FIRING)` - a fresh mouse/key **edge**. The VR
fire trigger is a held command answered by `CVRStereo::Hook_IsCommandOn()` /
`VRCommandOn()`, so no edge is produced. The stock `MULTIPLAYER_RESPAWN_WAIT_TIME` (0.2 s)
still applies; the game just waits forever for an input edge that VR never sends.

**Fix (in code, 2026-10-22):** in `CRiotClientShell::Update()`, once the stock
`m_fEarliestRespawnTime` has elapsed, VR is active, the game is in multiplayer and the
player is `PS_DEAD` in `GS_PLAYING`, call the existing `HandleRespawn()` directly. That
sends the same `MID_PLAYER_RESPAWN` message the fire path uses; the server's
`RiotServerShell` handler already checks `GetState() == PS_DEAD`. The request is one-shot
per death (`m_bVRRespawnSent`, cleared by `HandlePlayerStateChange()`), and the message
editing guard is kept. Single player is excluded on purpose: its death path still opens the
load menu, and the left trigger keeps its quick-load behaviour.

**Build:** `CShell.dll built Oct 22 2026 17:33:58`, SHA-256
`A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`; deployed to
`Release\ShogoVR\CShell.dll` and the GOG `ShogoVR\CShell.dll`.

**Refs:** `ShogoSource/ClientShellDLL/RiotClientShell.cpp` (`Update`,
`HandlePlayerStateChange`, `HandleRespawn`, `OnCommandOn`);
`ShogoSource/ClientShellDLL/RiotClientShell.h` (`m_bVRRespawnSent`);
`ShogoSource/ObjectDLL/RiotServerShell.cpp` (`MID_PLAYER_RESPAWN`);
`ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md` (edit 24);
`ShogoSourceModifications/ClientShellDLL/RiotClientShell.h.md` (edit 3).

---

## #49 - The VR menu's action rows act on the wrong items

**Status:** **Fixed in code (28 Oct 2026)** - awaiting a device pass

**Found:** while reading `VRMenu.cpp` to add feature #5. Not reported from a
device; it has been live since the 0.9/1.0 cycle.

**Observed (by inspection):** `CVRMenu::Return()` matched the selected row
against three fixed indices, `VRM_RECENTER 6` / `VRM_DEFAULTS 7` /
`VRM_BACK 8`. Those were right when the menu had 9 rows, but #3 and #4 took
`VRMENU_ITEMS` to 12 and the three `VRS_ACTION` rows moved to indices 9, 10 and
11. Nothing warned, because the defines are just numbers. The result in V1.0:

| Row the player selects | What pressing Enter actually did |
|---|---|
| Physical wep swap | recentred the view |
| Wep Swap Via RS | **silently reset every VR setting to its default** |
| Gesture melee sens. | left the menu |
| Recentre view / Reset to defaults / Back | nothing at all |

So all three real actions were dead, and one settings row was quietly
destructive.

**Root cause:** three hard-coded indices into `s_Settings[]` that no longer
matched the table, with nothing tying the two together.

**Fix (in code, 2026-10-28):** the actions are matched by **label** now
(`VRM_ACT_RECENTER` / `VRM_ACT_DEFAULTS` / `VRM_ACT_BACK` hold the label
strings), and `Return()` only treats a row as an action when its `nType` is
`VRS_ACTION`. Adding, removing or moving a row can no longer point an action at
the wrong thing. The non-action branch keys off that same test rather than an
index range.

**Refs:** `game/VRMenu.cpp` (the `VRM_ACT_*` defines, `Return`).
**Build:** `CShell.dll built Oct 28 2026 22:19:09`, SHA-256
`A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`; deployed to
`Release\ShogoVR\CShell.dll` and the GOG `ShogoVR\CShell.dll`.


---

## #50 - The physical-weapon-swap settings revert on every launch

**Status:** **Fixed in code (28 Oct 2026)** - awaiting a device pass

**Found:** while reading `LoadSettingsFile` for feature #5. Live since #4
shipped on 26 Oct.

**Observed (by inspection):** feature #4's two VR-menu toggles, "Physical wep
swap" and "Wep Swap Via RS", took effect immediately and were written to
`ShogoVR.ini`, but were back to their defaults (off and on) the next time the
game started.

**Root cause:** `CVRStereo::LoadSettingsFile()` applies the `[Game]` section as
console commands, and accepts only keys whose name begins with `VR`/`vr` - a
deliberate whitelist, since the value goes straight to `RunConsoleString`.
`PhysicalWepSwap` and `WepSwapThumbstick` were registered without that prefix,
so they failed the test and were silently dropped at startup. The live apply
worked only because `CVRMenu::WriteValue` runs its own `RunConsoleString` too.

**Fix (in code, 2026-10-28):** both CVars (and their ini keys) are renamed to
`VRPhysicalWepSwap` and `VRWepSwapThumbstick`, which the whitelist accepts.
`LoadSettingsFile` carries a value written under either old name across to the
new one once, on the first load, then removes the old key - so an ini from 1.0
or earlier keeps the player's choice instead of losing it. Any setting added
from here on must carry the `VR` prefix.

**Refs:** `game/VRStereo.cpp` (`LoadSettingsFile`, the VarTrack `Init` block);
`game/VRMenu.cpp` (`s_Settings`, and the mutual-exclusion blocks in `Change`
and `Return`).
**Build:** `CShell.dll built Oct 28 2026 22:19:09`, SHA-256
`A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`; deployed to
`Release\ShogoVR\CShell.dll` and the GOG `ShogoVR\CShell.dll`.

---

## #51 - The intro movie shows four part-frames instead of two

**Status:** **Fixed in code (28 Oct 2026)** - awaiting a device pass.

**Reported:** owner, from a device run - the intro video showed four panels side
by side rather than the two (one per eye) it is supposed to.

**Cause:** `ShiftMovieToRightHalf` was not idempotent, and nothing guaranteed it
would only run once per movie frame.

The engine's movie player draws the video centred on the whole side-by-side
image, which straddles the midline, so neither eye sees a whole frame. The mod
fixes that by capturing the finished window, blacking it, and drawing the video
into the middle of each half. That is correct exactly once per movie frame -
but it is called once per *engine* frame, from `UpdateMoviesState`, and the movie
plays at its own much lower rate. Every extra pass captured the window as the
previous pass had left it (our own two clones) and cloned *that*: the middle of
the pair is the right half of one copy plus the left half of the other, so each
eye ended up with two part-frames, four across the window.

Why it took until now to show up: whether a second pass lands inside a single
movie frame depends on how many engine frames fit into one, so it varies with
the frame rate. It was first seen on the **desktop** at 1920x1080 (the owner's
monitor, used for testing) rather than on device at 2800x1400 - the smaller
window runs faster. That is a difference in how often the bug is hit, not in
whether the code was wrong: the resolution is incidental, and the fix below is
geometry-independent for that reason. Nothing in the dual-pistol work (#5)
touched this path - the geometry is identical in both runs' logs
(`movie source detected` spans exactly `nW/2` either way).

**Fix (in code, 2026-10-28):** the capture is skipped when the window is already
showing our own output, which makes the whole operation idempotent - the scratch
surface still holds the last fresh frame, so the draw reproduces the same two
clones however many times it runs.

Telling a fresh frame from our own output took two attempts. The **first** asked
whether the margins beside the video were black, on the grounds that the player
leaves them black and our clones cover them. That is self-defeating: once we have
cloned even once, our output covers those margins for good, so the answer is
"stale" forever and the movie freezes on its first frame. On device that showed
as two frames in each half - the frozen first frame plus the live one drawn over
it - so four images across the window again, by a different route.

The **second** is the one that works. The player only ever draws inside
`m_rcMovieSrc`, so everything outside it is our own output by definition and says
nothing. Inside it the window holds either a new frame or exactly what we drew -
and we know what that was, since the left clone maps source pixel
`m_rcMovieSrc.left + x - xL` to `x`. `MovieFrameIsFresh` predicts our own output
and compares against it; still matching means no new frame arrived. Ambiguity
resolves to "stale", because a wrong "stale" costs one repeated frame while a
wrong "fresh" re-clones a composite, which is the bug itself.

A new movie also starts with the *previous* one's clones still on the window, so
`ResetMovieSource` now asks for one blanking pass: the window is cleared and
nothing else is done, and by the next pass the player has drawn a clean centred
frame onto black for detection and capture to work from. That costs one black
frame per movie and removes the contamination that was making the detected
rectangle vary between runs.

**Refs:** `game/VRStereo.cpp` (`ShiftMovieToRightHalf`, `MovieFrameIsFresh`,
`ResetMovieSource`); `game/VRStereo.h`.
**Build:** `CShell.dll built Oct 28 2026 22:19:09`, SHA-256
`A27B18B3289B59D02850C23C825548800F5BFEAD77BF933A7C311056F4A20989`; deployed to
`Release\ShogoVR\CShell.dll` and the GOG `ShogoVR\CShell.dll`.
