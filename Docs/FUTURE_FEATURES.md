# Future Features

This document tracks potential future enhancements for Shogo-VR-WXR. These items are not planned for the current release cycle and do not need to be implemented before finishing the open bugs.

## Potential Enhancements

In order of importance:

- **Physical motion melee attacks** - We could look at having physical motion trigger melee attacks when holding the knife or MCA sabre. I think this should be at an input level where the gesture of the dominant hand would fire the "attack" trigger if holding a melee weapon
- **Physical motion weapon swapping** Allow the player to reach over their shoulder with their left hand to swap to previous weapon, or right hand to swap to next weapon, with a small cooldown timer
    We would add an option to the VR menu to toggle this mode on/off
        Call it "Physical wep swap"
    We would add an option to the VR menu to also toggle thumbstick swapping on/off
        Call it "Wep swap thumbstick"
    If turning one OFF means that neither is ON, then turn the other option back to ON automatically
- **Dual pistols as two-handed weapons** - Perhaps we can modify the dual pistols to be one per hand rather than both on the dominant hand. The PCVR mod has this using whole new models, but we would prefer to preserve the original art.
    Things to consider here are how:
        1. We may want the left trigger to fire the left pistol and the right trigger to fire the right pistol instead in this mode
        2. the muzzle flashes will need to be modified for this weapon again to follow each hand instead
        3. The fire direction for both pistols will be totally different depending on which is firing
        4. Each pistol still needs its own cooldown before being able to fire again
- **Multiplayer support** - We can look into seeing if we can get MP working. For example: It would be amazing if the [Shogo Co-op mod](https://www.moddb.com/mods/shogo-coop) worked alongside this one. This one might need zero code changes at all in reality, just review to ensure it's possible
- **Display upscaling** - We could look at upscaling the display in some way, perhaps rendering twice as large and scaling down for display?
