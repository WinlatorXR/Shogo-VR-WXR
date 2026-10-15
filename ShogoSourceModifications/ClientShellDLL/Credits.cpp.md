# ShogoSource/ClientShellDLL/Credits.cpp

**Goal:** lay the credits / intro text out on the mod's small virtual screen so
it can be drawn per eye in VR (#12).

The credits text (`CCredit::DrawString`) is drawn with `DrawStringToSurface`, which
the VR module hooks: it keeps the text at native size and maps its centre through
the current per-eye transform. That only lands in the right place if the credits
were laid out for the same small virtual screen the menus use, so the static screen
size is taken from `g_VRStereo.GetMenuLayoutSize()` when VR is active.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/Credits.cpp`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. `#include "VRStereo.h"` | beside the other includes (pristine line 18) |
| 2. virtual screen size in `CCredit::SetStaticInfo` | after `GetSurfaceDims` (pristine line 196) |
| 3. virtual screen size in `CCredits::Init` | after `GetSurfaceDims` (pristine line 671) |

---

## 1. Include the VR module

Add, beside the other includes (our code):

```cpp
#include "VRStereo.h"
```

## 2. `CCredit::SetStaticInfo` — virtual layout

After the pristine `GetSurfaceDims(s_hScreen, ...)` that fills `s_dwScreenWidth` /
`s_dwScreenHeight`, override them with the mod's menu layout while VR is active
(our code):

```cpp
	int nVW = 0, nVH = 0;
	if (g_VRStereo.GetMenuLayoutSize(&nVW, &nVH) && nVW > 0 && nVH > 0)
	{
		s_dwScreenWidth  = (DDWORD)nVW;
		s_dwScreenHeight = (DDWORD)nVH;
	}
```

This also selects the larger font (the `> 520` branch) so the text stays readable.

## 3. `CCredits::Init` — matching `m_szScreen`

`CCredits::Init` caches the screen size into `m_szScreen` for the centred-string
helper; give it the same virtual size (our code):

```cpp
	int nVW = 0, nVH = 0;
	if (g_VRStereo.GetMenuLayoutSize(&nVW, &nVH) && nVW > 0 && nVH > 0)
	{
		m_szScreen.cx = nVW;
		m_szScreen.cy = nVH;
	}
```

See `Docs/BUGS_TO_FIX.md` #12.
