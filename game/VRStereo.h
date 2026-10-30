// ======================================================================= //
//
// MODULE  : VRStereo.h
//
// PURPOSE : Side-by-side stereo rendering for Shogo (VR mod, stage 1)
//
// ======================================================================= //
//
// How it works, in short:
//
//  3D:  The client shell owns the camera, so each frame we render the scene
//       twice, once into each half of the screen, with the camera shifted
//       half the eye distance left/right and a per-eye field of view.
//       Camera rect/FOV/position are restored afterwards, so the rest of
//       the game code never sees the stereo state.
//
//  2D:  HUD, menus, message boxes and loading screens are drawn once per
//       eye.  While an eye's 2D pass is running we temporarily swap a few
//       function pointers in the engine's ClientDE table so that every blit
//       to the screen surface is remapped (scaled + offset) into that eye's
//       half.  The 2D code itself still believes it is drawing to a normal
//       full-size screen.  For the second eye GetFrameTime() returns 0 so
//       HUD animations and timers don't run twice as fast.
//
// Everything is off unless the console variable VRStereo is 1 or 2.
//
// ======================================================================= //

#ifndef __VRSTEREO_H__
#define __VRSTEREO_H__

#include "client_de.h"
#include "VarTrack.h"
#include "ShogoVRShared.h"
#include "XrApiUDP.h"
#include "WeaponDefs.h"

// Values for the VRStereo console variable
#define VR_STEREO_OFF			0
#define VR_STEREO_FULLSBS		1	// each eye gets half the screen at its real aspect (headsets, cross/parallel viewing)
#define VR_STEREO_HALFSBS		2	// each eye is squeezed 2:1 horizontally (3D TVs, "half SBS" players)

// How the 3D view is placed into each eye
#define VR3D_IMMERSIVE			0	// fills the eye, full stereo (normal gameplay)
#define VR3D_PANEL_STEREO		1	// floating screen with reduced stereo (in-game cutscenes)
#define VR3D_PANEL_FLAT			2	// floating screen, no stereo (menu background)


class CVRStereo
{
	public:

		CVRStereo();

		void	Init(CClientDE* pClientDE);
		void	Term();

		DBOOL	IsActive();				// the stereo system is on
		DBOOL	IsStereoFrame();		// ...and this frame is drawn in stereo
		int		GetNumEyes()			{ return IsStereoFrame() ? 2 : 1; }

		// Call once per frame before drawing.  bFlat: this frame is a menu,
		// cutscene etc. - with the headset bridge running it's drawn as the
		// normal flat game and shown on a floating screen.  bHudStripAllowed:
		// the HUD may go into its own strip (not while a dialog is up).
		void	BeginFrame(DBOOL bFlat, DBOOL bHudStripAllowed);
		DBOOL	UseCutscenePanel()		{ return m_vtCutscenePanel.GetFloat(1.0f) != 0.0f; }

		// 3D.  Call between Start3D() and End3D().  If pObjects is DNULL the
		// whole scene is rendered (RenderCamera), otherwise only the listed
		// objects (RenderObjects).
		void	RenderStereo(HLOCALOBJ hCamera, HLOCALOBJ* pObjects, int nObjects,
							 DFLOAT fBaseFovX, int nMode3D, DBOOL bFootScale);

		// Casts a ray along the view to find what the crosshair is over.
		// pFilterList is a DNULL-terminated list of objects to ignore.
		void	UpdateAimDepth(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList);

		// 2D.  Wrap each per-eye draw in Begin2D(nEye) / End2D().
		// Both are no-ops when stereo is off.  bHud = this is the in-game
		// HUD, which in the headset is laid out on a smaller virtual screen
		// so it's bigger and closer to the centre of your view.
		void	Begin2D(int nEye, DBOOL bHud = DFALSE);
		void	End2D();

		// The engine's movie player draws straight into the window buffer,
		// centred on the whole SBS image.  Clone that finished frame into the
		// middle of each half (both eyes) after UpdateVideo().  ResetMovieSource
		// is called when a new movie starts so the video is re-detected.
		void	ShiftMovieToRightHalf();
		void	ResetMovieSource();
		// DFALSE when the window is showing our own two clones rather than a
		// fresh centred frame from the movie player - see ShiftMovieToRightHalf.
		DBOOL	MovieFrameIsFresh(HSURFACE hScreen, int xL, int vw, DDWORD nW, DDWORD nH);

		// Wrap the crosshair blits so they can be placed at aim depth.
		void	BeginCrosshair()		{ m_bCrosshairPass = DTRUE; }
		void	EndCrosshair()			{ m_bCrosshairPass = DFALSE; }

		// Console tools
		void	Calibrate(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList, DBOOL bFootScale);
		void	PrintInfo();

		// Headset link (WinlatorXR XrApi, in-process UDP).  Call UpdateBridge() once per frame.
		void	UpdateBridge();
		DBOOL	IsBridgeConnected()		{ return m_bBridgeAlive; }

		// In the headset the options/menu tree lays itself out for a small
		// virtual screen, so the per-eye xform scales it *up* into the panel
		// and native fonts become readable.  Fills in that size and returns
		// DTRUE when VR is active; returns DFALSE to leave the caller's real
		// screen size alone.
		DBOOL	GetMenuLayoutSize(int *pW, int *pH);
		// The shell polls this each frame; DTRUE once (after VR becomes
		// available) means it should re-run the menu's ScreenDimsChanged().
		DBOOL	ConsumeMenuLayoutChanged()	{ DBOOL b = m_bMenuLayoutDirty; m_bMenuLayoutDirty = DFALSE; return b; }

		// Screen flash (#28): the game's hurt/explosion flash normally goes
		// through the engine camera light-add, which renders as a broken
		// full-screen quad under the stereo layout, so while VR is active the
		// shell feeds the current tint here instead (once per frame while a
		// tint is active; ClearScreenFlash() when it ends) and the mod paints a
		// band down the outer edge of each eye (a peripheral-vision flash).
		// Colour channels are 0..1.
		void	SetScreenFlash(DFLOAT r, DFLOAT g, DFLOAT b);
		void	ClearScreenFlash();
		DBOOL	IsHeadTracking();
		void	ApplyHeadTracking(DFLOAT &fYaw, DFLOAT &fPitch);	// head/hand aim, stick turning
		void	RequestRecenter();

