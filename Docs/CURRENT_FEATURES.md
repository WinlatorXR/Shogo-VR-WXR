# Current Features

This document is features we want to implement for the current release, as future features are started they will be moved into here

## #1 - Add an additional mod selector to the ShogoVR launcher in launcher-xr

- The launcher will look for a subfolder called "Custom" inside the Shogo MAD game directory.
    If it doesn't exist then it will skip this feature and launch normally

- If it does exist, it will look for any .rez format files (eg: coop.rez) within this subfolder.
    If none it exist, it will skip this feature and launch normally

- However, if there are .rez files in that folder, it will show them in a popup window with a simple list of checkboxes that users can toggle on or off as they wish before clicking "GO" to launch

- It will not save or remember which were selected, instead users can pass their own list of mods to load automatically in the shortcut settings as a CSV string for the mods parameter to SHOGOVR.exe (eg: ShogoVR.exe -mods "coop.rez,someothermod.rez"), and it will look for them only in the "Custom" folder

- The ShogoVR launcher then launches the game as normal but with the new mods at the end, eg: "shogo.exe -rez ShogoVR -rez custom\WidescreenPatch.rez -rez custom\YourModName.rez"

- NOTE: I notice that right now the "Custom" folder seems to get added entirely always, this is likely not what a user wants if they want to optionally turn on and off the COOP mod for example.
    Users who want a mod to always load should put it in a folder called "AlwaysLoad" which ShogoVR.exe launcher will add if it exists always, no matter what else is passed to the launcher or selected in the checkbox GUI