# ShogoSource/ClientShellDLL/OptionsMenu.cpp

**Goal:** add a "vr" entry to the Options menu and route to the new VR sub-menu;
grow the menu's item arrays from six entries to seven.

Only the mod's own added lines appear in code blocks. Where a change is a
replacement, the original code is described in prose rather than quoted.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/OptionsMenu.cpp`
(169 lines); your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. `Init`             | lines 9–20; insert after line 17 (last `...Init` call) |
| 2. `ScreenDimsChanged`| lines 22–31; insert after line 28 |
| 3. `Return`           | lines 33–93; existing "back" branch is `m_nSelection == 5` at line 87 |
| 4. `LoadSurfaces`     | lines 95–149; items at 117–129, count loops at 134 and 143 |
| 5. `UnloadSurfaces`   | lines 151–168; count loop at 155 |

---

## 1. `COptionsMenu::Init()`

**Part of:** `COptionsMenu::Init(...)` (pristine lines 9–20).

After the existing per-sub-menu `Init(...)` calls (the last one is the joystick
sub-menu, line 17) and before `return DTRUE;` (line 19), add:

```cpp
if (!m_VRMenu.Init (pClientDE, pRiotMenu, this, nScreenWidth, nScreenHeight)) return DFALSE;
```

---

## 2. `COptionsMenu::ScreenDimsChanged()`

**Part of:** `COptionsMenu::ScreenDimsChanged(...)` (pristine lines 22–31).

After the other sub-menus are told about the new size (line 28) and before the
`CBaseMenu::ScreenDimsChanged` call (line 30), add:

```cpp
m_VRMenu.ScreenDimsChanged (nScreenWidth, nScreenHeight);
```

---

## 3. `COptionsMenu::Return()`

**Part of:** `COptionsMenu::Return()` (pristine lines 33–93).

This function dispatches on `m_nSelection`. Insert a **new** branch for
`m_nSelection == 5` that opens the VR sub-menu, and **change the index of the
existing "back to parent" branch from 5 to 6** (the existing branch is the
`m_nSelection == 5` block at pristine line 87).

The new branch to add:

```cpp
else if (m_nSelection == 5)
{
	m_VRMenu.Reset();
	m_pRiotMenu->SetCurrentMenu (&m_VRMenu);
}
```

---

## 4. `COptionsMenu::LoadSurfaces()`

**Part of:** `COptionsMenu::LoadSurfaces()` (pristine lines 95–149).

The menu currently builds six items (indices 0–5), with "back" at index 5 (the
`IDS_BACK` item at pristine lines 122 and 129). Change it so:

- the existing "back" item moves to **index 6**, and
- a new item is inserted at **index 5** whose label is the literal string `"vr"`.

The new "vr" item must be created for both the normal and selected variants, next
to the existing items (using the same `CTextHelper::CreateSurfaceFromString`
overload and the `IDS_MENUREPLACEMENTFONT` replacement-font argument the other
items use):

```cpp
m_GenericItem[5].hMenuItem = CTextHelper::CreateSurfaceFromString (m_pClientDE, pFontNormal, "vr", IDS_MENUREPLACEMENTFONT);
m_GenericItem[5].hMenuItemSelected = CTextHelper::CreateSurfaceFromString (m_pClientDE, pFontSelected, "vr", IDS_MENUREPLACEMENTFONT);
```

Then change the two item-count loops in this function — the loop that validates
that every item surface exists (pristine line 134) and the loop that reads each
item's dimensions (pristine line 143) — from `i < 6` to `i < 7`.

---

## 5. `COptionsMenu::UnloadSurfaces()`

**Part of:** `COptionsMenu::UnloadSurfaces()` (pristine lines 151–168).

Change its cleanup loop bound (pristine line 155) from `i < 6` to `i < 7` so the
seventh item is released too.