		// Render HMD_SYNC as a shade of red in the top-left corner (WinlatorXR
		// uses this to keep the captured frame in sync).  Call once per frame,
		// after all drawing, before the frame is presented.
		void	DrawXrSyncPixel();
		// Hurt/explosion vignette (#28) for every eye.  Call once per frame
		// after the world and the HUD, beside DrawXrSyncPixel - the same
		// post-render path, which is known to reach the captured frame.
		void	DrawScreenFlashFrame();
		void	DetectMovieSource();

		// Physical motion melee attacks (#3)
		void	SetCurrentWeaponId(int nId) { m_nCurrentWeaponId = nId; }
		DBOOL	IsMeleeWeaponEquipped();
		int		GetPistolFireHand()	{ return m_bPistolSplit ? m_nPistolFireHand : -1; }
		// Points a granted pistol shot at what that hand is aiming at, and
		// (unless VRPistolShotOrigin is off) starts it at that pistol's own
		// muzzle.  Returns DFALSE for anything but a split pair, leaving the
		// shot exactly as the game built it.
		DBOOL	ApplyPistolShotPose(int nWeaponId, const DVector &vCamPos, DBOOL bFootScale,
									DVector &vFlashPos, HLOCALOBJ hWeapon);

		// Motion controllers (through the bridge)
		DBOOL	IsControllerActive();
		// WinlatorXR leaks the controller face buttons as the keyboard letters
		// X/Y/B/A on top of the XrApi feed.  While the game is live and the
		// player is alive the shell drops those keys (the VR module owns those
		// buttons) - see CRiotClientShell::OnKeyDown/OnKeyUp.
		DBOOL	ShouldSwallowFaceKey(int nVk);
		DBOOL	UsesControllerAim();
		DDWORD	GetPressed()			{ return m_nPressed; }
		DDWORD	GetReleased()			{ return m_nReleased; }

		// Haptics (#40): the vibration follows the weapon's ammo counter, not
		// the trigger.  The shell calls this from the place ammo is actually
		// spent (CWeaponModel::Fire), so holding the trigger gives one pulse
		// per shot - a shotgun or a charged shot is one strong pulse instead of
		// a continuous shake - and it can never fire on a weapon switch, a dry
		// click, or an idle frame.
		void	ShotFired();

		// Grip remap (#42): on foot the game's double jump is silent, so the
		// right grip is reported as the left one (see VRCommandOn).  This is the
		// raw, pre-remap button word for anything that needs the real grips.
		DDWORD	GetRawButtons()			{ return m_nRawButtons; }
		void	SetGameplayInput(DBOOL b);
		DBOOL	IsGameplayInput()		{ return m_bGameplayInput; }
		void	SetPlayerDead(DBOOL b)		{ m_bPlayerDead = b; if (b) m_bPlayerJustDied = DTRUE; }
		void	ClearPlayerJustDied()		{ m_bPlayerJustDied = DFALSE; }
		DBOOL	PlayerJustDied()			{ return m_bPlayerJustDied; }
		void	SetLevelLoaded()			{ if (!m_bPlayerJustDied) { m_bAutosavePending = DTRUE; m_nAutosaveFrames = 0; } else { m_bPlayerJustDied = DFALSE; } }
		void	UpdateInput();				// per-frame: controller -> game commands / menu keys
		// Right A asks for the first/third-person view toggle.  The shell polls
		// this and performs the toggle directly: the game binds that view to the
		// keyboard letter B (defkeybd.cfg "##48"), which is one of the raw face
		// keys WinlatorXR also leaks, so injecting the bound key cannot work
		// (#23/#31).  Returns DTRUE once and clears the request.
		DBOOL	ConsumeViewToggle();
		DBOOL	GetMoveAxes(DFLOAT &fForward, DFLOAT &fRight);	// left stick (DFALSE if centred)
		int		PollWeaponCycle();			// right stick up/down: +1 next, -1 previous
		int		PollMenuDirection();		// sticks as arrow keys, with key repeat
		void	AdjustWeaponPose(DVector &vPos, DRotation &rRot, DBOOL bFootScale);	// puts the gun in your hand
		void	ScaleWeapon(HLOCALOBJ hWeapon, DBOOL bFootScale);

		// The third-person ("hand-held") gun model - no arms - in your hand,
		// instead of the first-person model.  Call after the first-person
		// weapon model has been updated for the frame.
		void	UpdateHeldGun(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos);

		// Muzzle flash placement (#29): the muzzle of the gun the player
		// actually sees, in world space, so the shell can draw the game's
		// muzzle-flash sprite there (VR holds the gun in the hand, but the
		// game still positions the flash for the flat-screen view).  Returns
		// DFALSE when VR isn't posing the gun by hand - the shell keeps its
		// own spot.
		// nFlashState is the WeaponState (W_FIRED / W_FIRED2) the flash is for;
		// pass it straight through from the shell so alternating barrels (Colt
		// 45) land on the right one (#41).  No default here: WeaponDefs.h's
		// WeaponState enum isn't visible in this header.
		DBOOL	GetMuzzleFlashWorld(int nWeaponId, const DVector &vCamPos, DBOOL bFootScale, DVector &vOut, int nFlashState);

		void	CheckRenderer(DBOOL bStable);	// resets the renderer if it's drawing at the wrong size
		void	FitGameWindow();			// resize the game window so its client area is the render size
		void	FitVRWindow();				// drop the title bar and fill the display, like HWXR

		// ShogoVR\ShogoVR.ini (written by the Shogo VR Settings window):
		// read at start and again whenever it changes.
		void	LoadSettingsFile(DBOOL bFirst);
		void	CheckSettingsFile();
		const char* GetIniPath()		{ return m_szIniPath; }

		// In a mech the crouch button transforms... (transform is now the left
		// stick click; this just records the mech/vehicle mode for the HUD).
		void	SetMechMode(DBOOL bMech, DBOOL bVehicle);
		DBOOL	GameHasFocus();
		void	HideAimHelpers();

