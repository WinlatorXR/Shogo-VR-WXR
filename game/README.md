# Game-side source

These are Shogo VR's own game-side files, compiled into `CShell.dll` together with Monolith's Shogo v2.2 source release:

| File | What it does |
|---|---|
| `VRStereo.cpp/.h` | Stereo rendering, the HUD panel, head tracking, controllers, two-handed aiming, body, held gun, settings file, opening notice, logs. |
| `VRMenu.cpp/.h` | *Options → vr settings* in the game, and the *VR Advanced* page behind it. |
| `VRCapture.cpp/.h` | Legacy direct capture: hands each finished frame from the renderer to the OpenXR bridge via a shared texture. Unused in the WinlatorXR XrApi port, which reads the game window instead. |
| `ShogoVRShared.h` | The shared-memory layout the OpenXR bridge used (identical in `bridge/`); legacy in the WinlatorXR XrApi port. |
| `dplobby.h` | A small stand-in for a DirectX 6 SDK header that modern SDKs no longer ship. |

## Seating a lent hand on device

`VRNudgeKeys 1` turns on a tuning aid in `VRStereo.cpp`: **q/e** walk the
shotgun's lent hand back and forth along the barrel, **r/u** drop and raise it,
**f/y** slide it sideways. `VRNudgeStep` (0.1) is how far one press moves it;
hold a key to repeat. Every change is logged with both the trim and the
resulting seating, so the numbers can be read out of `ShogoVR_game.log` and
baked into `CVRStereo::DomHandSeat`.

Off by default. The keys are polled rather than hooked, so they are not
consumed - whatever they are bound to still happens while this is on.

They use Monolith's headers but contain none of Monolith's code. Connecting them into Monolith's files requires edits that can't be published, because the source licence forbids redistributing that source.
