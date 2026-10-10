# ShogoSource/ClientShellDLL/BaseMenu.h

**Goal:** enlarge the base menu's reusable menu-item array so the mod's
`CVRMenu` (which holds one row per VR setting, 26 rows) fits inside it.

**Why this matters:** `CBaseMenu` keeps its menu rows in a fixed-size array
(`m_GenericItem`) whose capacity comes from a file-scope maximum-items
constant near the top of this header. The stock game never uses more rows
than that constant allows (the biggest stock menu is the detail-settings
menu, well under the limit). `CVRMenu` needs 26 rows, which exceeds the
stock capacity of 20. Writing rows 21..26 would run past the end of the
array into the `CBaseMenu` members that follow it (menu position fields)
and then into the start of `CVRMenu`'s own value-surface array — corrupting
those surface handles and leading to bogus `DeleteSurface` calls and a crash
when the Options menu is unloaded as a level starts.

**The edit:** raise the value of the maximum-items constant from its stock
value to 26. It is a single number at the top of the header; the array
declaration itself is not touched, it just sizes itself from the constant.
26 is the exact number of `CVRMenu` rows.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/BaseMenu.h`
(111 lines); your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. Maximum-items constant | line 14 (start-of-file `#define` block) |

---

## 1. Maximum generic menu items

**Part of:** the `#define` block near the top of the header (pristine line 14).

Change the numeric value of the maximum-items constant from 20 to 26. This is
the only change in this file; nothing else in `CBaseMenu` needs to know about
the VR menu.