		// ShogoVR\ShogoVR_game.log: notable events with the time, for bug reports.
		void	Log(const char* szFormat, ...);

		// The notice the Shogo source licence asks for on the opening screen:
		// who made the mod, and that Monolith didn't.  Shown on the menu for
		// the first 12 seconds.  pFont: a CBitmapFont (the menu's small font).
		void	DrawOpeningNotice(void* pFont);
		void	RequestTransform();
		DBOOL	EasyLadders();

		// Your own body (a client-side copy of your player model, animated
		// from how you actually move; head - and optionally arms - hidden).
		void	UpdateBody(HLOCALOBJ hPlayerObj, int nModelId, DBOOL bShow, DBOOL bCrouching);
		DBOOL	IsCrouchOn();		// crouch toggle/hold state, for the visible body (#20)
		void	HideBody();
		void	PrintBodyInfo(HLOCALOBJ hWeaponModel);
		DBOOL	IsTwoHanded()			{ return m_bTwoHanded; }
		void	UpdateAimMarker(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList);
		void	OnExitWorld();

	private:

		struct VRXform
		{
			DFLOAT	sx, sy;			// scale from virtual screen to real screen
			DFLOAT	ox, oy;			// offset (real screen pixels)
			int		clipL, clipR;	// region this pass may draw into: [clipL, clipR) x [clipT, clipB)
			int		clipT, clipB;
		};

		// Frame / geometry helpers
		void	RefreshFrame(DFLOAT fBaseFovX);
		int		GetMode();
		void	GetHalfRect(int nEye, int &nLeft, int &nRight);
		DFLOAT	EyeSign(int nEye)		{ return (nEye == 0) ? -1.0f : 1.0f; }	// -1 = left eye
		DFLOAT	EyeWidth()				{ return (DFLOAT)m_nScreenW * 0.5f; }
		DFLOAT	EyeAspect();
		DFLOAT	BaseEyeFovY(DFLOAT fBaseFovX);
		DFLOAT	FovXFromFovY(DFLOAT fFovY, DFLOAT fAspect);
		DFLOAT	FocalFromFovX(DFLOAT fFovX);
		DFLOAT	ClampFov(DFLOAT fFov);
		DFLOAT	GetWorldScale(DBOOL bFootScale);
		DFLOAT	GetIPDMeters();
		DFLOAT	GetHudShift();
		void	ComputeXform(int nEye, DFLOAT fScaleX, DFLOAT fScaleY, DFLOAT fShift,
							 DFLOAT fVirtW, DFLOAT fVirtH, VRXform &x);
		void	ComputeHudXform(int nEye, DBOOL bVirtualHud, VRXform &x);

		// 2D remapping helpers
		const VRXform& CurXform()		{ return (m_bCrosshairPass && m_bLastImmersive) ? m_xfCross : m_xfHud; }
		void	MapRect(const DRect &rcIn, DRect &rcOut);
		DBOOL	MapBlit(HSURFACE hSrc, DRect *pSrcRect, DRect *pVirtDest, DRect &rcDest, DRect &rcSrc);
		void	InstallHooks();
		void	RemoveHooks();
		DBOOL	SkipThisDraw();
		void	ComputeLayout();
		void	DrawScreenFlash(int nEye);
		void	UpdateCrouchOffset();	// ease the crouch eye-height offset (#20)

		// Headset link helpers
		void	OpenShared();
		void	CloseShared();
		void	WriteGameBlock();

