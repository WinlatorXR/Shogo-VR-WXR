# ShogoSource/ClientShellDLL/RiotMenu.cpp

**Goal:** let the VR module choose the virtual screen size the options/menu tree
lays itself out for, so its native (fixed-pixel) fonts land legibly in the
headset instead of being shrunk into each eye.

**Why this matters:** Shogo's menu art and fonts have fixed pixel sizes. The
whole menu tree is normally laid out for the full screen width and then drawn
1:1. In the headset the mod draws menus per eye through a 2D transform that
scales a virtual canvas *up* into the panel - so if the menu lays itself out for
a small virtual canvas (640x480, the resolution the menus were designed for) the
same transform makes the text bigger and readable. `CRiotMenu::ScreenDimsChanged()`
is the one place that reads the screen size and pushes it into every menu, so it
asks `g_VRStereo` for the menu layout size and falls back to the real screen when
VR is not in play.

All edits are additions. Line numbers below refer to the pristine
`shogosrc/ClientShellDLL/RiotMenu.cpp`; your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Include `VRStereo.h` | the include block at the top of the file |
| 2. `ScreenDimsChanged` virtual size | `CRiotMenu::ScreenDimsChanged()` (line 212) |

---

## 1. Include `VRStereo.h`

**Part of:** the include block at the top of the file.

Add, beside the other includes:

```cpp
#include "VRStereo.h"
```

---

## 2. `CRiotMenu::ScreenDimsChanged()` — ask the mod for the menu size

**Part of:** `CRiotMenu::ScreenDimsChanged()` (pristine line 212), the block
that reads the screen surface dimensions into `m_szScreen` before laying out the
main menu.

Replace the single `GetSurfaceDims` call that fills `m_szScreen` with (our code):

```cpp
	// In VR the options/menu tree lays itself out for a small virtual screen
	// (Shogo's fonts and art are fixed pixel sizes) and the per-eye 2D
	// transform scales the whole thing back up into the headset panel.
	int nVRW = 0, nVRH = 0;
	if (g_VRStereo.GetMenuLayoutSize(&nVRW, &nVRH))
	{
		m_szScreen.cx = (DDWORD)nVRW;
		m_szScreen.cy = (DDWORD)nVRH;
	}
	else
	{
		m_pClientDE->GetSurfaceDims (hScreen, &m_szScreen.cx, &m_szScreen.cy);
	}
```

Leave the rest of the function (the art rect, the `m_MainMenu.ScreenDimsChanged`
call) unchanged - they now use the virtual size automatically.

---

## Notes

- `GetMenuLayoutSize()` is a public method on `CVRStereo` (`game/VRStereo.h`);
  it returns DTRUE and fills the virtual size only while the mod is active and
  the headset bridge is live, otherwise DFALSE so the real screen size is kept.
- `CRiotMenu::Init()` still reads the real screen size, because it runs before
  `g_VRStereo.Init()` (so the `VRMenuLayout` VarTrack is not ready yet). The
  shell re-runs this `ScreenDimsChanged()` once VR becomes available - see
  `RiotClientShell.cpp.md`, edit 7.
- See `Docs/BUGS_TO_FIX.md` #5.
