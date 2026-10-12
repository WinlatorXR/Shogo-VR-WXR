# ShogoSource/ClientShellDLL/OptionsMenu.h

**Goal:** add the VR sub-menu (`CVRMenu`) to the Options menu class `COptionsMenu`
so it is created, loaded, unloaded and notified of world transitions alongside the
other sub-menus.

All edits are additions. Code blocks show only the mod's own added lines.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/OptionsMenu.h`
(57 lines); your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Include             | after line 9 (`#include "JoystickMenu.h"`) |
| 2. `LoadAllSurfaces`   | lines 18–20 |
| 3. `UnloadAllSurfaces` | lines 21–23 |
| 4. `OnEnterWorld` / `OnExitWorld` | lines 26–33 / 35–42 |
| 5. Member declaration  | after line 54 (`CJoystickMenu m_JoystickMenu;`) |

---

## 1. Include

**Part of:** the include block at the top of the header (pristine lines 4–9).

Add, next to the other sub-menu includes (for example after the `JoystickMenu.h`
include on line 9):

```cpp
#include "VRMenu.h"
```

---

## 2. `COptionsMenu::LoadAllSurfaces()`

**Part of:** the inline `LoadAllSurfaces()` member (pristine lines 18–20).

Extend the existing success test so it also fails when
`m_VRMenu.LoadAllSurfaces()` returns false — i.e. append this term to the same
boolean condition that already checks the other sub-menus:

```cpp
|| !m_VRMenu.LoadAllSurfaces()
```

---

## 3. `COptionsMenu::UnloadAllSurfaces()`

**Part of:** the inline `UnloadAllSurfaces()` member (pristine lines 21–23).

Alongside the existing per-sub-menu unload calls, add:

```cpp
m_VRMenu.UnloadAllSurfaces();
```

---

## 4. `COptionsMenu::OnEnterWorld()` and `OnExitWorld()`

**Part of:** the inline `OnEnterWorld()` / `OnExitWorld()` members (pristine
`OnEnterWorld` lines 26–33; `OnExitWorld` lines 35–42).

In `OnEnterWorld()`, add before the `CBaseMenu::OnEnterWorld()` call:

```cpp
m_VRMenu.OnEnterWorld();
```

In `OnExitWorld()`, add before the `CBaseMenu::OnExitWorld()` call:

```cpp
m_VRMenu.OnExitWorld();
```

---

## 5. Member declaration

**Part of:** the protected member list of `COptionsMenu` (pristine lines 50–54).

Next to the other sub-menu members (e.g. `m_JoystickMenu`, line 54), add:

```cpp
CVRMenu m_VRMenu;
```