		// Controller helpers
		DFLOAT	StickTurn();
		DBOOL	VRCommandOn(int nCommand);
		void	GetHandWorld(const DVector &vCamPos, DVector &vPos, DVector &vF, DVector &vU);
		// The same transform for an arbitrary hand pose, so the off-hand pistol
		// (#5) can be placed without a second copy of the body-frame maths.
		void	GetHandWorldFrom(const DVector &vCamPos, const DVector &vHandP,
								 const DVector &vHandF, const DVector &vHandU,
								 DVector &vPos, DVector &vF, DVector &vU);
		void	UpdateTwoHanded();
		int		HideModelNodes(HLOCALOBJ hObj, DBOOL bHead, DBOOL bArms);
		void	SetBodyAnim(const char* szAnim, DBOOL bLoop);
		void	SetAllNodesHidden(HLOCALOBJ hObj, DBOOL bHide);
		DBOOL	FindCommandKey(int nCommand, DDWORD &nScan, DBOOL &bExtended);
		void	StartBridgeIfNeeded();
		void	RememberCommandLine();
		void	ReleaseTransformKey();
		void	InjectScanTap(DDWORD nScan, DBOOL bExtended);
		void	InjectCommandTap(int nCommand);
		void	ReleaseInjectedKey();
		int		SetArmNodesHidden(HLOCALOBJ hObj, DBOOL bHide);
		// Dual pistols (#5): the Colt 45's first-person model holds both pistols
		// and both hands as two mirrored halves, so each half can be shown on its
		// own object.  nHalf 0 = the gun-hand half, 1 = the off-hand half.
		int		SetHalfNodesHidden(HLOCALOBJ hObj, int nHalf, DBOOL bHide);
		int		CountHalfNodes(HLOCALOBJ hObj, int nHalf);
		// Free off arm (#7): the submachine gun carries a second arm that holds
		// nothing, which can follow the off controller instead of the animation.
		void	UpdateOffArm(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos);
		int		SetArmGroupHidden(HLOCALOBJ hObj, int nGroup, DBOOL bHide);
		int		CountArmGroupNodes(HLOCALOBJ hObj, int nGroup);
		// DFALSE while the gun's own animation needs that arm - reloading seats
		// the magazine with it - so it is handed back for the duration.
		DBOOL	OffArmHandIsFree(HLOCALOBJ hPVModel);
		// A borrowed hand for a weapon whose model never drew one (the shotgun).
		void	UpdateDominantHand(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos);
		void	UpdateFreeOffHand(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos);
		// Two-handed grip (#8): the off hand goes on the gun instead of on its
		// own controller, and the gun aims along the line between the hands.
		int		SetHandGroupHidden(HLOCALOBJ hObj, int nGroup, DBOOL bHide);
		int		CountHandGroupNodes(HLOCALOBJ hObj, int nGroup);
		int		SetRifleArmsHidden(HLOCALOBJ hObj, DBOOL bHide);
		int		LentHandGroup(DBOOL bDominant);
		DBOOL	UseLentOffHand(int nWeaponId);
		// The built-in seating of the shotgun's lent hand, which differs
		// between the handedness modes - see the .cpp.
		void	DomHandSeat(DFLOAT &fX, DFLOAT &fY, DFLOAT &fZ);
		// Seats a lent hand on the WEAPON rather than on the controller.
		void	PoseLentHandOnGun(HLOCALOBJ hObj, int nGroup, HLOCALOBJ hGun,
								  const DVector &vHandPos,
								  DFLOAT fX, DFLOAT fY, DFLOAT fZ, DFLOAT fRoll);
		// TEMPORARY, for seating the lent hand on device - see the .cpp.
		void	UpdateNudgeKeys();
		// With VRVerboseDebug on, prints the incoming XrApi packet every few
		// seconds so the tester and the headset can be compared directly.
		void	LogRawPacket();
		// One log line per weapon change saying what the mod decided about
		// that weapon's hands - see UpdateHeldGun.
		void	LogWeaponHands(HLOCALOBJ hPVModel, int nWeaponId);
		// Which hand group sits further along the barrel, measured on the
		// model itself: 0, 1, or -1 when it cannot tell.
		int		ForwardHandGroup(HLOCALOBJ hObj);
		DBOOL	GetHandGroupDrift(HLOCALOBJ hObj, int nGroup, DVector &vDrift);
		DBOOL	CanEngageGrip();
		void	ToggleTwoHandGrip();
		// Shared by both: a hand borrowed from another weapon's model.
		HLOCALOBJ BorrowHand(const DVector &vCamPos);
		void	PoseBorrowedHand(HLOCALOBJ hObj, int nGroup, const DVector &vHandP,
								 const DVector &vHandF, const DVector &vHandU,
								 const DVector &vCamPos,
								 DFLOAT fTX, DFLOAT fTY, DFLOAT fTZ, DFLOAT fRoll);
		void	UpdateDualPistols(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos);
		// Where a hand node has drifted to under the fire animation, as a world
		// offset from its object's origin, so a copy can be placed to put that
		// node back in the player's hand.
		DBOOL	GetPistolAnchorDrift(HLOCALOBJ hObj, int nHalf, DVector &vDrift);
		// How far one named node has been carried from its object's origin.
		DBOOL	GetNodeDrift(HLOCALOBJ hObj, const char* szNode, DVector &vDrift);
		int		PickPistolFireHand();
		void	DumpModelNodes(HLOCALOBJ hObj, int nWeaponId);
		static DBOOL	Hook_IsCommandOn(int nCommand);
		DBOOL	(*m_pfnIsCommandOn)(int);

		// The mouse (and joystick) axes reach the game through GetAxisOffsets;
		// while the controllers are live we answer with zeros so keyboard and
		// mouse can't move or aim the player behind the mod's back.
		static void		Hook_GetAxisOffsets(DFLOAT *offsets);
		void	(*m_pfnGetAxisOffsets)(DFLOAT *);

