# ShogoSource/ClientShellDLL/Sparam.cpp

**Goal:** build-compatibility fixes (not VR-specific). Under the modern MSVC
toolset used for `CShell.dll`, the two pointers that hold `strstr` search results
in `Sparam_Get` must be `const` to compile cleanly.

No source is quoted; this is a type-qualifier change only.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/Sparam.cpp`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. `Sparam_Get` | function starts line 31; the two `strstr` locals are at lines 44 and 57 |

---

## 1. `Sparam_Get(char* sDest, const char* sSource, const char* sId)`

**Part of:** `Sparam_Get(...)` (pristine function starts line 31).

**Action:** change the two local variables that store the results of the `strstr`
calls — the one locating the parameter's opening marker (pristine line 44) and the
one locating its closing bracket (pristine line 57) — from `char*` to `const char*`.

That is:

- the variable receiving the result of the search for the parameter's opening
  marker becomes `const char*`;
- the variable receiving the result of the search for the closing marker becomes
  `const char*`.

No other change is required.
