# ShogoSource/ClientShellDLL/MainMenu.cpp

**Goal:** selecting the "Quit" (Exit) option quits immediately in VR, instead of
showing an "are you sure?" message box the controller cannot answer.

The Exit item showed a yes/no box (`IDS_SUREWANTQUIT`) that waits for the
configured "yes" key (the keyboard letter **Y**). In VR the motion controllers
have no convenient Y, so the box dead-ends. The shell now runs the box's own
"yes" action directly and never shows the box.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/MainMenu.cpp`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. exit branch in `CMainMenu::Return` | pristine line 111 |

---

## 1. `CMainMenu::Return` — quit on select

In the branch that handles the Exit selection (`m_nSelection == 4`), the pristine
code calls `DoYesNoMessageBox(IDS_SUREWANTQUIT, ExitGame, ...)`. Replace that with
a direct call to the same "yes" action (`ExitGame` is already declared at the top
of the file) so no box is created (our code):

```cpp
	ExitGame (DTRUE, 0);
```

`ExitGame(DTRUE, ...)` is exactly what the box's yes branch would have run, so the
behaviour is unchanged apart from dropping the prompt.
