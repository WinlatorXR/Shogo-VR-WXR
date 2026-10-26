# Complete Features

Features from `CURRENT_FEATURES.md` that are implemented and shipped, most recent first.

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