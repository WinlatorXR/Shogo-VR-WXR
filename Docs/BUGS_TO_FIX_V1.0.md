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
`9D81E74EB67F51BC840BAB4B513C7A7B86C05F5E43D1EDE0DCAC7F2AE0D30970`; deployed to
`Release\ShogoVR\CShell.dll` and the GOG `ShogoVR\CShell.dll`.

**Refs:** `ShogoSource/ClientShellDLL/RiotClientShell.cpp` (`Update`,
`HandlePlayerStateChange`, `HandleRespawn`, `OnCommandOn`);
`ShogoSource/ClientShellDLL/RiotClientShell.h` (`m_bVRRespawnSent`);
`ShogoSource/ObjectDLL/RiotServerShell.cpp` (`MID_PLAYER_RESPAWN`);
`ShogoSourceModifications/ClientShellDLL/RiotClientShell.cpp.md` (edit 24);
`ShogoSourceModifications/ClientShellDLL/RiotClientShell.h.md` (edit 3).
