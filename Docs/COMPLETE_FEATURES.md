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