		// Replacement engine functions (only installed during a 2D pass)
		static DRESULT	Hook_DrawSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY);
		static DRESULT	Hook_DrawSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY, HDECOLOR hColor);
		static DRESULT	Hook_DrawSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY, HDECOLOR hTransColor, HDECOLOR hFillColor);
		static DRESULT	Hook_ScaleSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect);
		static DRESULT	Hook_ScaleSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect, HDECOLOR hColor);
		static DRESULT	Hook_ScaleSurfaceToSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect, HDECOLOR hTransColor, HDECOLOR hFillColor);
		static DRESULT	Hook_FillRect(HSURFACE hDest, DRect *pRect, HDECOLOR hColor);
		static void		Hook_DrawStringToSurface(HSURFACE hDest, HDEFONT hFont, HSTRING hString, DRect *pRect, HDECOLOR hForeColor, HDECOLOR hBackColor);
		static float	Hook_GetFrameTime();
		static void		Hook_GetSurfaceDims(HSURFACE hSurf, DDWORD *pWidth, DDWORD *pHeight);
		static void		Hook_GetCameraRect(HLOCALOBJ hObj, DBOOL *pbFullscreen, int *pLeft, int *pTop, int *pRight, int *pBottom);

		// Original engine functions, saved while hooked
		DRESULT	(*m_pfnDrawSurfaceToSurface)(HSURFACE, HSURFACE, DRect*, int, int);
		DRESULT	(*m_pfnDrawSurfaceToSurfaceTransparent)(HSURFACE, HSURFACE, DRect*, int, int, HDECOLOR);
		DRESULT	(*m_pfnDrawSurfaceSolidColor)(HSURFACE, HSURFACE, DRect*, int, int, HDECOLOR, HDECOLOR);
		DRESULT	(*m_pfnScaleSurfaceToSurface)(HSURFACE, HSURFACE, DRect*, DRect*);
		DRESULT	(*m_pfnScaleSurfaceToSurfaceTransparent)(HSURFACE, HSURFACE, DRect*, DRect*, HDECOLOR);
		DRESULT	(*m_pfnScaleSurfaceToSurfaceSolidColor)(HSURFACE, HSURFACE, DRect*, DRect*, HDECOLOR, HDECOLOR);
		DRESULT	(*m_pfnFillRect)(HSURFACE, DRect*, HDECOLOR);
		HSURFACE	m_hMovieSurface;
		DRect		m_rcMovieSrc;
		DBOOL		m_bMovieSrcKnown;
		DBOOL		m_bMovieFrameStored;	// m_hMovieSurface holds a fresh movie frame
		DBOOL		m_bMovieNeedClear;		// blank the window once before trusting what is on it
		void	(*m_pfnDrawStringToSurface)(HSURFACE, HDEFONT, HSTRING, DRect*, HDECOLOR, HDECOLOR);
		float	(*m_pfnGetFrameTime)();
		void	(*m_pfnGetSurfaceDims)(HSURFACE, DDWORD*, DDWORD*);
		void	(*m_pfnGetCameraRect)(HLOCALOBJ, DBOOL*, int*, int*, int*, int*);

		CClientDE*	m_pClientDE;
		DBOOL		m_bHooked;
		DBOOL		m_bCrosshairPass;
		DBOOL		m_bLastImmersive;	// was the last 3D render this frame immersive?
		DBOOL		m_bFlash;			// a screen tint is active this frame (#28)
		DFLOAT		m_fFlashR, m_fFlashG, m_fFlashB;	// current tint colour, 0..1
		int			m_nCurEye;
		HSURFACE	m_hScreen;
		VRXform		m_xfHud;
		DBOOL		m_bVirtualHud;		// this 2D pass lays the HUD out on a smaller virtual screen
		DBOOL		m_bVirtual2D;		// this 2D pass lays 2D out on a virtual screen (HUD or menu)
		DBOOL		m_bSkipNonCross;	// this pass draws only the crosshair (HUD strip, second eye)
		DBOOL		m_bFlatFrame;		// this frame is flat (menus etc. on a floating screen)
		DBOOL		m_bHudStripAllowed;
		DBOOL		m_bHudStrip;		// this frame's HUD goes into its own strip
		long		m_nEyeH;			// eye pictures use rows [0, m_nEyeH)
		long		m_nHudX, m_nHudY, m_nHudW, m_nHudH;
		int			m_nHudScale;
		DFLOAT		m_fHudVirtW, m_fHudVirtH;
		DFLOAT		m_fMenuVirtW, m_fMenuVirtH;	// size menus are laid out for (smaller = bigger text)
		DFLOAT		m_fVirtW;			// size of the screen the 2D code thinks it's drawing to
		DFLOAT		m_fVirtH;
		VRXform		m_xfCross;

		// Per-frame values
		int			m_nMode;
		DDWORD		m_nScreenW;
		DDWORD		m_nScreenH;
		DFLOAT		m_fLastBaseFovX;	// game's unzoomed horizontal FOV (radians)
		DFLOAT		m_fFocalBase;		// eye focal length in pixels at the unzoomed FOV
		DFLOAT		m_fFocalRender;		// eye focal length of the last immersive render
		DFLOAT		m_fIpdWorld;		// eye separation (world units) of the last immersive render
		DFLOAT		m_fEyeFovX;			// last immersive per-eye FOV (radians), for VRInfo
		DFLOAT		m_fEyeFovY;
		DFLOAT		m_fAimInvDepth;		// smoothed 1 / distance to the aim point (world units)
		DFLOAT		m_fBaseTanX;		// tan(half-FOV) of an unzoomed eye picture
		DFLOAT		m_fBaseTanY;

		// Headset link state
		CXrApiUDP*		m_pXrApi;			// WinlatorXR XrApi UDP client (in-process)
		void*			m_hMapping;
		ShogoVRShared*	m_pShared;
		DBOOL		m_bBridgeAlive;
		DBOOL		m_bMenuLayoutDirty;	// VR menu size became available: menus need re-laying out
		long		m_nLastBridgeBeat;
		DFLOAT		m_fLastBridgeBeatTime;
		long		m_nBridgeFlags;
		long		m_nPoseId;			// pose read this frame
		DVector		m_vHeadF;			// head forward/up/position, LithTech space (metres)
		DVector		m_vHeadU;
		DVector		m_vHeadP;
		DFLOAT		m_fHeadYaw;
		DFLOAT		m_fHeadPitch;
		DFLOAT		m_fNeedTanX;		// FOV the headset wants covered
		DFLOAT		m_fNeedTanY;
		DBOOL		m_bHaveLastHeadYaw;
		DFLOAT		m_fLastHeadYaw;
		DFLOAT		m_fBodyYaw;			// mouse/keyboard yaw (aim yaw minus head yaw)
		DBOOL		m_bHeadApplied;		// ApplyHeadTracking ran this frame
		DDWORD		m_nGameHwnd;
		long		m_nGameBeat;
		long		m_nReportPoseId;	// what the last render used - sent to the bridge
		long		m_nReportViewMode;
		DFLOAT		m_fReportTanX;
		DFLOAT		m_fReportTanY;
		DFLOAT		m_fReportSep;
		long		m_nRenderW, m_nRenderH;		// screen mode, for the bridge's window fixer
		long		m_nClientW, m_nClientH;		// our window's inside, in our own pixels
		long		m_nSurfW, m_nSurfH;			// what the renderer is really drawing at
		DFLOAT		m_fRendererBadSince;
		DFLOAT		m_fLastRendererReset;
		int			m_nRendererResets;
		DFLOAT		m_fWindowBadSince;
		DFLOAT		m_fLastWindowFix;
		int			m_nWindowFixes;
		long		m_nLoggedWindowW, m_nLoggedWindowH;
		DFLOAT		m_fGunScaleUsed;
		char		m_szIniPath[300];
		DDWORD		m_nIniTimeLo, m_nIniTimeHi;
		DFLOAT		m_fLastIniCheck;

		// Controllers
		long		m_nCtrlFlags;
		DDWORD		m_nButtons;
		DDWORD		m_nRawButtons;		// the runtime's button word, before any handling
		DDWORD		m_nPrevButtons;
		char		m_szPrevBools[24];	// last raw runtime T/F string seen, for the #45 press log
		DDWORD		m_nPressed;			// went down this frame
		DDWORD		m_nReleased;		// went up this frame
		DFLOAT		m_fMoveX, m_fMoveY;
		DFLOAT		m_fTurnX, m_fTurnY;
		DFLOAT		m_fHapticL, m_fHapticR;	// remaining vibration frames sent to the runtime
		DVector		m_vRightF, m_vRightU, m_vRightP;
		DVector		m_vLeftF, m_vLeftU, m_vLeftP;
		DBOOL		m_bLeftHanded;		// dominant hand is the left (Controls/LeftHanded): swap hands
		DBOOL		m_bGameplayInput;	// controller buttons act as game commands (not menu keys)
		DBOOL		m_bWantViewToggle;	// Right A pressed - the shell performs the view toggle (#31)
		DDWORD		m_nSuppressed;		// buttons held when gameplay resumed - ignored until released
		DBOOL		m_bAimByHand;		// this frame's aim comes from the right hand
		DBOOL		m_bPlayerDead;		// the player is dead (left trigger quick-loads)
		DBOOL		m_bCrouchOn;		// crouch toggle state (X button)
		DFLOAT		m_fCrouchAmt;		// eased crouch amount 0..1, lowers the eyes (#20)
		DBOOL		m_bAutosavePending;	// a level just loaded - quicksave once playable (#16)
		int			m_nAutosaveFrames;	// frames of continuous play since the level loaded
		DBOOL		m_bPlayerJustDied;	// set when player dies; if true on SetLevelLoaded, don't autosave (#43)
		DBOOL		m_bHaveBody;
		DFLOAT		m_fLastOutYaw;
		DBOOL		m_bSnapLatched;
		int			m_nWeaponLatch;
		DFLOAT		m_fWepSwapLastTime;
		DBOOL		m_bWepSwapPhysLeftLatch;
		DBOOL		m_bWepSwapPhysRightLatch;
		int			m_nMenuDir;
		DFLOAT		m_fMenuRepeatTime;
		DFLOAT		m_fWorldScaleUsed;
		HLOCALOBJ	m_hAimMarker;		// reticle (the game's third-person crosshair sprite)
		HLOCALOBJ	m_hAimDot;			// red dot
		HLOCALOBJ	m_hLaser;			// laser beam (a line system)
		HDELINE		m_hLaserLines[5];

		// Two-handed aiming: the gun points from the gun hand towards the other hand
		DBOOL		m_bTwoHanded;
		DVector		m_vAimF, m_vAimU;	// aim direction actually used (one or two hands)

		// Body
		HLOCALOBJ	m_hBody;
		int			m_nBodyModelId;
		DVector		m_vBodyLastPos;
		DVector		m_vBodyVel;
		DFLOAT		m_fBodyMaxSpeed;
		DFLOAT		m_fBodyVisYaw;
		DBOOL		m_bBodyVisYawSet;
		char		m_szBodyAnim[16];
		int			m_nBodyHidden;		// nodes hidden on the body
		DBOOL		m_bBodyArmsShown;

		// Held (third-person) gun
		HLOCALOBJ	m_hHeldGun;
		int			m_nHeldGunId;
		HLOCALOBJ	m_hHiddenPV;		// first-person weapon model whose parts we've hidden
		HLOCALOBJ	m_hArmlessPV;		// first-person weapon model whose arm parts we've hidden
		DDWORD		m_nDumpedWeapons[2];	// weapons whose model parts are listed in ShogoVR_models.txt

		// Dual pistols (#5)
		HLOCALOBJ	m_hOffHandGun;		// the off hand's pistol: our own copy of the Colt 45 model
		int			m_nOffHandGunId;	// which weapon that copy was made for
		HLOCALOBJ	m_hSplitPV;			// first-person model we've split into halves
		DBOOL		m_bPistolSplit;		// the split is live this frame
		DBOOL		m_bOffArmSplit;		// #7: the off arm is on its controller this frame
		HLOCALOBJ	m_hOffArmPV;		// the model we hid an arm group on
		HLOCALOBJ	m_hOffArmAnimFor;	// the model the animation indices below were read from
		DDWORD		m_nOffArmFreeAnim[8];	// animations during which the arm is free
		int			m_nOffArmFreeCount;
		HLOCALOBJ	m_hDomHand;			// borrowed dominant hand (shotgun)
		int			m_nDomHandFor;		// which weapon it was borrowed for
		int			m_nLoggedWeapon;	// last weapon LogWeaponHands reported
		HLOCALOBJ	m_hFreeHand;		// borrowed off hand (knife, squeaky toy)
		DBOOL		m_bTwoHandGrip;		// #8: the off hand is on the gun
		int			m_nPistolFireHand;	// the hand whose shot is in flight, or -1
		int			m_nPistolLastHand;	// the hand that fired the previous shot, for alternating
		DDWORD		m_nOffHandAnim;		// animation the off-hand copy is following, so it can track the real one
		DVector		m_vPistolAnchorFix;	// gun-hand anchor correction, applied by AdjustWeaponPose
		DVector		m_vWeaponPoseOrigin;	// the origin AdjustWeaponPose actually used this frame
		HLOCALOBJ	m_hCameraObj;		// the game camera, cached so the weapon pose can find it
		DBOOL		m_bWeaponPoseOrigin;	// ...and whether it has been given one yet
		DVector		m_vPistolAimF;		// aim being eased towards the firing pistol's own direction
		DBOOL		m_bPistolAimValid;	// m_vPistolAimF has been seeded
		DBOOL		m_bCaptureHook;

		// Physical motion melee (#3)
		int			m_nCurrentWeaponId;
		DVector		m_vMeleeHandPrevP;
		DBOOL		m_bMeleeHandPrevValid;
		DFLOAT		m_fMeleeLastFireTime;
		DBOOL		m_bMeleeTrigger;

		DBOOL		m_bMech;
		DBOOL		m_bVehicle;
		DFLOAT		m_fLoggedZoom;
		DFLOAT		m_fLoggedCamFovX, m_fLoggedCamFovY;
		long		m_nLoggedRenderW, m_nLoggedRenderH, m_nLoggedSurfW, m_nLoggedSurfH, m_nLoggedClientW, m_nLoggedClientH;
		DBOOL		m_bLoggedBridge;
		int			m_nLoggedHudStrip;
		int			m_nLoggedCapture;		// 0 nothing yet, 1 frames seen, 2 frames handed over
		DBOOL		m_bTransformKeyDown;
		DDWORD		m_nTransformUpTick;
		DDWORD		m_nTransformScan;
		DBOOL		m_bTransformExt;
		DBOOL		m_bWarnedNoTransformKey;
		DDWORD		m_nInjScan;			// a key we pressed for the game, released on a timer
		DBOOL		m_bInjExt;
		DDWORD		m_nInjUpTick;
		DBOOL		m_bStartedBridge;		// we started the headset bridge (and close it again)
		int			m_nBridgeStarts;
		DDWORD		m_nNextBridgeCheck;		// GetTickCount() of the next "is a bridge running?" check
		char		m_szAuthors[200];		// from ShogoVR\AUTHORS.txt
		DFLOAT		m_fNoticeStart;
		HSURFACE	m_hNotice[2];
		DBOOL		m_bNoticeDone;

		// Console variables
		VarTrack	m_vtStereo;			// VRStereo         0 off, 1 full SBS, 2 half SBS
		VarTrack	m_vtIPD;			// VRIPD            eye distance in millimetres
		VarTrack	m_vtFov;			// VRFov            vertical per-eye FOV in degrees (0 = match the game)
		VarTrack	m_vtScaleFoot;		// VRScaleFoot      world units per metre on foot
		VarTrack	m_vtScaleMCA;		// VRScaleMCA       world units per metre in an MCA
		VarTrack	m_vtEyeHeightFoot;	// VREyeHeightFoot  metres, used by VRCalibrate
		VarTrack	m_vtEyeHeightMCA;	// VREyeHeightMCA   metres, used by VRCalibrate
		VarTrack	m_vtHudDepth;		// VRHudDepth       metres
		VarTrack	m_vtVerbose;		// VRVerboseDebug   the diagnostic logging, off by default
		DFLOAT		m_fNextRawLog;		// when to print the next raw XrApi packet
		VarTrack	m_vtHudAutoScale;	// VRHudAutoScale   shrink the HUD on oversized containers
		VarTrack	m_vtHudScale;		// VRHudScale       size of the HUD/menu panel on a monitor (1 = fit the eye)
		VarTrack	m_vtHudWidth;		// VRHudWidth       headset: width of the HUD in degrees
		VarTrack	m_vtHudLayout;		// VRHudLayout      headset: screen width the HUD is laid out for (smaller = bigger HUD)
		VarTrack	m_vtMenuWidth;		// VRMenuWidth      headset: width of menus/floating screen in degrees
		VarTrack	m_vtMenuLayout;		// VRMenuLayout     headset: screen width menus are laid out for (smaller = bigger text)
		VarTrack	m_vtHudStrip;		// VRHudStrip       -1 auto, 1 always, 0 never: HUD as its own sharp panel
		VarTrack	m_vtHudPixels;		// VRHudPixels      whole-number size of the HUD in its strip (1 = original pixels)
		VarTrack	m_vtHudSnap;		// VRHudSnap        1 = whole-number HUD scaling when drawn in the eyes
		VarTrack	m_vtFlatMenus;		// VRFlatMenus      1 = menus/cutscenes as the flat game on a floating screen
		VarTrack	m_vtCrossScale;		// VRCrosshairScale crosshair size in screen pixels (1 = original)
		VarTrack	m_vtCrossDepth;		// VRCrosshairDepth 1 = crosshair sits on what you aim at, 0 = at HUD depth
		VarTrack	m_vtCutscenePanel;	// VRCutscenePanel  1 = show cutscenes on a floating screen
		VarTrack	m_vtCutsceneStereo;	// VRCutsceneStereo stereo strength inside the cutscene screen (0..1)
		VarTrack	m_vtSwapEyes;		// VRSwapEyes       1 = right eye on the left half
		VarTrack	m_vtAutoStereo;		// VRAutoStereo     1 = turn stereo on by itself when the bridge runs
		VarTrack	m_vtHeadTracking;	// VRHeadTracking   1 = the headset turns the view (and aim)
		VarTrack	m_vtHeadPosition;	// VRHeadPosition   1 = leaning/moving your head moves the view
		VarTrack	m_vtControllers;	// VRControllers    1 = use motion controllers
		VarTrack	m_vtAimMode;		// VRAimMode        1 = right hand aims, 0 = head aims
		VarTrack	m_vtSnapTurn;		// VRSnapTurn       degrees per snap (0 = smooth turning)
		VarTrack	m_vtTurnSpeed;		// VRTurnSpeed      smooth turning speed, degrees per second
		VarTrack	m_vtGunX;			// VRGunX/Y/Z       nudge the gun model in your hand (game units)
		VarTrack	m_vtGunY;
		VarTrack	m_vtGunZ;
		VarTrack	m_vtAimMarker;		// VRAimMarker      0 = no aim helper (older setting)
		VarTrack	m_vtAimStyle;		// VRAimStyle       0 laser + dot, 1 dot, 2 reticle, 3 none
		VarTrack	m_vtGameCrosshair;	// VRGameCrosshair  1 = draw the game's own crosshair (default off)
		VarTrack	m_vtAimDotSize;		// VRAimDotSize     size of the aim dot
		VarTrack	m_vtGunScale;		// VRGunScale       size of the gun/arms in your hand, on foot
		VarTrack	m_vtGunScaleMCA;	// VRGunScaleMCA    same, in an MCA
		VarTrack	m_vtFixRenderer;	// VRFixRenderer    1 = reset the renderer if it draws at the wrong size
		VarTrack	m_vtVRFullscreen;	// VRFullscreen     1 = drop the title bar and fill the display
		VarTrack	m_vtMoveDir;		// VRMoveDir        walk towards: 0 gun hand, 1 head, 2 other hand
		VarTrack	m_vtTwoHand;		// VRTwoHand        1 = support the gun with your other hand to aim it
		VarTrack	m_vtHandFlip;		// VRHandFlip       0 = off (default; roll corrected for a Quest 3), 1 = as reported
		VarTrack	m_vtBody;			// VRBody           1 = show your body
		VarTrack	m_vtBodyArms;		// VRBodyArms       1 = show the body's arms too (they can't follow your hands)
		VarTrack	m_vtBodyOffset;		// VRBodyOffset     metres the body sits behind your eyes
		VarTrack	m_vtCrouchDrop;		// VRCrouchDrop     metres to lower the eyes while crouched (#20)
		VarTrack	m_vtMoveDeadZone;	// VRMoveDeadZone   left stick dead zone (radial), 0..1 (#22)
		VarTrack	m_vtWeaponSwapDead;	// VRWeaponSwapDead right stick up/down dead zone to change weapon, 0..1 (#37)
		VarTrack	m_vtGunModel;		// VRGunModel       1 = third-person gun (no arms), 0 = first-person model with arms
		VarTrack	m_vtHeldPitch;		// VRHeldPitch/Yaw/Roll  degrees, to correct the held gun's angle
		VarTrack	m_vtHeldYaw;
		VarTrack	m_vtHeldRoll;
		VarTrack	m_vtHeldX;			// VRHeldX/Y/Z      nudge the held gun in your hand (game units)
		VarTrack	m_vtHeldY;
		VarTrack	m_vtHeldZ;
		VarTrack	m_vtHeldScale;		// VRHeldScale      size of the held gun
		VarTrack	m_vtFlashOffset;	// VRFlashOffset    muzzle flash: how far past the barrel (out along it)
		VarTrack	m_vtFlashLower;		// VRFlashLower     muzzle flash: how far below the barrel
		VarTrack	m_vtMuzzleLateral;	// VRMuzzleLateral  muzzle flash: left/right (0 = on the barrel axis)
		VarTrack	m_vtPistolSpread;	// VRPistolSpread   muzzle flash: dual-pistol left/right spread scale (1 = game default)
		VarTrack	m_vtGunArms;		// VRGunArms        1 = show Sanjuro's arms on the first-person gun
		VarTrack	m_vtDirectCapture;	// VRDirectCapture  1 = hand frames straight from the renderer to the bridge
		VarTrack	m_vtEasyLadders;	// VREasyLadders    1 = on a ladder the stick climbs up/down
		VarTrack	m_vtAutoBridge;		// VRAutoBridge     1 = start the headset bridge with the game
		VarTrack	m_vtHaptics;		// VRHaptics        1 = pulse the controllers on fire/weapon edges
		VarTrack	m_vtSyncHold;		// VRSyncHold       1 = block for a new HMD_SYNC each frame (default on)
		VarTrack	m_vtFlashTest;		// VRFlashTest      0 = off; non-zero forces a red flash every frame (diagnostic)
		VarTrack	m_vtMeleeSens;		// Gesture melee sensitivity
		VarTrack	m_vtWepSwapPhysical;	// VRPhysicalWepSwap
		VarTrack	m_vtWepSwapThumb;		// VRWepSwapThumbstick
		VarTrack	m_vtDualPistols;	// VRDualPistols    1 = a Colt 45 in each hand (#5)
		VarTrack	m_vtPistolSwapHalves;	// VRPistolSwapHalves 1 = swap which model half goes in which hand
		VarTrack	m_vtPistolOffX;		// VRPistolOffX     off-hand pistol nudge, right (model units)
		VarTrack	m_vtPistolOffY;		// VRPistolOffY     off-hand pistol nudge, up
		VarTrack	m_vtPistolOffZ;		// VRPistolOffZ     off-hand pistol nudge, forward
		VarTrack	m_vtPistolOffRoll;	// VRPistolOffRoll  off-hand pistol roll, degrees
		VarTrack	m_vtSplitOffHand;	// VRSplitOffHand   1 = free arm follows the off controller (#7)
		VarTrack	m_vtOffHandAuto;	// VROffHandAuto    work out the fore hand by measuring
		VarTrack	m_vtLentHandSwap;	// VRLentHandSwap   flip which borrowed hand is which
		VarTrack	m_vtNudgeKeys;		// VRNudgeKeys      TEMPORARY q/e/r/u hand nudging
		VarTrack	m_vtNudgeStep;		// VRNudgeStep      how far one press moves it
		DDWORD		m_nNudgeNextTick;	// auto-repeat while a nudge key is held
		int			m_nNudgeLastHand;	// -1, or the handedness the trims belong to
		VarTrack	m_vtOffHandSwap;	// VROffHandSwap    which arm group is the free one
		VarTrack	m_vtOffHandAnchor;	// VROffHandAnchor  1 = pin the free palm to the controller
		VarTrack	m_vtOffHandX;		// VROffHandX       free-arm seating trims
		VarTrack	m_vtOffHandY;
		VarTrack	m_vtOffHandZ;
		VarTrack	m_vtOffHandRoll;	// VROffHandRoll    free-arm roll, degrees
		VarTrack	m_vtDomHand;		// VRDomHand        1 = lend a hand to guns that have none
		VarTrack	m_vtDomHandX;		// VRDomHandX       its seating trims
		VarTrack	m_vtDomHandY;
		VarTrack	m_vtDomHandZ;
		VarTrack	m_vtDomHandRoll;	// VRDomHandRoll    its roll, degrees
		VarTrack	m_vtFreeHand;		// VRFreeHand       1 = lend an off hand to weapons that have none
		VarTrack	m_vtFreeHandX;		// VRFreeHandX      its seating trims
		VarTrack	m_vtFreeHandY;
		VarTrack	m_vtFreeHandZ;
		VarTrack	m_vtFreeHandRoll;	// VRFreeHandRoll   its roll, degrees
		VarTrack	m_vtHandArm;		// VRHandArm        1 = draw the forearm with a hand we pose ourselves
		VarTrack	m_vtTwoHandWeapons;	// VRTwoHandWeapons  the master switch the
										// "Two handed weapons" menu row drives:
										// off restores the stock hands and
										// dominant-hand aim on every weapon
		VarTrack	m_vtTwoHandGrip;	// VRTwoHandGrip    1 = the off trigger toggles a two-handed grip (#8)
		VarTrack	m_vtGripDist;		// VRGripDist       furthest the hands may be apart to take hold, metres
		VarTrack	m_vtGripAngle;		// VRGripAngle      how far off the barrel the off hand may be, degrees
		VarTrack	m_vtPistolMirror;	// VRPistolMirror   1 = undo the hand-flip roll for the mirrored off-hand half
		VarTrack	m_vtPistolAnchor;	// VRPistolAnchor   1 = pin each hand's palm node to its controller
		VarTrack	m_vtPistolAimSnap;	// VRPistolAimSnap  how fast the aim swings to the firing pistol (0 = never)
		VarTrack	m_vtPistolShotOrigin;	// VRPistolShotOrigin 1 = shots start at the firing pistol's muzzle
};

extern CVRStereo g_VRStereo;

#endif // __VRSTEREO_H__
