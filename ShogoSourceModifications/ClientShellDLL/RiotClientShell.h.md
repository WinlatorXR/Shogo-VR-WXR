# ShogoSource/ClientShellDLL/RiotClientShell.h

**Goal:** add the `CRiotClientShell` member the movie-clone edit needs.

This is a single new member next to the other movie-related view flag
(`m_bMovieCameraRect`). It records whether the current intro movie is being
drawn through the VR per-eye clone (`game/VRStereo.cpp` `ShiftMovieToRightHalf`),
so `UpdateMoviesState()` knows to re-blit it and `PlayIntroMovies()` knows to
start it at native size.

Line numbers below refer to the pristine `shogosrc/ClientShellDLL/RiotClientShell.h`;
your copy may differ by a few lines.

| Edit | Anchor in pristine file |
|------|-------------------------|
| 1. `m_bMovieShiftRight` member | beside `m_bMovieCameraRect` (pristine line 304) |

---

## 1. `DBOOL m_bMovieShiftRight` member

**Part of:** the private member block near `m_bMovieCameraRect` (pristine line
304).

Add, next to `m_bMovieCameraRect` (our code):

```cpp
DBOOL			m_bMovieShiftRight;			// VR: movies drawn small and slid to the right half
```

It is initialised to `DFALSE` in the constructor and set from
`g_VRStereo.IsActive()` each time an intro movie starts. See the
`RiotClientShell.cpp.md` note (edit 15) for its use in `PlayIntroMovies()` and
`UpdateMoviesState()`, and `Docs/Pre-LaunchDocs/BUGS_TO_FIX.md` #10.

---

## 2. `void UpdateCreditsStereo()` declaration

**Part of:** the state-update method group, beside `UpdateCreditsState()` /
`UpdateDemoInfoState()` (pristine line 602).

Add (our code):

```cpp
		void	UpdateCreditsStereo();	// draw credits/intro per eye (#12)
```

Defined in `RiotClientShell.cpp` (see that note, edit 22). See
`Docs/Pre-LaunchDocs/BUGS_TO_FIX.md` #12.
