# ShogoSource/ClientShellDLL/MainMenu.cpp

**Goal:** submitting the "Quit" (Exit) option confirms the "are you sure?"
message box for the player, so quitting is a single action in VR.

The Exit item shows a yes/no box (`IDS_SUREWANTQUIT`) that waits for the
configured "yes" key (the keyboard letter **Y**). In VR the motion controllers
have no convenient Y, so the box would dead-end. The VR module already injects
keyboard input for the menus (arrow keys, Enter); the shell now asks it to tap Y
as soon as the box is created.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/MainMenu.cpp`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. `#include "VRStereo.h"` | beside the other includes (pristine line 7) |
| 2. exit branch in `CMainMenu::Return` | pristine line 111 |

---

## 1. Include the VR module

Add, beside the other includes (our code):

```cpp
#include "VRStereo.h"
```

## 2. `CMainMenu::Return` — confirm the quit

In the branch that handles the Exit selection (`m_nSelection == 4`), the pristine
code only shows the yes/no box. See whether the box was created and, if so, ask
the VR module to tap the Y key for the player (our code):

```cpp
	if (pClientShell->DoYesNoMessageBox (IDS_SUREWANTQUIT, ExitGame, 0, TH_ALIGN_CENTER))
	{
		g_VRStereo.InjectKeyTap (0x15, DFALSE);		// 0x15 = keyboard scan code for Y
	}
```

`InjectKeyTap` presses and releases the key through the same `SendInput`
scan-code path the menu navigation uses, so the engine's `OnKeyDown` runs the
yes branch of the box exactly as if the player had pressed Y.
