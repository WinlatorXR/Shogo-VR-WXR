// ======================================================================= //
//
// MODULE  : VRStereo.cpp
//
// PURPOSE : Side-by-side stereo rendering for Shogo (VR mod, stage 1)
//
// ======================================================================= //

#include "VRStereo.h"
#include "ClientWeaponUtils.h"
#include "RiotCommandIDs.h"
#include "WeaponDefs.h"
#include "ModelFuncs.h"
#include "CharacterAlignment.h"
#include "VRCapture.h"
#include "BitmapFont.h"
#include "TextHelper.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

static void VRInstallCrashReporter();
static void VRRemoveCrashReporter();
static void VRSendScanCode(DDWORD nScan, DBOOL bExtended, DBOOL bUp);

CVRStereo g_VRStereo;

#define VR_PI				3.141592653589793f
#define VR_DEG2RAD(x)		((x) * VR_PI / 180.0f)
#define VR_RAD2DEG(x)		((x) * 180.0f / VR_PI)
#define VR_MIN_FOV			0.0315f		// engine clamps to PI/100
#define VR_MAX_FOV			3.1100f		// engine clamps to 99*PI/100
#define VR_AIM_RANGE		10000.0f	// how far we look for something under the crosshair
#define VR_AIM_SMOOTHING	12.0f		// higher = crosshair depth follows faster
#define VR_FLOOR_RANGE		4000.0f		// how far VRCalibrate looks for the floor

// Every mod-owned copy that rides along with the engine's first-person weapon
// has to be drawn in the SAME pass as that weapon, or it sorts against the
// world instead of against the gun.  The engine creates the PV weapon with
// FLAG_REALLYCLOSE (WeaponModel.cpp), which the renderer uses to draw PV
// models last and in a compressed depth range, so they never clip into a wall
// the player is standing against.  A copy without the flag is an ordinary
// world object: its depth is compared against the world's rather than the
// gun's, and the result is a lent hand that punches straight through the
// weapon it is supposed to be holding - which is what the owner saw once the
// shotgun's hand was finally in the right place.
//
// So the lent hands, the split off-hand and the second pistol all carry it.
// NOT the body avatar or the VRGunModel=1 held gun: those are deliberately
// world objects and should be occluded by level geometry like anything else.
#define VR_PV_FLAGS			(FLAG_VISIBLE | FLAG_REALLYCLOSE | FLAG_MODELGOURAUDSHADE)

static int VRRound(DFLOAT f)
{
	return (int)floor(f + 0.5f);
}

static DFLOAT VRMinF(DFLOAT a, DFLOAT b) { return (a < b) ? a : b; }

// Clips one axis of a blit to [c0, c1), moving the source edges by the same
// proportion so a clipped scaled blit still lines up.
static DBOOL VRClip1D(int &d0, int &d1, int &s0, int &s1, int c0, int c1)
{
	if (d1 <= d0 || s1 <= s0) return DFALSE;

	int nDstLen = d1 - d0;
	int nSrcLen = s1 - s0;

	if (d0 < c0)
	{
		s0 += (int)(((DFLOAT)(c0 - d0) * (DFLOAT)nSrcLen) / (DFLOAT)nDstLen);
		d0 = c0;
	}
	if (d1 > c1)
	{
		s1 -= (int)(((DFLOAT)(d1 - c1) * (DFLOAT)nSrcLen) / (DFLOAT)nDstLen);
		d1 = c1;
	}

	return (d1 > d0 && s1 > s0);
}


// ======================================================================= //
//	Construction / setup
// ======================================================================= //

CVRStereo::CVRStereo()
{
	m_pClientDE			= DNULL;
	m_bHooked			= DFALSE;
	m_bCrosshairPass	= DFALSE;
	m_bLastImmersive	= DFALSE;
	m_bFlash			= DFALSE;
	m_fFlashR			= 0.0f;
	m_fFlashG			= 0.0f;
	m_fFlashB			= 0.0f;
	m_nCurEye			= 0;
	m_hScreen			= DNULL;
	m_nMode				= VR_STEREO_OFF;
	m_nScreenW			= 640;
	m_nScreenH			= 480;
	m_fLastBaseFovX		= VR_DEG2RAD(90.0f);
	m_fFocalBase		= 1.0f;
	m_fFocalRender		= 1.0f;
	m_fIpdWorld			= 0.0f;
	m_fEyeFovX			= 0.0f;
	m_fEyeFovY			= 0.0f;
	m_fAimInvDepth		= 0.0f;
	m_fBaseTanX			= 1.0f;
	m_fBaseTanY			= 1.0f;

	m_pXrApi				= DNULL;
	m_hMapping				= DNULL;
	m_pShared				= DNULL;
	m_bBridgeAlive			= DFALSE;
	m_bMenuLayoutDirty		= DFALSE;
	m_nLastBridgeBeat		= 0;
	m_fLastBridgeBeatTime	= -100.0f;
	m_nBridgeFlags			= 0;
	m_nPoseId				= 0;
	VEC_SET(m_vHeadF, 0.0f, 0.0f, 1.0f);
	VEC_SET(m_vHeadU, 0.0f, 1.0f, 0.0f);
	VEC_SET(m_vHeadP, 0.0f, 0.0f, 0.0f);
	m_fHeadYaw				= 0.0f;
	m_fHeadPitch			= 0.0f;
	m_fNeedTanX				= 0.0f;
	m_fNeedTanY				= 0.0f;
	m_bHaveLastHeadYaw		= DFALSE;
	m_fLastHeadYaw			= 0.0f;
	m_fBodyYaw				= 0.0f;
	m_bHeadApplied			= DFALSE;
	m_nGameHwnd				= 0;
	m_nGameBeat				= 0;
	m_nReportPoseId			= 0;
	m_nReportViewMode		= SHOGOVR_VIEW_OFF;
	m_fReportTanX			= 1.0f;
	m_fReportTanY			= 1.0f;
	m_fReportSep			= 0.0f;
	m_nRenderW = m_nRenderH	= 0;
	m_nClientW = m_nClientH	= 0;
	m_nSurfW = m_nSurfH		= 0;
	m_fRendererBadSince		= -1.0f;
	m_fLastRendererReset	= -1000.0f;
	m_nRendererResets		= 0;
	m_fWindowBadSince		= -1.0f;
	m_fLastWindowFix		= -1000.0f;
	m_nWindowFixes			= 0;
	m_nLoggedWindowW = m_nLoggedWindowH = 0;
	m_fGunScaleUsed			= 1.0f;
	m_szIniPath[0]			= 0;
	m_nIniTimeLo = m_nIniTimeHi = 0;
	m_fLastIniCheck			= -100.0f;

	m_pfnIsCommandOn		= DNULL;
	m_pfnGetAxisOffsets		= DNULL;
	m_nCtrlFlags			= 0;
	m_nButtons				= 0;
	m_nPrevButtons			= 0;
	m_szPrevBools[0]		= 0;
	m_nPressed				= 0;
	m_nReleased				= 0;
	m_fMoveX = m_fMoveY		= 0.0f;
	m_fTurnX = m_fTurnY		= 0.0f;
	m_fHapticL = m_fHapticR	= 0.0f;
	VEC_SET(m_vRightF, 0.0f, 0.0f, 1.0f);	VEC_SET(m_vRightU, 0.0f, 1.0f, 0.0f);	VEC_INIT(m_vRightP);
	VEC_SET(m_vLeftF, 0.0f, 0.0f, 1.0f);	VEC_SET(m_vLeftU, 0.0f, 1.0f, 0.0f);	VEC_INIT(m_vLeftP);
	m_bLeftHanded			= DFALSE;
	m_bGameplayInput		= DFALSE;
	m_bWantViewToggle		= DFALSE;
	m_nSuppressed			= 0;
	m_bAimByHand			= DFALSE;
	m_bPlayerDead			= DFALSE;
	m_bCrouchOn				= DFALSE;
	m_fCrouchAmt			= 0.0f;
	m_bAutosavePending		= DFALSE;
	m_nAutosaveFrames		= 0;
	m_bPlayerJustDied		= DFALSE;
	m_bHaveBody				= DFALSE;
	m_fLastOutYaw			= 0.0f;
	m_bSnapLatched			= DFALSE;
	m_nWeaponLatch			= 0;
	m_fWepSwapLastTime		= -1000.0f;
	m_bWepSwapPhysLeftLatch	= DFALSE;
	m_bWepSwapPhysRightLatch= DFALSE;
	m_nMenuDir				= 0;
	m_fMenuRepeatTime		= 0.0f;
	m_fWorldScaleUsed		= 40.0f;
	m_hAimMarker			= DNULL;
	m_hAimDot				= DNULL;
	m_hLaser				= DNULL;
	{ int k; for (k = 0; k < 5; k++) m_hLaserLines[k] = DNULL; }
	m_bTwoHanded			= DFALSE;
	VEC_SET(m_vAimF, 0.0f, 0.0f, 1.0f);
	VEC_SET(m_vAimU, 0.0f, 1.0f, 0.0f);
	m_hBody					= DNULL;
	m_nBodyModelId			= -1;
	VEC_INIT(m_vBodyLastPos);
	VEC_INIT(m_vBodyVel);
	m_fBodyMaxSpeed			= 1.0f;
	m_fBodyVisYaw			= 0.0f;
	m_bBodyVisYawSet		= DFALSE;
	m_szBodyAnim[0]			= 0;
	m_nBodyHidden			= 0;
	m_bBodyArmsShown		= DFALSE;
	m_hHeldGun				= DNULL;
	m_nHeldGunId			= -1;
	m_hHiddenPV				= DNULL;
	m_hArmlessPV			= DNULL;
	m_nDumpedWeapons[0] = m_nDumpedWeapons[1] = 0;
	m_hOffHandGun			= DNULL;
	m_nOffHandGunId			= -1;
	m_hSplitPV				= DNULL;
	m_bPistolSplit			= DFALSE;
	m_nPistolFireHand		= -1;
	m_nPistolLastHand		= -1;
	m_nOffHandAnim			= (DDWORD)-1;
	m_bOffArmSplit			= DFALSE;
	m_hOffArmPV				= DNULL;
	m_hOffArmAnimFor		= DNULL;
	m_nOffArmFreeCount		= 0;
	m_hDomHand				= DNULL;
	m_nDomHandFor			= -1;
	m_nLoggedWeapon			= -1;
	m_nNudgeNextTick		= 0;
	m_nNudgeLastHand		= -1;
	m_fNextRawLog			= 0.0f;
	m_hFreeHand				= DNULL;
	m_bTwoHandGrip			= DFALSE;
	VEC_INIT(m_vPistolAnchorFix);
	VEC_INIT(m_vWeaponPoseOrigin);
	m_bWeaponPoseOrigin		= DFALSE;
	m_hCameraObj			= DNULL;
	VEC_INIT(m_vPistolAimF);
	m_bPistolAimValid		= DFALSE;
	m_bCaptureHook			= DFALSE;

	m_nCurrentWeaponId		= GUN_NONE;
	VEC_INIT(m_vMeleeHandPrevP);
	m_bMeleeHandPrevValid	= DFALSE;
	m_fMeleeLastFireTime	= -1000.0f;
	m_bMeleeTrigger			= DFALSE;
	m_bMech					= DFALSE;
	m_bVehicle				= DFALSE;
	m_fLoggedZoom			= 1.0f;
	m_fLoggedCamFovX = m_fLoggedCamFovY = 0.0f;
	m_nLoggedRenderW = m_nLoggedRenderH = m_nLoggedSurfW = m_nLoggedSurfH = m_nLoggedClientW = m_nLoggedClientH = 0;
	m_bLoggedBridge			= DFALSE;
	m_nLoggedHudStrip		= -1;
	m_nLoggedCapture		= 0;
	m_bTransformKeyDown		= DFALSE;
	m_nTransformUpTick		= 0;
	m_nTransformScan		= 0;
	m_bTransformExt			= DFALSE;
	m_bWarnedNoTransformKey	= DFALSE;
	m_nInjScan				= 0;
	m_bInjExt				= DFALSE;
	m_nInjUpTick			= 0;
	m_bStartedBridge		= DFALSE;
	m_nBridgeStarts			= 0;
	m_nNextBridgeCheck		= 0;
	m_szAuthors[0]			= 0;
	m_fNoticeStart			= -1.0f;
	m_hNotice[0] = m_hNotice[1] = DNULL;
	m_bNoticeDone			= DFALSE;

	m_pfnDrawSurfaceToSurface				= DNULL;
	m_pfnDrawSurfaceToSurfaceTransparent	= DNULL;
	m_pfnDrawSurfaceSolidColor				= DNULL;
	m_pfnScaleSurfaceToSurface				= DNULL;
	m_pfnScaleSurfaceToSurfaceTransparent	= DNULL;
	m_pfnScaleSurfaceToSurfaceSolidColor	= DNULL;
	m_pfnFillRect							= DNULL;
	m_pfnDrawStringToSurface				= DNULL;
	m_pfnGetFrameTime						= DNULL;
	m_pfnGetSurfaceDims						= DNULL;
	m_pfnGetCameraRect						= DNULL;
	m_hMovieSurface							= DNULL;
	memset(&m_rcMovieSrc, 0, sizeof(m_rcMovieSrc));
	m_bMovieSrcKnown						= DFALSE;
	m_bMovieFrameStored						= DFALSE;
	m_bMovieNeedClear						= DFALSE;
	m_bVirtualHud							= DFALSE;
	m_bSkipNonCross							= DFALSE;
	m_bFlatFrame							= DFALSE;
	m_bHudStripAllowed						= DFALSE;
	m_bHudStrip								= DFALSE;
	m_nEyeH									= 480;
	m_nHudX = m_nHudY = m_nHudW = m_nHudH	= 0;
	m_nHudScale								= 1;
	m_fHudVirtW								= 512.0f;
	m_fHudVirtH								= 384.0f;
	m_fMenuVirtW							= 640.0f;
	m_fMenuVirtH							= 480.0f;
	m_bVirtual2D							= DFALSE;
	m_fVirtW								= 640.0f;
	m_fVirtH								= 480.0f;

	memset(&m_xfHud, 0, sizeof(m_xfHud));
	memset(&m_xfCross, 0, sizeof(m_xfCross));
}

void CVRStereo::Init(CClientDE* pClientDE)
{
	if (!pClientDE) return;

	// Existing values (from autoexec.cfg or the command line) are kept;
	// these defaults are only used if the variable doesn't exist yet.
	m_vtStereo.Init			(pClientDE, "VRStereo",			NULL, 0.0f);
	m_vtIPD.Init			(pClientDE, "VRIPD",			NULL, 64.0f);
	m_vtFov.Init			(pClientDE, "VRFov",			NULL, 0.0f);
	m_vtScaleFoot.Init		(pClientDE, "VRScaleFoot",		NULL, 40.0f);
	m_vtScaleMCA.Init		(pClientDE, "VRScaleMCA",		NULL, 8.0f);
	m_vtEyeHeightFoot.Init	(pClientDE, "VREyeHeightFoot",	NULL, 1.7f);
	m_vtEyeHeightMCA.Init	(pClientDE, "VREyeHeightMCA",	NULL, 10.0f);
	m_vtHudDepth.Init		(pClientDE, "VRHudDepth",		NULL, 3.5f);
	m_vtHudScale.Init		(pClientDE, "VRHudScale",		NULL, 1.0f);
	// 0.6, not 1: settled on device at 4200x2100, where it works out to 0.8 -
	// the size the owner had reached by hand with VRHudScale.  See
	// ComputeHudXform for why a strength rather than a size.
	m_vtHudAutoScale.Init	(pClientDE, "VRHudAutoScale",	NULL, 0.6f);

	// The per-weapon hand diagnostics and the raw packet dump.  Off by default:
	// they are for working out why something looks wrong, and they write a lot.
	m_vtVerbose.Init		(pClientDE, "VRVerboseDebug",	NULL, 0.0f);
	m_vtHudWidth.Init		(pClientDE, "VRHudWidth",		NULL, 60.0f);
	m_vtHudLayout.Init		(pClientDE, "VRHudLayout",		NULL, 512.0f);
	m_vtMenuWidth.Init		(pClientDE, "VRMenuWidth",		NULL, 80.0f);
	m_vtMenuLayout.Init		(pClientDE, "VRMenuLayout",		NULL, 640.0f);
	m_vtHudStrip.Init		(pClientDE, "VRHudStrip",		NULL, -1.0f);
	m_vtHudPixels.Init		(pClientDE, "VRHudPixels",		NULL, 1.0f);
	m_vtHudSnap.Init		(pClientDE, "VRHudSnap",		NULL, 1.0f);
	m_vtFlatMenus.Init		(pClientDE, "VRFlatMenus",		NULL, 1.0f);
	m_vtCrossScale.Init		(pClientDE, "VRCrosshairScale",	NULL, 1.0f);
	m_vtCrossDepth.Init		(pClientDE, "VRCrosshairDepth",	NULL, 1.0f);
	m_vtCutscenePanel.Init	(pClientDE, "VRCutscenePanel",	NULL, 1.0f);
	m_vtCutsceneStereo.Init	(pClientDE, "VRCutsceneStereo",	NULL, 0.5f);
	m_vtSwapEyes.Init		(pClientDE, "VRSwapEyes",		NULL, 0.0f);
	m_vtAutoStereo.Init		(pClientDE, "VRAutoStereo",		NULL, 1.0f);
	m_vtHeadTracking.Init	(pClientDE, "VRHeadTracking",	NULL, 1.0f);
	m_vtHeadPosition.Init	(pClientDE, "VRHeadPosition",	NULL, 1.0f);
	m_vtControllers.Init	(pClientDE, "VRControllers",	NULL, 1.0f);
	m_vtAimMode.Init		(pClientDE, "VRAimMode",		NULL, 1.0f);
	m_vtSnapTurn.Init		(pClientDE, "VRSnapTurn",		NULL, 0.0f);
	m_vtTurnSpeed.Init		(pClientDE, "VRTurnSpeed",		NULL, 150.0f);
	m_vtGunX.Init			(pClientDE, "VRGunX",			NULL, 0.0f);
	m_vtGunY.Init			(pClientDE, "VRGunY",			NULL, 0.0f);
	m_vtGunZ.Init			(pClientDE, "VRGunZ",			NULL, 0.0f);
	m_vtAimMarker.Init		(pClientDE, "VRAimMarker",		NULL, 1.0f);
	m_vtAimStyle.Init		(pClientDE, "VRAimStyle",		NULL, 3.0f);
	m_vtGameCrosshair.Init	(pClientDE, "VRGameCrosshair",	NULL, 0.0f);
	m_vtAimDotSize.Init		(pClientDE, "VRAimDotSize",		NULL, 1.0f);
	m_vtGunScale.Init		(pClientDE, "VRGunScale",		NULL, 1.5f);
	m_vtGunScaleMCA.Init	(pClientDE, "VRGunScaleMCA",	NULL, 1.5f);
	m_vtFixRenderer.Init	(pClientDE, "VRFixRenderer",	NULL, 1.0f);
	m_vtVRFullscreen.Init	(pClientDE, "VRFullscreen",		NULL, 1.0f);
	m_vtMoveDir.Init		(pClientDE, "VRMoveDir",		NULL, 1.0f);
	m_vtTwoHand.Init		(pClientDE, "VRTwoHand",		NULL, 0.0f);
	m_vtHandFlip.Init		(pClientDE, "VRHandFlip",		NULL, 0.0f);
	m_vtBody.Init			(pClientDE, "VRBody",			NULL, 1.0f);
	m_vtBodyArms.Init		(pClientDE, "VRBodyArms",		NULL, 0.0f);
	m_vtBodyOffset.Init		(pClientDE, "VRBodyOffset",		NULL, 0.15f);
	m_vtCrouchDrop.Init		(pClientDE, "VRCrouchDrop",		NULL, 0.6f);
	m_vtMoveDeadZone.Init	(pClientDE, "VRMoveDeadZone",	NULL, 0.15f);
	m_vtWeaponSwapDead.Init	(pClientDE, "VRWeaponSwapDead",	NULL, 0.80f);
	m_vtGunModel.Init		(pClientDE, "VRGunModel",		NULL, 0.0f);
	m_vtGunArms.Init		(pClientDE, "VRGunArms",		NULL, 1.0f);
	m_vtEasyLadders.Init	(pClientDE, "VREasyLadders",	NULL, 1.0f);
	m_vtAutoBridge.Init		(pClientDE, "VRAutoBridge",		NULL, 1.0f);
	m_vtHaptics.Init		(pClientDE, "VRHaptics",		NULL, 1.0f);
	m_vtSyncHold.Init		(pClientDE, "VRSyncHold",		NULL, 1.0f);
	m_vtFlashTest.Init		(pClientDE, "VRFlashTest",		NULL, 0.0f);
	m_vtMeleeSens.Init		(pClientDE, "VRMeleeSens",		NULL, 15.0f);
	m_vtWepSwapPhysical.Init(pClientDE, "VRPhysicalWepSwap",	NULL, 0.0f);
	m_vtWepSwapThumb.Init	(pClientDE, "VRWepSwapThumbstick",	NULL, 1.0f);
	// Dual pistols (#5).  VRPistolSwapHalves and the four VRPistolOff* trims
	// exist because which half of the Colt 45 model reads as the "left" pistol,
	// and how it needs seating in the hand, can only really be judged in the
	// headset - so they are live CVars, tunable on device without a rebuild,
	// exactly like the VRHeld* and VRGun* nudges above.
	m_vtDualPistols.Init	(pClientDE, "VRDualPistols",		NULL, 1.0f);
	// Settled on device: with this off, each controller held the OTHER hand's
	// half - which reads correctly until you notice the fingers and thumb are
	// mirrored.  On swaps only which half is drawn on which object; each one
	// is still posed from its own controller, so angles are unaffected.
	m_vtPistolSwapHalves.Init(pClientDE, "VRPistolSwapHalves",	NULL, 1.0f);
	m_vtPistolOffX.Init		(pClientDE, "VRPistolOffX",			NULL, 0.0f);
	m_vtPistolOffY.Init		(pClientDE, "VRPistolOffY",			NULL, 0.0f);
	m_vtPistolOffZ.Init		(pClientDE, "VRPistolOffZ",			NULL, 0.0f);
	m_vtPistolOffRoll.Init	(pClientDE, "VRPistolOffRoll",		NULL, 0.0f);
	// Shogo animates the pistols heavily, and the animation moves them a long
	// way from the model's origin - so placing the object at the hand leaves
	// the visible pistol somewhere else entirely.  With this on, each copy is
	// placed so that its own hand's palm node lands on the controller, whatever
	// the animation is doing.  Off reverts to placing the object origin.
	// Rolls the off-hand pistol 180 degrees about its barrel.  DEFAULT OFF: the
	// guess was that half B being a mirrored copy of half A would need the
	// opposite roll to the gun hand, but on device that is what put the
	// off-hand pistol upside down - the mirrored geometry and the mirrored hand
	// pose already cancel out, so the gun hand's own correction is right for
	// both.  Kept as a switch because it is the one value here that could only
	// be settled in the headset.  VRPistolOffRoll is the fine trim.
	m_vtPistolMirror.Init	(pClientDE, "VRPistolMirror",		NULL, 0.0f);
	m_vtPistolAnchor.Init	(pClientDE, "VRPistolAnchor",		NULL, 1.0f);

	// Free off arm (#7).  The submachine gun's model carries a second arm that
	// holds nothing and just goes where the animation puts it; on puts it on the
	// off controller instead.  VROffHandSwap picks which of the model's two arm
	// groups is the free one - the art names BOTH arms "R_"/"Right_", so which
	// is which cannot be read from the names and only the headset can settle it.
	m_vtSplitOffHand.Init	(pClientDE, "VRSplitOffHand",		NULL, 1.0f);
	m_vtOffHandAuto.Init	(pClientDE, "VROffHandAuto",		NULL, 1.0f);
	m_vtLentHandSwap.Init	(pClientDE, "VRLentHandSwap",		NULL, 0.0f);
	// Off: the lent hand's seating is settled (DomHandSeat).  The keys stay in
	// the build because the same job will come up for the next model that
	// needs a hand lent to it - VRNudgeKeys 1 brings them back.
	m_vtNudgeKeys.Init		(pClientDE, "VRNudgeKeys",			NULL, 0.0f);
	m_vtNudgeStep.Init		(pClientDE, "VRNudgeStep",			NULL, 0.1f);
	m_vtOffHandSwap.Init	(pClientDE, "VROffHandSwap",		NULL, 0.0f);
	m_vtOffHandAnchor.Init	(pClientDE, "VROffHandAnchor",		NULL, 1.0f);
	m_vtOffHandX.Init		(pClientDE, "VROffHandX",			NULL, 0.0f);
	m_vtOffHandY.Init		(pClientDE, "VROffHandY",			NULL, 0.0f);
	m_vtOffHandZ.Init		(pClientDE, "VROffHandZ",			NULL, 0.0f);
	m_vtOffHandRoll.Init	(pClientDE, "VROffHandRoll",		NULL, 0.0f);

	// The shotgun's model draws only its FORE hand, on the pump - the hand that
	// would be on the rear grip was never modelled, so in VR the gun hangs off
	// the dominant controller with nothing holding it.  On lends it a hand
	// borrowed from another weapon's model.
	m_vtDomHand.Init		(pClientDE, "VRDomHand",			NULL, 1.0f);
	m_vtDomHandX.Init		(pClientDE, "VRDomHandX",			NULL, 0.0f);
	// Trims ON TOP of the built-in seating in DomHandSeat - zero means "as
	// seated on device", not "no offset".
	m_vtDomHandY.Init		(pClientDE, "VRDomHandY",			NULL, 0.0f);
	m_vtDomHandZ.Init		(pClientDE, "VRDomHandZ",			NULL, 0.0f);
	m_vtDomHandRoll.Init	(pClientDE, "VRDomHandRoll",		NULL, 0.0f);

	// The same idea for the other hand: the knife and the squeaky toy are drawn
	// with one hand only, so the player's off hand is simply not there.  On
	// lends it an open hand, borrowed the same way.
	m_vtFreeHand.Init		(pClientDE, "VRFreeHand",			NULL, 1.0f);
	m_vtFreeHandX.Init		(pClientDE, "VRFreeHandX",			NULL, 0.0f);
	m_vtFreeHandY.Init		(pClientDE, "VRFreeHandY",			NULL, 0.0f);
	m_vtFreeHandZ.Init		(pClientDE, "VRFreeHandZ",			NULL, 0.0f);
	m_vtFreeHandRoll.Init	(pClientDE, "VRFreeHandRoll",		NULL, 0.0f);

	// Whether a hand WE place keeps its forearm.  Default off, and that is not
	// a cosmetic preference: the arm is modelled running back to the shoulder,
	// and once the hand is pinned to a controller the arm points wherever that
	// hand happens to be rotated rather than back towards the player.  On
	// device it reads as a large slab hanging in mid-air.  Nothing can aim it -
	// the engine has no way to write a node's transform - so the honest fix is
	// to draw the hand alone, which is what most VR games do anyway.
	m_vtHandArm.Init		(pClientDE, "VRHandArm",			NULL, 0.0f);

	// Two-handed grip (#8).  Reach your off hand out along the barrel and pull
	// its trigger to take hold of the gun with both hands; pull it again to let
	// go.  The defaults are the thresholds the older proximity two-handed aim
	// already used, so taking hold feels as it always did - it just no longer
	// happens by accident.
	// One switch over everything two-handed, because that is how the owner
	// wants it offered: the "Two handed weapons" row on the VR Advanced page.
	// Off, the weapon models draw their own hands and the gun aims from the
	// dominant hand - V1.0 behaviour - and the two settings below become the
	// finer, ini-only controls over the halves of the feature.
	m_vtTwoHandWeapons.Init	(pClientDE, "VRTwoHandWeapons",		NULL, 1.0f);
	m_vtTwoHandGrip.Init	(pClientDE, "VRTwoHandGrip",		NULL, 1.0f);
	m_vtGripDist.Init		(pClientDE, "VRGripDist",			NULL, 0.9f);
	m_vtGripAngle.Init		(pClientDE, "VRGripAngle",			NULL, 25.0f);
	// How fast the aim swings across to whichever pistol just fired, as a rate
	// (bigger is faster).  DEFAULT OFF, and it should stay off: each shot
	// already leaves down its own pistol (ApplyPistolShotPose), so the only
	// thing this adds is swinging the player's VIEW towards the off hand, which
	// on device read as the gun-hand pistol being yanked off the controller to
	// point at whatever the other hand had just shot.  Kept only as an opt-in.
	m_vtPistolAimSnap.Init	(pClientDE, "VRPistolAimSnap",		NULL, 0.0f);
	// Moving the shot ORIGIN out to the firing pistol is what makes the off
	// hand's shots really come from the off hand, but it also puts the origin
	// an arm's length from the camera, which can place it the wrong side of a
	// wall in a tight corridor.  On by default, and switchable on device: with
	// it off, shots keep the game's own origin and only the direction is
	// per-hand.
	m_vtPistolShotOrigin.Init(pClientDE, "VRPistolShotOrigin",	NULL, 1.0f);
	m_vtDirectCapture.Init	(pClientDE, "VRDirectCapture",	NULL, 1.0f);
	m_vtHeldPitch.Init		(pClientDE, "VRHeldPitch",		NULL, 0.0f);
	m_vtHeldYaw.Init		(pClientDE, "VRHeldYaw",		NULL, 0.0f);
	m_vtHeldRoll.Init		(pClientDE, "VRHeldRoll",		NULL, 0.0f);
	m_vtHeldX.Init			(pClientDE, "VRHeldX",			NULL, 0.0f);
	m_vtHeldY.Init			(pClientDE, "VRHeldY",			NULL, 0.0f);
	m_vtHeldZ.Init			(pClientDE, "VRHeldZ",			NULL, 0.0f);
	m_vtHeldScale.Init		(pClientDE, "VRHeldScale",		NULL, 1.0f);

	// Muzzle-flash offset for the held gun, in the held model's own frame
	// (x = right, y = up, z = forward/out along the barrel).  The game places
	// the flash from its muzzle-offset table, which was measured for the flat
	// game's gun model - not the mod's held model - so a straight copy lands
	// off the barrel.  Only the axes the owner wants are exposed:
	//   VRFlashLower   - how far *below* the barrel the sprite sits (y)
	//   VRFlashOffset  - how far *out past* the muzzle it sits (z)
	//                    (#46: these apply to the DUAL PISTOLS only, scaled
	//                    down x0.5 / x0.3 at the use site; every other weapon
	//                    keeps the muzzle table's own y/z untouched - see the
	//                    #46 entry in Docs/Pre-LaunchDocs/Bugs_To_Fix_V0.9.md)
	//   VRMuzzleLateral- the left/right nudge (x) for **single-barrel** weapons
	//   VRPistolSpread - the **dual pistols'** left/right spread scale (x): the
	//                    game's per-barrel value is kept (barrel 1 left, barrel
	//                    2 right - one flash per hand, #41) but scaled about the
	//                    centre, because on device the two flashes sat a little
	//                    too far apart.  1 = the game's own spread.
	// All are live so the feel can be tuned on-device without a rebuild.
	// #46 (owner's fix): the defaults are trimmed again (2.0/2.4 -> 0.5/1.16)
	// and the use site now splits the weapons - the DUAL PISTOLS get scaled
	// corrections (x0.3 forward, x0.5 drop), so on foot their flash sits
	// 0.5x0.3x1.5 = 0.225 model units past the muzzle table and
	// 1.16x0.5x1.5 = 0.87 below it.  Every OTHER weapon skips the correction
	// entirely and keeps the muzzle table's own y/z - the gap flagged in the
	// #46 review (the old else-branches silently applied the full live values
	// because GetFloat()'s 0.0 fallback never applies once the CVar has been
	// registered) is now closed; see the #46 entry in Docs/Pre-LaunchDocs/Bugs_To_Fix_V0.9.md.
	// (History: pass 7 trimmed both from 3.0 to 2.0/2.4; pass 8 to 1.5/2.16 -
	// which still put the pistols too far forward/too low AND visibly broke
	// every other weapon - hence the split above.)
	m_vtFlashOffset.Init	(pClientDE, "VRFlashOffset",	NULL, 0.5f);
	m_vtFlashLower.Init		(pClientDE, "VRFlashLower",		NULL, 1.16f);
	m_vtMuzzleLateral.Init	(pClientDE, "VRMuzzleLateral",	NULL, 0.0f);
	m_vtPistolSpread.Init	(pClientDE, "VRPistolSpread",	NULL, 0.63f);
	m_pClientDE = pClientDE;

	// Window handle for the bridge, so it knows what to capture.
	void* pHwnd = DNULL;
	pClientDE->GetEngineHook("HWND", &pHwnd);
	m_nGameHwnd = (DDWORD)pHwnd;

	// Controller buttons become game commands (fire, jump, crouch) by
	// answering "yes" when the game asks the engine whether they're on.
	m_pfnIsCommandOn = pClientDE->IsCommandOn;
	pClientDE->IsCommandOn = Hook_IsCommandOn;

	// While the controllers are in use the mouse must not also turn the view
	// (WinlatorXR injects the headset's mouse; we answer with nothing).
	m_pfnGetAxisOffsets = pClientDE->GetAxisOffsets;
	pClientDE->GetAxisOffsets = Hook_GetAxisOffsets;

	LoadSettingsFile(DTRUE);
	Log("Shogo VR started (CShell.dll built " __DATE__ " " __TIME__ ")");
	VRInstallCrashReporter();

	// WinlatorXR XrApi: in-process UDP client (replaces the old OpenXR bridge).
	if (m_vtAutoBridge.GetFloat(1.0f) != 0.0f)
	{
		m_pXrApi = new CXrApiUDP();
		m_pXrApi->SetSyncHold(m_vtSyncHold.GetFloat(0.0f) != 0.0f);
		m_pXrApi->Start();
		if (m_pXrApi->IsRunning())
			Log("XrApi: listening on udp 7872/7873 (headset %s %s)",
				m_pXrApi->GetHmdMake(), m_pXrApi->GetHmdModel());
		else
			Log("XrApi: could not open the UDP sockets");
	}

	// The creators (ShogoVR\AUTHORS.txt, one per line) for the opening notice.
	if (m_szIniPath[0])
	{
		char szPath[320];
		strncpy(szPath, m_szIniPath, sizeof(szPath) - 1);
		szPath[sizeof(szPath) - 1] = 0;
		char* pSlash = strrchr(szPath, '\\');
		if (pSlash)
		{
			strcpy(pSlash + 1, "AUTHORS.txt");
			FILE* f = fopen(szPath, "r");
			if (f)
			{
				char szLine[200];
				while (fgets(szLine, sizeof(szLine), f))
				{
					char* p = szLine;
					if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
					int n = (int)strlen(p);
					while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ')) p[--n] = 0;
					if (!n) continue;
					if (m_szAuthors[0] && strlen(m_szAuthors) + 2 < sizeof(m_szAuthors)) strcat(m_szAuthors, ", ");
					strncat(m_szAuthors, p, sizeof(m_szAuthors) - strlen(m_szAuthors) - 1);
				}
				fclose(f);
			}
		}
	}

	// Direct capture: frames straight from the renderer to the headset bridge.
	if (m_vtDirectCapture.GetFloat(1.0f) != 0.0f)
	{
		m_bCaptureHook = VRCapture_Install() ? DTRUE : DFALSE;
	}

	pClientDE->CPrint("Shogo VR stereo module loaded (VRStereo = %d).", (int)m_vtStereo.GetFloat());
}

void CVRStereo::Term()
{
	Log("Shogo VR closing normally");
	VRRemoveCrashReporter();				// before this DLL goes away
	ReleaseTransformKey();

	// Shut the XrApi link down (closes the sockets, removes the vr file).
	if (m_pXrApi)
	{
		m_pXrApi->Stop();
		delete m_pXrApi;
		m_pXrApi = DNULL;
	}
	VRCapture_Uninstall();		// before this DLL goes away
	m_bCaptureHook = DFALSE;
	RemoveHooks();
	if (m_pClientDE && m_pfnIsCommandOn)
	{
		m_pClientDE->IsCommandOn = m_pfnIsCommandOn;
		m_pfnIsCommandOn = DNULL;
	}
	if (m_pClientDE && m_pfnGetAxisOffsets)
	{
		m_pClientDE->GetAxisOffsets = m_pfnGetAxisOffsets;
		m_pfnGetAxisOffsets = DNULL;
	}
	if (m_nInjScan) { VRSendScanCode(m_nInjScan, m_bInjExt, DTRUE); m_nInjScan = 0; }
	if (m_hMovieSurface && m_pClientDE)
	{
		m_pClientDE->DeleteSurface(m_hMovieSurface);
		m_hMovieSurface = DNULL;
	}
	m_hAimMarker = DNULL;
	m_hAimDot = DNULL;
	m_hLaser = DNULL;
	m_hBody = DNULL;
	m_hHeldGun = DNULL;
	m_hOffHandGun = DNULL;
	m_hSplitPV = DNULL;
	m_bPistolSplit = DFALSE;
	m_bOffArmSplit = DFALSE;
	m_hOffArmPV = DNULL;
	m_hOffArmAnimFor = DNULL;
	m_hDomHand = DNULL;
	m_nDomHandFor = -1;
	m_hFreeHand = DNULL;
	m_bTwoHandGrip = DFALSE;
	m_bWeaponPoseOrigin = DFALSE;
	m_hCameraObj = DNULL;
	CloseShared();
	m_bCrosshairPass = DFALSE;
	m_pClientDE = DNULL;
}

DBOOL CVRStereo::IsActive()
{
	if (!m_pClientDE) return DFALSE;
	return (GetMode() != VR_STEREO_OFF);
}

DBOOL CVRStereo::IsStereoFrame()
{
	return IsActive() && !m_bFlatFrame;
}

void CVRStereo::BeginFrame(DBOOL bFlat, DBOOL bHudStripAllowed)
{
	// Flat frames only with a bridge that can show them on a floating screen.
	DBOOL bLayers = m_bBridgeAlive && (m_nBridgeFlags & SHOGOVR_BRIDGE_LAYERS);
	m_bFlatFrame		= bFlat && bLayers && IsActive() && m_vtFlatMenus.GetFloat(1.0f) != 0.0f;
	m_bHudStripAllowed	= bHudStripAllowed && bLayers;

	if (m_bFlatFrame)
	{
		HideAimHelpers();				// world objects - keep them out of cutscenes
		HideBody();
		if (m_hHeldGun) m_pClientDE->SetObjectFlags(m_hHeldGun, 0);
		if (m_hOffHandGun) m_pClientDE->SetObjectFlags(m_hOffHandGun, 0);
		m_bHudStrip			= DFALSE;
		m_nReportViewMode	= SHOGOVR_VIEW_FLAT;
		m_nReportPoseId		= 0;
		m_fReportSep		= 0.0f;
		WriteGameBlock();
	}
}

int CVRStereo::GetMode()
{
	int nMode = (int)m_vtStereo.GetFloat(0.0f);
	if (nMode != VR_STEREO_FULLSBS && nMode != VR_STEREO_HALFSBS) nMode = VR_STEREO_OFF;

	// The headset bridge is running: switch stereo on by itself.
	if (nMode == VR_STEREO_OFF && m_bBridgeAlive && m_vtAutoStereo.GetFloat(1.0f) != 0.0f)
	{
		nMode = VR_STEREO_FULLSBS;
	}
	return nMode;
}


// ======================================================================= //
//	Geometry helpers
// ======================================================================= //

void CVRStereo::RefreshFrame(DFLOAT fBaseFovX)
{
	if (!m_pClientDE) return;

	DDWORD nWidth = 640, nHeight = 480;
	m_pClientDE->GetSurfaceDims(m_pClientDE->GetScreenSurface(), &nWidth, &nHeight);
	if (nWidth < 2)  nWidth  = 2;
	if (nHeight < 1) nHeight = 1;

	m_nScreenW	= nWidth;
	m_nScreenH	= nHeight;
	m_nMode		= GetMode();

	if (fBaseFovX > 0.0f) m_fLastBaseFovX = fBaseFovX;

	ComputeLayout();

	DFLOAT fFovY = BaseEyeFovY(m_fLastBaseFovX);
	DFLOAT fFovX = ClampFov(FovXFromFovY(fFovY, EyeAspect()));
	m_fFocalBase = FocalFromFovX(fFovX);
	m_fBaseTanX  = (DFLOAT)tan(fFovX * 0.5f);
	m_fBaseTanY  = (DFLOAT)tan(fFovY * 0.5f);
}

void CVRStereo::GetHalfRect(int nEye, int &nLeft, int &nRight)
{
	// VRSwapEyes puts the right eye's picture on the left half (cross-eyed viewing)
	int nHalf = nEye;
	if (m_vtSwapEyes.GetFloat(0.0f) != 0.0f) nHalf = 1 - nEye;

	int nMid = (int)m_nScreenW / 2;
	if (nHalf == 0)
	{
		nLeft	= 0;
		nRight	= nMid;
	}
	else
	{
		nLeft	= nMid;
		nRight	= (int)m_nScreenW;
	}
}

// The size menus should lay themselves out for.  While VR is active this is
// the small virtual screen from ComputeLayout (fonts/art are fixed pixel
// sizes, so a smaller layout makes them bigger and the per-eye xform scales
// the whole thing back up into the headset panel).  Returns DFALSE when VR is
// not in play, so the caller keeps the real screen size. (#5)
DBOOL CVRStereo::GetMenuLayoutSize(int *pW, int *pH)
{
	if (!IsActive() || !m_bBridgeAlive) return DFALSE;
	if (pW) *pW = (int)VRRound(m_fMenuVirtW);
	if (pH) *pH = (int)VRRound(m_fMenuVirtH);
	return DTRUE;
}

// Screen flash (#28).  The game's hurt/explosion flash is normally applied by
// the shell as an engine camera light-add, but under the stereo render that
// light-add comes out as a broken full-screen quad.  While VR is active the
// shell feeds the current tint colour here each frame instead and the mod draws
// a clean per-eye edge vignette (DrawScreenFlash).
void CVRStereo::SetScreenFlash(DFLOAT r, DFLOAT g, DFLOAT b)
{
	DBOOL bWas = m_bFlash;
	m_fFlashR = r;
	m_fFlashG = g;
	m_fFlashB = b;
	m_bFlash = (r > 0.0f || g > 0.0f || b > 0.0f) ? DTRUE : DFALSE;
	// if (m_bFlash != bWas) Log("VR flash %s (%.2f, %.2f, %.2f)", m_bFlash ? "on" : "off",
	// 						  (double)r, (double)g, (double)b);
}

void CVRStereo::ClearScreenFlash()
{
	m_bFlash = DFALSE;
}

void CVRStereo::ComputeLayout()
{
	// The HUD in the headset is laid out for a small virtual screen...
	DFLOAT fLayout = m_vtHudLayout.GetFloat(512.0f);
	if (fLayout < 512.0f) fLayout = 512.0f;		// below this Shogo shrinks its own HUD
	if (fLayout > (DFLOAT)m_nScreenW) fLayout = (DFLOAT)m_nScreenW;
	m_fHudVirtW = (DFLOAT)VRRound(fLayout);
	m_fHudVirtH = (DFLOAT)VRRound(fLayout * 0.75f);

	// The options/menu tree is laid out for a virtual screen too, so its
	// native fonts get scaled *up* into the headset panel and stay readable.
	// 640 is the resolution the menus were designed for (it is also the
	// threshold below which they switch to a smaller font), so 640 keeps the
	// original look; smaller values trade content for a larger text size.
	DFLOAT fMenuLayout = m_vtMenuLayout.GetFloat(640.0f);
	if (fMenuLayout < 320.0f) fMenuLayout = 320.0f;
	if (fMenuLayout > (DFLOAT)m_nScreenW) fMenuLayout = (DFLOAT)m_nScreenW;
	DFLOAT fNewMenuW = (DFLOAT)VRRound(fMenuLayout);
	DFLOAT fNewMenuH = (DFLOAT)VRRound(fMenuLayout * 0.75f);
	if (fNewMenuW != m_fMenuVirtW || fNewMenuH != m_fMenuVirtH)
	{
		m_fMenuVirtW = fNewMenuW;
		m_fMenuVirtH = fNewMenuH;
		if (m_bBridgeAlive) m_bMenuLayoutDirty = DTRUE;	// menus must re-lay out (#5)
	}

	// ...and, when possible, drawn once at its original pixel size into a
	// strip along the bottom of the window, which the bridge shows as its own
	// panel - pixel-exact instead of being squeezed into each eye's picture.
	m_nEyeH		= (long)m_nScreenH;
	m_bHudStrip	= DFALSE;
	m_nHudX = m_nHudY = m_nHudW = m_nHudH = 0;

	DFLOAT fSetting = m_vtHudStrip.GetFloat(-1.0f);
	if (!m_bHudStripAllowed || m_bFlatFrame || fSetting == 0.0f || m_nMode != VR_STEREO_FULLSBS) return;

	int nScale = (int)m_vtHudPixels.GetFloat(1.0f);
	if (nScale < 1) nScale = 1;
	if (nScale > 4) nScale = 4;

	long nHudW = (long)m_fHudVirtW * nScale;
	long nHudH = (long)m_fHudVirtH * nScale;
	long nEyeH = (long)m_nScreenH - nHudH;
	if (nHudW > (long)m_nScreenW || nEyeH < (long)m_nScreenH / 2) return;

	// Automatic: only when the eyes lose (almost) nothing.  The eye pictures
	// are usually taller than the headset can show (the extra rows are cut
	// off anyway), so the strip often comes for free; accept up to a 10%
	// cost, since a sharp HUD is worth it.
	if (fSetting < 0.0f)
	{
		if (m_fNeedTanX < 0.01f || m_fNeedTanY < 0.01f) return;
		DFLOAT fNeededEyeH = ((DFLOAT)m_nScreenW * 0.5f) * m_fNeedTanY / m_fNeedTanX;
		if ((DFLOAT)nEyeH < fNeededEyeH * 0.90f) return;
	}

	m_bHudStrip	= DTRUE;
	if (m_nLoggedHudStrip != 1) { Log("HUD on its own panel (%ldx%ld)", (long)m_fHudVirtW * nScale, (long)m_fHudVirtH * nScale); m_nLoggedHudStrip = 1; }
	m_nHudScale	= nScale;
	m_nEyeH		= nEyeH;
	m_nHudX		= 0;
	m_nHudY		= nEyeH;
	m_nHudW		= nHudW;
	m_nHudH		= nHudH;
}

DBOOL CVRStereo::SkipThisDraw()
{
	if (m_bCrosshairPass)
	{
		if (m_vtGameCrosshair.GetFloat(0.0f) == 0.0f) return DTRUE;	// the game's crosshair is off by default
		if (m_bAimByHand) return DTRUE;								// the 3D aim dot replaces it
	}
	if (m_bSkipNonCross && !(m_bCrosshairPass && m_bLastImmersive)) return DTRUE;	// HUD already in its strip
	return DFALSE;
}

DFLOAT CVRStereo::EyeAspect()
{
	// Aspect of one eye's picture as the viewer finally sees it.  In half SBS
	// the picture gets stretched back to full width by the TV/player.
	DFLOAT fW = (DFLOAT)m_nScreenW;
	DFLOAT fH = (DFLOAT)m_nEyeH;

	if (m_nMode == VR_STEREO_HALFSBS) return fW / fH;
	return (fW * 0.5f) / fH;
}

DFLOAT CVRStereo::BaseEyeFovY(DFLOAT fBaseFovX)
{
	if (m_bBridgeAlive && m_fNeedTanX > 0.01f && m_fNeedTanY > 0.01f)
	{
		// In the headset: the eye picture's shape is fixed by the screen
		// mode, so pick the vertical FOV that makes the picture at least as
		// wide AND as tall as the headset's view (any extra is cropped off).
		DFLOAT fTanY	= m_fNeedTanY;
		DFLOAT fAspect	= EyeAspect();
		if (fAspect > 0.01f && (m_fNeedTanX / fAspect) > fTanY) fTanY = m_fNeedTanX / fAspect;
		return ClampFov(2.0f * (DFLOAT)atan(fTanY));
	}

	DFLOAT fFov = m_vtFov.GetFloat(0.0f);
	if (fFov >= 1.0f) return ClampFov(VR_DEG2RAD(fFov));

	// Match the game: Shogo uses fovY = fovX * height / width.
	return ClampFov((fBaseFovX * (DFLOAT)m_nScreenH) / (DFLOAT)m_nScreenW);
}

DFLOAT CVRStereo::FovXFromFovY(DFLOAT fFovY, DFLOAT fAspect)
{
	return 2.0f * (DFLOAT)atan(tan(fFovY * 0.5f) * fAspect);
}

DFLOAT CVRStereo::FocalFromFovX(DFLOAT fFovX)
{
	// Pixels (in the eye's half of the screen) per unit of tangent.
	DFLOAT fTan = (DFLOAT)tan(fFovX * 0.5f);
	if (fTan < 0.001f) fTan = 0.001f;
	return (EyeWidth() * 0.5f) / fTan;
}

DFLOAT CVRStereo::ClampFov(DFLOAT fFov)
{
	if (fFov < VR_MIN_FOV) return VR_MIN_FOV;
	if (fFov > VR_MAX_FOV) return VR_MAX_FOV;
	return fFov;
}

DFLOAT CVRStereo::GetWorldScale(DBOOL bFootScale)
{
	DFLOAT fScale = bFootScale ? m_vtScaleFoot.GetFloat(40.0f) : m_vtScaleMCA.GetFloat(8.0f);
	if (fScale < 0.01f) fScale = 0.01f;
	return fScale;
}

DFLOAT CVRStereo::GetIPDMeters()
{
	DFLOAT fIPD = m_vtIPD.GetFloat(64.0f);
	if (fIPD < 0.0f)   fIPD = 0.0f;
	if (fIPD > 200.0f) fIPD = 200.0f;
	return fIPD / 1000.0f;
}

DFLOAT CVRStereo::GetHudShift()
{
	// Horizontal shift (pixels) that puts a flat element VRHudDepth metres away.
	DFLOAT fDepth = m_vtHudDepth.GetFloat(3.5f);
	if (fDepth < 0.1f) fDepth = 0.1f;
	return (GetIPDMeters() * 0.5f) * m_fFocalBase / fDepth;
}

void CVRStereo::ComputeXform(int nEye, DFLOAT fScaleX, DFLOAT fScaleY, DFLOAT fShift,
							 DFLOAT fVirtW, DFLOAT fVirtH, VRXform &x)
{
	// Maps the centre of the virtual screen (the one the 2D code thinks it's
	// drawing to) onto the centre of this eye's half, scaled by
	// (fScaleX, fScaleY) and shifted sideways.
	int nLeft = 0, nRight = 0;
	GetHalfRect(nEye, nLeft, nRight);

	DFLOAT fH		= (DFLOAT)m_nEyeH;
	DFLOAT fEyeW	= (DFLOAT)(nRight - nLeft);

	x.sx	= fScaleX;
	x.sy	= fScaleY;
	x.ox	= (DFLOAT)nLeft + fEyeW * 0.5f - fVirtW * 0.5f * fScaleX + fShift;
	x.oy	= fH * 0.5f - fVirtH * 0.5f * fScaleY;
	x.clipL	= nLeft;
	x.clipR	= nRight;
	x.clipT	= 0;
	x.clipB	= (int)m_nEyeH;
}

// The container width the HUD was sized against - the usual device resolution
// of 2800x1400.  See ComputeHudXform.
#define VR_HUD_REF_WIDTH	2800

void CVRStereo::ComputeHudXform(int nEye, DBOOL bVirtualHud, VRXform &x)
{
	DFLOAT fVirtW = m_bVirtual2D ? m_fVirtW : (DFLOAT)m_nScreenW;
	DFLOAT fVirtH = m_bVirtual2D ? m_fVirtH : (DFLOAT)m_nScreenH;
	DFLOAT sx, sy;

	if (m_bBridgeAlive)
	{
		// In the headset: give the panel a fixed angular width, so it sits
		// comfortably inside your view whatever the resolution or headset.
		DFLOAT fDeg = bVirtualHud ? m_vtHudWidth.GetFloat(60.0f) : m_vtMenuWidth.GetFloat(80.0f);
		if (fDeg < 10.0f)  fDeg = 10.0f;
		if (fDeg > 170.0f) fDeg = 170.0f;

		DFLOAT fTanW	= 2.0f * (DFLOAT)tan(VR_DEG2RAD(fDeg) * 0.5f);		// panel width in tangent units
		DFLOAT fFocalX	= m_fFocalBase;										// eye pixels per tangent unit
		DFLOAT fFocalY	= ((DFLOAT)m_nEyeH * 0.5f) / (m_fBaseTanY > 0.01f ? m_fBaseTanY : 1.0f);
		sx = fTanW * fFocalX / fVirtW;
		sy = fTanW * fFocalY / fVirtW;		// same tangent scale vertically keeps the shape

		// Shogo's HUD is pixel art: uneven scaling (0.76x, 1.5x...) drops or
		// doubles pixel columns and makes text ragged.  Round to whole pixels.
		if (bVirtualHud && m_vtHudSnap.GetFloat(1.0f) != 0.0f && m_nMode == VR_STEREO_FULLSBS)
		{
			DFLOAT fWhole = (DFLOAT)floor(sx + 0.5f);
			if (fWhole < 1.0f) fWhole = 1.0f;
			sx = sy = fWhole;
		}
	}
	else
	{
		DFLOAT fHudScale = m_vtHudScale.GetFloat(1.0f);

		// Supersampling (#6) is done by running the WinlatorXR container above
		// the headset's own resolution, and the HUD does not come along for
		// the ride: it is 2D art blitted at fixed sizes, so a wider container
		// changes how much of the view it covers.  Above the reference width
		// the HUD is scaled back towards the share it had there.
		//
		// VRHudAutoScale is how much of that correction to apply: 1 is the
		// full ratio, 0 switches it off, and anything between is a part of the
		// way.  It is a strength rather than a size because the right amount is
		// a matter of taste and only the headset can settle it; VRHudScale
		// still trims on top.
		//
		// The default is 0.6, which the headset settled.  The full ratio was
		// too much: at 4200 wide it gives 2800/4200 = 0.667, while the owner
		// had independently arrived at 0.8 by hand.  0.6 of the correction is
		// exactly 0.8 there (1 + 0.6 * -0.333), so the automatic value now
		// lands on the hand-picked one and VRHudScale can stay at 1.0 - which
		// matters, because a player at the reference width must not inherit a
		// HUD shrunk for somebody else's container.
		DFLOAT fAuto = m_vtHudAutoScale.GetFloat(1.0f);
		if (fAuto > 0.0f && m_nScreenW > VR_HUD_REF_WIDTH)
		{
			if (fAuto > 1.0f) fAuto = 1.0f;
			DFLOAT fRatio = (DFLOAT)VR_HUD_REF_WIDTH / (DFLOAT)m_nScreenW;
			fHudScale *= 1.0f + fAuto * (fRatio - 1.0f);
		}

		if (fHudScale < 0.1f) fHudScale = 0.1f;

		int nLeft = 0, nRight = 0;
		GetHalfRect(nEye, nLeft, nRight);
		DFLOAT fEyeW = (DFLOAT)(nRight - nLeft);

		if (m_nMode == VR_STEREO_HALFSBS)
		{
			sx = 0.5f * fHudScale;
			sy = fHudScale;
		}
		else
		{
			DFLOAT fFit = VRMinF(fEyeW / fVirtW, (DFLOAT)m_nScreenH / fVirtH);
			if (fFit > 1.0f) fFit = 1.0f;
			sx = sy = fFit * fHudScale;
		}
	}

	// Left eye moves right, right eye moves left => the panel floats in front.
	DFLOAT fShift = -EyeSign(nEye) * GetHudShift();

	ComputeXform(nEye, sx, sy, fShift, fVirtW, fVirtH, x);
}


// ======================================================================= //
//	3D
// ======================================================================= //

// Note: RenderStereo caches hCamera into m_hCameraObj below, which is how
// AdjustWeaponPose gets at the real camera position - see the note there.
void CVRStereo::RenderStereo(HLOCALOBJ hCamera, HLOCALOBJ* pObjects, int nObjects,
							 DFLOAT fBaseFovX, int nMode3D, DBOOL bFootScale)
{
	if (hCamera) m_hCameraObj = hCamera;

	if (!m_pClientDE || !hCamera) return;

	CClientDE* pClientDE = m_pClientDE;

	RefreshFrame(fBaseFovX);

	// Save the camera state so the rest of the game never sees our changes...

	DBOOL bOrigFull = DFALSE;
	int nOrigL = 0, nOrigT = 0, nOrigR = 0, nOrigB = 0;
	pClientDE->GetCameraRect(hCamera, &bOrigFull, &nOrigL, &nOrigT, &nOrigR, &nOrigB);

	DFLOAT fOrigFovX = 0.0f, fOrigFovY = 0.0f;
	pClientDE->GetCameraFOV(hCamera, &fOrigFovX, &fOrigFovY);

	DVector vOrigPos;
	pClientDE->GetObjectPos(hCamera, &vOrigPos);

	DRotation rRot;
	DVector vU, vR, vF;
	pClientDE->GetObjectRotation(hCamera, &rRot);
	pClientDE->GetRotationVectors(&rRot, &vU, &vR, &vF);
	VEC_NORM(vR);

	// The mono rectangle the game wants (used by the panel modes)...

	DRect rcMono;
	if (bOrigFull)
	{
		rcMono.left = 0; rcMono.top = 0;
		rcMono.right = (int)m_nScreenW; rcMono.bottom = (int)m_nScreenH;
	}
	else
	{
		rcMono.left = nOrigL; rcMono.top = nOrigT;
		rcMono.right = nOrigR; rcMono.bottom = nOrigB;
	}

	// Zoom: the game narrows its camera FOV for the sniper scope, and changes
	// it for other reasons too (a 4:3 one per player-mode change, a wider one
	// in a mech's vehicle mode).  In a headset none of that may change what we
	// present, so we DON'T feed it into the eye FOV (#19): the picture stays
	// pinned to the headset base FOV and fills the view (a narrow eye FOV made
	// the runtime present a tiny centre image and could get stuck zoomed).  The
	// ratio is only logged so camera FOV changes still show up in the log;
	// weapon zoom is removed for now and never applied to the eye view.

	DFLOAT fZoom = 1.0f;
	if (fOrigFovX > 0.001f && m_fLastBaseFovX > 0.001f) fZoom = fOrigFovX / m_fLastBaseFovX;
	if (fZoom > 0.97f) fZoom = 1.0f;
	if (fZoom < 0.01f) fZoom = 0.01f;

	if (fZoom >= 1.0f && ((DFLOAT)fabs(fOrigFovX - m_fLoggedCamFovX) > 0.01f || (DFLOAT)fabs(fOrigFovY - m_fLoggedCamFovY) > 0.01f))
	{
		Log("Game camera FOV is now %.1f x %.1f degrees (headset view pinned)", VR_RAD2DEG(fOrigFovX), VR_RAD2DEG(fOrigFovY));
		m_fLoggedCamFovX = fOrigFovX;
		m_fLoggedCamFovY = fOrigFovY;
	}
	if ((DFLOAT)fabs(fZoom - m_fLoggedZoom) > 0.05f)
	{
		Log("Camera zoom %.2fx (not applied to the eye view)", fZoom);
		m_fLoggedZoom = fZoom;
	}

	DFLOAT fWorldScale = GetWorldScale(bFootScale);
	m_fWorldScaleUsed = fWorldScale;
	DFLOAT fIpdWorld = GetIPDMeters() * fWorldScale;

	m_bLastImmersive = (nMode3D == VR3D_IMMERSIVE);

	// Head tracking: view = body yaw (mouse) followed by the full head
	// orientation, plus the head's position.  The aim already includes the
	// head's yaw/pitch (ApplyHeadTracking), so the crosshair stays centred.

	DBOOL bTracked = (nMode3D == VR3D_IMMERSIVE) && m_bHeadApplied && IsHeadTracking();
	DRotation rRender;
	ROT_COPY(rRender, rRot);
	DVector vBasePos;
	VEC_COPY(vBasePos, vOrigPos);

	if (bTracked)
	{
		DRotation rBody;
		DVector bU, bR, bF, vT, vHF, vHU;
		pClientDE->SetupEuler(&rBody, 0.0f, m_fBodyYaw, 0.0f);
		pClientDE->GetRotationVectors(&rBody, &bU, &bR, &bF);

		VEC_MULSCALAR(vHF, bR, m_vHeadF.x);
		VEC_MULSCALAR(vT,  bU, m_vHeadF.y);	VEC_ADD(vHF, vHF, vT);
		VEC_MULSCALAR(vT,  bF, m_vHeadF.z);	VEC_ADD(vHF, vHF, vT);

		VEC_MULSCALAR(vHU, bR, m_vHeadU.x);
		VEC_MULSCALAR(vT,  bU, m_vHeadU.y);	VEC_ADD(vHU, vHU, vT);
		VEC_MULSCALAR(vT,  bF, m_vHeadU.z);	VEC_ADD(vHU, vHU, vT);

		pClientDE->AlignRotation(&rRender, &vHF, &vHU);
		pClientDE->GetRotationVectors(&rRender, &vU, &vR, &vF);
		VEC_NORM(vR);

		if (m_vtHeadPosition.GetFloat(1.0f) != 0.0f)
		{
			DVector vOff;
			VEC_MULSCALAR(vOff, bR, m_vHeadP.x * fWorldScale);
			VEC_MULSCALAR(vT,   bU, m_vHeadP.y * fWorldScale);	VEC_ADD(vOff, vOff, vT);
			VEC_MULSCALAR(vT,   bF, m_vHeadP.z * fWorldScale);	VEC_ADD(vOff, vOff, vT);
			VEC_ADD(vBasePos, vBasePos, vOff);
		}

		// Crouch (#20): the game never ducks on foot, so lower the eyes along
		// the body's up axis while the crouch controls are on.
		if (m_fCrouchAmt > 0.0f)
		{
			DFLOAT fDrop = m_fCrouchAmt * m_vtCrouchDrop.GetFloat(0.6f) * fWorldScale;
			VEC_MULSCALAR(vT, bU, fDrop);
			VEC_SUB(vBasePos, vBasePos, vT);
		}

		pClientDE->SetObjectRotation(hCamera, &rRender);
	}

	DFLOAT fReportSep = 0.0f;

	for (int nEye = 0; nEye < 2; nEye++)
	{
		DFLOAT fEyeFovX	= fOrigFovX;
		DFLOAT fEyeFovY	= fOrigFovY;
		DFLOAT fSep		= 0.0f;

		if (nMode3D == VR3D_IMMERSIVE)
		{
			int nLeft = 0, nRight = 0;
			GetHalfRect(nEye, nLeft, nRight);
			pClientDE->SetCameraRect(hCamera, DFALSE, nLeft, 0, nRight, (int)m_nEyeH);

			// Zoom never changes the rendered scale (#19): the eye FOV stays
			// pinned to the headset base so the SBS picture keeps filling the
			// view.  Weapon zoom is removed for now, so nothing else to do.
			fEyeFovY = ClampFov(BaseEyeFovY(m_fLastBaseFovX));
			fEyeFovX = ClampFov(FovXFromFovY(fEyeFovY, EyeAspect()));

			fSep = fIpdWorld;
			fReportSep = GetIPDMeters();

			m_fFocalRender	= FocalFromFovX(fEyeFovX);
			m_fIpdWorld		= fSep;
			m_fEyeFovX		= fEyeFovX;
			m_fEyeFovY		= fEyeFovY;
		}
		else
		{
			VRXform x;
			ComputeHudXform(nEye, DFALSE, x);

			DRect rcEye;
			rcEye.left		= VRRound(x.ox + (DFLOAT)rcMono.left   * x.sx);
			rcEye.right		= VRRound(x.ox + (DFLOAT)rcMono.right  * x.sx);
			rcEye.top		= VRRound(x.oy + (DFLOAT)rcMono.top    * x.sy);
			rcEye.bottom	= VRRound(x.oy + (DFLOAT)rcMono.bottom * x.sy);

			if (rcEye.left < x.clipL)				rcEye.left = x.clipL;
			if (rcEye.right > x.clipR)				rcEye.right = x.clipR;
			if (rcEye.top < 0)						rcEye.top = 0;
			if (rcEye.bottom > (int)m_nScreenH)		rcEye.bottom = (int)m_nScreenH;
			if (rcEye.right <= rcEye.left || rcEye.bottom <= rcEye.top) continue;

			pClientDE->SetCameraRect(hCamera, DFALSE, rcEye.left, rcEye.top, rcEye.right, rcEye.bottom);

			if (nMode3D == VR3D_PANEL_STEREO)
			{
				DFLOAT fStrength = m_vtCutsceneStereo.GetFloat(0.5f);
				if (fStrength < 0.0f) fStrength = 0.0f;
				if (fStrength > 1.0f) fStrength = 1.0f;
				fSep = fIpdWorld * fStrength;
				fReportSep = GetIPDMeters() * fStrength;
			}
		}

		pClientDE->SetCameraFOV(hCamera, fEyeFovX, fEyeFovY);

		DVector vEyePos, vOffset;
		VEC_MULSCALAR(vOffset, vR, EyeSign(nEye) * fSep * 0.5f);
		VEC_ADD(vEyePos, vBasePos, vOffset);
		pClientDE->SetObjectPos(hCamera, &vEyePos);

		if (pObjects)
		{
			pClientDE->RenderObjects(hCamera, pObjects, nObjects);
		}
		else
		{
			pClientDE->RenderCamera(hCamera);
		}
	}

	// Put everything back...

	pClientDE->SetCameraRect(hCamera, bOrigFull, nOrigL, nOrigT, nOrigR, nOrigB);
	pClientDE->SetCameraFOV(hCamera, fOrigFovX, fOrigFovY);
	pClientDE->SetObjectPos(hCamera, &vOrigPos);
	if (bTracked) pClientDE->SetObjectRotation(hCamera, &rRot);

	// Tell the bridge what this frame was rendered with...

	if (nMode3D == VR3D_IMMERSIVE)
	{
		m_fReportTanX = (DFLOAT)tan(m_fEyeFovX * 0.5f);
		m_fReportTanY = (DFLOAT)tan(m_fEyeFovY * 0.5f);
	}
	else
	{
		m_fReportTanX = m_fBaseTanX;
		m_fReportTanY = m_fBaseTanY;
	}
	m_fReportSep		= fReportSep;
	m_nReportViewMode	= bTracked ? SHOGOVR_VIEW_TRACKED : SHOGOVR_VIEW_SCREEN;
	m_nReportPoseId		= bTracked ? m_nPoseId : 0;
	VRCapture_SetFramePose(m_nReportPoseId);			// stamped onto the captured frame
	m_bHeadApplied		= DFALSE;

	WriteGameBlock();
}

void CVRStereo::UpdateAimDepth(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList)
{
	if (!m_pClientDE || !hCamera || !IsActive()) return;

	CClientDE* pClientDE = m_pClientDE;

	DVector vPos, vU, vR, vF;
	DRotation rRot;
	pClientDE->GetObjectPos(hCamera, &vPos);
	pClientDE->GetObjectRotation(hCamera, &rRot);
	pClientDE->GetRotationVectors(&rRot, &vU, &vR, &vF);
	VEC_NORM(vF);

	ClientIntersectQuery query;
	ClientIntersectInfo info;

	VEC_COPY(query.m_From, vPos);
	VEC_MULSCALAR(query.m_To, vF, VR_AIM_RANGE);
	VEC_ADD(query.m_To, query.m_To, vPos);
	VEC_COPY(query.m_Direction, vF);
	query.m_Flags		= INTERSECT_OBJECTS | IGNORE_NONSOLID;
	query.m_FilterFn	= pFilterList ? ObjListFilterFn : DNULL;
	query.m_pUserData	= pFilterList;

	DFLOAT fTarget = 0.0f;		// 0 = nothing hit, crosshair at infinity

	if (pClientDE->IntersectSegment(&query, &info))
	{
		DVector vDiff;
		VEC_SUB(vDiff, info.m_Point, vPos);
		DFLOAT fDist = VEC_MAG(vDiff);
		if (fDist < 1.0f) fDist = 1.0f;
		fTarget = 1.0f / fDist;
	}

	// Smooth it (in inverse depth, which is what the eyes respond to)...

	DFLOAT fFrameTime = pClientDE->GetFrameTime();
	DFLOAT k = fFrameTime * VR_AIM_SMOOTHING;
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;

	m_fAimInvDepth += (fTarget - m_fAimInvDepth) * k;
}


// ======================================================================= //
//	2D
// ======================================================================= //

void CVRStereo::Begin2D(int nEye, DBOOL bHud)
{
	if (!m_pClientDE || !IsStereoFrame()) return;

	if (nEye == 0)
	{
		RefreshFrame(0.0f);
	}

	m_nCurEye = nEye;
	m_hScreen = m_pClientDE->GetScreenSurface();

	// In the headset the in-game HUD and the options/menus are laid out for
	// small virtual screens: Shogo's art and fonts have fixed pixel sizes, so
	// a smaller layout makes them bigger, and it pulls the HUD's corner gauges
	// in from the blurry edges of the lenses.  Menus get their own virtual
	// size (see ComputeLayout) so their native fonts stay readable.

	m_bVirtualHud = bHud && m_bBridgeAlive;
	m_bVirtual2D  = m_bBridgeAlive;		// this 2D pass lays out virtually (HUD or menu)
	if (m_bVirtualHud)
	{
		m_fVirtW = m_fHudVirtW;
		m_fVirtH = m_fHudVirtH;
	}
	else if (m_bBridgeAlive)
	{
		m_fVirtW = m_fMenuVirtW;
		m_fVirtH = m_fMenuVirtH;
	}
	else
	{
		m_fVirtW = (DFLOAT)m_nScreenW;
		m_fVirtH = (DFLOAT)m_nScreenH;
	}

	// HUD / menu panel...

	m_bSkipNonCross = DFALSE;
	if (m_bVirtualHud && m_bHudStrip)
	{
		// The HUD goes once, at whole-pixel size, into its own strip; only
		// the crosshair (if any) is drawn into each eye.
		if (nEye == 0)
		{
			m_xfHud.sx		= (DFLOAT)m_nHudScale;
			m_xfHud.sy		= (DFLOAT)m_nHudScale;
			m_xfHud.ox		= (DFLOAT)m_nHudX;
			m_xfHud.oy		= (DFLOAT)m_nHudY;
			m_xfHud.clipL	= (int)m_nHudX;
			m_xfHud.clipR	= (int)(m_nHudX + m_nHudW);
			m_xfHud.clipT	= (int)m_nHudY;
			m_xfHud.clipB	= (int)(m_nHudY + m_nHudH);
		}
		else
		{
			m_bSkipNonCross = DTRUE;
		}
	}
	else
	{
		ComputeHudXform(nEye, m_bVirtualHud, m_xfHud);
	}

	// Crosshair: original pixel size, centred on the eye, at aim depth...

	DFLOAT fCrossScale = m_vtCrossScale.GetFloat(1.0f);
	if (fCrossScale < 0.1f) fCrossScale = 0.1f;

	DFLOAT fCrossSX = (m_nMode == VR_STEREO_HALFSBS) ? fCrossScale * 0.5f : fCrossScale;
	DFLOAT fCrossSY = fCrossScale;

	DFLOAT fDisparity = GetHudShift();
	if (m_vtCrossDepth.GetFloat(1.0f) != 0.0f)
	{
		fDisparity = (m_fIpdWorld * 0.5f) * m_fFocalRender * m_fAimInvDepth;
	}

	ComputeXform(nEye, fCrossSX, fCrossSY, -EyeSign(nEye) * fDisparity, m_fVirtW, m_fVirtH, m_xfCross);

	InstallHooks();
}

void CVRStereo::End2D()
{
	if (!m_bHooked) return;

	RemoveHooks();
	m_nCurEye = 0;
	m_bVirtualHud = DFALSE;
	m_bVirtual2D = DFALSE;
	m_bSkipNonCross = DFALSE;
	m_fVirtW = (DFLOAT)m_nScreenW;
	m_fVirtH = (DFLOAT)m_nScreenH;
}

// Screen flash (#28): one opaque band down the *outer* edge of each eye - the
// left eye's left side and the right eye's right side - standing in for the
// game's hurt (red) / explosion (white) full-screen flash.  The shell feeds the
// tint colour (SetScreenFlash) instead of using the engine camera light-add,
// whose full-screen quad breaks under the stereo render.  Two filled rectangles
// per frame: cheap, and it reads as a peripheral-vision ("tunnel vision") flash
// without covering the centre of view.
void CVRStereo::DrawScreenFlash(int nEye)
{
	if (!m_bFlash || !m_pClientDE || !m_pfnFillRect) return;

	HSURFACE hScreen = m_pClientDE->GetScreenSurface();
	if (!hScreen) return;

	int nLeft = 0, nRight = 0;
	GetHalfRect(nEye, nLeft, nRight);
	int nEyeW = nRight - nLeft;
	int nEyeH = (int)m_nEyeH;
	if (nEyeW < 8 || nEyeH < 8) return;

	DFLOAT fR = m_fFlashR, fG = m_fFlashG, fB = m_fFlashB;

	// Strength = the tint's brightest channel; the band widens with it.
	DFLOAT fStr = fR;
	if (fG > fStr) fStr = fG;
	if (fB > fStr) fStr = fB;
	if (fStr <= 0.001f) return;
	if (fStr > 1.0f) fStr = 1.0f;

	int nBand = (int)(fStr * 0.30f * (DFLOAT)nEyeW);
	if (nBand < 4) nBand = 4;
	if (nBand > nEyeW) nBand = nEyeW;

	DRect rc;
	rc.top		= 0;
	rc.bottom	= nEyeH;
	if (nLeft <= 0)
	{
		// Left eye: band down its left (outer) edge.
		rc.left		= nLeft;
		rc.right	= nLeft + nBand;
	}
	else
	{
		// Right eye: band down its right (outer) edge.
		rc.left		= nRight - nBand;
		rc.right	= nRight;
	}

	static DBOOL s_bLogged = DFALSE;
	if (!s_bLogged)
	{
		Log("VR flash drawn: eye %d rect %d,0 - %d,%d, col %.2f,%.2f,%.2f",
			nEye, rc.left, rc.right, nEyeH, (double)fR, (double)fG, (double)fB);
		s_bLogged = DTRUE;
	}

	HDECOLOR col = m_pClientDE->SetupColor1(fR, fG, fB, DFALSE);
	m_pfnFillRect(hScreen, &rc, col);
}

// The engine's movie player (inside Client.exe) draws the Smacker frame
// straight into the window buffer, centred on the whole SBS image, so the
// mod can't intercept it to place it in an eye.  Clone the finished frame so
// the movie shows in the middle of each half: WinlatorXR shows the left half
// to the left eye and the right half to the right eye, so both eyes see the
// intro.  Done with the original (unhooked) surface calls.
void CVRStereo::ShiftMovieToRightHalf()
{
	if (!m_pClientDE) return;

	// The movie plays right at startup, possibly before any Begin2D/End2D has
	// come round to InstallHooks(), so the original pointers may not be cached
	// yet.  Snapshot them if needed (when hooks aren't installed the table
	// still holds the originals).
	if (!m_pfnDrawSurfaceToSurface || !m_pfnFillRect || !m_pfnGetSurfaceDims)
	{
		m_pfnDrawSurfaceToSurface	= m_pClientDE->DrawSurfaceToSurface;
		m_pfnFillRect				= m_pClientDE->FillRect;
		m_pfnGetSurfaceDims			= m_pClientDE->GetSurfaceDims;
	}
	if (!m_pfnDrawSurfaceToSurface || !m_pfnFillRect || !m_pfnGetSurfaceDims) return;

	HSURFACE hScreen = m_pClientDE->GetScreenSurface();
	if (!hScreen) return;

	DDWORD nW = 0, nH = 0;
	m_pfnGetSurfaceDims(hScreen, &nW, &nH);
	if (nW < 8 || nH < 1) return;

	static DBOOL s_bLogged = DFALSE;
	if (!s_bLogged)
	{
		Log("movie frame clone: window %dx%d", (int)nW, (int)nH);
		s_bLogged = DTRUE;
	}

	// Keep a scratch surface the size of the window (recreate if resized).
	if (m_hMovieSurface)
	{
		DDWORD nTW = 0, nTH = 0;
		m_pfnGetSurfaceDims(m_hMovieSurface, &nTW, &nTH);
		if (nTW != nW || nTH != nH)
		{
			m_pClientDE->DeleteSurface(m_hMovieSurface);
			m_hMovieSurface = DNULL;
			m_bMovieSrcKnown = DFALSE;
			m_bMovieFrameStored = DFALSE;
		}
	}
	if (!m_hMovieSurface)
	{
		m_hMovieSurface = m_pClientDE->CreateSurface(nW, nH);
		if (!m_hMovieSurface) return;
	}

	// Some renderers only accept these blits inside the optimized-2D block.
	m_pClientDE->StartOptimized2D();

	// A new movie starts with the PREVIOUS one's two clones still on the
	// window, and capturing that would store a composite as if it were a movie
	// frame - and then clone the composite for the rest of the movie.  Blank
	// the window and do nothing else this pass; by the next one the player will
	// have drawn a clean centred frame onto black, which is what detection and
	// the capture below both need.  Costs one black frame per movie.
	if (m_bMovieNeedClear)
	{
		DRect rcClear;
		rcClear.left = 0; rcClear.top = 0; rcClear.right = (int)nW; rcClear.bottom = (int)nH;
		m_pfnFillRect(hScreen, &rcClear, m_pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE));
		m_bMovieNeedClear = DFALSE;
		m_pClientDE->EndOptimized2D();
		return;
	}

	// Where the two clones go.  Worked out before the capture because the
	//    freshness test below has to predict our own output to recognise it.
	int vwPre = m_rcMovieSrc.right - m_rcMovieSrc.left;
	int xLPre = (int)nW / 4 - vwPre / 2;

	// 1) copy the whole window into the scratch surface - but ONLY while the
	//    window is still showing a fresh frame from the movie player.
	//
	//    This runs once per engine frame and the movie plays at its own, much
	//    lower rate, so most passes find the window exactly as the previous
	//    pass left it: holding our own two clones.  Capturing that and cloning
	//    it again takes the middle of the pair - the right half of one copy and
	//    the left half of the other - and draws THAT into both halves, which is
	//    four part-frames across the window instead of two whole ones (#51).
	//
	//    Skipping the capture is all that is needed: the scratch still holds the
	//    last fresh frame, so the draw below reproduces the same two clones and
	//    the operation becomes idempotent.
	if (!m_bMovieFrameStored || MovieFrameIsFresh(hScreen, xLPre, vwPre, nW, nH))
	{
		m_pfnDrawSurfaceToSurface(m_hMovieSurface, hScreen, DNULL, 0, 0);
		m_bMovieFrameStored = DTRUE;
	}

	// 2) the first frame of each movie: find where the video is, so we can
	//    draw it into each half with positive coordinates only.
	if (!m_bMovieSrcKnown) DetectMovieSource();

	// 3) paint the whole screen pure black (SetupColor1 gives a real engine
	//    black; a raw 0 isn't one) so nothing old shows through behind the
	//    two clones, then draw the video into each half.
	DRect rcFull;
	rcFull.left = 0; rcFull.top = 0; rcFull.right = (int)nW; rcFull.bottom = (int)nH;
	m_pfnFillRect(hScreen, &rcFull, m_pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE));

	int vw = m_rcMovieSrc.right - m_rcMovieSrc.left;
	int vh = m_rcMovieSrc.bottom - m_rcMovieSrc.top;
	if (m_bMovieSrcKnown && vw > 0 && vh > 0 && vw <= (int)nW / 2 && vh <= (int)nH)
	{
		int xL = (int)nW / 4 - vw / 2;			// middle of the left half
		int xR = (int)nW - (int)nW / 4 - vw / 2;	// middle of the right half
		// Sit the movie a little lower than screen centre: the user wanted the
		// intro ~1/8 of the screen height down from the top, then ~50px more.
		// Clamp so a full-height frame still lands fully on-screen.
		int yOff = (int)nH / 8 + 50;
		if (vh + yOff > (int)nH) yOff = (int)nH - vh;
		if (yOff < 0) yOff = 0;
		m_pfnDrawSurfaceToSurface(hScreen, m_hMovieSurface, &m_rcMovieSrc, xL, yOff);
		m_pfnDrawSurfaceToSurface(hScreen, m_hMovieSurface, &m_rcMovieSrc, xR, yOff);
	}
	else
	{
		// Couldn't find the movie (e.g. a black lead-in frame): keep the plain
		// right-half slide, but do NOT lower it.  This clone is the WHOLE
		// window, so shifting it down just clips whatever sits near the bottom
		// of the frame - that is what made the intro look cut off on the bottom
		// (#26).  Only a successful detection (the branch above) earns the drop.
		m_pfnDrawSurfaceToSurface(hScreen, m_hMovieSurface, DNULL, (int)nW / 4, 0);
	}

	m_pClientDE->EndOptimized2D();
}

// Find where the movie sits inside a just-captured copy of the window.  The
// player centres the video on the window with black all around, so the
// smallest box holding non-black pixels in the centre band is the movie.
void CVRStereo::DetectMovieSource()
{
	m_bMovieSrcKnown = DFALSE;
	memset(&m_rcMovieSrc, 0, sizeof(m_rcMovieSrc));
	if (!m_pClientDE || !m_hMovieSurface || !m_pfnGetSurfaceDims) return;

	DDWORD nW = 0, nH = 0;
	m_pfnGetSurfaceDims(m_hMovieSurface, &nW, &nH);
	if (nW < 8 || nH < 1) return;

	int nL = (int)nW / 4;
	int nR = (int)nW - nL;

	HDECOLOR hBlack = m_pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE);
	HDECOLOR c = 0;

	// 1) Vertical extent first, down a few columns spread across the centre
	//    band.  The old version only sampled a fixed trio of rows, so a short
	//    video sitting between them was never seen: detection failed, the plain
	//    clone fallback ran, and once that was lowered it looked cut off (#26).
	int cols[5] = { nL, nL + (nR - nL) / 4, (int)nW / 2, nR - (nR - nL) / 4, nR };
	int nMinY = (int)nH, nMaxY = -1;
	for (int ci = 0; ci < 5; ci++)
	{
		for (int y = 0; y < (int)nH; y++)
		{
			if (m_pClientDE->GetPixel(m_hMovieSurface, (DDWORD)cols[ci], (DDWORD)y, &c) == DE_OK && c != hBlack)
			{
				if (y < nMinY) nMinY = y;
				if (y > nMaxY) nMaxY = y;
			}
		}
	}
	if (nMaxY <= nMinY) return;

	// 2) Horizontal extent along a spread of rows: the three the old code used,
	//    plus three inside the video found above.  The union means we never lose
	//    a wider box the old sampler produced, but we also catch a short video
	//    whose rows sat between the old fixed samples.
	int rows[6] = {
		(int)nH / 4, (int)nH / 2, (int)nH * 3 / 4,
		nMinY + (nMaxY - nMinY) / 4, (nMinY + nMaxY) / 2, nMaxY - (nMaxY - nMinY) / 4
	};
	int nMinX = nR, nMaxX = nL - 1;
	for (int r = 0; r < 6; r++)
	{
		for (int x = nL; x < nR; x++)
		{
			if (m_pClientDE->GetPixel(m_hMovieSurface, (DDWORD)x, (DDWORD)rows[r], &c) == DE_OK && c != hBlack)
			{
				if (x < nMinX) nMinX = x;
				if (x > nMaxX) nMaxX = x;
			}
		}
	}
	if (nMaxX <= nMinX) return;

	m_rcMovieSrc.left	= nMinX;
	m_rcMovieSrc.right	= nMaxX + 1;
	m_rcMovieSrc.top	= nMinY;
	m_rcMovieSrc.bottom	= nMaxY + 1;
	m_bMovieSrcKnown	= DTRUE;
	Log("movie source detected: %d,%d - %d,%d", m_rcMovieSrc.left, m_rcMovieSrc.top, m_rcMovieSrc.right, m_rcMovieSrc.bottom);
}

void CVRStereo::ResetMovieSource()
{
	m_bMovieSrcKnown = DFALSE;
	m_bMovieFrameStored = DFALSE;
	m_bMovieNeedClear = DTRUE;
}

// Is the window showing a fresh movie frame, or still the two clones we drew
// over it last pass?
//
// The first attempt at this asked whether the margins beside the video were
// black, on the grounds that the player leaves them black and our clones cover
// them.  That is self-defeating: once we have cloned even once, our own output
// covers those margins for good, so the answer is "stale" forever and the movie
// freezes on its first frame - which is what put a second, older frame in each
// half of the window.
//
// The reliable question is a different one.  The player only ever draws INSIDE
// m_rcMovieSrc, so everything outside it is our own output by definition and
// says nothing about whether a frame has arrived.  Inside it, the window holds
// either a new frame or - if the player had nothing new this pass - exactly
// what we drew.  And we know exactly what we drew: the left clone put source
// pixel (m_rcMovieSrc.left + x - xL) at x.  So predict our own output and
// compare against it.  Still matching means no new frame.
//
// Ambiguity is resolved as "stale", because a wrong "stale" costs one repeated
// frame while a wrong "fresh" re-clones a composite, which is the bug itself.
DBOOL CVRStereo::MovieFrameIsFresh(HSURFACE hScreen, int xL, int vw, DDWORD nW, DDWORD nH)
{
	if (!m_pClientDE || !hScreen || !m_hMovieSurface) return DTRUE;
	if (!m_bMovieSrcKnown || vw <= 0) return DTRUE;

	// The band where the player's frame and our left clone overlap: the only
	// place the two predictions differ in a way we can read.
	int x0 = m_rcMovieSrc.left;		if (xL      > x0) x0 = xL;
	int x1 = m_rcMovieSrc.right;	if (xL + vw < x1) x1 = xL + vw;
	if (x1 - x0 < 8) return DTRUE;

	int nTop = m_rcMovieSrc.top, nBot = m_rcMovieSrc.bottom;
	if (nBot - nTop < 4) return DTRUE;

	HDECOLOR hBlack = m_pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE);
	HDECOLOR cScr = 0, cSrc = 0;

	int rows[3] = { (nTop * 3 + nBot) / 4, (nTop + nBot) / 2, (nTop + nBot * 3) / 4 };
	int nSeen = 0, nSame = 0, nLit = 0;
	int r, i;
	for (r = 0; r < 3; r++)
	{
		int y = rows[r];
		if (y < 0 || y >= (int)nH) continue;
		for (i = 1; i <= 8; i++)
		{
			int x  = x0 + (x1 - x0) * i / 9;
			int sx = m_rcMovieSrc.left + (x - xL);		// what our own clone put there
			if (sx < 0 || sx >= (int)nW) continue;
			if (m_pClientDE->GetPixel(hScreen,       (DDWORD)x,  (DDWORD)y, &cScr) != DE_OK) continue;
			if (m_pClientDE->GetPixel(m_hMovieSurface, (DDWORD)sx, (DDWORD)y, &cSrc) != DE_OK) continue;
			nSeen++;
			if (cScr == cSrc) nSame++;
			if (cScr != hBlack || cSrc != hBlack) nLit++;
		}
	}

	if (nSeen == 0) return DTRUE;		// nothing to go on
	if (nLit == 0)  return DFALSE;		// all black either way - tells us nothing, so assume stale
	return (nSame * 4 >= nSeen * 3) ? DFALSE : DTRUE;
}

void CVRStereo::InstallHooks()
{
	if (m_bHooked || !m_pClientDE) return;

	CClientDE* p = m_pClientDE;

	m_pfnDrawSurfaceToSurface				= p->DrawSurfaceToSurface;
	m_pfnDrawSurfaceToSurfaceTransparent	= p->DrawSurfaceToSurfaceTransparent;
	m_pfnDrawSurfaceSolidColor				= p->DrawSurfaceSolidColor;
	m_pfnScaleSurfaceToSurface				= p->ScaleSurfaceToSurface;
	m_pfnScaleSurfaceToSurfaceTransparent	= p->ScaleSurfaceToSurfaceTransparent;
	m_pfnScaleSurfaceToSurfaceSolidColor	= p->ScaleSurfaceToSurfaceSolidColor;
	m_pfnFillRect							= p->FillRect;
	m_pfnDrawStringToSurface				= p->DrawStringToSurface;
	m_pfnGetFrameTime						= p->GetFrameTime;
	m_pfnGetSurfaceDims						= p->GetSurfaceDims;
	m_pfnGetCameraRect						= p->GetCameraRect;

	p->DrawSurfaceToSurface					= Hook_DrawSurfaceToSurface;
	p->DrawSurfaceToSurfaceTransparent		= Hook_DrawSurfaceToSurfaceTransparent;
	p->DrawSurfaceSolidColor				= Hook_DrawSurfaceSolidColor;
	p->ScaleSurfaceToSurface				= Hook_ScaleSurfaceToSurface;
	p->ScaleSurfaceToSurfaceTransparent		= Hook_ScaleSurfaceToSurfaceTransparent;
	p->ScaleSurfaceToSurfaceSolidColor		= Hook_ScaleSurfaceToSurfaceSolidColor;
	p->FillRect								= Hook_FillRect;
	p->DrawStringToSurface					= Hook_DrawStringToSurface;
	p->GetFrameTime							= Hook_GetFrameTime;
	p->GetSurfaceDims						= Hook_GetSurfaceDims;
	p->GetCameraRect						= Hook_GetCameraRect;

	m_bHooked = DTRUE;
}

void CVRStereo::RemoveHooks()
{
	if (!m_bHooked || !m_pClientDE) return;

	CClientDE* p = m_pClientDE;

	p->DrawSurfaceToSurface					= m_pfnDrawSurfaceToSurface;
	p->DrawSurfaceToSurfaceTransparent		= m_pfnDrawSurfaceToSurfaceTransparent;
	p->DrawSurfaceSolidColor				= m_pfnDrawSurfaceSolidColor;
	p->ScaleSurfaceToSurface				= m_pfnScaleSurfaceToSurface;
	p->ScaleSurfaceToSurfaceTransparent		= m_pfnScaleSurfaceToSurfaceTransparent;
	p->ScaleSurfaceToSurfaceSolidColor		= m_pfnScaleSurfaceToSurfaceSolidColor;
	p->FillRect								= m_pfnFillRect;
	p->DrawStringToSurface					= m_pfnDrawStringToSurface;
	p->GetFrameTime							= m_pfnGetFrameTime;
	p->GetSurfaceDims						= m_pfnGetSurfaceDims;
	p->GetCameraRect						= m_pfnGetCameraRect;

	m_bHooked = DFALSE;
}

void CVRStereo::MapRect(const DRect &rcIn, DRect &rcOut)
{
	const VRXform &x = CurXform();

	rcOut.left		= VRRound(x.ox + (DFLOAT)rcIn.left   * x.sx);
	rcOut.right		= VRRound(x.ox + (DFLOAT)rcIn.right  * x.sx);
	rcOut.top		= VRRound(x.oy + (DFLOAT)rcIn.top    * x.sy);
	rcOut.bottom	= VRRound(x.oy + (DFLOAT)rcIn.bottom * x.sy);

	// Don't let thin lines vanish when scaled down.
	if (rcOut.right <= rcOut.left && rcIn.right > rcIn.left)	rcOut.right = rcOut.left + 1;
	if (rcOut.bottom <= rcOut.top && rcIn.bottom > rcIn.top)	rcOut.bottom = rcOut.top + 1;
}

// Works out where a blit to the virtual screen lands on the real screen.
// pVirtDest == DNULL means "unscaled blit at (rcDest.left, rcDest.top)".
// Returns DFALSE if nothing is left to draw after clipping.
DBOOL CVRStereo::MapBlit(HSURFACE hSrc, DRect *pSrcRect, DRect *pVirtDest, DRect &rcDest, DRect &rcSrc)
{
	if (SkipThisDraw()) return DFALSE;

	if (pSrcRect)
	{
		rcSrc = *pSrcRect;
	}
	else
	{
		DDWORD nW = 0, nH = 0;
		m_pClientDE->GetSurfaceDims(hSrc, &nW, &nH);
		rcSrc.left = 0; rcSrc.top = 0;
		rcSrc.right = (int)nW; rcSrc.bottom = (int)nH;
	}

	DRect rcVirt;
	if (pVirtDest)
	{
		rcVirt = *pVirtDest;
	}
	else
	{
		rcVirt.left		= rcDest.left;
		rcVirt.top		= rcDest.top;
		rcVirt.right	= rcDest.left + (rcSrc.right - rcSrc.left);
		rcVirt.bottom	= rcDest.top + (rcSrc.bottom - rcSrc.top);
	}

	MapRect(rcVirt, rcDest);

	const VRXform &x = CurXform();

	if (!VRClip1D(rcDest.left, rcDest.right, rcSrc.left, rcSrc.right, x.clipL, x.clipR)) return DFALSE;
	if (!VRClip1D(rcDest.top, rcDest.bottom, rcSrc.top, rcSrc.bottom, x.clipT, x.clipB)) return DFALSE;

	return DTRUE;
}


// ======================================================================= //
//	Hooked engine functions
// ======================================================================= //

DRESULT CVRStereo::Hook_DrawSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnDrawSurfaceToSurface(hDest, hSrc, pSrcRect, destX, destY);

	DRect rcDest, rcSrc;
	rcDest.left = destX; rcDest.top = destY;
	if (!s.MapBlit(hSrc, pSrcRect, DNULL, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurface(hDest, hSrc, &rcDest, &rcSrc);
}

DRESULT CVRStereo::Hook_DrawSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY, HDECOLOR hColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnDrawSurfaceToSurfaceTransparent(hDest, hSrc, pSrcRect, destX, destY, hColor);

	DRect rcDest, rcSrc;
	rcDest.left = destX; rcDest.top = destY;
	if (!s.MapBlit(hSrc, pSrcRect, DNULL, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurfaceTransparent(hDest, hSrc, &rcDest, &rcSrc, hColor);
}

DRESULT CVRStereo::Hook_DrawSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc, DRect *pSrcRect, int destX, int destY, HDECOLOR hTransColor, HDECOLOR hFillColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnDrawSurfaceSolidColor(hDest, hSrc, pSrcRect, destX, destY, hTransColor, hFillColor);

	DRect rcDest, rcSrc;
	rcDest.left = destX; rcDest.top = destY;
	if (!s.MapBlit(hSrc, pSrcRect, DNULL, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurfaceSolidColor(hDest, hSrc, &rcDest, &rcSrc, hTransColor, hFillColor);
}

DRESULT CVRStereo::Hook_ScaleSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnScaleSurfaceToSurface(hDest, hSrc, pDestRect, pSrcRect);

	DRect rcFull;
	if (!pDestRect)
	{
		rcFull.left = 0; rcFull.top = 0;
		rcFull.right = VRRound(s.m_fVirtW); rcFull.bottom = VRRound(s.m_fVirtH);
		pDestRect = &rcFull;
	}

	DRect rcDest, rcSrc;
	if (!s.MapBlit(hSrc, pSrcRect, pDestRect, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurface(hDest, hSrc, &rcDest, &rcSrc);
}

DRESULT CVRStereo::Hook_ScaleSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect, HDECOLOR hColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnScaleSurfaceToSurfaceTransparent(hDest, hSrc, pDestRect, pSrcRect, hColor);

	DRect rcFull;
	if (!pDestRect)
	{
		rcFull.left = 0; rcFull.top = 0;
		rcFull.right = VRRound(s.m_fVirtW); rcFull.bottom = VRRound(s.m_fVirtH);
		pDestRect = &rcFull;
	}

	DRect rcDest, rcSrc;
	if (!s.MapBlit(hSrc, pSrcRect, pDestRect, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurfaceTransparent(hDest, hSrc, &rcDest, &rcSrc, hColor);
}

DRESULT CVRStereo::Hook_ScaleSurfaceToSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc, DRect *pDestRect, DRect *pSrcRect, HDECOLOR hTransColor, HDECOLOR hFillColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnScaleSurfaceToSurfaceSolidColor(hDest, hSrc, pDestRect, pSrcRect, hTransColor, hFillColor);

	DRect rcFull;
	if (!pDestRect)
	{
		rcFull.left = 0; rcFull.top = 0;
		rcFull.right = VRRound(s.m_fVirtW); rcFull.bottom = VRRound(s.m_fVirtH);
		pDestRect = &rcFull;
	}

	DRect rcDest, rcSrc;
	if (!s.MapBlit(hSrc, pSrcRect, pDestRect, rcDest, rcSrc)) return LT_OK;

	return s.m_pfnScaleSurfaceToSurfaceSolidColor(hDest, hSrc, &rcDest, &rcSrc, hTransColor, hFillColor);
}

DRESULT CVRStereo::Hook_FillRect(HSURFACE hDest, DRect *pRect, HDECOLOR hColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen) return s.m_pfnFillRect(hDest, pRect, hColor);
	if (s.SkipThisDraw()) return LT_OK;

	DRect rcVirt;
	if (pRect)
	{
		rcVirt = *pRect;
	}
	else
	{
		rcVirt.left = 0; rcVirt.top = 0;
		rcVirt.right = VRRound(s.m_fVirtW); rcVirt.bottom = VRRound(s.m_fVirtH);
	}

	DRect rcDest;
	s.MapRect(rcVirt, rcDest);

	// Clip (the "source" edges are dummies here)...
	const VRXform &x = s.CurXform();
	int s0 = rcDest.left, s1 = rcDest.right;
	if (!VRClip1D(rcDest.left, rcDest.right, s0, s1, x.clipL, x.clipR)) return LT_OK;
	s0 = rcDest.top; s1 = rcDest.bottom;
	if (!VRClip1D(rcDest.top, rcDest.bottom, s0, s1, x.clipT, x.clipB)) return LT_OK;

	return s.m_pfnFillRect(hDest, &rcDest, hColor);
}

void CVRStereo::Hook_DrawStringToSurface(HSURFACE hDest, HDEFONT hFont, HSTRING hString, DRect *pRect, HDECOLOR hForeColor, HDECOLOR hBackColor)
{
	CVRStereo &s = g_VRStereo;
	if (hDest != s.m_hScreen || !pRect)
	{
		s.m_pfnDrawStringToSurface(hDest, hFont, hString, pRect, hForeColor, hBackColor);
		return;
	}
	if (s.SkipThisDraw()) return;

	// Engine text can't be scaled, so keep it at its native size and move
	// its centre to where the scaled layout says it should be.

	int nTextW = 0, nTextH = 0;
	s.m_pClientDE->GetStringDimensions(hFont, hString, &nTextW, &nTextH);

	const VRXform &x = s.CurXform();
	DFLOAT fCX = x.ox + ((DFLOAT)pRect->left + (DFLOAT)nTextW * 0.5f) * x.sx;
	DFLOAT fCY = x.oy + ((DFLOAT)pRect->top + (DFLOAT)nTextH * 0.5f) * x.sy;

	DRect rc;
	rc.left		= VRRound(fCX - (DFLOAT)nTextW * 0.5f);
	rc.top		= VRRound(fCY - (DFLOAT)nTextH * 0.5f);
	rc.right	= x.clipR;
	rc.bottom	= x.clipB;
	if (rc.left < x.clipL) rc.left = x.clipL;
	if (rc.top < x.clipT) rc.top = x.clipT;
	if (rc.left >= rc.right || rc.top >= rc.bottom) return;

	s.m_pfnDrawStringToSurface(hDest, hFont, hString, &rc, hForeColor, hBackColor);
}

void CVRStereo::Hook_GetSurfaceDims(HSURFACE hSurf, DDWORD *pWidth, DDWORD *pHeight)
{
	CVRStereo &s = g_VRStereo;
	if (s.m_bVirtual2D && hSurf == s.m_hScreen)
	{
		if (pWidth)  *pWidth  = (DDWORD)VRRound(s.m_fVirtW);
		if (pHeight) *pHeight = (DDWORD)VRRound(s.m_fVirtH);
		return;
	}
	s.m_pfnGetSurfaceDims(hSurf, pWidth, pHeight);
}

void CVRStereo::Hook_GetCameraRect(HLOCALOBJ hObj, DBOOL *pbFullscreen, int *pLeft, int *pTop, int *pRight, int *pBottom)
{
	CVRStereo &s = g_VRStereo;
	s.m_pfnGetCameraRect(hObj, pbFullscreen, pLeft, pTop, pRight, pBottom);
	if (!s.m_bVirtualHud) return;

	// Express the view rectangle on the virtual HUD screen too.
	DFLOAT fX = s.m_fVirtW / (DFLOAT)s.m_nScreenW;
	DFLOAT fY = s.m_fVirtH / (DFLOAT)s.m_nScreenH;
	if (pbFullscreen && *pbFullscreen)
	{
		if (pLeft)   *pLeft   = 0;
		if (pTop)    *pTop    = 0;
		if (pRight)  *pRight  = VRRound(s.m_fVirtW);
		if (pBottom) *pBottom = VRRound(s.m_fVirtH);
		return;
	}
	if (pLeft)   *pLeft   = VRRound((DFLOAT)*pLeft   * fX);
	if (pTop)    *pTop    = VRRound((DFLOAT)*pTop    * fY);
	if (pRight)  *pRight  = VRRound((DFLOAT)*pRight  * fX);
	if (pBottom) *pBottom = VRRound((DFLOAT)*pBottom * fY);
}

float CVRStereo::Hook_GetFrameTime()
{
	CVRStereo &s = g_VRStereo;

	// The second eye redraws the same frame - don't advance animations again.
	if (s.m_nCurEye > 0) return 0.0f;

	return s.m_pfnGetFrameTime();
}


// ======================================================================= //
//	Console tools
// ======================================================================= //

void CVRStereo::Calibrate(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList, DBOOL bFootScale)
{
	if (!m_pClientDE) return;
	CClientDE* pClientDE = m_pClientDE;

	if (!hCamera)
	{
		pClientDE->CPrint("VRCalibrate: no camera yet - start a level first.");
		return;
	}

	DVector vPos;
	pClientDE->GetObjectPos(hCamera, &vPos);

	ClientIntersectQuery query;
	ClientIntersectInfo info;

	VEC_COPY(query.m_From, vPos);
	VEC_COPY(query.m_To, vPos);
	query.m_To.y -= VR_FLOOR_RANGE;
	VEC_SET(query.m_Direction, 0.0f, -1.0f, 0.0f);
	query.m_Flags		= INTERSECT_OBJECTS | IGNORE_NONSOLID;
	query.m_FilterFn	= pFilterList ? ObjListFilterFn : DNULL;
	query.m_pUserData	= pFilterList;

	if (!pClientDE->IntersectSegment(&query, &info))
	{
		pClientDE->CPrint("VRCalibrate: couldn't find the floor below the camera.");
		return;
	}

	DFLOAT fHeightUnits = vPos.y - info.m_Point.y;
	if (fHeightUnits < 1.0f)
	{
		pClientDE->CPrint("VRCalibrate: the camera is too close to the floor (%.1f units).", fHeightUnits);
		return;
	}

	DFLOAT fEyeHeight = bFootScale ? m_vtEyeHeightFoot.GetFloat(1.7f) : m_vtEyeHeightMCA.GetFloat(10.0f);
	if (fEyeHeight < 0.1f) fEyeHeight = 0.1f;

	DFLOAT fScale = fHeightUnits / fEyeHeight;
	const char* pVar = bFootScale ? "VRScaleFoot" : "VRScaleMCA";

	char str[128];
	sprintf(str, "+%s %f", pVar, fScale);		// '+' = saved in the config file
	pClientDE->RunConsoleString(str);

	pClientDE->CPrint("VRCalibrate: eye is %.1f units above the floor, target %.2f m -> %s = %.2f units per metre.",
					  fHeightUnits, fEyeHeight, pVar, fScale);
}

void CVRStereo::PrintInfo()
{
	if (!m_pClientDE) return;
	CClientDE* pClientDE = m_pClientDE;

	RefreshFrame(0.0f);

	static const char* s_pModeNames[] = { "off", "full side-by-side", "half side-by-side" };

	int nEyeW = (int)m_nScreenW / 2;
	pClientDE->CPrint("VR: VRStereo %d (%s), screen %dx%d, each eye %dx%d",
					  m_nMode, s_pModeNames[m_nMode], (int)m_nScreenW, (int)m_nScreenH, nEyeW, (int)m_nScreenH);

	pClientDE->CPrint("VR: VRIPD %.1f mm, VRFov %.1f (last eye FOV %.1f x %.1f deg)",
					  m_vtIPD.GetFloat(64.0f), m_vtFov.GetFloat(0.0f),
					  VR_RAD2DEG(m_fEyeFovX), VR_RAD2DEG(m_fEyeFovY));

	pClientDE->CPrint("VR: VRScaleFoot %.2f, VRScaleMCA %.2f units per metre (last eye separation %.2f units)",
					  m_vtScaleFoot.GetFloat(40.0f), m_vtScaleMCA.GetFloat(8.0f), m_fIpdWorld);

	pClientDE->CPrint("VR: headset bridge %s, head tracking %s, controllers %s (%s aims)",
					  m_bBridgeAlive ? "connected" : "not running",
					  IsHeadTracking() ? "on" : "off",
					  IsControllerActive() ? "on" : "off",
					  UsesControllerAim() ? "right hand" : "head");
	pClientDE->CPrint("VR: game resolution %dx%d, drawing at %dx%d, window %dx%d",
					  (int)m_nRenderW, (int)m_nRenderH, (int)m_nSurfW, (int)m_nSurfH, (int)m_nClientW, (int)m_nClientH);

	DFLOAT fAimDist = (m_fAimInvDepth > 0.00001f) ? (1.0f / m_fAimInvDepth) : -1.0f;
	pClientDE->CPrint("VR: HUD %s (strip %dx%d at %dx), eye pictures %dx%d",
					  m_bHudStrip ? "on its own sharp panel" : "drawn into each eye",
					  (int)m_nHudW, (int)m_nHudH, m_nHudScale, (int)m_nScreenW / 2, (int)m_nEyeH);
	pClientDE->CPrint("VR: direct capture %s - renderer frames seen %d, handed to the bridge %d",
					  m_bCaptureHook ? "ready" : "not available", VRCapture_Seen(), VRCapture_Frames());
	pClientDE->CPrint("VR: VRHudWidth %.0f deg, VRHudLayout %.0f, VRMenuWidth %.0f deg",
					  m_vtHudWidth.GetFloat(60.0f), m_vtHudLayout.GetFloat(512.0f), m_vtMenuWidth.GetFloat(80.0f));
	pClientDE->CPrint("VR: VRHudDepth %.2f m (%.1f px shift), VRHudScale %.2f, aim distance %s%.0f units",
					  m_vtHudDepth.GetFloat(3.5f), GetHudShift(), m_vtHudScale.GetFloat(1.0f),
					  (fAimDist < 0.0f) ? "infinite " : "", (fAimDist < 0.0f) ? 0.0f : fAimDist);
}


// ======================================================================= //
//	Headset link (ShogoVRBridge.exe)
// ======================================================================= //

void CVRStereo::OpenShared()
{
	if (m_pShared) return;

	HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
									 sizeof(ShogoVRShared), SHOGOVR_SHARED_NAME);
	if (!hMap) return;
	DBOOL bExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

	ShogoVRShared* p = (ShogoVRShared*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShogoVRShared));
	if (!p)
	{
		CloseHandle(hMap);
		return;
	}

	if (!bExisted || p->magic != SHOGOVR_MAGIC)
	{
		p->magic		= SHOGOVR_MAGIC;
		p->version		= SHOGOVR_VERSION;
		p->structSize	= sizeof(ShogoVRShared);
	}

	m_hMapping	= (void*)hMap;
	m_pShared	= p;
}

void CVRStereo::CloseShared()
{
	if (m_pShared)
	{
		m_nReportViewMode = SHOGOVR_VIEW_OFF;
		WriteGameBlock();
		UnmapViewOfFile(m_pShared);
		m_pShared = DNULL;
	}
	if (m_hMapping)
	{
		CloseHandle((HANDLE)m_hMapping);
		m_hMapping = DNULL;
	}
	m_bBridgeAlive = DFALSE;
}

void CVRStereo::UpdateBridge()
{
	if (!m_pClientDE) return;
	CheckSettingsFile();

	if (!m_nGameHwnd)
	{
		void* pHwnd = DNULL;
		m_pClientDE->GetEngineHook("HWND", &pHwnd);
		m_nGameHwnd = (DDWORD)pHwnd;
	}

	// Poll the WinlatorXR XrApi.  Fresh packets mean the headset is live.
	XrApiPose pose;
	DBOOL bGot = DFALSE;
	if (m_pXrApi && m_pXrApi->IsRunning())
		bGot = m_pXrApi->Poll(pose);
	if (!bGot) pose.bValid = DFALSE;

	DFLOAT fNow = m_pClientDE->GetTime();
	if (bGot && pose.bValid)
	{
		m_nLastBridgeBeat++;
		m_fLastBridgeBeatTime = fNow;
	}
	m_bBridgeAlive = (m_nLastBridgeBeat != 0) && ((fNow - m_fLastBridgeBeatTime) < 1.0f);
	if (m_bBridgeAlive != m_bLoggedBridge)
	{
		Log(m_bBridgeAlive ? "Headset connected (XrApi)" : "Headset gone (XrApi)");
		if (m_bBridgeAlive) m_bMenuLayoutDirty = DTRUE;		// VR menu size is now in play (#5)
		m_bLoggedBridge = m_bBridgeAlive;
	}

	if (bGot && pose.bValid && m_bBridgeAlive)
	{
		// XrApi has no layer support - it captures the one game window - so the
		// HUD strip and floating panels are off, and the HUD and menus are drawn
		// into each eye instead (see BeginFrame / ComputeLayout / Begin2D).
		m_nBridgeFlags	= SHOGOVR_BRIDGE_HEADSET_ACTIVE;
		m_nPoseId++;
		VEC_SET(m_vHeadF, pose.headF[0], pose.headF[1], pose.headF[2]);
		VEC_SET(m_vHeadU, pose.headU[0], pose.headU[1], pose.headU[2]);
		VEC_SET(m_vHeadP, pose.headP[0], pose.headP[1], pose.headP[2]);
		m_fHeadYaw		= pose.headYaw;
		m_fHeadPitch	= pose.headPitch;
		m_fNeedTanX		= pose.tanX;
		m_fNeedTanY		= pose.tanY;

		m_nCtrlFlags	= (pose.bCtrlActive ? SHOGOVR_CTRL_ACTIVE : 0)
						| (pose.bRightPose ? SHOGOVR_CTRL_RIGHT_POSE : 0)
						| (pose.bLeftPose  ? SHOGOVR_CTRL_LEFT_POSE  : 0);
		m_nButtons		= pose.buttons;
		m_fMoveX = pose.moveX;	m_fMoveY = pose.moveY;
		m_fTurnX = pose.turnX;	m_fTurnY = pose.turnY;
		VEC_SET(m_vRightF, pose.rF[0], pose.rF[1], pose.rF[2]);	VEC_SET(m_vRightU, pose.rU[0], pose.rU[1], pose.rU[2]);	VEC_SET(m_vRightP, pose.rP[0], pose.rP[1], pose.rP[2]);
		VEC_SET(m_vLeftF, pose.lF[0], pose.lF[1], pose.lF[2]);	VEC_SET(m_vLeftU, pose.lU[0], pose.lU[1], pose.lU[2]);	VEC_SET(m_vLeftP, pose.lP[0], pose.lP[1], pose.lP[2]);

		// Dominant hand (Controls/LeftHanded): the rest of the mod treats the
		// "right" hand as the gun hand, so in left-handed mode the two poses
		// trade places and every gun model follows the other controller.  Only
		// the two TRIGGERS change action - sticks, buttons and the grips stay
		// put.  The grips are deliberately NOT swapped: the left grip is always
		// "jump" and the right grip always "jump + double jump" (see
		// XrApiUDP.cpp, #42), so a swap here would move the double jump to the
		// other hand and leave it silent on foot again.
		if (m_bLeftHanded)
		{
			DVector vT;
			VEC_COPY(vT, m_vRightF);	VEC_COPY(m_vRightF, m_vLeftF);	VEC_COPY(m_vLeftF, vT);
			VEC_COPY(vT, m_vRightU);	VEC_COPY(m_vRightU, m_vLeftU);	VEC_COPY(m_vLeftU, vT);
			VEC_COPY(vT, m_vRightP);	VEC_COPY(m_vRightP, m_vLeftP);	VEC_COPY(m_vLeftP, vT);

			DDWORD n = m_nButtons;
			m_nButtons = (n & ~(DDWORD)(SHOGOVR_BTN_FIRE | SHOGOVR_BTN_LEFT_TRIGGER))
					   | ((n & SHOGOVR_BTN_FIRE)			? SHOGOVR_BTN_LEFT_TRIGGER : 0)
					   | ((n & SHOGOVR_BTN_LEFT_TRIGGER)	? SHOGOVR_BTN_FIRE			: 0);
		}
	}
	else if (!m_bBridgeAlive)
	{
		m_nCtrlFlags = 0;
		m_nButtons = 0;
	}

	UpdateTwoHanded();

	// Button edges for this frame
	if (!IsControllerActive()) m_nButtons = 0;
	m_nRawButtons	= m_nButtons;	// the runtime's word, as reported (see GetRawButtons)

	// #42 is fixed at the SOURCE: CXrApiUDP::ButtonBits() sets the jump bit for
	// the right grip as well as the double-jump bit (`XB(0) | XB(12)`), so the
	// right grip genuinely looks like a jump to everything downstream - which is
	// what plays the sound on foot - while the double-jump bit stays for the MCA.
	// See game/XrApiUDP.cpp.  (A second, later OR of the same bit used to sit
	// here; it was redundant with that source-level fix and has been removed so
	// the intent lives in exactly one place.)  #45 restates the same contract
	// for the 1.0 cycle - jump from EITHER grip, double jump only from the
	// right grip, and the right grip sends JUMP and DOUBLEJUMP together - and
	// the mapping above matches it exactly; the log below exists so the next
	// device pass PROVES it against the runtime's own bool string instead of
	// taking the source's word for it.

	// Log presses with the RAW runtime bool string attached (#45).  One line
	// per press carries both halves of the question: which physical grip the
	// runtime says went down (XrApi order: 0 = L_GRIP ... 12 = R_GRIP - see
	// game/XrApiUDP.cpp ButtonBits) and which bits the game was then handed.
	// Stick-direction chars are skipped - they chatter at the dead-zone edge
	// while driving - and the jump bits' own press edge is kept as a second
	// trigger, so a grip that lands on an unexpected index still leaves a line.
	{
		const char* szBools = (m_pXrApi && m_pXrApi->IsRunning()) ? m_pXrApi->GetBools() : "";
		int nLen  = (int)strlen(szBools);
		int nPrev = (int)strlen(m_szPrevBools);
		static const int anBtnIdx[] = { 0, 1, 2, 7, 8, 9, 10, 11, 12, 13, 18 };	// every non-stick bool
		DBOOL bDown = (!m_szPrevBools[0] && nLen > 0) ? DTRUE : DFALSE;			// first packet: record it as received
		for (int k = 0; k < (int)(sizeof(anBtnIdx) / sizeof(anBtnIdx[0])); k++)
		{
			int  i    = anBtnIdx[k];
			char cNew = (i < nLen)  ? szBools[i]       : (char)0;
			char cOld = (i < nPrev) ? m_szPrevBools[i] : (char)0;
			if (cNew == 'T' && cOld != 'T') { bDown = DTRUE; break; }
		}
		DDWORD nNewGrip = m_nButtons & ~m_nPrevButtons;
		//if (bDown || (nNewGrip & (SHOGOVR_BTN_JUMP | SHOGOVR_BTN_DOUBLEJUMP)))
			// Log("VR: input - runtime bools [%s] Lgrip(0)=%d Rgrip(12)=%d -> JUMP=%d DOUBLEJUMP=%d",
			// 	szBools,
			// 	(nLen > 0  && szBools[0]  == 'T') ? 1 : 0,
			// 	(nLen > 12 && szBools[12] == 'T') ? 1 : 0,
			// 	(m_nButtons & SHOGOVR_BTN_JUMP) ? 1 : 0,
			// 	(m_nButtons & SHOGOVR_BTN_DOUBLEJUMP) ? 1 : 0);
		strncpy(m_szPrevBools, szBools, sizeof(m_szPrevBools) - 1);
		m_szPrevBools[sizeof(m_szPrevBools) - 1] = 0;
	}

	m_nPressed		= m_nButtons & ~m_nPrevButtons;
	m_nReleased		= m_nPrevButtons & ~m_nButtons;
	m_nPrevButtons	= m_nButtons;
	m_nSuppressed	&= m_nButtons;
	m_bAimByHand	= DFALSE;		// ApplyHeadTracking sets it again during gameplay

	if (!IsHeadTracking()) m_bHaveBody = DFALSE;
	if (!IsActive()) m_nReportViewMode = SHOGOVR_VIEW_OFF;

	// Resolution and window size, for logging and the renderer fixer.
	RMode mode;
	memset(&mode, 0, sizeof(mode));
	if (m_pClientDE->GetRenderMode(&mode) == LT_OK)
	{
		m_nRenderW = (long)mode.m_Width;
		m_nRenderH = (long)mode.m_Height;
	}
	DDWORD nSurfW = 0, nSurfH = 0;
	m_pClientDE->GetSurfaceDims(m_pClientDE->GetScreenSurface(), &nSurfW, &nSurfH);
	m_nSurfW = (long)nSurfW;
	m_nSurfH = (long)nSurfH;

	RECT rcClient;
	if (m_nGameHwnd && GetClientRect((HWND)m_nGameHwnd, &rcClient))
	{
		m_nClientW = rcClient.right - rcClient.left;
		m_nClientH = rcClient.bottom - rcClient.top;
	}

	if (m_nRenderW != m_nLoggedRenderW || m_nRenderH != m_nLoggedRenderH || m_nSurfW != m_nLoggedSurfW ||
		m_nSurfH != m_nLoggedSurfH || m_nClientW != m_nLoggedClientW || m_nClientH != m_nLoggedClientH)
	{
		Log("Resolution %ldx%ld, drawing at %ldx%ld, window %ldx%ld", m_nRenderW, m_nRenderH, m_nSurfW, m_nSurfH, m_nClientW, m_nClientH);
		m_nLoggedRenderW = m_nRenderW; m_nLoggedRenderH = m_nRenderH;
		m_nLoggedSurfW = m_nSurfW; m_nLoggedSurfH = m_nSurfH;
		m_nLoggedClientW = m_nClientW; m_nLoggedClientH = m_nClientH;
	}

	// Controller haptics (#40): one pulse per *shot*, not per frame the trigger
	// is held.  ShotFired() (called from CWeaponModel::Fire, where the ammo is
	// actually spent) puts a frame or two of vibration on the gun hand.  Tying
	// it to the shot rather than the trigger means a slow weapon, a shotgun or
	// a charged shot buzzes once for the shot instead of shaking continuously,
	// and a weapon switch or an empty clip produces nothing at all - the
	// weapon-change pulse that used to live here is deliberately gone.
	if (m_fHapticL > 0.0f) m_fHapticL -= 1.0f;
	if (m_fHapticR > 0.0f) m_fHapticR -= 1.0f;
	if (m_fHapticL < 0.0f) m_fHapticL = 0.0f;
	if (m_fHapticR < 0.0f) m_fHapticR = 0.0f;

	// Tell the runtime what we're doing: haptics, VR on, SBS when stereo is
	// active, and the per-eye FOV we render at (0 = runtime default).
	if (m_pXrApi && m_pXrApi->IsRunning())
	{
		int nMode3D = (GetMode() != VR_STEREO_OFF) ? 1 : 0;
		DFLOAT fFovX = (m_fEyeFovX > 0.01f) ? VR_RAD2DEG(m_fEyeFovX) : 0.0f;
		DFLOAT fFovY = (m_fEyeFovY > 0.01f) ? VR_RAD2DEG(m_fEyeFovY) : 0.0f;
		m_pXrApi->SendFrame(m_fHapticL, m_fHapticR, 1, nMode3D, fFovX, fFovY);
	}

	if (m_vtVRFullscreen.GetFloat(1.0f) != 0.0f)
		FitVRWindow();				// no title bar, fills the display (like HWXR)
	else
		FitGameWindow();
}

DBOOL CVRStereo::IsHeadTracking()
{
	if (!m_pClientDE || !m_bBridgeAlive || m_nPoseId == 0) return DFALSE;
	if (!(m_nBridgeFlags & SHOGOVR_BRIDGE_HEADSET_ACTIVE)) return DFALSE;
	if (m_vtHeadTracking.GetFloat(1.0f) == 0.0f) return DFALSE;
	return IsActive();
}

void CVRStereo::ApplyHeadTracking(DFLOAT &fYaw, DFLOAT &fPitch)
{
	if (!IsHeadTracking())
	{
		m_bHaveBody = DFALSE;
		return;
	}

	// Body yaw: where your feet point.  The mouse (and anything else in the
	// game that turns you, e.g. respawning) changes the game's yaw between
	// our calls - fold that into the body, then add stick turning.

	if (!m_bHaveBody)
	{
		m_fBodyYaw		= fYaw - m_fHeadYaw;
		m_fLastOutYaw	= fYaw;
		m_bHaveBody		= DTRUE;
	}
	m_fBodyYaw += (fYaw - m_fLastOutYaw);
	m_fBodyYaw += StickTurn();

	// Aim: the right hand if you have one (VRAimMode 1), otherwise the head.
	// Pitch never comes from the mouse - tilting the world is a quick way to feel sick.

	DFLOAT fAimYaw = m_fHeadYaw, fAimPitch = m_fHeadPitch;
	if (UsesControllerAim())
	{
		DFLOAT fx = m_vAimF.x, fy = m_vAimF.y, fz = m_vAimF.z;
		fAimYaw		= (DFLOAT)atan2(fx, fz);
		fAimPitch	= (DFLOAT)-atan2(fy, (DFLOAT)sqrt(fx*fx + fz*fz));
		m_bAimByHand = DTRUE;
	}

	DFLOAT fMaxPitch = VR_PI * 0.5f - 0.1f;
	if (fAimPitch >  fMaxPitch) fAimPitch =  fMaxPitch;
	if (fAimPitch < -fMaxPitch) fAimPitch = -fMaxPitch;

	fYaw	= m_fBodyYaw + fAimYaw;
	fPitch	= fAimPitch;

	m_fLastOutYaw	= fYaw;
	m_bHeadApplied	= DTRUE;
}

void CVRStereo::RequestRecenter()
{
	if (!m_pClientDE) return;

	if (m_pXrApi && m_pXrApi->IsRunning())
	{
		m_pXrApi->RequestRecenter();
		m_pClientDE->CPrint("VR: recentred.");
	}
	else
	{
		m_pClientDE->CPrint("VR: the headset link isn't running.");
	}
}

// Screen flash (#28), once per frame: force the diagnostic colour when
// VRFlashTest is on, then paint the hurt/explosion vignette into every eye.
// Called on the same post-render path as the sync pixel (after the world and
// the HUD, before the frame is presented) so the fills land on the frame the
// runtime captures.
void CVRStereo::DrawScreenFlashFrame()
{
	if (!m_pClientDE) return;

	// The post-render path can run before any stereo Begin2D has cached the
	// original FillRect; take it now if the hooks aren't holding the slot.
	if (!m_pfnFillRect && !m_bHooked) m_pfnFillRect = m_pClientDE->FillRect;
	if (!m_pfnFillRect) return;

	if (m_vtFlashTest.GetFloat(0.0f) != 0.0f)
	{
		m_fFlashR = 1.0f;
		m_fFlashG = 0.0f;
		m_fFlashB = 0.0f;
		m_bFlash  = DTRUE;
	}

	if (!m_bFlash) return;

	int nEyes = GetNumEyes();
	for (int nEye = 0; nEye < nEyes; nEye++) DrawScreenFlash(nEye);
}

// WinlatorXR reads a shade of red from the top-left corner of the captured
// frame to keep itself in sync.  Red = HMD_SYNC (0..255).  A 10x10 square, as
// HWXR uses, so a small capture offset never lands on the border; in SBS the
// right eye half gets the same square so whichever half the runtime samples
// carries the frame ID.
void CVRStereo::DrawXrSyncPixel()
{
	if (!m_pClientDE || !m_pXrApi || !m_bBridgeAlive) return;

	int r = (int)(m_pXrApi->LastSync() + 0.5f);
	if (r < 0) r = 0;
	if (r > 255) r = 255;

	HSURFACE hScreen = m_pClientDE->GetScreenSurface();
	if (!hScreen) return;

	DRect rc;
	rc.left = 0; rc.top = 0;
	rc.right = 10; rc.bottom = 10;
	DDWORD col = m_pClientDE->SetupColor1((DFLOAT)r / 255.0f, 0.0f, 0.0f, DFALSE);
	m_pClientDE->FillRect(hScreen, &rc, col);

	if (IsActive())
	{
		DDWORD w = 0, h = 0;
		m_pClientDE->GetSurfaceDims(hScreen, &w, &h);
		if (w >= 24)
		{
			rc.left = (long)(w / 2); rc.top = 0;
			rc.right = rc.left + 10; rc.bottom = 10;
			m_pClientDE->FillRect(hScreen, &rc, col);
		}
	}
}

void CVRStereo::WriteGameBlock()
{
	if (!m_pShared) return;
	ShogoVRShared* s = m_pShared;

	InterlockedIncrement((long*)&s->gameSeq);		// odd: writing

	m_nGameBeat++;
	if (m_nGameBeat <= 0) m_nGameBeat = 1;
	s->gameHeartbeat	= m_nGameBeat;
	s->gameHwnd			= (unsigned int)m_nGameHwnd;
	s->usedPoseId		= m_nReportPoseId;
	s->viewMode			= m_nReportViewMode;
	s->swapEyes			= (m_vtSwapEyes.GetFloat(0.0f) != 0.0f) ? 1 : 0;
	s->renderTanX		= m_fReportTanX;
	s->renderTanY		= m_fReportTanY;
	s->eyeSeparation	= m_fReportSep;
	s->renderWidth		= m_nRenderW;
	s->renderHeight		= m_nRenderH;
	s->clientWidth		= m_nClientW;
	s->clientHeight		= m_nClientH;
	s->surfaceWidth		= m_nSurfW;
	s->surfaceHeight	= m_nSurfH;

	DBOOL bStripShown	= m_bHudStrip && (m_nReportViewMode == SHOGOVR_VIEW_TRACKED || m_nReportViewMode == SHOGOVR_VIEW_SCREEN);
	s->layoutFlags		= bStripShown ? SHOGOVR_LAYOUT_HUD : 0;
	s->eyeHeight		= (m_nReportViewMode == SHOGOVR_VIEW_FLAT) ? 0 : m_nEyeH;
	s->hudX				= m_nHudX;
	s->hudY				= m_nHudY;
	s->hudW				= m_nHudW;
	s->hudH				= m_nHudH;
	s->hudAngle			= VR_DEG2RAD(m_vtHudWidth.GetFloat(60.0f));
	s->hudDistance		= m_vtHudDepth.GetFloat(3.5f);
	s->screenAngle		= VR_DEG2RAD(m_vtMenuWidth.GetFloat(80.0f));

	InterlockedIncrement((long*)&s->gameSeq);		// even: done
}


// ======================================================================= //
//	Motion controllers
// ======================================================================= //

DBOOL CVRStereo::IsControllerActive()
{
	if (!m_pClientDE || !m_bBridgeAlive) return DFALSE;
	if (!(m_nCtrlFlags & SHOGOVR_CTRL_ACTIVE)) return DFALSE;
	if (m_vtControllers.GetFloat(1.0f) == 0.0f) return DFALSE;
	return IsActive();
}

DBOOL CVRStereo::UsesControllerAim()
{
	return IsControllerActive() && (m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE) && m_vtAimMode.GetFloat(1.0f) != 0.0f;
}

// WinlatorXR maps the controller face buttons to the keyboard letters X/Y/B/A
// on top of the XrApi feed, so the game sees stray keystrokes for buttons the
// VR module already handles.  While the game is live and the player is alive,
// drop them (see CRiotClientShell::OnKeyDown/OnKeyUp).
DBOOL CVRStereo::ShouldSwallowFaceKey(int nVk)
{
	if (!IsControllerActive() || !m_bGameplayInput || m_bPlayerDead) return DFALSE;
	switch (nVk)
	{
		case 'A': case 'B': case 'X': case 'Y': return DTRUE;
	}
	return DFALSE;
}

DBOOL CVRStereo::Hook_IsCommandOn(int nCommand)
{
	CVRStereo &s = g_VRStereo;
	// With the controllers in use the real keyboard is ignored, otherwise the
	// WinlatorXR keyboard and the controllers would both drive the player.
	if (!s.IsControllerActive() && s.m_pfnIsCommandOn && s.m_pfnIsCommandOn(nCommand)) return DTRUE;
	return s.VRCommandOn(nCommand);
}

// Mouse look/strafe arrives here (the game's yaw/pitch and strafe flags).  With
// the controllers in use the mouse must not also turn the view, so report none.
void CVRStereo::Hook_GetAxisOffsets(DFLOAT *offsets)
{
	CVRStereo &s = g_VRStereo;
	if (s.IsControllerActive())
	{
		offsets[0] = offsets[1] = offsets[2] = 0.0f;
		return;
	}
	if (s.m_pfnGetAxisOffsets) s.m_pfnGetAxisOffsets(offsets);
}

void CVRStereo::ShotFired()
{
	// A motion-melee slash is a one-shot: once the weapon actually swings,
	// drop the synthetic trigger so it does not auto-repeat.
	m_bMeleeTrigger = DFALSE;

	// Dual pistols (#5): this is the one call that happens exactly once per
	// shot - CWeaponModel::Fire makes it, right where the ammo count comes
	// down - so this is where the shot is assigned to one of the two pistols.
	// It has to happen here and not in ApplyPistolShotPose, because Fire()
	// runs BEFORE the FiredWeapon() block that sends the shot: picking the
	// hand there would leave this function buzzing the hand that fired the
	// PREVIOUS shot.  ApplyPistolShotPose and GetMuzzleFlashWorld both read
	// the answer later in the same frame.
	int nPistolHand = -1;
	if (m_bPistolSplit)
	{
		nPistolHand = PickPistolFireHand();
		if (nPistolHand >= 0)
		{
			m_nPistolFireHand = nPistolHand;
			m_nPistolLastHand = nPistolHand;	// so the next shot alternates
		}
		// Make UpdateDualPistols restart the off-hand copy's animation this
		// frame, so the two halves stay in phase even when the game replays the
		// same fire animation twice running and its index does not change.
		m_nOffHandAnim = (DDWORD)-1;
	}

	// A shot was actually fired (an ammo-consuming shot, or a dry click): buzz
	// the gun hand.  Called once per shot by CWeaponModel::Fire, which is the
	// one place that decrements the ammo count, so this cannot fire on a
	// weapon switch or on an idle frame.  The value is deliberately low - 2
	// frames is felt as a short kick - and it is scaled by the Haptics setting
	// (0 disables the feature entirely, as before).
	if (!IsControllerActive()) return;
	if (m_vtHaptics.GetFloat(1.0f) == 0.0f) return;

	// With the pistols split, buzz the hand that actually fired rather than
	// the dominant one - half the shots come from the other hand.  Hand 0 is
	// the gun hand, which is the physical right unless LeftHanded is set.
	if (nPistolHand >= 0)
	{
		DBOOL bGunHand = (DBOOL)(nPistolHand == 0);
		DBOOL bBuzzLeft = m_bLeftHanded ? bGunHand : !bGunHand;
		if (bBuzzLeft)	m_fHapticL = 2.0f;
		else			m_fHapticR = 2.0f;
		return;
	}

	// #8: holding the gun in both hands means feeling it in both.
	if (m_bTwoHandGrip)
	{
		m_fHapticL = 2.0f;
		m_fHapticR = 2.0f;
		return;
	}

	if (m_bLeftHanded)	m_fHapticL = 2.0f;		// the left trigger is the gun trigger
	else				m_fHapticR = 2.0f;
}

DBOOL CVRStereo::IsMeleeWeaponEquipped()
{
	if (!IsControllerActive()) return DFALSE;
	if (GetWeaponType((RiotWeaponId)m_nCurrentWeaponId) == MELEE) return DTRUE;
	return DFALSE;
}

DBOOL CVRStereo::VRCommandOn(int nCommand)
{
	if (!m_bGameplayInput || !IsControllerActive()) return DFALSE;

	DDWORD nHeld = m_nButtons & ~m_nSuppressed;
	switch (nCommand)
	{
		case COMMAND_ID_FIRING:
		{
			// A detected melee slash fires for a short window so that every
			// IsCommandOn(FIRING) query in this frame (player flags, move
			// manager, stealth check) sees the same shot.  ShotFired() clears
			// the latch once the weapon actually swings; the timeout is only
			// a safety net.
			if (m_bMeleeTrigger)
			{
				DFLOAT fNow = m_pClientDE ? m_pClientDE->GetTime() : 0.0f;
				if ((fNow - m_fMeleeLastFireTime) < 0.25f) return DTRUE;
				m_bMeleeTrigger = DFALSE;
			}
			// Dual pistols (#5): either trigger fires, and the game's own
			// animation cadence stays the single gate on how fast - which is
			// what keeps the pair at the stock rate of fire and the server
			// seeing the stock stream of shots.  Which of the two pistols owns
			// each shot is settled later, in PickPistolFireHand.
			if (m_bPistolSplit)
			{
				return ((nHeld & SHOGOVR_BTN_FIRE) || (nHeld & SHOGOVR_BTN_LEFT_TRIGGER)) ? DTRUE : DFALSE;
			}
			return (nHeld & SHOGOVR_BTN_FIRE) ? DTRUE : DFALSE;
		}

		// Grips (#42): the left grip sends JUMP; the right grip sends BOTH
		// JUMP and DOUBLEJUMP (see UpdateInput - the right grip's jump bit is
		// set alongside its double-jump bit).  So a regular jump ALWAYS happens
		// - which is what carries the jump sound - and the engine additionally
		// sees the double jump where it means something (MCA).  On foot the
		// engine's double-jump path is silent by design, but the regular jump
		// now plays its sound.
		case COMMAND_ID_JUMP:	return ((nHeld & SHOGOVR_BTN_JUMP) || (nHeld & SHOGOVR_BTN_DOUBLEJUMP)) ? DTRUE : DFALSE;
		case COMMAND_ID_DOUBLEJUMP:	return (nHeld & SHOGOVR_BTN_DOUBLEJUMP) ? DTRUE : DFALSE;
		case COMMAND_ID_DUCK:	return (m_bCrouchOn || (nHeld & SHOGOVR_BTN_CROUCH_HOLD)) ? DTRUE : DFALSE;

		// Walking.  The engine has no "set command" call exposed to the shell,
		// so movement flags are driven through this query instead:
		// CMoveMgr::UpdateControlFlags() reads IsCommandOn(FORWARD/REVERSE/
		// STRAFE_*) each frame to build BC_CFLG_*.  The analog values fed to
		// UpdateAxisMovement() do NOT scale the speed: CMoveMgr skips an
		// axis's analog cap as soon as its BC_CFLG_* flag is set, so a
		// nonzero axis moves at full speed (#47) - the dead zone's per-axis
		// cut in GetMoveAxes() is what actually shapes the feel.  Turning
		// stays on the right stick (StickTurn).  GetMoveAxes() also applies
		// the rotate-to-aim transform, so direction agrees with the flags.
		case COMMAND_ID_FORWARD:
		case COMMAND_ID_REVERSE:
		case COMMAND_ID_STRAFE_RIGHT:
		case COMMAND_ID_STRAFE_LEFT:
		{
			DFLOAT fFwd = 0.0f, fRight = 0.0f;
			GetMoveAxes(fFwd, fRight);
			//Added in some additional deadzoning here
			DFLOAT fDead = m_vtMoveDeadZone.GetFloat(0.15f);
			if (nCommand == COMMAND_ID_FORWARD)      return (fFwd  > fDead) ? DTRUE : DFALSE;
			if (nCommand == COMMAND_ID_REVERSE)      return (fFwd  < -fDead) ? DTRUE : DFALSE;
			if (nCommand == COMMAND_ID_STRAFE_RIGHT) return (fRight > fDead) ? DTRUE : DFALSE;
			return (fRight < -fDead) ? DTRUE : DFALSE;
		}
	}
	return DFALSE;
}

void CVRStereo::SetGameplayInput(DBOOL b)
{
	// Coming back from a menu with the trigger still down shouldn't fire.
	if (b && !m_bGameplayInput) m_nSuppressed = m_nButtons;
	m_bGameplayInput = b;
}

DBOOL CVRStereo::GetMoveAxes(DFLOAT &fForward, DFLOAT &fRight)
{
	fForward = fRight = 0.0f;
	if (!m_bGameplayInput || !IsControllerActive()) return DFALSE;

	// Dead zone (#47): a radial zone with a per-axis cut on top.
	//
	// The radial part is the upstream Shogo-VR behaviour: take the length of
	// the whole stick vector, and if it is past the dead zone rescale the
	// WHOLE vector by one common factor, so the push direction is preserved
	// exactly.  Tunable in ShogoVR.ini / Options > vr settings (0..1:
	// 0.15 = 15%).
	//
	// The per-axis cut is the part that actually does something on this
	// engine (the owner found it, #47): movement is consumed as BINARY
	// commands - VRCommandOn() answers FORWARD/REVERSE/STRAFE_* from the SIGN
	// of these values - and CMoveMgr's velocity cap then skips an axis's
	// analog cap the moment its BC_CFLG_* flag is set (the
	// OverrideForwardBackward logic in CMoveMgr.cpp).  So the gentle ramp the
	// radial rescale produces never reaches the player: any nonzero axis
	// value means full speed on that axis, and a tiny second-axis value - a
	// slightly-off-straight push - becomes full-speed strafe.  Zeroing each
	// axis whose own raw deflection is still inside the dead zone is the only
	// lever this engine leaves: it is where each axis switches on, and it
	// keeps the off-axis value from ever firing.
	// (An earlier all-per-axis zone was rejected on device - movement felt
	// like it only worked diagonally - so the zone's shape stays radial and
	// the cut only removes the small axis.  The two compose: a pure diagonal
	// now needs EACH axis past the dead zone, ~21% of stick length in total.)
	DFLOAT fDead = m_vtMoveDeadZone.GetFloat(0.15f);
	if (fDead < 0.0f) fDead = 0.0f;
	if (fDead > 0.95f) fDead = 0.95f;			// keep (1 - dead) > 0

	DFLOAT fLen = (DFLOAT)sqrt(m_fMoveX * m_fMoveX + m_fMoveY * m_fMoveY);
	if (fLen <= fDead) return DFALSE;

	DFLOAT fScale = ((fLen > 1.0f ? 1.0f : fLen) - fDead) / (1.0f - fDead) / fLen;
	fForward	= m_fMoveY * fScale;
	fRight		= m_fMoveX * fScale;

	// Per-axis cut (#47): kill an axis whose own raw deflection is inside the
	// dead zone, whatever the other axis is doing.  Both axes can end up
	// zeroed (a weak diagonal inside both cuts) - that still returns DTRUE on
	// purpose, so the shell keeps ignoring the keyboard/joystick axes (see
	// the GetMoveAxes call in RiotClientShell.cpp).
	DFLOAT fAbsX = (DFLOAT)fabs(m_fMoveX), fAbsY = (DFLOAT)fabs(m_fMoveY);
	if (fAbsY <= fDead) fForward = 0.0f;
	if (fAbsX <= fDead) fRight   = 0.0f;

	// The game walks you relative to where you aim.  To walk where you look
	// (or where the other hand points) instead, turn the stick by the angle
	// between the two.
	int nDir = (int)m_vtMoveDir.GetFloat(0.0f);
	if (nDir != 0 && IsHeadTracking())
	{
		DFLOAT fAimYaw = UsesControllerAim() ? (DFLOAT)atan2(m_vAimF.x, m_vAimF.z) : m_fHeadYaw;
		DFLOAT fRefYaw = fAimYaw;
		if (nDir == 1)
			fRefYaw = m_fHeadYaw;
		else if (nDir == 2 && (m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE))
			fRefYaw = (DFLOAT)atan2(m_vLeftF.x, m_vLeftF.z);

		DFLOAT fPhi = fRefYaw - fAimYaw;
		DFLOAT c = (DFLOAT)cos(fPhi), sn = (DFLOAT)sin(fPhi);
		DFLOAT x = fRight, y = fForward;
		fRight		= x * c + y * sn;
		fForward	= -x * sn + y * c;
	}

	return DTRUE;
}

// ======================================================================= //
//	Settings file
// ======================================================================= //

void CVRStereo::LoadSettingsFile(DBOOL bFirst)
{
	if (!m_pClientDE) return;

	// The game runs in its own folder; the mod and its settings are in ShogoVR\.
	if (!m_szIniPath[0])
	{
		char szDir[MAX_PATH];
		szDir[0] = 0;
		GetCurrentDirectoryA(MAX_PATH, szDir);
		_snprintf(m_szIniPath, sizeof(m_szIniPath) - 1, "%s\\ShogoVR\\ShogoVR.ini", szDir);
		m_szIniPath[sizeof(m_szIniPath) - 1] = 0;
	}

	// #50: the two weapon-swap keys shipped without the "VR" prefix that the
	// [Game] whitelist below insists on, so they were written to the ini and
	// applied live but silently dropped on the next launch - the setting
	// reverted every time the game started.  They are VR-prefixed now; carry a
	// value written under the old name across once, then retire the old key, so
	// this only ever does anything to an ini from 1.0 or earlier.  Done before
	// the timestamp is read below, so our own write doesn't look like an edit.
	if (bFirst)
	{
		static const char* const s_szRenamed[][2] =
		{
			{ "PhysicalWepSwap",	"VRPhysicalWepSwap"		},
			{ "WepSwapThumbstick",	"VRWepSwapThumbstick"	},
			{ 0, 0 }
		};
		int iR;
		for (iR = 0; s_szRenamed[iR][0]; iR++)
		{
			char szOld[32], szNew[32];
			GetPrivateProfileStringA("Game", s_szRenamed[iR][0], "", szOld, sizeof(szOld), m_szIniPath);
			if (!szOld[0]) continue;			// nothing written under the old name
			GetPrivateProfileStringA("Game", s_szRenamed[iR][1], "", szNew, sizeof(szNew), m_szIniPath);
			if (!szNew[0]) WritePrivateProfileStringA("Game", s_szRenamed[iR][1], szOld, m_szIniPath);
			WritePrivateProfileStringA("Game", s_szRenamed[iR][0], NULL, m_szIniPath);
		}
	}

	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExA(m_szIniPath, GetFileExInfoStandard, &fa)) return;
	m_nIniTimeLo = fa.ftLastWriteTime.dwLowDateTime;
	m_nIniTimeHi = fa.ftLastWriteTime.dwHighDateTime;

	// Dominant hand ([Controls] LeftHanded, set by the VR menu and the desktop
	// settings window).  The bridge applied this for PCVR; on the WinlatorXR
	// path the game reads it itself so left-handed mode works with no bridge.
	DBOOL bLeftHanded = GetPrivateProfileIntA("Controls", "LeftHanded", 0, m_szIniPath) != 0;
	if (bLeftHanded != m_bLeftHanded)
	{
		m_bLeftHanded = bLeftHanded;
		Log("VR: dominant hand %s", m_bLeftHanded
			? "left (gun follows the left hand, triggers/grips swapped)"
			: "right");
	}

	// [Game] holds console variables: "VRHudWidth=60" -> "VRHudWidth 60".
	// Only VR... names with plain numbers are accepted.
	static char s_buf[8192];
	DWORD nLen = GetPrivateProfileSectionA("Game", s_buf, sizeof(s_buf), m_szIniPath);
	if (nLen == 0) return;

	int nApplied = 0;
	char* p = s_buf;
	while (*p)
	{
		char* pNext = p + strlen(p) + 1;
		char* pEq = strchr(p, '=');
		if (pEq && (p[0] == 'V' || p[0] == 'v') && (p[1] == 'R' || p[1] == 'r'))
		{
			*pEq = 0;
			char* pVal = pEq + 1;
			while (*pVal == ' ' || *pVal == '\t') pVal++;

			DBOOL bOk = (strlen(p) < 40 && *pVal != 0 && strlen(pVal) < 20);
			char* q;
			for (q = p; bOk && *q; q++)		if (!isalnum((unsigned char)*q)) bOk = DFALSE;
			for (q = pVal; bOk && *q; q++)	if (!strchr("0123456789.-+eE", *q)) bOk = DFALSE;

			if (bOk)
			{
				char szCmd[96];
				_snprintf(szCmd, sizeof(szCmd) - 1, "%s %s", p, pVal);
				szCmd[sizeof(szCmd) - 1] = 0;
				m_pClientDE->RunConsoleString(szCmd);
				nApplied++;
			}
		}
		p = pNext;
	}

	if (!bFirst && nApplied) m_pClientDE->CPrint("VR: settings updated from ShogoVR.ini.");
}

void CVRStereo::CheckSettingsFile()
{
	if (!m_pClientDE || !m_szIniPath[0]) return;
	DFLOAT fNow = m_pClientDE->GetTime();
	if (fNow - m_fLastIniCheck < 1.0f && fNow >= m_fLastIniCheck) return;
	m_fLastIniCheck = fNow;

	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExA(m_szIniPath, GetFileExInfoStandard, &fa)) return;
	if (fa.ftLastWriteTime.dwLowDateTime != m_nIniTimeLo || fa.ftLastWriteTime.dwHighDateTime != m_nIniTimeHi)
	{
		LoadSettingsFile(DFALSE);
	}
}

DFLOAT CVRStereo::StickTurn()
{
	if (!m_bGameplayInput || !IsControllerActive()) return 0.0f;

	DFLOAT x = m_fTurnX, y = m_fTurnY;
	DFLOAT ax = (DFLOAT)fabs(x), ay = (DFLOAT)fabs(y);
	if (ay > 0.6f && ay > ax) return 0.0f;		// that's weapon switching, not turning

	DFLOAT fSnap = m_vtSnapTurn.GetFloat(45.0f);
	if (fSnap > 0.0f)
	{
		if (!m_bSnapLatched && ax > 0.7f)
		{
			m_bSnapLatched = DTRUE;
			return (x > 0.0f ? 1.0f : -1.0f) * VR_DEG2RAD(fSnap);
		}
		if (ax < 0.3f) m_bSnapLatched = DFALSE;
		return 0.0f;
	}

	const DFLOAT fDead = 0.2f;
	if (ax <= fDead) return 0.0f;
	DFLOAT fAmount = (ax - fDead) / (1.0f - fDead);
	if (fAmount > 1.0f) fAmount = 1.0f;
	return (x > 0.0f ? 1.0f : -1.0f) * fAmount * VR_DEG2RAD(m_vtTurnSpeed.GetFloat(150.0f)) * m_pClientDE->GetFrameTime();
}

int CVRStereo::PollWeaponCycle()
{
	if (!m_bGameplayInput || !IsControllerActive())
	{
		m_nWeaponLatch = 0;
		return 0;
	}

	// Physical motion weapon swapping (#4): reach behind shoulder (left = prev, right = next)
	if (m_vtWepSwapPhysical.GetFloat(0.0f) != 0.0f)
	{
		CClientDE* pClientDE = m_pClientDE;
		DFLOAT fNow = pClientDE ? pClientDE->GetTime() : 0.0f;
		if ((fNow - m_fWepSwapLastTime) >= 0.6f)
		{
			DVector vH = m_vHeadP;
			DVector vHF = m_vHeadF;
			DVector vHU = m_vHeadU;
			DVector vHR;
			VEC_CROSS(vHR, vHF, vHU);
			DFLOAT fMagHR = (DFLOAT)sqrt(VEC_DOT(vHR, vHR));
			if (fMagHR > 0.000001f) { vHR.x /= fMagHR; vHR.y /= fMagHR; vHR.z /= fMagHR; }

			// Left controller
			{
				DVector vRel;
				VEC_SUB(vRel, m_vLeftP, vH);
				DFLOAT fDotF = VEC_DOT(vRel, vHF);
				// roughly equal height to HMD or below/around - check vertical relative to head
				DFLOAT fDotU = vRel.y; // world up approx
				DFLOAT fDotR = VEC_DOT(vRel, vHR);
				// behind front of HMD means in front? no - behind means negative dot with forward
				if (fDotF < -0.05f)
				{
					// close to shoulder height
					if (fabs(fDotU) < 0.25f && fabs(fDotR) > 0.05f)
					{
						if (fDotR < -0.05f && !m_bWepSwapPhysLeftLatch)
						{
							m_bWepSwapPhysLeftLatch = DTRUE;
							m_fWepSwapLastTime = fNow;
							if (m_vtHaptics.GetFloat(1.0f) != 0.0f) m_fHapticL = 2.0f;
							return -1;
						}
					}
				}
				if (fDotF >= -0.01f) m_bWepSwapPhysLeftLatch = DFALSE;
			}
			// Right controller
			{
				DVector vRel;
				VEC_SUB(vRel, m_vRightP, vH);
				DFLOAT fDotF = VEC_DOT(vRel, vHF);
				DFLOAT fDotU = vRel.y;
				DFLOAT fDotR = VEC_DOT(vRel, vHR);
				if (fDotF < -0.05f)
				{
					if (fabs(fDotU) < 0.25f && fabs(fDotR) > 0.05f)
					{
						if (fDotR > 0.05f && !m_bWepSwapPhysRightLatch)
						{
							m_bWepSwapPhysRightLatch = DTRUE;
							m_fWepSwapLastTime = fNow;
							if (m_vtHaptics.GetFloat(1.0f) != 0.0f) m_fHapticR = 2.0f;
							return 1;
						}
					}
				}
				if (fDotF >= -0.01f) m_bWepSwapPhysRightLatch = DFALSE;
			}
		}
	}

	// Thumbstick swap
	if (m_vtWepSwapThumb.GetFloat(1.0f) == 0.0f)
	{
		m_nWeaponLatch = 0;
		return 0;
	}

	// Up/down on the right stick changes weapon.  Require a deliberate push so
	// a light nudge - or drift while turning - doesn't cycle weapons by
	// accident: the stick has to clear this dead zone before a swap fires, and
	// fall back below the (lower) reset before it can fire again (hysteresis).
	// Tunable in ShogoVR.ini / Options > vr settings: the VRWeaponSwapDead
	// dead zone is the press threshold; the reset sits 0.35 below it.
	DFLOAT fSwapPress = m_vtWeaponSwapDead.GetFloat(0.80f);
	if (fSwapPress < 0.0f) fSwapPress = 0.0f;
	if (fSwapPress > 1.0f) fSwapPress = 1.0f;
	DFLOAT fSwapReset = fSwapPress - 0.35f;
	if (fSwapReset < 0.0f) fSwapReset = 0.0f;

	DFLOAT y = m_fTurnY, ax = (DFLOAT)fabs(m_fTurnX), ay = (DFLOAT)fabs(m_fTurnY);
	if (m_nWeaponLatch == 0 && ay > fSwapPress && ay > ax)
	{
		m_nWeaponLatch = (y > 0.0f) ? 1 : -1;
		return m_nWeaponLatch;
	}
	if (ay < fSwapReset) m_nWeaponLatch = 0;
	return 0;
}

int CVRStereo::PollMenuDirection()
{
	if (!IsControllerActive())
	{
		m_nMenuDir = 0;
		return 0;
	}

	// Either stick, whichever is pushed further, acts as the arrow keys.
	DFLOAT x = m_fMoveX, y = m_fMoveY;
	if ((m_fTurnX * m_fTurnX + m_fTurnY * m_fTurnY) > (x * x + y * y)) { x = m_fTurnX; y = m_fTurnY; }

	int nDir = 0;
	if ((DFLOAT)fabs(y) > 0.6f && fabs(y) >= fabs(x))	nDir = (y > 0.0f) ? VK_UP : VK_DOWN;
	else if ((DFLOAT)fabs(x) > 0.6f)					nDir = (x > 0.0f) ? VK_RIGHT : VK_LEFT;

	DFLOAT fNow = m_pClientDE->GetTime();
	if (nDir == 0)
	{
		if (fabs(x) < 0.3f && fabs(y) < 0.3f) m_nMenuDir = 0;
		return 0;
	}
	if (nDir != m_nMenuDir)
	{
		m_nMenuDir = nDir;
		m_fMenuRepeatTime = fNow + 0.45f;
		return nDir;
	}
	if (fNow >= m_fMenuRepeatTime)
	{
		m_fMenuRepeatTime = fNow + 0.15f;
		return nDir;
	}
	return 0;
}

void CVRStereo::GetHandWorld(const DVector &vCamPos, DVector &vPos, DVector &vF, DVector &vU)
{
	// Dual pistols (#5): with the pair split there is no shared aim to follow -
	// each pistol points where its own hand points.  This matters because
	// m_vAimF also steers the player's view, so taking it here would rotate the
	// gun-hand pistol to point wherever the view had swung: on device that
	// showed up as the right pistol whipping round to aim at whatever the LEFT
	// pistol had just shot, overriding the right controller completely.
	if (m_bPistolSplit)
	{
		GetHandWorldFrom(vCamPos, m_vRightP, m_vRightF, m_vRightU, vPos, vF, vU);
		return;
	}

	// The gun hand, pointing wherever the player is aiming - which may be the
	// two-handed direction rather than the hand's own (see UpdateTwoHanded).
	GetHandWorldFrom(vCamPos, m_vRightP, m_vAimF, m_vAimU, vPos, vF, vU);
}

// The same hand-to-world transform for any hand pose.  Split out of
// GetHandWorld for the dual pistols (#5), whose off-hand copy is placed from
// the off hand's own pose: the body-frame rotation, world scale, crouch drop
// and hand-flip correction all have to match the gun hand exactly, so they
// live here once rather than being copied per hand.
void CVRStereo::GetHandWorldFrom(const DVector &vCamPos, const DVector &vHandP,
								 const DVector &vHandF, const DVector &vHandU,
								 DVector &vPos, DVector &vF, DVector &vU)
{
	CClientDE* pClientDE = m_pClientDE;

	DRotation rBody;
	DVector bU, bR, bF, vT;
	pClientDE->SetupEuler(&rBody, 0.0f, m_fBodyYaw, 0.0f);
	pClientDE->GetRotationVectors(&rBody, &bU, &bR, &bF);

	DFLOAT fScale = m_fWorldScaleUsed;
	VEC_MULSCALAR(vPos, bR, vHandP.x * fScale);
	VEC_MULSCALAR(vT,   bU, vHandP.y * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT,   bF, vHandP.z * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_ADD(vPos, vPos, vCamPos);

	// Crouch (#20): RenderStereo lowers the camera to fake ducking, but it
	// restores the camera object to its standing spot once the eyes are drawn,
	// so anything placed relative to the camera (the held gun, the muzzle
	// flash, the aim ray, the hidden first-person model that still fires the
	// shots) would stay up while the eyes drop.  Drop them by the same amount,
	// along the same body up axis, so the whole view moves together.
	if (m_fCrouchAmt > 0.0f)
	{
		DFLOAT fDrop = m_fCrouchAmt * m_vtCrouchDrop.GetFloat(0.6f) * fScale;
		VEC_MULSCALAR(vT, bU, fDrop);
		VEC_SUB(vPos, vPos, vT);
	}

	VEC_MULSCALAR(vF, bR, vHandF.x);
	VEC_MULSCALAR(vT, bU, vHandF.y);	VEC_ADD(vF, vF, vT);
	VEC_MULSCALAR(vT, bF, vHandF.z);	VEC_ADD(vF, vF, vT);

	VEC_MULSCALAR(vU, bR, vHandU.x);
	VEC_MULSCALAR(vT, bU, vHandU.y);	VEC_ADD(vU, vU, vT);
	VEC_MULSCALAR(vT, bF, vHandU.z);	VEC_ADD(vU, vU, vT);

	// Some controllers report the hand upside down, so by default we rotate the
	// hand/weapon 180 degrees about the barrel (roll) to agree with the hand.
	// The flag is inverted: 0 (default) applies the roll correction, 1 leaves
	// the hand as the runtime reported it.
	if (m_vtHandFlip.GetFloat(0.0f) == 0.0f) VEC_MULSCALAR(vU, vU, -1.0f);
}

void CVRStereo::AdjustWeaponPose(DVector &vPos, DRotation &rRot, DBOOL bFootScale)
{
	if (!m_pClientDE || !m_bAimByHand) return;

	// The gun model is scaled about its own origin, so scale the nudge too -
	// that keeps the grip where it was in your hand when you change the size.
	DFLOAT fGunScale = bFootScale ? m_vtGunScale.GetFloat(1.5f) : m_vtGunScaleMCA.GetFloat(1.0f);
	if (fGunScale < 0.2f) fGunScale = 0.2f;
	if (fGunScale > 5.0f) fGunScale = 5.0f;

	// vPos does NOT arrive as the camera position, whatever this function used
	// to claim: CWeaponModel has already added six offsets to it - the weapon's
	// own placement and its bob - and those are built from the GUN hand's
	// rotation.  Measuring a hand from that origin therefore feeds the gun
	// hand's movement into wherever that hand is put, which showed up on device
	// two ways: the gun-hand weapon got a doubled dose of its own controller,
	// and the off-hand pistol (#5) and free arm (#7) were dragged about by the
	// gun hand even though they belong to the other controller.
	//
	// So use the real camera, which the renderer hands us every frame.  Every
	// hand is then measured from one fixed point and follows only its own
	// controller.  The weapon's bob is deliberately dropped: in VR the gun's
	// position comes from the player's hand, and a bob the hand did not make is
	// exactly the "pulled by the other hand" feeling being removed here.
	//
	// If the camera is not known yet, fall back to what we were handed - the
	// pre-1.0 behaviour, and no worse than it ever was.
	DVector vOrigin;
	if (m_hCameraObj)	{ m_pClientDE->GetObjectPos(m_hCameraObj, &vOrigin); }
	else				{ VEC_COPY(vOrigin, vPos); }

	// Remembered so the mod's own hand-held copies are measured from the very
	// same point, and so cannot drift apart from the gun hand's model.
	VEC_COPY(m_vWeaponPoseOrigin, vOrigin);
	m_bWeaponPoseOrigin = DTRUE;

	// The gun goes to the right hand, pointing where the hand points.
	DVector vHandPos, vHandF, vHandU, hU, hR, hF, vT;
	GetHandWorld(vOrigin, vHandPos, vHandF, vHandU);
	m_pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
	m_pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);

	VEC_COPY(vPos, vHandPos);
	VEC_MULSCALAR(vT, hR, m_vtGunX.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hU, m_vtGunY.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hF, m_vtGunZ.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);

	// Dual pistols (#5): with the pair split, cancel out however far the fire
	// animation has carried this half's hand from the model's origin, so the
	// grip stays in the player's hand.  UpdateDualPistols measures it; see
	// GetPistolAnchorDrift for why it has to be done this way round.
	if (m_bPistolSplit) VEC_SUB(vPos, vPos, m_vPistolAnchorFix);
}

void CVRStereo::ScaleWeapon(HLOCALOBJ hWeapon, DBOOL bFootScale)
{
	if (!m_pClientDE || !hWeapon) return;

	// Shogo's arms and guns were sized to sit in the corner of a flat screen;
	// held in your hand in VR they look small, so they can be enlarged.
	DFLOAT fScale = 1.0f;
	if (m_bAimByHand)
	{
		fScale = bFootScale ? m_vtGunScale.GetFloat(1.5f) : m_vtGunScaleMCA.GetFloat(1.0f);
		if (fScale < 0.2f) fScale = 0.2f;
		if (fScale > 5.0f) fScale = 5.0f;
	}

	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);
	m_pClientDE->SetObjectScale(hWeapon, &vScale);
	m_fGunScaleUsed = fScale;
}

void CVRStereo::CheckRenderer(DBOOL bStable)
{
	// If the renderer rebuilt itself while the window was tiny or minimized,
	// it can carry on drawing a smaller picture inside the full-size window -
	// which in the headset looks like the world shrinking and the field of
	// view ballooning.  Re-applying the screen mode makes it draw at full size.

	if (!m_pClientDE || !IsActive() || !m_bBridgeAlive || m_vtFixRenderer.GetFloat(1.0f) == 0.0f) return;
	if (m_nRenderW <= 0 || m_nRenderH <= 0 || m_nSurfW <= 0 || m_nSurfH <= 0) return;

	DFLOAT fNow = m_pClientDE->GetTime();
	DBOOL bWrong = (m_nSurfW != m_nRenderW || m_nSurfH != m_nRenderH);
	DBOOL bWindowReady = (m_nClientW == m_nRenderW && m_nClientH == m_nRenderH) &&
						 !(m_nGameHwnd && IsIconic((HWND)m_nGameHwnd));

	if (!bWrong || !bStable || !bWindowReady)
	{
		m_fRendererBadSince = -1.0f;		// fine, or wait until the window is right again
		return;
	}

	if (m_fRendererBadSince < 0.0f)
	{
		m_fRendererBadSince = fNow;
		return;
	}
	if (fNow - m_fRendererBadSince < 1.5f) return;

	// Don't loop if it doesn't help: at most 3 resets a minute.
	if (fNow - m_fLastRendererReset > 60.0f) m_nRendererResets = 0;
	if (m_nRendererResets >= 3) return;

	RMode mode;
	memset(&mode, 0, sizeof(mode));
	if (m_pClientDE->GetRenderMode(&mode) != LT_OK) return;

	Log("Renderer was drawing at %ldx%ld instead of %ldx%ld - resetting it", m_nSurfW, m_nSurfH, m_nRenderW, m_nRenderH);
	m_pClientDE->CPrint("VR: Shogo was drawing at %dx%d instead of %dx%d - resetting the renderer.",
						(int)m_nSurfW, (int)m_nSurfH, (int)m_nRenderW, (int)m_nRenderH);
	m_pClientDE->SetRenderMode(&mode);

	m_nRendererResets++;
	m_fLastRendererReset = fNow;
	m_fRendererBadSince = -1.0f;
}

// WinlatorXR captures the game window, so its inside should be exactly the
// render resolution - that gives a pixel-for-pixel capture and puts the
// HMD_SYNC pixel where the runtime looks for it.  If the window is the wrong
// size (the engine sometimes rebuilds it small after being minimised), nudge it
// back.  Honours VRFixRenderer.  This is the fallback path used when
// VRFullscreen is off; with it on, FitVRWindow makes the window borderless and
// fills the display instead.  The old OpenXR bridge did this with
// PlanWindow/FixGameWindow; this is the portable core of that behaviour.
void CVRStereo::FitGameWindow()
{
	if (!m_pClientDE || !IsActive() || !m_bBridgeAlive || m_vtFixRenderer.GetFloat(1.0f) == 0.0f) return;
	if (!m_nGameHwnd || !IsWindow((HWND)m_nGameHwnd)) return;
	if (m_nRenderW <= 0 || m_nRenderH <= 0) return;

	HWND hwnd = (HWND)m_nGameHwnd;
	if (IsIconic(hwnd)) { m_fWindowBadSince = -1.0f; return; }

	DFLOAT fNow = m_pClientDE->GetTime();

	if (m_nClientW == m_nRenderW && m_nClientH == m_nRenderH)
	{
		if (m_nLoggedWindowW != m_nClientW || m_nLoggedWindowH != m_nClientH)
		{
			Log("Game window is the render size (%ldx%ld)", m_nClientW, m_nClientH);
			m_nLoggedWindowW = m_nClientW; m_nLoggedWindowH = m_nClientH;
		}
		m_fWindowBadSince = -1.0f;
		return;
	}

	// Let the size settle first (it is briefly wrong during start-up).
	if (m_fWindowBadSince < 0.0f) { m_fWindowBadSince = fNow; return; }
	if (fNow - m_fWindowBadSince < 1.5f) return;

	// At most three tries a minute, so we never fight the engine.
	if (fNow - m_fLastWindowFix > 60.0f) m_nWindowFixes = 0;
	if (m_nWindowFixes >= 3) return;

	// Outer size that leaves exactly the render resolution inside, clamped so
	// the window still fits the monitor.
	LONG style = GetWindowLongW(hwnd, GWL_STYLE);
	LONG ex    = GetWindowLongW(hwnd, GWL_EXSTYLE);
	RECT rc    = { 0, 0, m_nRenderW, m_nRenderH };
	AdjustWindowRectEx(&rc, (DWORD)style, FALSE, (DWORD)ex);
	LONG outerW = rc.right - rc.left, outerH = rc.bottom - rc.top;

	MONITORINFO mi = { sizeof(mi) };
	if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
	{
		LONG maxW = mi.rcWork.right - mi.rcWork.left;
		LONG maxH = mi.rcWork.bottom - mi.rcWork.top;
		if (outerW > maxW) outerW = maxW;
		if (outerH > maxH) outerH = maxH;
	}

	RECT wr;
	GetWindowRect(hwnd, &wr);
	Log("Making the game window %ldx%ld (was %ldx%ld) so its inside is %ldx%ld",
		outerW, outerH, wr.right - wr.left, wr.bottom - wr.top, m_nRenderW, m_nRenderH);

	SetWindowPos(hwnd, NULL, 0, 0, outerW, outerH,
				 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);

	m_nWindowFixes++;
	m_fLastWindowFix = fNow;
	m_fWindowBadSince = -1.0f;
}

// WinlatorXR captures the game window, and reads the HMD_SYNC pixel from its
// top-left corner.  The window therefore has to fill the whole display with no
// title bar - otherwise the corner the runtime samples is game chrome, and the
// sync pixel never appears.  This is the same treatment HWXR (Halo CE WXR)
// gives its window: strip the frame and size it to the screen.  Idempotent, so
// it is safe to call every frame; also re-applies the style if the engine has
// rebuilt the window.  Honours VRFullscreen.
void CVRStereo::FitVRWindow()
{
	if (!m_pClientDE || !IsActive() || !m_bBridgeAlive || m_vtVRFullscreen.GetFloat(1.0f) == 0.0f) return;
	if (!m_nGameHwnd || !IsWindow((HWND)m_nGameHwnd)) return;

	HWND hwnd = (HWND)m_nGameHwnd;
	if (IsIconic(hwnd)) return;

	const LONG_PTR kNoFrame = WS_THICKFRAME | WS_BORDER | WS_CAPTION;
	LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	if (style & kNoFrame)
	{
		style &= ~kNoFrame;
		SetWindowLongPtrW(hwnd, GWL_STYLE, style);
		Log("VR window: dropped the title bar");
	}

	int cx = GetSystemMetrics(SM_CXSCREEN);
	int cy = GetSystemMetrics(SM_CYSCREEN);

	RECT rc;
	if (GetWindowRect(hwnd, &rc))
	{
		if (rc.left || rc.top || rc.right - rc.left != cx || rc.bottom - rc.top != cy)
		{
			rc.left = 0; rc.top = 0; rc.right = cx; rc.bottom = cy;
			SetWindowPos(hwnd, HWND_TOP, rc.left, rc.top, cx, cy,
						 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			Log("VR window: filling the display at %dx%d", cx, cy);
			SetForegroundWindow(hwnd);
		}
	}
	else
	{
		SetWindowPos(hwnd, HWND_TOP, 0, 0, cx, cy,
					 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		SetForegroundWindow(hwnd);
	}
}

void CVRStereo::HideAimHelpers()
{
	if (!m_pClientDE) return;
	if (m_hAimMarker) m_pClientDE->SetObjectFlags(m_hAimMarker, 0);
	if (m_hAimDot) m_pClientDE->SetObjectFlags(m_hAimDot, 0);
	if (m_hLaser) m_pClientDE->SetObjectFlags(m_hLaser, 0);
}

// Where the gun points: a laser beam with a dot, just a dot, or the game's
// reticle (VRAimStyle 0 / 1 / 2; 3 = nothing).
void CVRStereo::UpdateAimMarker(HLOCALOBJ hCamera, HLOCALOBJ* pFilterList)
{
	if (!m_pClientDE || !hCamera) return;
	CClientDE* pClientDE = m_pClientDE;

	int nStyle = (int)m_vtAimStyle.GetFloat(2.0f);
	if (m_vtAimMarker.GetFloat(1.0f) == 0.0f) nStyle = 3;		// older on/off setting
	if (!m_bAimByHand || nStyle < 0 || nStyle > 2)
	{
		HideAimHelpers();
		return;
	}

	// Cast from the hand along the gun.
	DVector vCamPos, vHandPos, vHandF, vHandU;
	pClientDE->GetObjectPos(hCamera, &vCamPos);
	GetHandWorld(vCamPos, vHandPos, vHandF, vHandU);
	VEC_NORM(vHandF);

	ClientIntersectQuery query;
	ClientIntersectInfo info;
	VEC_COPY(query.m_From, vHandPos);
	VEC_MULSCALAR(query.m_To, vHandF, VR_AIM_RANGE);
	VEC_ADD(query.m_To, query.m_To, vHandPos);
	VEC_COPY(query.m_Direction, vHandF);
	query.m_Flags		= INTERSECT_OBJECTS | IGNORE_NONSOLID;
	query.m_FilterFn	= pFilterList ? ObjListFilterFn : DNULL;
	query.m_pUserData	= pFilterList;

	DFLOAT fDist = VR_AIM_RANGE;
	if (pClientDE->IntersectSegment(&query, &info))
	{
		DVector vDiff;
		VEC_SUB(vDiff, info.m_Point, vHandPos);
		fDist = VEC_MAG(vDiff);
	}

	// Pull the marker slightly towards you so it doesn't sink into walls.
	DFLOAT fPlace = (fDist > 20.0f) ? fDist - 8.0f : fDist * 0.6f;
	DVector vHit;
	VEC_MULSCALAR(vHit, vHandF, fPlace);
	VEC_ADD(vHit, vHit, vHandPos);

	// Reticle: the same sprite the game uses for its third-person crosshair.
	if (nStyle == 2)
	{
		if (!m_hAimMarker)
		{
			ObjectCreateStruct theStruct;
			INIT_OBJECTCREATESTRUCT(theStruct);
			theStruct.m_ObjectType = OT_SPRITE;
			strncpy(theStruct.m_Filename, "Sprites\\Crosshair.spr", sizeof(theStruct.m_Filename) - 1);
			theStruct.m_Flags = FLAG_VISIBLE | FLAG_GLOWSPRITE | FLAG_NOLIGHT;
			m_hAimMarker = pClientDE->CreateObject(&theStruct);
			if (!m_hAimMarker) return;
			DVector vScale;
			VEC_SET(vScale, 0.5f, 0.5f, 1.0f);
			pClientDE->SetObjectScale(m_hAimMarker, &vScale);
		}
		pClientDE->SetObjectPos(m_hAimMarker, &vHit);
		pClientDE->SetObjectFlags(m_hAimMarker, FLAG_VISIBLE | FLAG_GLOWSPRITE | FLAG_NOLIGHT);
	}
	else if (m_hAimMarker) pClientDE->SetObjectFlags(m_hAimMarker, 0);

	// Dot (laser and dot styles): a red dot that stays the same size on screen.
	// Not a "glow" sprite - those shrink more slowly with distance, which made
	// the dot grow the further away it was.
	if (nStyle == 0 || nStyle == 1)
	{
		if (!m_hAimDot)
		{
			ObjectCreateStruct theStruct;
			INIT_OBJECTCREATESTRUCT(theStruct);
			theStruct.m_ObjectType = OT_SPRITE;
			strncpy(theStruct.m_Filename, "Sprites\\glow.spr", sizeof(theStruct.m_Filename) - 1);
			theStruct.m_Flags = FLAG_VISIBLE | FLAG_NOLIGHT;
			m_hAimDot = pClientDE->CreateObject(&theStruct);
			if (!m_hAimDot) return;
		}
		DFLOAT fSize = m_vtAimDotSize.GetFloat(1.0f);
		if (fSize < 0.1f) fSize = 0.1f;
		DFLOAT fScale = fSize * 0.0004f * fPlace;		// grows with distance = same size on screen
		if (fScale < 0.01f * fSize) fScale = 0.01f * fSize;
		DVector vScale;
		VEC_SET(vScale, fScale, fScale, 1.0f);
		pClientDE->SetObjectScale(m_hAimDot, &vScale);
		pClientDE->SetObjectColor(m_hAimDot, 1.0f, 0.12f, 0.08f, 1.0f);
		pClientDE->SetObjectPos(m_hAimDot, &vHit);
		pClientDE->SetObjectFlags(m_hAimDot, FLAG_VISIBLE | FLAG_NOLIGHT);
	}
	else if (m_hAimDot) pClientDE->SetObjectFlags(m_hAimDot, 0);

	// Laser: a thin red beam from just ahead of the gun to the dot.
	if (nStyle == 0)
	{
		if (!m_hLaser)
		{
			ObjectCreateStruct theStruct;
			INIT_OBJECTCREATESTRUCT(theStruct);
			theStruct.m_ObjectType = OT_LINESYSTEM;
			theStruct.m_Flags = FLAG_VISIBLE;
			VEC_COPY(theStruct.m_Pos, vHandPos);
			m_hLaser = pClientDE->CreateObject(&theStruct);
			if (!m_hLaser) return;
		}

		DVector vStart, vT, vR;
		VEC_MULSCALAR(vT, vHandF, 4.0f * m_fWorldScaleUsed / 40.0f);		// a hand's width ahead (about 10 cm)
		VEC_ADD(vStart, vHandPos, vT);
		VEC_CROSS(vR, vHandU, vHandF);
		VEC_NORM(vR);

		// Five lines from slightly spread starts to one end: reads as a beam
		// rather than a single pixel line.
		DFLOAT fSpread = 0.003f * m_fWorldScaleUsed;
		DVector vOffsets[5];
		VEC_INIT(vOffsets[0]);
		VEC_MULSCALAR(vOffsets[1], vR, fSpread);
		VEC_MULSCALAR(vOffsets[2], vR, -fSpread);
		VEC_MULSCALAR(vOffsets[3], vHandU, fSpread);
		VEC_MULSCALAR(vOffsets[4], vHandU, -fSpread);

		DRotation rIdentity;
		ROT_INIT(rIdentity);
		pClientDE->SetObjectPos(m_hLaser, &vStart);
		pClientDE->SetObjectRotation(m_hLaser, &rIdentity);

		// The lines are replaced every frame rather than moved: the engine works
		// out where a line system is visible when its lines are added, so moved
		// lines can end up never being drawn.
		int k;
		for (k = 0; k < 5; k++)
		{
			if (m_hLaserLines[k]) pClientDE->RemoveLine(m_hLaser, m_hLaserLines[k]);
			m_hLaserLines[k] = DNULL;
		}
		for (k = 0; k < 5; k++)
		{
			DELine line;
			VEC_COPY(line.m_Points[0].m_Pos, vOffsets[k]);
			VEC_SUB(line.m_Points[1].m_Pos, vHit, vStart);
			line.m_Points[0].r = 1.0f;	line.m_Points[0].g = 0.1f;	line.m_Points[0].b = 0.08f;	line.m_Points[0].a = k ? 0.45f : 0.9f;
			line.m_Points[1].r = 1.0f;	line.m_Points[1].g = 0.1f;	line.m_Points[1].b = 0.08f;	line.m_Points[1].a = k ? 0.25f : 0.6f;
			m_hLaserLines[k] = pClientDE->AddLine(m_hLaser, &line);
		}
		pClientDE->SetObjectFlags(m_hLaser, FLAG_VISIBLE);
	}
	else if (m_hLaser) pClientDE->SetObjectFlags(m_hLaser, 0);
}

void CVRStereo::OnExitWorld()
{
	if (m_pClientDE && m_hAimMarker)
	{
		m_pClientDE->DeleteObject(m_hAimMarker);
	}
	m_hAimMarker = DNULL;
	if (m_pClientDE && m_hAimDot) m_pClientDE->DeleteObject(m_hAimDot);
	if (m_pClientDE && m_hLaser) m_pClientDE->DeleteObject(m_hLaser);
	m_hAimDot = DNULL;
	m_hLaser = DNULL;
	{ int k; for (k = 0; k < 5; k++) m_hLaserLines[k] = DNULL; }
	if (m_pClientDE && m_hBody)
	{
		m_pClientDE->DeleteObject(m_hBody);
	}
	m_hBody = DNULL;
	m_nBodyModelId = -1;
	if (m_pClientDE && m_hHeldGun)
	{
		m_pClientDE->DeleteObject(m_hHeldGun);
	}
	m_hHeldGun = DNULL;
	m_nHeldGunId = -1;
	if (m_pClientDE && m_hOffHandGun)
	{
		m_pClientDE->DeleteObject(m_hOffHandGun);
	}
	m_hOffHandGun = DNULL;
	m_nOffHandGunId = -1;
	m_hSplitPV = DNULL;				// the model it pointed at is going away with the world
	m_bPistolSplit = DFALSE;
	m_hHiddenPV = DNULL;
	m_bHaveBody = DFALSE;
}


// ======================================================================= //
//	Two-handed aiming
// ======================================================================= //

// Hold your other hand out in front of the gun hand, roughly along the
// barrel, and the gun points from your gun hand towards it - steadier, and
// how you'd hold a rifle.  Lets go when the hands come apart or off-line.
void CVRStereo::UpdateTwoHanded()
{
	DBOOL bWas = m_bTwoHanded;
	m_bTwoHanded = DFALSE;
	VEC_COPY(m_vAimF, m_vRightF);
	VEC_COPY(m_vAimU, m_vRightU);

	// Dual pistols (#5): two-handed aim means bracing one gun with the other
	// hand, which is exactly what you are not doing with a pistol in each
	// hand.  Instead the aim eases across to whichever pistol last fired, so
	// that the shot, the crosshair and the view agree about which gun is
	// doing the shooting.  With VRPistolAimSnap at 0 the aim stays on the gun
	// hand and never swings.
	if (m_bPistolSplit)
	{
		DFLOAT fRate = m_vtPistolAimSnap.GetFloat(0.0f);
		if (fRate <= 0.0f) return;					// m_vAimF is already the gun hand

		// The pistol the aim should be pointing down: whichever fired last,
		// falling back to the gun hand before the first shot.
		const DVector& vWant = (m_nPistolLastHand == 1) ? m_vLeftF : m_vRightF;

		if (!m_bPistolAimValid)
		{
			VEC_COPY(m_vPistolAimF, vWant);
			m_bPistolAimValid = DTRUE;
		}
		else
		{
			// Exponential ease, framerate independent.
			DFLOAT fDt = m_pClientDE ? m_pClientDE->GetFrameTime() : 0.016666f;
			if (fDt < 0.0f || fDt > 0.25f) fDt = 0.016666f;
			DFLOAT fT = fRate * fDt;
			if (fT > 1.0f) fT = 1.0f;

			DVector vDelta;
			VEC_SUB(vDelta, vWant, m_vPistolAimF);
			VEC_MULSCALAR(vDelta, vDelta, fT);
			VEC_ADD(m_vPistolAimF, m_vPistolAimF, vDelta);
			// Braces matter: the VEC_ macros expand to a block, so an
			// unbraced if/else around one will not compile.
			DFLOAT fMag = VEC_MAG(m_vPistolAimF);
			if (fMag > 0.0001f)	{ VEC_MULSCALAR(m_vPistolAimF, m_vPistolAimF, 1.0f / fMag); }
			else				{ VEC_COPY(m_vPistolAimF, vWant); }
		}

		VEC_COPY(m_vAimF, m_vPistolAimF);
		return;
	}
	m_bPistolAimValid = DFALSE;

	// #8: with the grip deliberately taken, the hands steer the gun whatever
	// the thresholds below would have said - the player has told us they are
	// holding it, so it must not slip out of two-handed aim just because they
	// moved their hands a little.
	DBOOL bHeldGrip = m_bTwoHandGrip;

	// The submachine gun is held one-handed on the owner's instruction, so it
	// never aims from two hands at all - not by grip, not by proximity.
	if (m_nCurrentWeaponId == GUN_MAC10_ID) return;

	// The off hand is only on the gun once the grip has been taken.  While it
	// is away on its own controller - #7's free hand, or a two-handed weapon
	// waiting for the player to take hold - the line between the hands means
	// nothing, so V1.0's proximity aim must not steer off it: the gun would
	// swing towards a hand that is visibly not holding it, and #8's trigger
	// toggle would be pointless, proximity having already done the job.
	if (!bHeldGrip && m_bOffArmSplit) return;

	if (!bHeldGrip && m_vtTwoHand.GetFloat(1.0f) == 0.0f) return;
	if (!(m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE) || !(m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE)) return;

	DVector vD;
	VEC_SUB(vD, m_vLeftP, m_vRightP);
	DFLOAT fDist = VEC_MAG(vD);
	if (fDist < 0.0001f) return;
	if (!bHeldGrip && (fDist < 0.12f || fDist > 0.9f)) return;		// metres

	DVector vDir;
	VEC_MULSCALAR(vDir, vD, 1.0f / fDist);
	DFLOAT fCos = VEC_DOT(vDir, m_vRightF);
	DFLOAT fNeed = bWas ? 0.766f : 0.906f;			// enter within 25 degrees, stay within 40
	if (!bHeldGrip && fCos < fNeed) return;

	m_bTwoHanded = DTRUE;
	VEC_COPY(m_vAimF, vDir);

	// Keep the gun's roll from the gun hand.
	DVector vT;
	DFLOAT fUp = VEC_DOT(vDir, m_vRightU);
	VEC_MULSCALAR(vT, vDir, fUp);
	VEC_SUB(m_vAimU, m_vRightU, vT);
	DFLOAT fLen = VEC_MAG(m_vAimU);
	if (fLen > 0.001f)
	{
		VEC_MULSCALAR(m_vAimU, m_vAimU, 1.0f / fLen);
	}
	else
	{
		VEC_COPY(m_vAimU, m_vRightU);
	}
}

// ======================================================================= //
//	Body
// ======================================================================= //

// Hides the parts of a model whose names say head (so you don't see the
// inside of your own head) and, optionally, arms (the engine can't bend
// them to follow your controllers).  Returns how many nodes are hidden.
int CVRStereo::HideModelNodes(HLOCALOBJ hObj, DBOOL bHead, DBOOL bArms)
{
	if (!m_pClientDE || !hObj) return 0;

	static const char* s_head[] = { "head", "neck", "helm", "face", "hair", "eye", "jaw", "mouth", "skull", "visor", 0 };
	static const char* s_arms[] = { "arm", "hand", "finger", "thumb", "elbow", "wrist", "shoulder", "bicep", "clav",
									"palm", "gun", "weapon", "rifle", 0 };
	int nHidden = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64], szLower[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		int i;
		for (i = 0; szName[i] && i < 63; i++) szLower[i] = (char)tolower((unsigned char)szName[i]);
		szLower[i] = 0;

		DBOOL bIsHead = DFALSE, bIsArm = DFALSE;
		for (i = 0; s_head[i]; i++) if (strstr(szLower, s_head[i])) bIsHead = DTRUE;
		for (i = 0; s_arms[i]; i++) if (strstr(szLower, s_arms[i])) bIsArm = DTRUE;

		DBOOL bHide = (bHead && bIsHead) || (bArms && bIsArm);
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
		if (bHide) nHidden++;
	}
	return nHidden;
}

void CVRStereo::SetAllNodesHidden(HLOCALOBJ hObj, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
	}
}

// Hides (or shows) the arm and hand parts of a first-person weapon model -
// by part name, so it works for every weapon whose arms are separate parts.
int CVRStereo::SetArmNodesHidden(HLOCALOBJ hObj, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return 0;
	static const char* s_arms[] = { "arm", "hand", "finger", "thumb", "elbow", "wrist", "shoulder", "bicep", "clav",
									"palm", "glove", "sleeve", "knuckle", 0 };
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64], szLower[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		int i;
		for (i = 0; szName[i] && i < 63; i++) szLower[i] = (char)tolower((unsigned char)szName[i]);
		szLower[i] = 0;
		// Monolith's first-person models name the hand parts R_Arm, Right_Palm,
		// R_pointer_base, R_pinky_tip... (see ShogoVR_models.txt), while the
		// gun's parts have names like cyl8 or extru6.
		DBOOL bArm = (strncmp(szLower, "r_", 2) == 0 || strncmp(szLower, "l_", 2) == 0 ||
					  strncmp(szLower, "r-", 2) == 0 || strncmp(szLower, "l-", 2) == 0 ||
					  strncmp(szLower, "right_", 6) == 0 || strncmp(szLower, "left_", 5) == 0);
		for (i = 0; !bArm && s_arms[i]; i++) if (strstr(szLower, s_arms[i])) bArm = DTRUE;
		if (bArm)
		{
			m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
			nCount++;
		}
	}
	return nCount;
}

// Dual pistols (#5) -------------------------------------------------------
//
// The Colt 45's first-person model (Models\PV_Weapons\Colt45_pv.abc) is a
// SINGLE object holding BOTH pistols and BOTH of Sanjuro's hands, as two
// mirrored halves - see the "Weapon 13" lines in ShogoVR_models.txt, where the
// second half repeats the first with a "3" appended or a different number.
// On a flat screen that is fine, the pair sits in the corner together; in VR
// each pistol belongs in its own hand.  Making a second copy of the model
// would show FOUR pistols, so instead the halves are split by part name: half
// A stays on the game's own model (which AdjustWeaponPose already puts in the
// gun hand) and half B is shown on one copy of the same model, posed to the
// off hand.  The art is the game's own and is not modified.
//
// Both halves are listed by name on purpose.  An "ends in 3" rule would miss
// the second pistol's own parts - cube4, bool6, cyl4 and extru4 - and hiding
// the wrong set leaves half a pistol floating in each hand.  Only geometry is
// listed: each half's nulls (null1/null2/null8/null9 and the bare "1"/"4")
// draw nothing and may parent each other, so they are left alone.
static const char* const s_PistolHalfA[] =
{
	"palm",			"pointer_base",		"pointer_middle",	"pointer_tip",
	"pinky_base",	"pinky_middle",		"pinky_tip",
	"ring_base",	"ring_middle",		"ring_tip",
	"middle_base",	"middle_middle",	"middle_tip",
	"thumb_base",	"thumb_middle",		"thumb_tip",
	"cube1",		"bool3",			"cyl1",				"extru1",	0
};
static const char* const s_PistolHalfB[] =
{
	"palm3",		"pointer_base3",	"pointer_middle3",	"pointer_tip3",
	"pinky_base3",	"pinky_middle3",	"pinky_tip3",
	"ring_base3",	"ring_middle3",		"ring_tip3",
	"middle_base3",	"middle_middle3",	"middle_tip3",
	"thumb_base3",	"thumb_middle3",	"thumb_tip3",
	"cube4",		"bool6",			"cyl4",				"extru4",	0
};

// How many parts each half must contribute before the split is trusted.  A
// replacement Colt45_pv.abc from some other mod will not carry these names,
// and showing somebody half a pistol is worse than showing them the stock
// pair - so art that does not match falls back to the game's own behaviour.
#define VR_PISTOL_HALF_MIN	12

// 0 = the gun-hand half, 1 = the off-hand half, -1 = part of neither.
static int PistolHalfIndex(const char* szName)
{
	int i;
	for (i = 0; s_PistolHalfA[i]; i++) if (stricmp(szName, s_PistolHalfA[i]) == 0) return 0;
	for (i = 0; s_PistolHalfB[i]; i++) if (stricmp(szName, s_PistolHalfB[i]) == 0) return 1;
	return -1;
}

int CVRStereo::SetHalfNodesHidden(HLOCALOBJ hObj, int nHalf, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (PistolHalfIndex(szName) != nHalf) continue;
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
		nCount++;
	}
	return nCount;
}

int CVRStereo::CountHalfNodes(HLOCALOBJ hObj, int nHalf)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (PistolHalfIndex(szName) == nHalf) nCount++;
	}
	return nCount;
}



// ======================================================================= //
//	Free off arm (#7)
// ======================================================================= //

// The submachine gun's first-person model (weapon 18) is 50 parts in three
// blocks: the arm holding the gun, the gun, and a second arm that holds
// nothing and simply goes wherever the animation puts it.  That second arm is
// the one this feature moves onto the off controller.  The gun block is never
// touched.
//
// Taken verbatim from a device dump (Docs/GameLogs/2026-10-28/ShogoVR_models.txt),
// and the names have three traps in them:
//
//  - "R-thumb_middle" uses a HYPHEN where every sibling uses an underscore.
//    It is an art typo and it is in both arms.  Do not "correct" it.
//  - The second arm mixes suffixes: R_Arm3, but Right_Palm4 and 4 on every
//    finger.  A single-suffix rule finds the fingers and loses the arm, which
//    would leave a severed hand in the air.
//  - BOTH arms are named "R_"/"Right_" - the art reuses the right-hand naming
//    for the left hand - so which one is free cannot be read from the names.
//    That is what VROffHandSwap is for.
static const char* const s_OffArmA[] =
{
	"R_Arm",			"Right_Palm",
	"R_pointer_base",	"R_pointer_middle",		"R_pointer_tip",
	"R_pinky_base",		"R_pinky_middle",		"R_pinky_tip",
	"R_ring_base",		"R_ring_middle",		"R_ring_tip",
	"R_middle_base",	"R_middle_middle",		"R_middle_tip",
	"R_thumb_base",		"R-thumb_middle",		"R_thumb_tip",	0
};
static const char* const s_OffArmB[] =
{
	"R_Arm3",			"Right_Palm4",
	"R_pointer_base4",	"R_pointer_middle4",	"R_pointer_tip4",
	"R_pinky_base4",	"R_pinky_middle4",		"R_pinky_tip4",
	"R_ring_base4",		"R_ring_middle4",		"R_ring_tip4",
	"R_middle_base4",	"R_middle_middle4",		"R_middle_tip4",
	"R_thumb_base4",	"R-thumb_middle4",		"R_thumb_tip4",	0
};

// The palm each group hangs its hand on, for anchoring to the controller, and
// the forearm that runs back from it (see VRHandArm).
#define VR_OFFARM_PALM(g)	(((g) == 0) ? "Right_Palm" : "Right_Palm4")
#define VR_OFFARM_ARM(g)	(((g) == 0) ? "R_Arm" : "R_Arm3")

// How many parts a group must contribute before the split is trusted, so a
// replacement model that does not carry these names keeps stock behaviour
// rather than losing an arm.
#define VR_OFFARM_MIN	12

// 0 = the first arm group, 1 = the second, -1 = part of neither (the gun).
static int OffArmIndex(const char* szName)
{
	int i;
	for (i = 0; s_OffArmA[i]; i++) if (stricmp(szName, s_OffArmA[i]) == 0) return 0;
	for (i = 0; s_OffArmB[i]; i++) if (stricmp(szName, s_OffArmB[i]) == 0) return 1;
	return -1;
}

// ======================================================================= //
//	Two-handed grip (#8)
// ======================================================================= //

// The hand groups of the two-handed on-foot weapons, and ONLY the hands - no
// gun parts.  That matters: weapons 15, 16, 17 and 19 have gun parts called
// cube1, cyl1 and extru1, which are also Colt 45 PISTOL parts, so the #5 tables
// cannot be reused here without classifying half a rifle as a hand.
//
// Two naming conventions appear across the on-foot weapons and both are
// listed: 15, 16, 17 and 19 use palm/palm3, while 14 and 18 use the
// Right_Palm/R_Arm form already tabulated for #7 (hyphen typo and mixed
// suffixes included).  Which group is the off hand cannot be read from either -
// every hand in this art is named "R_"/"Right_" or carries no side at all - so
// group 1 is assumed and VROffHandSwap moves it.
static const char* const s_HandA[] =
{
	"palm",			"pointer_base",		"pointer_middle",	"pointer_tip",
	"pinky_base",	"pinky_middle",		"pinky_tip",
	"ring_base",	"ring_middle",		"ring_tip",
	"middle_base",	"middle_middle",	"middle_tip",
	"thumb_base",	"thumb_middle",		"thumb_tip",	0
};
static const char* const s_HandB[] =
{
	"palm3",		"pointer_base3",	"pointer_middle3",	"pointer_tip3",
	"pinky_base3",	"pinky_middle3",	"pinky_tip3",
	"ring_base3",	"ring_middle3",		"ring_tip3",
	"middle_base3",	"middle_middle3",	"middle_tip3",
	"thumb_base3",	"thumb_middle3",	"thumb_tip3",	0
};

// The forearms of the assault-rifle family (15, 16, 17 and the TOW).  The
// hand tables above stop at the wrist, so moving a hand to the off controller
// used to leave its forearm behind, floating on the gun with nothing attached
// - which is what the owner saw on the rifle, both grenade launchers and the
// TOW.  The submachine-gun family needs none of this: its arms are named
// R_Arm/R_Arm3 and the hand tables already carry them.
//
// Hidden as one set rather than per hand, because the pairing is not stable:
// 15/16/17 put tetra5 with the first hand and tetra4 with the second, and the
// TOW has them the other way round.  tetra1 and cube1_3 always belong to the
// first, tetra3 and cube1_4 to the second, but since the mod hides forearms by
// default anyway (VRHandArm), hiding all six together is both simpler and what
// the one-handed pose wants.  Taking the grip stands the whole split down, so
// the model draws its own arms again and they come back.
static const char* const s_RifleArm[] =
{
	"tetra1", "tetra3", "tetra4", "tetra5", "cube1_3", "cube1_4", 0
};

static DBOOL IsRifleArmNode(const char* szName)
{
	int i;
	for (i = 0; s_RifleArm[i]; i++) if (stricmp(szName, s_RifleArm[i]) == 0) return DTRUE;
	return DFALSE;
}

// 0 = first hand group, 1 = second, -1 = not a hand.  Covers both conventions.
static int HandGroupIndex(const char* szName)
{
	int i;
	for (i = 0; s_HandA[i];   i++) if (stricmp(szName, s_HandA[i])   == 0) return 0;
	for (i = 0; s_HandB[i];   i++) if (stricmp(szName, s_HandB[i])   == 0) return 1;
	for (i = 0; s_OffArmA[i]; i++) if (stricmp(szName, s_OffArmA[i]) == 0) return 0;
	for (i = 0; s_OffArmB[i]; i++) if (stricmp(szName, s_OffArmB[i]) == 0) return 1;
	return -1;
}

int CVRStereo::SetRifleArmsHidden(HLOCALOBJ hObj, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (!IsRifleArmNode(szName)) continue;
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
		nCount++;
	}
	return nCount;
}

int CVRStereo::SetHandGroupHidden(HLOCALOBJ hObj, int nGroup, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (HandGroupIndex(szName) != nGroup) continue;
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
		nCount++;
	}
	return nCount;
}

int CVRStereo::CountHandGroupNodes(HLOCALOBJ hObj, int nGroup)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (HandGroupIndex(szName) == nGroup) nCount++;
	}
	return nCount;
}

// Anchor on whichever palm this model actually has: a model carries one naming
// convention or the other, never both.
DBOOL CVRStereo::GetHandGroupDrift(HLOCALOBJ hObj, int nGroup, DVector &vDrift)
{
	if (GetNodeDrift(hObj, (nGroup == 0) ? "palm" : "palm3", vDrift)) return DTRUE;
	return GetNodeDrift(hObj, VR_OFFARM_PALM(nGroup), vDrift);
}

// The weapons the player may take hold of in both hands.  Two exclusions, and
// they are different in kind:
//
//  - The Colt 45 is two separate guns, one per hand (#5), never one gun in two
//    hands, so the off trigger goes on firing the off pistol as before.
//  - The submachine gun is held one-handed on the owner's instruction, even
//    though the art has a second hand for it.  Its off hand is #7's free hand
//    and stays free; see HasSplitOffHand below, which is why these are two
//    lists and not one.
//
// Mech weapons are out of scope - the various mechs hold them differently.
static DBOOL IsTwoHandWeapon(int nWeaponId)
{
	return (DBOOL)(nWeaponId == GUN_SHOTGUN_ID		||
				   nWeaponId == GUN_ASSAULTRIFLE_ID	||
				   nWeaponId == GUN_ENERGYGRENADE_ID ||
				   nWeaponId == GUN_KATOGRENADE_ID	||
				   nWeaponId == GUN_TOW_ID);
}

// The weapons whose redundant hand is taken off the gun and given to the off
// controller.  Every grip-capable weapon, plus the submachine gun, which gets
// the free hand (#7) but never the grip (#8).
static DBOOL HasSplitOffHand(int nWeaponId)
{
	return (DBOOL)(IsTwoHandWeapon(nWeaponId) || nWeaponId == GUN_MAC10_ID);
}

// Which of the two hand groups is further along the barrel?
//
// This replaces guessing per weapon.  The part names cannot say: the device
// log shows the assault rifle family carrying palm/palm3 and the submachine
// gun carrying Right_Palm/Right_Palm4, and the two families order their hands
// OPPOSITELY - keeping group A holds the SMG correctly but leaves the rifle
// gripped by its fore-end with nothing on the trigger.
//
// So measure it instead.  Both palms are read off the posed model and
// projected onto the gun's own forward axis; the one further forward is the
// fore hand, which is the one the off hand should take.  Whatever is left is
// the trigger hand and belongs on the gun.  That rule needs no table and
// holds for any model, including ones this mod has never seen.
//
// Returns -1 when it cannot tell - a weapon with only one hand group (the
// shotgun), or two that sit level - and the caller keeps VROffHandSwap.
int CVRStereo::ForwardHandGroup(HLOCALOBJ hObj)
{
	if (!m_pClientDE || !hObj) return -1;

	DVector vA, vB;
	if (!GetNodeDrift(hObj, "palm",  vA) && !GetNodeDrift(hObj, "Right_Palm",  vA)) return -1;
	if (!GetNodeDrift(hObj, "palm3", vB) && !GetNodeDrift(hObj, "Right_Palm4", vB)) return -1;

	DRotation rRot;
	DVector vU, vR, vF;
	m_pClientDE->GetObjectRotation(hObj, &rRot);
	m_pClientDE->GetRotationVectors(&rRot, &vU, &vR, &vF);

	DFLOAT fA = VEC_DOT(vA, vF);
	DFLOAT fB = VEC_DOT(vB, vF);
	if ((DFLOAT)fabs(fA - fB) < 0.5f) return -1;		// level enough to be a coin toss
	return (fA > fB) ? 0 : 1;
}

// May the player take hold right now?  The off hand has to be out along the
// barrel - the same geometry the old proximity two-handed aim used.
DBOOL CVRStereo::CanEngageGrip()
{
	if (!(m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE)) return DFALSE;
	if (!(m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE))  return DFALSE;

	DVector vD;
	VEC_SUB(vD, m_vLeftP, m_vRightP);
	DFLOAT fDist = VEC_MAG(vD);
	if (fDist < 0.12f) return DFALSE;					// hands together is not a grip
	if (fDist > m_vtGripDist.GetFloat(0.9f)) return DFALSE;

	DVector vDir;
	VEC_MULSCALAR(vDir, vD, 1.0f / fDist);
	DFLOAT fNeed = (DFLOAT)cos(VR_DEG2RAD(m_vtGripAngle.GetFloat(25.0f)));
	return (DBOOL)(VEC_DOT(vDir, m_vRightF) >= fNeed);
}

// The off trigger, pressed while a two-handed weapon is in hand and the player
// is alive.  Letting go never needs the gesture - only taking hold does - so a
// grip can always be dropped however the hands happen to be held.
void CVRStereo::ToggleTwoHandGrip()
{
	if (m_vtTwoHandWeapons.GetFloat(1.0f) == 0.0f) return;
	if (m_vtTwoHandGrip.GetFloat(1.0f) == 0.0f) return;
	if (!IsTwoHandWeapon(m_nCurrentWeaponId)) return;

	if (m_bTwoHandGrip)
	{
		m_bTwoHandGrip = DFALSE;
		Log("VR grip: let go - one hand");
		return;
	}
	if (CanEngageGrip())
	{
		m_bTwoHandGrip = DTRUE;
		Log("VR grip: took hold - two hands");
	}
}

int CVRStereo::SetArmGroupHidden(HLOCALOBJ hObj, int nGroup, DBOOL bHide)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (OffArmIndex(szName) != nGroup) continue;
		m_pClientDE->SetModelNodeHideStatus(hObj, szName, bHide);
		nCount++;
	}
	return nCount;
}

int CVRStereo::CountArmGroupNodes(HLOCALOBJ hObj, int nGroup)
{
	if (!m_pClientDE || !hObj) return 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		if (OffArmIndex(szName) == nGroup) nCount++;
	}
	return nCount;
}

// The off arm is only free while the gun is idle or firing.  Reloading drives
// that same hand to the magazine well to seat the clip, and select/deselect
// swing the whole model - holding the hand on the controller through any of
// those would tear the animation apart, so it is handed back for the duration.
//
// CWeaponModel looks its animations up by name, so the mod can do the same and
// needs no engine change.  The free list is resolved once per model; anything
// NOT on it counts as busy, so an animation we have not accounted for degrades
// to stock behaviour rather than to an arm torn out of it.
DBOOL CVRStereo::OffArmHandIsFree(HLOCALOBJ hPVModel)
{
	if (!m_pClientDE || !hPVModel) return DFALSE;

	if (m_hOffArmAnimFor != hPVModel)
	{
		m_hOffArmAnimFor   = hPVModel;
		m_nOffArmFreeCount = 0;

		static const char* const s_szFree[] =
		{ "Idle_1", "Idle_2", "Fire", "Fire2", "Start_fire", "End_fire", "Fire_zoom", 0 };

		int i;
		for (i = 0; s_szFree[i] && m_nOffArmFreeCount < 8; i++)
		{
			HMODELANIM hAnim = m_pClientDE->GetAnimIndex(hPVModel, (char*)s_szFree[i]);
			if (hAnim != (HMODELANIM)-1) m_nOffArmFreeAnim[m_nOffArmFreeCount++] = (DDWORD)hAnim;
		}
		Log("VR off arm: %d of the gun's animations leave the arm free", m_nOffArmFreeCount);
	}

	if (m_nOffArmFreeCount <= 0) return DFALSE;

	DDWORD nAnim = m_pClientDE->GetModelAnimation(hPVModel);
	int i;
	for (i = 0; i < m_nOffArmFreeCount; i++) if (m_nOffArmFreeAnim[i] == nAnim) return DTRUE;

	// Not a free animation - but GetModelAnimation reports which animation the
	// model is ON, not whether it is still playing, and nothing resets it when a
	// one-shot finishes.  After a reload the index therefore sits on "Reload"
	// until the game next sets another one, which it does when the player fires:
	// that is why the arm stayed attached after reloading until a shot was taken.
	// A non-looping animation that has finished is no longer using the hand, so
	// take it back as soon as the model says it is done.
	if (m_pClientDE->GetModelPlaybackState(hPVModel) & MS_PLAYDONE) return DTRUE;
	return DFALSE;
}


// Some weapons are drawn with only one hand, so the other one is simply not
// there in VR: the shotgun (14) draws its FORE hand on the pump and never the
// hand on the rear grip, while the knife (22) and the squeaky toy (21) draw
// only the hand holding them.  On a flat screen nobody notices; in the headset
// a controller with nothing on it is very obvious.
//
// So lend them one, borrowed from the submachine gun's model, which carries
// both a hand closed round a grip and an open one.  The borrowed hand is posed
// to its controller exactly as the weapon itself is - same origin, same scale -
// so the two move together, and the weapon's own art is never touched.
//
// Borrowed from a DIFFERENT model, so there is no animation of the equipped
// weapon to follow: it is pinned to one looping idle at creation and left
// there.  (A copy left to free-run tumbles - see #5.)
// Where the shotgun's lent hand sits on the weapon.
//
// This was tuned twice and transferred neither time, and the reason is the
// frame it was measured in.  The offset used to be applied along the HAND's
// axes, which rotate with the player's wrist - so a set of numbers found with
// the desktop tester's synthetic poses landed somewhere else entirely once a
// real wrist held the gun at a real angle.  The device screenshots show the
// hand thrown clear of the weapon, which is exactly what an eight-unit offset
// does when the frame it is measured in turns.
//
// That also explains the "impossible" asymmetry in the earlier numbers: Y
// disagreed between the handedness modes by 1.4 because the two modes hold the
// wrist differently, not because anything was wrong with the seating.
//
// So the offset is applied along the GUN's axes now (PoseLentHandOnGun), where
// it is a property of the model and nothing else.  A hand on a grip does not
// move when the wrist turns - the whole weapon turns with it - so a value
// found once holds in both handedness modes, on desktop and on device alike.
//
// It is NOT zero, though, and the controller screenshots say why.  The
// controller sits slightly forward of the shotgun's rear grip and slightly
// above it - which is simply how a player holds a controller whose own grip
// points down and back from the tracked point.  So the lent hand needs a
// short step back along the barrel and a short step down to land on the
// weapon's handle.  Two numbers, in the gun's frame, shared by both modes.
//
// These are those two numbers, confirmed on device and on desktop in both
// handedness modes with no trim on top - which is the evidence that the gun's
// frame was the right one to measure in.  Three rounds of tuning in the
// hand's frame never produced a pair that held anywhere but where it was
// found.
void CVRStereo::DomHandSeat(DFLOAT &fX, DFLOAT &fY, DFLOAT &fZ)
{
	// One value for both hands, and the device logs are why.
	//
	// The grip is a fixed point on the model, so in the gun's frame it can only
	// have one address - unless the gun's frame itself flips with handedness,
	// which was the obvious suspect.  It does not.  Two bases logged on device
	// minutes apart, one mode each, both have R pointing the same way:
	//
	//   left-handed    R  0.971  0.173 -0.166
	//   right-handed   R  0.973 -0.210  0.090
	//
	// So the two tuned values, 19.65 units apart along gun-X, put the hand on
	// OPPOSITE SIDES of the weapon. They cannot both be the grip, and the
	// right-handed one is the verified one.
	//
	// The left-handed value was measured while the lent hand was still the
	// wrong-handed mesh - a right hand on a left wrist, thumb on the wrong side
	// - so it was being dragged around to fix an appearance that no position
	// could fix. The donor fix cured that; this undoes the damage it caused.
	fX = 0.0f;		// on the weapon's centreline
	fY = -2.0f;		// down from the controller, onto the handle
	fZ = -3.0f;		// and back along the barrel, off the receiver
}

// Seats a lent hand on the weapon: position from the player's CONTROLLER,
// orientation from the GUN.
//
// The controller overlay settled this. Shown next to the weapon on device, the
// dominant controller sits on the shotgun's receiver - the mod poses the gun to
// the hand, so the grip is already where the player's hand is, by construction.
// The lent hand therefore belongs at the controller and needs no offset at all;
// every value tuned for it was pushing it OFF a grip it would otherwise have
// landed on, which is why the hand kept appearing beside the weapon.
//
// That is the whole of this bug. Three rounds of tuning produced ever-larger
// offsets because each one was correcting for the previous frame choice rather
// than for anything real, and a large offset is exactly what turns a small
// pose difference into a hand floating in mid-air.
//
// Orientation still comes from the gun, which was the good half of the earlier
// attempt: a hand gripping a weapon turns with the weapon, not with the wrist.
// The offsets remain as a trim in the gun's frame and default to zero.
void CVRStereo::PoseLentHandOnGun(HLOCALOBJ hObj, int nGroup, HLOCALOBJ hGun,
								  const DVector &vHandPos,
								  DFLOAT fX, DFLOAT fY, DFLOAT fZ, DFLOAT fRoll)
{
	CClientDE* pClientDE = m_pClientDE;
	if (!pClientDE || !hObj || !hGun) return;

	SetAllNodesHidden(hObj, DTRUE);
	SetHandGroupHidden(hObj, nGroup, DFALSE);

	DRotation rGun;
	DVector gU, gR, gF;
	pClientDE->GetObjectRotation(hGun, &rGun);
	pClientDE->GetRotationVectors(&rGun, &gU, &gR, &gF);

	DRotation rRot = rGun;
	if (fRoll != 0.0f) pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));

	DFLOAT fScale = m_fGunScaleUsed;
	if (fScale < 0.2f) fScale = 0.2f;
	if (fScale > 5.0f) fScale = 5.0f;

	DVector vPos, vT;
	VEC_COPY(vPos, vHandPos);
	VEC_MULSCALAR(vT, gR, fX * fScale);		VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, gU, fY * fScale);		VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, gF, fZ * fScale);		VEC_ADD(vPos, vPos, vT);

	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);
	pClientDE->SetObjectRotation(hObj, &rRot);
	pClientDE->SetObjectScale(hObj, &vScale);
	pClientDE->SetObjectPos(hObj, &vPos);

	DVector vDrift;
	if (GetNodeDrift(hObj, (nGroup == 0) ? "palm" : "palm3", vDrift))
	{
		DVector vFixed;
		VEC_SUB(vFixed, vPos, vDrift);
		pClientDE->SetObjectPos(hObj, &vFixed);
	}

	pClientDE->SetObjectFlags(hObj, VR_PV_FLAGS);
}

// Which half of the donor to lend: the dominant hand, or the off hand.
int CVRStereo::LentHandGroup(DBOOL bDominant)
{
	int nDom = m_bLeftHanded ? 0 : 1;
	if (m_vtLentHandSwap.GetFloat(0.0f) != 0.0f) nDom = 1 - nDom;
	return bDominant ? nDom : (1 - nDom);
}

HLOCALOBJ CVRStereo::BorrowHand(const DVector &vCamPos)
{
	if (!m_pClientDE) return DNULL;

	// The donor is the Colt 45, and the reason is handedness.  The engine
	// cannot mirror a model, so a right hand stays a right hand however it is
	// posed - a left-handed player needs a genuine left hand to exist in some
	// model's art.  The submachine gun was the first donor tried and it does
	// carry two hands, but they are a gripping hand and an OPEN one (it holds
	// a magazine), so lending the second to a left-handed player put a flat,
	// open hand on the gun.  The Colt 45 is the one on-foot model with a
	// mirrored PAIR of hands both closed round a pistol grip - that is the
	// whole point of #5 - which is exactly what a rear grip wants, in either
	// handedness.
	char* szModel = GetPVModelName(GUN_COLT45_ID);
	char* szSkin  = GetPVModelSkin(GUN_COLT45_ID);
	if (!szModel || !szSkin) return DNULL;

	ObjectCreateStruct theStruct;
	INIT_OBJECTCREATESTRUCT(theStruct);
	theStruct.m_ObjectType = OT_MODEL;
	strncpy(theStruct.m_Filename, szModel, sizeof(theStruct.m_Filename) - 1);
	strncpy(theStruct.m_SkinName, szSkin,  sizeof(theStruct.m_SkinName)  - 1);
	theStruct.m_Flags = VR_PV_FLAGS;
	VEC_COPY(theStruct.m_Pos, vCamPos);

	HLOCALOBJ hObj = m_pClientDE->CreateObject(&theStruct);
	if (!hObj) return DNULL;

	HMODELANIM hAnim = m_pClientDE->GetAnimIndex(hObj, "Idle_1");
	if (hAnim == (HMODELANIM)-1) hAnim = m_pClientDE->GetAnimIndex(hObj, "Idle_2");
	if (hAnim != (HMODELANIM)-1)
	{
		m_pClientDE->SetModelAnimation(hObj, (DDWORD)hAnim);
		m_pClientDE->SetModelLooping(hObj, DTRUE);
		m_pClientDE->ResetModelAnimation(hObj);
	}
	return hObj;
}

// Show one arm group of a borrowed hand and put it on the given controller.
void CVRStereo::PoseBorrowedHand(HLOCALOBJ hObj, int nGroup, const DVector &vHandP,
								 const DVector &vHandF, const DVector &vHandU,
								 const DVector &vCamPos,
								 DFLOAT fTX, DFLOAT fTY, DFLOAT fTZ, DFLOAT fRoll)
{
	CClientDE* pClientDE = m_pClientDE;
	if (!pClientDE || !hObj) return;

	// SetHandGroupHidden covers both naming conventions, so this works for the
	// Colt 45's palm/palm3 as well as the R_Arm/Right_Palm models.  The Colt 45
	// halves carry no forearm at all - only nulls, hand and pistol - so there
	// is nothing here to hide for VRHandArm, and nothing to look like a slab.
	SetAllNodesHidden(hObj, DTRUE);
	SetHandGroupHidden(hObj, nGroup, DFALSE);

	// The same origin the weapon itself was measured from - the real camera,
	// not the weapon's bobbed position; see AdjustWeaponPose.
	DVector vOrigin;
	if (m_bWeaponPoseOrigin)	{ VEC_COPY(vOrigin, m_vWeaponPoseOrigin); }
	else						{ VEC_COPY(vOrigin, vCamPos); }

	DVector vPos, vF, vU, hU, hR, hF, vT;
	GetHandWorldFrom(vOrigin, vHandP, vHandF, vHandU, vPos, vF, vU);

	DRotation rRot;
	pClientDE->AlignRotation(&rRot, &vF, &vU);
	if (fRoll != 0.0f) pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));
	pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);

	DFLOAT fScale = m_fGunScaleUsed;
	if (fScale < 0.2f) fScale = 0.2f;
	if (fScale > 5.0f) fScale = 5.0f;

	VEC_MULSCALAR(vT, hR, (m_vtGunX.GetFloat(0.0f) + fTX) * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hU, (m_vtGunY.GetFloat(0.0f) + fTY) * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hF, (m_vtGunZ.GetFloat(0.0f) + fTZ) * fScale);	VEC_ADD(vPos, vPos, vT);

	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);
	pClientDE->SetObjectRotation(hObj, &rRot);
	pClientDE->SetObjectScale(hObj, &vScale);
	pClientDE->SetObjectPos(hObj, &vPos);

	DVector vDrift;
	if (GetNodeDrift(hObj, (nGroup == 0) ? "palm" : "palm3", vDrift))
	{
		DVector vFixed;
		VEC_SUB(vFixed, vPos, vDrift);
		pClientDE->SetObjectPos(hObj, &vFixed);
	}

	pClientDE->SetObjectFlags(hObj, VR_PV_FLAGS);
}

// The shotgun's missing hand on the rear grip - group 0, the one modelled
// closed round a grip.
void CVRStereo::UpdateDominantHand(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	if (!m_pClientDE) return;

	DBOOL bWant = m_bAimByHand && IsControllerActive() &&
				  nWeaponId == GUN_SHOTGUN_ID &&
				  m_vtDomHand.GetFloat(1.0f) != 0.0f &&
				  m_vtGunModel.GetFloat(1.0f) == 0.0f &&
				  m_vtGunArms.GetFloat(1.0f) != 0.0f &&
				  hPVModel != DNULL &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE);

	if (!bWant)
	{
		if (m_hDomHand) m_pClientDE->SetObjectFlags(m_hDomHand, 0);
		return;
	}

	if (!m_hDomHand)
	{
		m_hDomHand = BorrowHand(vCamPos);
		if (!m_hDomHand) return;
		m_nDomHandFor = nWeaponId;
		Log("VR borrowed hand: lent the shotgun a hand for its rear grip");
	}

	// Which half of the Colt 45 is the player's right hand: half B, by #5's
	// settled VRPistolSwapHalves=1, which keeps half B with the gun hand - and
	// the gun hand is the right one when right-handed.  So the dominant hand
	// is half B right-handed and half A left-handed.  VRLentHandSwap flips the
	// pair if that reading proves backwards on device; the pose needs no
	// change either way, since m_vRight* is already the dominant hand.
	// Where the player's dominant hand actually is - measured from the real
	// camera, the same origin the weapon itself was posed from.
	DVector vOrigin;
	if (m_bWeaponPoseOrigin)	{ VEC_COPY(vOrigin, m_vWeaponPoseOrigin); }
	else						{ VEC_COPY(vOrigin, vCamPos); }

	DVector vHandPos, vHandF, vHandU;
	GetHandWorldFrom(vOrigin, m_vRightP, m_vRightF, m_vRightU, vHandPos, vHandF, vHandU);

	DFLOAT fSeatX, fSeatY, fSeatZ;
	DomHandSeat(fSeatX, fSeatY, fSeatZ);
	PoseLentHandOnGun(m_hDomHand, LentHandGroup(DTRUE), hPVModel, vHandPos,
					  fSeatX + m_vtDomHandX.GetFloat(0.0f), fSeatY + m_vtDomHandY.GetFloat(0.0f),
					  fSeatZ + m_vtDomHandZ.GetFloat(0.0f), m_vtDomHandRoll.GetFloat(0.0f));
}

// Weapons drawn with only the hand that holds them, leaving the player's off
// hand empty: the knife and the squeaky toy.  They get group 1, the open hand,
// since there is nothing in it.
//
// Deliberately a short list rather than "any weapon with one hand": the shotgun
// also has one hand, but it is the FORE hand, so what it is missing is the
// dominant one - handled above.  Which hand a model is missing cannot be read
// from the names (both are called R_/Right_), so each weapon is listed after
// being looked at.
static DBOOL WeaponLacksOffHand(int nWeaponId)
{
	return (DBOOL)(nWeaponId == GUN_TANTO_ID || nWeaponId == GUN_SQUEAKYTOY_ID);
}

// Whose off hand the player sees: one lent from the Colt 45, or the weapon's
// own spare hand moved onto the controller.
//
// Weapons that never modelled an off hand always borrow one.  Left-handed, so
// does everything else - because the model's spare hand is a fixed-handedness
// mesh and nothing can mirror it, so moving it onto the player's right wrist
// puts a left hand there with the thumb on the wrong side.  The lent hand is a
// mirrored pair and can simply supply the other one.
//
// Right-handed the weapon's own hand is used, as before: it is the real art,
// it already looks right, and it is verified.
DBOOL CVRStereo::UseLentOffHand(int nWeaponId)
{
	if (WeaponLacksOffHand(nWeaponId)) return DTRUE;
	return (DBOOL)(m_bLeftHanded && HasSplitOffHand(nWeaponId));
}

void CVRStereo::UpdateFreeOffHand(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	if (!m_pClientDE) return;

	// A weapon that HAS a spare hand only shows a lent one while that spare
	// hand is actually off the gun.  UpdateOffArm has already decided that for
	// this frame - it stands the split down for a reload, for a weapon switch
	// and for a taken grip - so following m_bOffArmSplit means the lent hand
	// disappears at exactly the moments the model takes its hands back, rather
	// than hanging in the air through the reload and through two-handed mode.
	//
	// Weapons that never had a spare hand (knife, squeaky toy) have no split to
	// follow and keep their lent hand throughout.
	DBOOL bWant = m_bAimByHand && IsControllerActive() &&
				  UseLentOffHand(nWeaponId) &&
				  (!HasSplitOffHand(nWeaponId) || m_bOffArmSplit) &&
				  m_vtFreeHand.GetFloat(1.0f) != 0.0f &&
				  m_vtGunModel.GetFloat(1.0f) == 0.0f &&
				  m_vtGunArms.GetFloat(1.0f) != 0.0f &&
				  hPVModel != DNULL &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE);

	if (!bWant)
	{
		if (m_hFreeHand) m_pClientDE->SetObjectFlags(m_hFreeHand, 0);
		return;
	}

	if (!m_hFreeHand)
	{
		m_hFreeHand = BorrowHand(vCamPos);
		if (!m_hFreeHand) return;
		Log("VR borrowed hand: lent weapon %d an off hand", nWeaponId);
	}

	// The off hand is the other half, by the same argument.
	PoseBorrowedHand(m_hFreeHand, LentHandGroup(DFALSE), m_vLeftP, m_vLeftF, m_vLeftU, vCamPos,
					 m_vtFreeHandX.GetFloat(0.0f), m_vtFreeHandY.GetFloat(0.0f),
					 m_vtFreeHandZ.GetFloat(0.0f), m_vtFreeHandRoll.GetFloat(0.0f));
}

// Called every frame from UpdateHeldGun, straight after the dual pistols, and
// shares the same mod-owned copy object: the two features are different weapons
// so they can never both want it.  Purely visual - nothing here touches firing,
// ammo, aim or anything the server sees.
void CVRStereo::UpdateOffArm(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	CClientDE* pClientDE = m_pClientDE;
	if (!pClientDE) return;

	DBOOL bSwap     = (DBOOL)(m_vtOffHandSwap.GetFloat(0.0f) != 0.0f);

	// Measured beats configured: the fore hand is whichever is further along
	// the barrel, so the trigger hand is the one left on the gun.  Falls back
	// to VROffHandSwap when the model gives no answer.
	if (m_vtOffHandAuto.GetFloat(1.0f) != 0.0f)
	{
		int nFwd = ForwardHandGroup(hPVModel);
		if (nFwd >= 0) bSwap = (DBOOL)(nFwd == 0);
	}

	int   nKeepGrp  = bSwap ? 1 : 0;	// stays on the game's model, holding the gun
	int   nMoveGrp  = bSwap ? 0 : 1;	// goes to the off controller

	// Hiding the arms (VRGunArms) and moving one of them are the same decision
	// made two ways, so the split stands down when the player has asked for no
	// arms at all.  VRGunModel=1 replaces the model with the third-person held
	// gun, which has no second arm.
	// With the grip taken the off hand belongs on the GUN, so the split stands
	// down and the model draws both hands itself - which is the whole point of
	// two-handed mode.
	DBOOL bWant = m_bAimByHand && IsControllerActive() &&
				  HasSplitOffHand(nWeaponId) &&
				  !m_bTwoHandGrip &&
				  m_vtTwoHandWeapons.GetFloat(1.0f) != 0.0f &&
				  m_vtSplitOffHand.GetFloat(1.0f) != 0.0f &&
				  m_vtGunModel.GetFloat(1.0f) == 0.0f &&
				  m_vtGunArms.GetFloat(1.0f) != 0.0f &&
				  !m_bPistolSplit &&
				  hPVModel != DNULL &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE) &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE);

	if (bWant && CountHandGroupNodes(hPVModel, nMoveGrp) < VR_OFFARM_MIN)
	{
		static DBOOL s_bWarned = DFALSE;
		if (!s_bWarned)
		{
			s_bWarned = DTRUE;
			Log("VR off arm: weapon %d has no recognisable off-hand group - leaving it alone.", nWeaponId);
		}
		bWant = DFALSE;
	}

	// The reload borrows the hand back.  Accepted behaviour, not a fault.
	if (bWant && !OffArmHandIsFree(hPVModel)) bWant = DFALSE;

	if (!bWant)
	{
		// Give the arm back to the model we took it from - and only to that
		// one, since the game builds a new model on every weapon switch.
		if (m_hOffArmPV && m_hOffArmPV == hPVModel && m_vtGunModel.GetFloat(1.0f) == 0.0f)
		{
			SetHandGroupHidden(hPVModel, 0, DFALSE);
			SetHandGroupHidden(hPVModel, 1, DFALSE);
			SetRifleArmsHidden(hPVModel, DFALSE);
		}
		// Putting the weapon away, or losing a controller, drops the grip too:
		// it must never be possible to leave it stuck on.
		if (!IsTwoHandWeapon(nWeaponId) ||
			!(m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE) ||
			!(m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE)) m_bTwoHandGrip = DFALSE;
		m_hOffArmPV = DNULL;
		if (m_bOffArmSplit && m_hOffHandGun) pClientDE->SetObjectFlags(m_hOffHandGun, 0);
		m_bOffArmSplit = DFALSE;
		return;
	}

	// Re-applied every frame: a weapon switch brings a new model with nothing
	// hidden on it.
	SetHandGroupHidden(hPVModel, nMoveGrp, DTRUE);
	SetHandGroupHidden(hPVModel, nKeepGrp, DFALSE);
	SetRifleArmsHidden(hPVModel, (DBOOL)(m_vtHandArm.GetFloat(0.0f) == 0.0f));
	m_hOffArmPV = hPVModel;

	// Left-handed the spare hand still comes OFF the gun, but it is not what
	// the player gets to see - UpdateFreeOffHand lends a correctly handed one
	// instead (UseLentOffHand).  m_bOffArmSplit stays set either way: the off
	// hand is away from the weapon, which is what suppresses proximity aim.
	if (UseLentOffHand(nWeaponId))
	{
		if (m_hOffHandGun) pClientDE->SetObjectFlags(m_hOffHandGun, 0);
		m_bOffArmSplit = DTRUE;
		return;
	}

	// Our own copy of the very same model, showing ONLY the arm we moved.
	if (!m_hOffHandGun || m_nOffHandGunId != nWeaponId)
	{
		if (m_hOffHandGun) pClientDE->DeleteObject(m_hOffHandGun);
		m_hOffHandGun = DNULL;

		char* szModel = GetPVModelName(nWeaponId);
		char* szSkin  = GetPVModelSkin(nWeaponId);
		if (!szModel || !szSkin) { m_bOffArmSplit = DFALSE; return; }

		ObjectCreateStruct theStruct;
		INIT_OBJECTCREATESTRUCT(theStruct);
		theStruct.m_ObjectType = OT_MODEL;
		strncpy(theStruct.m_Filename, szModel, sizeof(theStruct.m_Filename) - 1);
		strncpy(theStruct.m_SkinName, szSkin,  sizeof(theStruct.m_SkinName)  - 1);
		theStruct.m_Flags = VR_PV_FLAGS;
		VEC_COPY(theStruct.m_Pos, vCamPos);
		m_hOffHandGun = pClientDE->CreateObject(&theStruct);
		if (!m_hOffHandGun) { m_bOffArmSplit = DFALSE; return; }
		m_nOffHandGunId = nWeaponId;
		m_nOffHandAnim  = (DDWORD)-1;
	}
	SetAllNodesHidden(m_hOffHandGun, DTRUE);
	SetHandGroupHidden(m_hOffHandGun, nMoveGrp, DFALSE);
	if (m_vtHandArm.GetFloat(0.0f) == 0.0f)
		pClientDE->SetModelNodeHideStatus(m_hOffHandGun, (char*)VR_OFFARM_ARM(nMoveGrp), DTRUE);

	// Follow the game's animation, for the same reason the pistols do: nothing
	// else drives the copy, and a free-running one tumbles.
	DDWORD nAnim = pClientDE->GetModelAnimation(hPVModel);
	if (nAnim != (DDWORD)-1 && nAnim != m_nOffHandAnim)
	{
		pClientDE->SetModelAnimation(m_hOffHandGun, nAnim);
		pClientDE->SetModelLooping(m_hOffHandGun, pClientDE->GetModelLooping(hPVModel));
		pClientDE->ResetModelAnimation(m_hOffHandGun);
		m_nOffHandAnim = nAnim;
	}

	// The same origin the gun hand was measured from - which is the real camera,
	// not the weapon's bobbed position; see AdjustWeaponPose.  That is what
	// keeps this arm following its own controller instead of being dragged
	// around by the gun hand.
	DVector vOrigin;
	if (m_bWeaponPoseOrigin)	{ VEC_COPY(vOrigin, m_vWeaponPoseOrigin); }
	else						{ VEC_COPY(vOrigin, vCamPos); }

	DVector vHandPos, vHandF, vHandU, hU, hR, hF, vT;
	GetHandWorldFrom(vOrigin, m_vLeftP, m_vLeftF, m_vLeftU, vHandPos, vHandF, vHandU);

	DRotation rRot;
	pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
	DFLOAT fRoll = m_vtOffHandRoll.GetFloat(0.0f);
	if (fRoll != 0.0f) pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));
	pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);

	DFLOAT fScale = m_fGunScaleUsed;
	if (fScale < 0.2f) fScale = 0.2f;
	if (fScale > 5.0f) fScale = 5.0f;

	VEC_MULSCALAR(vT, hR, (m_vtGunX.GetFloat(0.0f) + m_vtOffHandX.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hU, (m_vtGunY.GetFloat(0.0f) + m_vtOffHandY.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hF, (m_vtGunZ.GetFloat(0.0f) + m_vtOffHandZ.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);

	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);
	pClientDE->SetObjectRotation(m_hOffHandGun, &rRot);
	pClientDE->SetObjectScale(m_hOffHandGun, &vScale);
	pClientDE->SetObjectPos(m_hOffHandGun, &vHandPos);

	// Put this arm's palm on the controller, whatever the animation has done
	// with it.  Only one object moves here, so unlike the pistols there is no
	// second placement for it to disagree with.
	if (m_vtOffHandAnchor.GetFloat(1.0f) != 0.0f)
	{
		DVector vDrift;
		if (GetHandGroupDrift(m_hOffHandGun, nMoveGrp, vDrift))
		{
			DVector vFixed;
			VEC_SUB(vFixed, vHandPos, vDrift);
			pClientDE->SetObjectPos(m_hOffHandGun, &vFixed);
		}
	}

	pClientDE->SetObjectFlags(m_hOffHandGun, VR_PV_FLAGS);
	m_bOffArmSplit = DTRUE;
}

// Called every frame from UpdateHeldGun, before it touches the third-person
// held gun.  Decides whether the split is on, keeps each half in the right
// hand, and tidies up the moment the weapon changes.
//
// Note on ordering: the shell calls CWeaponModel::UpdateWeaponModel BEFORE
// UpdateHeldGun, so m_bPistolSplit is always one frame old by the time
// VRCommandOn, ApplyPistolShotPose and GetMuzzleFlashWorld read it - and
// UpdateTwoHanded, earlier still, reads the same stale value.  They therefore
// all agree with each other, and the only effect is one frame either side of a
// weapon change, which is not perceptible.  Leaving it alone avoids reordering
// Monolith's own update for no real gain.  (m_nCurrentWeaponId is one frame
// stale in exactly the same way, which is what the melee gesture has always
// worked from.)
void CVRStereo::UpdateDualPistols(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	CClientDE* pClientDE = m_pClientDE;
	if (!pClientDE) return;

	// Which half reads as the left-hand pistol is a judgement that can only
	// really be made in the headset, so it is a live CVar, not a constant.
	DBOOL bSwap    = (DBOOL)(m_vtPistolSwapHalves.GetFloat(1.0f) != 0.0f);

	// The two halves are a mirrored PAIR - one left hand, one right - so which
	// half belongs on which controller flips with handedness.  VRPistolSwapHalves
	// was settled right-handed; left-handed the gun hand is the player's left,
	// so it wants the other half.  Without this the thumbs point the wrong way
	// round, which is exactly how it looked on device.
	//
	// Same reasoning as LentHandGroup, and the same trap: a value tuned in one
	// handedness mode is only half the answer.
	if (m_bLeftHanded) bSwap = (DBOOL)!bSwap;

	int   nGunHalf = bSwap ? 1 : 0;
	int   nOffHalf = bSwap ? 0 : 1;

	// The split only makes sense while the mod is posing the first-person
	// model by hand and both controllers are tracking.  VRGunModel=1 replaces
	// that model with the third-person held gun, which is a single pickup
	// model with nothing to split, so there the stock behaviour is kept.
	DBOOL bWant = m_bAimByHand && IsControllerActive() &&
				  nWeaponId == GUN_COLT45_ID &&
				  m_vtDualPistols.GetFloat(1.0f) != 0.0f &&
				  m_vtGunModel.GetFloat(1.0f) == 0.0f &&
				  hPVModel != DNULL &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE) &&
				  (m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE);

	if (bWant &&
		(CountHalfNodes(hPVModel, 0) < VR_PISTOL_HALF_MIN ||
		 CountHalfNodes(hPVModel, 1) < VR_PISTOL_HALF_MIN))
	{
		static DBOOL s_bWarned = DFALSE;
		if (!s_bWarned)
		{
			s_bWarned = DTRUE;
			Log("VR dual pistols: Colt45_pv.abc has no recognisable halves - keeping the stock pair.");
		}
		bWant = DFALSE;
	}

	if (!bWant)
	{
		// Put back anything we hid - but only on the model we hid it on (the
		// game makes a new one on every weapon switch, and an old handle may
		// already be gone), and only while the first-person model is the one
		// on show.  With VRGunModel=1 the held-gun path above has just hidden
		// every node on purpose, so there the right move is to touch nothing.
		if (m_hSplitPV && m_hSplitPV == hPVModel && m_vtGunModel.GetFloat(1.0f) == 0.0f)
		{
			SetHalfNodesHidden(hPVModel, 0, DFALSE);
			SetHalfNodesHidden(hPVModel, 1, DFALSE);
		}
		m_hSplitPV = DNULL;
		if (m_hOffHandGun) pClientDE->SetObjectFlags(m_hOffHandGun, 0);
		m_bPistolSplit = DFALSE;
		m_nPistolFireHand = -1;
		m_nPistolLastHand = -1;
		m_nOffHandAnim    = (DDWORD)-1;
		m_bPistolAimValid = DFALSE;
		VEC_INIT(m_vPistolAnchorFix);
		return;
	}

	// Re-applied every frame for the same reason SetArmNodesHidden is: a new
	// model arrives with each weapon switch, with nothing hidden on it.
	SetHalfNodesHidden(hPVModel, nOffHalf, DTRUE);
	SetHalfNodesHidden(hPVModel, nGunHalf, DFALSE);
	m_hSplitPV = hPVModel;

	// Our own copy of the very same model, showing only the other half.
	if (!m_hOffHandGun || m_nOffHandGunId != nWeaponId)
	{
		if (m_hOffHandGun) pClientDE->DeleteObject(m_hOffHandGun);
		m_hOffHandGun = DNULL;

		char* szModel = GetPVModelName(nWeaponId);
		char* szSkin  = GetPVModelSkin(nWeaponId);
		if (!szModel || !szSkin) { m_bPistolSplit = DFALSE; return; }

		ObjectCreateStruct theStruct;
		INIT_OBJECTCREATESTRUCT(theStruct);
		theStruct.m_ObjectType = OT_MODEL;
		strncpy(theStruct.m_Filename, szModel, sizeof(theStruct.m_Filename) - 1);
		strncpy(theStruct.m_SkinName, szSkin,  sizeof(theStruct.m_SkinName)  - 1);
		theStruct.m_Flags = VR_PV_FLAGS;
		VEC_COPY(theStruct.m_Pos, vCamPos);
		m_hOffHandGun = pClientDE->CreateObject(&theStruct);
		if (!m_hOffHandGun) { m_bPistolSplit = DFALSE; return; }
		m_nOffHandGunId = nWeaponId;
		m_nOffHandAnim  = (DDWORD)-1;
	}
	SetHalfNodesHidden(m_hOffHandGun, nGunHalf, DTRUE);
	SetHalfNodesHidden(m_hOffHandGun, nOffHalf, DFALSE);

	// The copy is its own object, so NOTHING drives its animation: left to
	// itself it free-runs on whatever animation it was created on, while the
	// game plays the real weapon's idle, fire and reload animations on its own
	// model.  Shogo swings the pistols a long way in those animations, so an
	// unsynchronised copy reads as the off-hand pistol tumbling and rolling on
	// its own, never settling - which is exactly what it did on device.  Follow
	// the game's model instead, so both halves are always mid-way through the
	// same animation.
	//
	// There is no frame-level animation control in the interface (no
	// SetModelPlaybackState), so the two are kept in phase by restarting the
	// copy whenever the game's model changes animation.  Both then tick at the
	// same rate from the same frame.  The alternating fire animations change
	// index on every shot, so a shot resynchronises them for free; ShotFired
	// forces it as well, for the case where the same fire animation replays.
	DDWORD nAnim = pClientDE->GetModelAnimation(hPVModel);
	if (nAnim != (DDWORD)-1 && nAnim != m_nOffHandAnim)
	{
		pClientDE->SetModelAnimation(m_hOffHandGun, nAnim);
		pClientDE->SetModelLooping(m_hOffHandGun, pClientDE->GetModelLooping(hPVModel));
		pClientDE->ResetModelAnimation(m_hOffHandGun);
		m_nOffHandAnim = nAnim;
	}

	// The gun-hand model is the game's own, and AdjustWeaponPose is what
	// places it - so measure its drift here, where the handle is known to be
	// live, and let AdjustWeaponPose apply it next frame.  (It runs earlier in
	// the frame than this does; one frame of lag in a figure that is
	// remeasured every frame is not visible.)
	DVector vGunDrift;
	DBOOL   bGunAnchor = GetPistolAnchorDrift(hPVModel, nGunHalf, vGunDrift);

	// Posed from the off hand's OWN direction, not the aim direction: each
	// pistol points where its own hand points.  The player's view still
	// follows the gun hand - see UpdateTwoHanded and ApplyHeadTracking.
	// Measured from whatever origin AdjustWeaponPose was handed for the gun hand
	// earlier this frame, NOT from the camera - see the note there.  Starting
	// the two pistols from different origins is what put one of them a few units
	// further from its controller than the other.
	DVector vOrigin;
	if (m_bWeaponPoseOrigin)	{ VEC_COPY(vOrigin, m_vWeaponPoseOrigin); }
	else						{ VEC_COPY(vOrigin, vCamPos); }

	DVector vHandPos, vHandF, vHandU, hU, hR, hF, vT;
	GetHandWorldFrom(vOrigin, m_vLeftP, m_vLeftF, m_vLeftU, vHandPos, vHandF, vHandU);

	// Optional extra 180-degree roll about the barrel, off by default - see the
	// VRPistolMirror note in Init for why the mirrored half does NOT need one.
	if (m_vtPistolMirror.GetFloat(0.0f) != 0.0f) { VEC_MULSCALAR(vHandU, vHandU, -1.0f); }

	DRotation rRot;
	pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
	DFLOAT fRoll = m_vtPistolOffRoll.GetFloat(0.0f);
	if (fRoll != 0.0f) pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));
	pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);

	// The same scale ScaleWeapon gave the game's own model, so the two pistols
	// always match each other.
	DFLOAT fScale = m_fGunScaleUsed;
	if (fScale < 0.2f) fScale = 0.2f;
	if (fScale > 5.0f) fScale = 5.0f;

	// The gun-hand nudge AdjustWeaponPose applies, plus the off-hand-only trim.
	VEC_MULSCALAR(vT, hR, (m_vtGunX.GetFloat(0.0f) + m_vtPistolOffX.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hU, (m_vtGunY.GetFloat(0.0f) + m_vtPistolOffY.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hF, (m_vtGunZ.GetFloat(0.0f) + m_vtPistolOffZ.GetFloat(0.0f)) * fScale);	VEC_ADD(vHandPos, vHandPos, vT);

	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);
	pClientDE->SetObjectRotation(m_hOffHandGun, &rRot);
	pClientDE->SetObjectScale(m_hOffHandGun, &vScale);
	pClientDE->SetObjectPos(m_hOffHandGun, &vHandPos);

	// Shogo animates the pistols a long way from the model's origin, so
	// placing the object at the hand is not enough - the visible pistol ends
	// up wherever the animation has carried it.  Measure where this half's
	// palm has actually landed and shift the object by the opposite amount, so
	// the grip sits in the player's hand through the whole animation.
	DVector vOffDrift;
	DBOOL   bOffAnchor = GetPistolAnchorDrift(m_hOffHandGun, nOffHalf, vOffDrift);

	// Anchoring is all-or-nothing across the pair.  Each half is anchored on its
	// OWN hand node ("palm" for one, "palm3" for the other), so if one of those
	// lookups fails that pistol alone keeps the animation's offset while the
	// other has it taken out - and the two sit at visibly different distances
	// from their controllers.  Correcting one but not the other is worse than
	// correcting neither, so if either fails, both go unanchored and the pair
	// stays symmetric.
	if (!bGunAnchor || !bOffAnchor)
	{
		VEC_INIT(vGunDrift);
		VEC_INIT(vOffDrift);
	}
	VEC_COPY(m_vPistolAnchorFix, vGunDrift);

	{
		DVector vFixed;
		VEC_SUB(vFixed, vHandPos, vOffDrift);
		pClientDE->SetObjectPos(m_hOffHandGun, &vFixed);
	}

	// Diagnostics for the two pistols sitting differently against their
	// controllers.  Printed a few frames in and then again once things have
	// settled, rather than on the very first frame, because the gun hand's
	// anchor is applied by AdjustWeaponPose a frame later than it is measured.
	//
	// Everything needed to see where an asymmetry comes from is here: the raw
	// controller poses, the two hand targets after the body-frame transform,
	// the two anchor drifts, and where both objects actually ended up.  With
	// the UDP tester sending a fixed pose, these numbers are constant and the
	// two sides should differ only by the controllers' own separation.
	{
		static int s_nAnchorFrames = 0;
		s_nAnchorFrames++;
		if (s_nAnchorFrames == 30 || s_nAnchorFrames == 150)
		{
			DVector vGunPos, vOffPos, vGunHand, vGunF, vGunU;
			VEC_INIT(vGunPos); VEC_INIT(vOffPos);
			pClientDE->GetObjectPos(hPVModel, &vGunPos);
			pClientDE->GetObjectPos(m_hOffHandGun, &vOffPos);
			GetHandWorldFrom(vOrigin, m_vRightP, m_vRightF, m_vRightU, vGunHand, vGunF, vGunU);

			Log("VR pistols #%d raw  R[%.4f,%.4f,%.4f] L[%.4f,%.4f,%.4f] scale %.2f/%.2f",
				s_nAnchorFrames,
				m_vRightP.x, m_vRightP.y, m_vRightP.z,
				m_vLeftP.x,  m_vLeftP.y,  m_vLeftP.z,
				m_fWorldScaleUsed, fScale);
			Log("VR pistols #%d hand gun[%.1f,%.1f,%.1f] off[%.1f,%.1f,%.1f]",
				s_nAnchorFrames,
				vGunHand.x, vGunHand.y, vGunHand.z,
				vHandPos.x, vHandPos.y, vHandPos.z);
			Log("VR pistols #%d drift gun(half %d,%s)=%d[%.1f,%.1f,%.1f] off(half %d,%s)=%d[%.1f,%.1f,%.1f]",
				s_nAnchorFrames,
				nGunHalf, (nGunHalf == 0) ? "palm" : "palm3", (int)bGunAnchor,
				vGunDrift.x, vGunDrift.y, vGunDrift.z,
				nOffHalf, (nOffHalf == 0) ? "palm" : "palm3", (int)bOffAnchor,
				vOffDrift.x, vOffDrift.y, vOffDrift.z);
			Log("VR pistols #%d final gun[%.1f,%.1f,%.1f] off[%.1f,%.1f,%.1f] cam[%.1f,%.1f,%.1f] origin[%.1f,%.1f,%.1f]",
				s_nAnchorFrames,
				vGunPos.x, vGunPos.y, vGunPos.z,
				vOffPos.x, vOffPos.y, vOffPos.z,
				vCamPos.x, vCamPos.y, vCamPos.z,
				vOrigin.x, vOrigin.y, vOrigin.z);
		}
	}

	pClientDE->SetObjectFlags(m_hOffHandGun, VR_PV_FLAGS);

	// Let go of both triggers and the alternation starts over, so the next
	// burst begins on the dominant hand again rather than wherever the last
	// one happened to stop.
	{
		DDWORD nHeld = m_nButtons & ~m_nSuppressed;
		if (!(nHeld & SHOGOVR_BTN_FIRE) && !(nHeld & SHOGOVR_BTN_LEFT_TRIGGER)) m_nPistolLastHand = -1;
	}

	m_bPistolSplit = DTRUE;
}

// Dual pistols (#5): which pistol fires this shot.
//
// There is deliberately NO second cooldown here.  Shogo has one CWeaponModel
// for the Colt 45 and its rate of fire is the length of the fire animation, and
// that single cadence is left exactly as the game set it: the pair fires at the
// stock rate between them, so the server sees the same stream of
// MID_WEAPON_FIRE messages it always did and ammo drains at the stock rate.
// All this does is decide which of the two pistols owns each shot the game
// decides to fire.
//
// With both triggers held the two alternate, so the dominant hand fires first
// and the other follows one cadence later.  With one trigger held that pistol
// takes every shot.  Nothing is queued: the triggers are read at the moment
// the shot happens, so a pull that starts and ends inside a pistol's cooldown
// is simply never seen - to fire that pistol again you have to be holding its
// trigger when the next shot comes round.
int CVRStereo::PickPistolFireHand()
{
	// Suppression matters: the trigger that dismissed a menu must not also
	// fire the instant play resumes.
	DDWORD nHeld = m_nButtons & ~m_nSuppressed;

	// The gun hand's trigger is SHOGOVR_BTN_FIRE and the off hand's is
	// SHOGOVR_BTN_LEFT_TRIGGER *after* the left-handed swap in UpdateBridge,
	// so this reads correctly in both handedness modes with no extra
	// bookkeeping: whichever trigger is pulled, the pistol the player can see
	// in that hand is the one that fires.
	DBOOL bGun = (DBOOL)((nHeld & SHOGOVR_BTN_FIRE) != 0);
	DBOOL bOff = (DBOOL)((nHeld & SHOGOVR_BTN_LEFT_TRIGGER) != 0);

	if (bGun && bOff)
	{
		// Alternate.  m_nPistolLastHand starts at -1, so the dominant hand
		// takes the first shot of a burst.
		return (m_nPistolLastHand == 0) ? 1 : 0;
	}
	if (bGun) return 0;
	if (bOff) return 1;
	return -1;
}

// How far the fire animation has carried one half's hand away from its
// object's origin, in world space.  Returned as the drift to subtract, so a
// caller that wants the palm at vHand places the object at vHand - vDrift.
//
// GetModelNodeTransform is a read of the node's CURRENT global transform, and
// there is no matching write in the engine's interface - so the only way to
// move a hand is to move the whole object and cancel out the animation's own
// offset, which is what this measures.  A frame of staleness in the figure
// does not matter: it is recomputed every frame and the animation is
// continuous.
DBOOL CVRStereo::GetPistolAnchorDrift(HLOCALOBJ hObj, int nHalf, DVector &vDrift)
{
	VEC_INIT(vDrift);
	if (!m_pClientDE || !hObj) return DFALSE;
	if (m_vtPistolAnchor.GetFloat(1.0f) == 0.0f) return DFALSE;

	// The hand each half is holding its pistol with.  Anchoring on the palm
	// rather than on a null keeps the grip in the player's hand, which is the
	// part that has to line up.
	return GetNodeDrift(hObj, (nHalf == 0) ? "palm" : "palm3", vDrift);
}

// The same measurement for any named node: where it has ended up, as an offset
// from its object's origin, so a caller can place the object at
// (target - drift) and land that node on the target.
DBOOL CVRStereo::GetNodeDrift(HLOCALOBJ hObj, const char* szNode, DVector &vDrift)
{
	VEC_INIT(vDrift);
	if (!m_pClientDE || !hObj || !szNode || !szNode[0]) return DFALSE;

	DVector vNode, vObj;
	DRotation rNode;
	if (!m_pClientDE->GetModelNodeTransform(hObj, (char*)szNode, &vNode, &rNode)) return DFALSE;
	m_pClientDE->GetObjectPos(hObj, &vObj);

	VEC_SUB(vDrift, vNode, vObj);
	return DTRUE;
}

// Dual pistols (#5): aim the shot the game just fired down the pistol that
// fired it.
//
// CWeaponModel::SendFireMsg takes the direction from the weapon object's own
// rotation and the origin from m_vFlashPos, and the server honours both
// verbatim (CPlayerObj::HandleWeaponFireMessage), so rewriting the two here -
// after the game has built them and before the message goes out - is all it
// takes to make the off hand's shots come from the off hand.  No message
// format changes and the server is untouched.
//
// Note the weapon object's rotation is set only for SendFireMsg's benefit:
// AdjustWeaponPose re-poses the model for display immediately afterwards.
DBOOL CVRStereo::ApplyPistolShotPose(int nWeaponId, const DVector &vCamPos, DBOOL bFootScale,
									 DVector &vFlashPos, HLOCALOBJ hWeapon)
{
	if (!m_pClientDE || !m_bPistolSplit) return DFALSE;
	if (nWeaponId != GUN_COLT45_ID) return DFALSE;

	// The hand was settled in ShotFired(), which the engine called a few lines
	// earlier in this same frame (see the note there for why it has to be that
	// way round).
	if (m_nPistolFireHand < 0) return DFALSE;

	DVector vHandPos, vHandF, vHandU;
	if (m_nPistolFireHand == 1)	GetHandWorldFrom(vCamPos, m_vLeftP, m_vLeftF, m_vLeftU, vHandPos, vHandF, vHandU);
	else						GetHandWorld(vCamPos, vHandPos, vHandF, vHandU);

	// Direction: straight down the hand that pulled.  (For the gun hand this
	// is what the camera rotation already gave, since the split turns
	// two-handed aim off - so only the off hand really changes here.)
	if (hWeapon)
	{
		DRotation rRot;
		m_pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
		m_pClientDE->SetObjectRotation(hWeapon, &rRot);
	}

	// Origin: the same muzzle the flash is drawn at, so the shot and the
	// flash agree.
	if (m_vtPistolShotOrigin.GetFloat(1.0f) != 0.0f)
	{
		DVector vMuzzle;
		if (GetMuzzleFlashWorld(nWeaponId, vCamPos, bFootScale, vMuzzle, W_FIRED))
		{
			VEC_COPY(vFlashPos, vMuzzle);
		}
	}

	return DTRUE;
}

// Appends a weapon model's part names to ShogoVR\ShogoVR_models.txt - which
// parts exist decides what can be hidden; send this file in if the arms on
// some gun don't disappear.  Only written with VRVerboseDebug on; the caller
// checks, and so does this, since writing a file is not something a debug
// switch should leave to one caller remembering.
void CVRStereo::DumpModelNodes(HLOCALOBJ hObj, int nWeaponId)
{
	if (!m_pClientDE || !hObj || !m_szIniPath[0]) return;
	if (m_vtVerbose.GetFloat(0.0f) == 0.0f) return;
	char szPath[320];
	strncpy(szPath, m_szIniPath, sizeof(szPath) - 1);
	szPath[sizeof(szPath) - 1] = 0;
	char* pSlash = strrchr(szPath, '\\');
	if (!pSlash) return;
	strcpy(pSlash + 1, "ShogoVR_models.txt");

	FILE* f = fopen(szPath, "a");
	if (!f) return;
	fprintf(f, "Weapon %d (first-person model):", nWeaponId);
	HMODELNODE hNode = INVALID_MODEL_NODE;
	int n = 0;
	while (m_pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		m_pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		fprintf(f, "%s %s", n++ ? "," : "", szName);
	}
	fprintf(f, "  (%d parts)\n", n);
	fclose(f);
}

// ======================================================================= //
//	Transforming (mech vehicle mode)
// ======================================================================= //

void CVRStereo::Log(const char* szFormat, ...)
{
	if (!m_szIniPath[0]) return;
	static int s_bStarted = 0;
	char szPath[320];
	strncpy(szPath, m_szIniPath, sizeof(szPath) - 1);
	szPath[sizeof(szPath) - 1] = 0;
	char* pSlash = strrchr(szPath, '\\');
	if (!pSlash) return;
	strcpy(pSlash + 1, "ShogoVR_game.log");

	FILE* f = fopen(szPath, s_bStarted ? "a" : "w");		// a fresh log each time the game starts
	if (!f) return;
	s_bStarted = 1;
	time_t t = time(NULL);
	struct tm* pTm = localtime(&t);
	if (pTm) fprintf(f, "[%02d:%02d:%02d] ", pTm->tm_hour, pTm->tm_min, pTm->tm_sec);
	va_list args;
	va_start(args, szFormat);
	vfprintf(f, szFormat, args);
	va_end(args);
	fprintf(f, "\n");
	fclose(f);
}

// However Shogo was started (Shogo.exe, Steam, the Shogo VR shortcut), make
// sure the headset bridge runs: start it - without a window, so it can't
// take focus from the game - unless it's already running.
void CVRStereo::StartBridgeIfNeeded()
{
	if (m_vtAutoBridge.GetFloat(1.0f) == 0.0f || !m_szIniPath[0]) return;

	HANDLE hRunning = OpenMutexA(SYNCHRONIZE, FALSE, "Local\\ShogoVRBridge_Running");
	if (hRunning)
	{
		CloseHandle(hRunning);
		if (!m_nBridgeStarts) Log("Headset bridge already running");
		return;
	}
	if (m_nBridgeStarts >= 5) return;			// it keeps stopping - don't loop
	m_nBridgeStarts++;

	char szDir[320];
	strncpy(szDir, m_szIniPath, sizeof(szDir) - 1);
	szDir[sizeof(szDir) - 1] = 0;
	char* pSlash = strrchr(szDir, '\\');
	if (!pSlash) return;
	*pSlash = 0;

	char szExe[400];
	_snprintf(szExe, sizeof(szExe) - 1, "%s\\ShogoVRBridge.exe", szDir);
	szExe[sizeof(szExe) - 1] = 0;
	if (GetFileAttributesA(szExe) == INVALID_FILE_ATTRIBUTES)
	{
		Log("Headset bridge not found at %s", szExe);
		return;
	}

	char szCmd[440];
	_snprintf(szCmd, sizeof(szCmd) - 1, "\"%s\" --game-pid %lu", szExe, (unsigned long)GetCurrentProcessId());
	szCmd[sizeof(szCmd) - 1] = 0;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));
	si.cb = sizeof(si);
	if (CreateProcessA(szExe, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, szDir, &si, &pi))
	{
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		m_bStartedBridge = DTRUE;
		Log("Started the headset bridge");
	}
	else
	{
		Log("Couldn't start the headset bridge (error %lu)", GetLastError());
	}
}

// Remember exactly how Shogo was started, so the Shogo VR shortcut can start
// it the same way (whatever Shogo's own launcher passes on this PC).
void CVRStereo::RememberCommandLine()
{
	const char* szCmd = GetCommandLineA();
	if (!szCmd || !m_szIniPath[0]) return;
	Log("Command line: %s", szCmd);
	if (strstr(szCmd, "+VRLauncher")) return;			// the shortcut started us - nothing new to learn

	// Skip the program's own path (quoted or not) - but some launchers leave
	// it out and pass only the options, so never skip an option itself.
	const char* p = szCmd;
	while (*p == ' ') p++;
	if (*p != '-' && *p != '+')
	{
		if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; }
		else { while (*p && *p != ' ') p++; }
		while (*p == ' ') p++;
	}
	if (!*p || !strstr(p, "-rez")) return;			// nothing worth remembering

	// A leading '!' keeps the ini reader from stripping quotes off the ends.
	char szValue[2048];
	_snprintf(szValue, sizeof(szValue) - 1, "!%s", p);
	szValue[sizeof(szValue) - 1] = 0;
	WritePrivateProfileStringA("Launch", "GameArgs", szValue, m_szIniPath);
}

void CVRStereo::DrawOpeningNotice(void* pFont)
{
	if (!m_pClientDE || !pFont || m_bNoticeDone) return;
	CClientDE* pClientDE = m_pClientDE;

	DFLOAT fNow = pClientDE->GetTime();
	if (m_fNoticeStart < 0.0f) m_fNoticeStart = fNow;
	if (fNow - m_fNoticeStart > 12.0f)
	{
		int i;
		for (i = 0; i < 2; i++) { if (m_hNotice[i]) pClientDE->DeleteSurface(m_hNotice[i]); m_hNotice[i] = DNULL; }
		m_bNoticeDone = DTRUE;
		return;
	}

	if (!m_hNotice[0])
	{
		char szLine1[300];
		_snprintf(szLine1, sizeof(szLine1) - 1, "SHOGO VR - unofficial VR mod by %s", m_szAuthors[0] ? m_szAuthors : "its creators");
		szLine1[sizeof(szLine1) - 1] = 0;
		m_hNotice[0] = CTextHelper::CreateSurfaceFromString(pClientDE, (CBitmapFont*)pFont, szLine1);
		m_hNotice[1] = CTextHelper::CreateSurfaceFromString(pClientDE, (CBitmapFont*)pFont,
			(char*)"THIS LEVEL IS NOT MADE BY OR SUPPORTED BY Monolith Productions, or any of its affiliates and subsidiaries.");
		if (!m_hNotice[0] || !m_hNotice[1]) return;
	}

	HSURFACE hScreen = pClientDE->GetScreenSurface();
	DDWORD nScreenW = 0, nScreenH = 0, w[2] = { 0, 0 }, h[2] = { 0, 0 };
	pClientDE->GetSurfaceDims(hScreen, &nScreenW, &nScreenH);
	pClientDE->GetSurfaceDims(m_hNotice[0], &w[0], &h[0]);
	pClientDE->GetSurfaceDims(m_hNotice[1], &w[1], &h[1]);

	// A dark band along the bottom so it reads over any menu background.
	int nBandH = (int)(h[0] + h[1]) + 16;
	DRect rcBand;
	rcBand.left = 0;
	rcBand.top = (int)nScreenH - nBandH;
	rcBand.right = (int)nScreenW;
	rcBand.bottom = (int)nScreenH;
	pClientDE->FillRect(hScreen, &rcBand, pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE));

	int y = rcBand.top + 6, i;
	for (i = 0; i < 2; i++)
	{
		pClientDE->DrawSurfaceToSurfaceTransparent(hScreen, m_hNotice[i], DNULL, ((int)nScreenW - (int)w[i]) / 2, y, DNULL);
		y += (int)h[i] + 4;
	}
}

DBOOL CVRStereo::GameHasFocus()
{
	return m_nGameHwnd && GetForegroundWindow() == (HWND)m_nGameHwnd;
}

// ======================================================================= //
//	Crash reporting: which module failed and where, in ShogoVR_game.log, plus
//	a crash dump (ShogoVR\ShogoVR_crash.dmp) - so a crash can be traced.
// ======================================================================= //

static PVOID						s_hVectored = NULL;
static LPTOP_LEVEL_EXCEPTION_FILTER	s_pfnPrevFilter = NULL;
static DBOOL						s_bFilterInstalled = DFALSE;

static void VRDescribeAddress(void* pAddr, char* szOut, int nOut)
{
	HMODULE hMod = NULL;
	char szPath[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pAddr, &hMod) && hMod)
		GetModuleFileNameA(hMod, szPath, MAX_PATH);
	const char* pName = strrchr(szPath, '\\');
	pName = pName ? pName + 1 : szPath;
	_snprintf(szOut, nOut - 1, "%s+0x%lX", pName, (unsigned long)((char*)pAddr - (char*)hMod));
	szOut[nOut - 1] = 0;
}

static LONG CALLBACK VRVectoredHandler(EXCEPTION_POINTERS* p)
{
	DWORD dwCode = p->ExceptionRecord->ExceptionCode;
	if (dwCode != EXCEPTION_ACCESS_VIOLATION && dwCode != EXCEPTION_STACK_OVERFLOW && dwCode != EXCEPTION_ILLEGAL_INSTRUCTION &&
		dwCode != EXCEPTION_INT_DIVIDE_BY_ZERO && dwCode != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
	static LONG s_nCount = 0;
	if (InterlockedIncrement(&s_nCount) > 5) return EXCEPTION_CONTINUE_SEARCH;		// a handful is enough
	char szWhere[160];
	VRDescribeAddress(p->ExceptionRecord->ExceptionAddress, szWhere, sizeof(szWhere));
	g_VRStereo.Log("Exception 0x%08lX at %s (it may still be handled)", (unsigned long)dwCode, szWhere);
	return EXCEPTION_CONTINUE_SEARCH;
}

typedef BOOL (WINAPI *VRMiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
struct VRMiniDumpExceptionInfo { DWORD ThreadId; EXCEPTION_POINTERS* ExceptionPointers; BOOL ClientPointers; };

static LONG WINAPI VRUnhandledFilter(EXCEPTION_POINTERS* p)
{
	char szWhere[160];
	VRDescribeAddress(p->ExceptionRecord->ExceptionAddress, szWhere, sizeof(szWhere));
	g_VRStereo.Log("CRASH: exception 0x%08lX at %s", (unsigned long)p->ExceptionRecord->ExceptionCode, szWhere);

	// A small crash dump next to the logs.
	const char* szIni = g_VRStereo.GetIniPath();
	HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
	VRMiniDumpWriteDumpFn pfnDump = hDbgHelp ? (VRMiniDumpWriteDumpFn)GetProcAddress(hDbgHelp, "MiniDumpWriteDump") : NULL;
	if (pfnDump && szIni && szIni[0])
	{
		char szPath[320];
		strncpy(szPath, szIni, sizeof(szPath) - 1);
		szPath[sizeof(szPath) - 1] = 0;
		char* pSlash = strrchr(szPath, '\\');
		if (pSlash)
		{
			strcpy(pSlash + 1, "ShogoVR_crash.dmp");
			HANDLE hFile = CreateFileA(szPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				VRMiniDumpExceptionInfo mei;
				mei.ThreadId = GetCurrentThreadId();
				mei.ExceptionPointers = p;
				mei.ClientPointers = FALSE;
				if (pfnDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, 0 /* MiniDumpNormal */, &mei, NULL, NULL))
					g_VRStereo.Log("Crash dump saved as ShogoVR_crash.dmp");
				CloseHandle(hFile);
			}
		}
	}
	return s_pfnPrevFilter ? s_pfnPrevFilter(p) : EXCEPTION_CONTINUE_SEARCH;
}

static void VRInstallCrashReporter()
{
	if (!s_hVectored) s_hVectored = AddVectoredExceptionHandler(0, VRVectoredHandler);
	if (!s_bFilterInstalled)
	{
		s_pfnPrevFilter = SetUnhandledExceptionFilter(VRUnhandledFilter);
		s_bFilterInstalled = DTRUE;
	}
}

static void VRRemoveCrashReporter()
{
	if (s_hVectored) { RemoveVectoredExceptionHandler(s_hVectored); s_hVectored = NULL; }
	if (s_bFilterInstalled) { SetUnhandledExceptionFilter(s_pfnPrevFilter); s_bFilterInstalled = DFALSE; }
}

void CVRStereo::SetMechMode(DBOOL bMech, DBOOL bVehicle)
{
	if (bMech != m_bMech || bVehicle != m_bVehicle)
	{
		Log("Player mode: %s", !bMech ? "on foot" : (bVehicle ? "mech, vehicle mode" : "mech, walking"));
	}
	m_bMech = bMech;
	m_bVehicle = bVehicle;
}

DBOOL CVRStereo::EasyLadders()
{
	return m_vtEasyLadders.GetFloat(1.0f) != 0.0f;
}

static void VRSendScanCode(DDWORD nScan, DBOOL bExtended, DBOOL bUp)
{
	INPUT in;
	ZeroMemory(&in, sizeof(in));
	in.type = INPUT_KEYBOARD;
	in.ki.wScan = (WORD)nScan;
	in.ki.dwFlags = KEYEVENTF_SCANCODE | (bExtended ? KEYEVENTF_EXTENDEDKEY : 0) | (bUp ? KEYEVENTF_KEYUP : 0);
	SendInput(1, &in, sizeof(INPUT));
}

// Key name as the game's input system reports it -> keyboard scan code.
static DBOOL VRKeyNameToScan(const char* szName, DDWORD &nScan, DBOOL &bExtended)
{
	if (szName[0] == '#' && szName[1] == '#')			// "##30" style: the code itself
	{
		int n = atoi(szName + 2);
		if (n <= 0 || n > 255) return DFALSE;
		nScan = (DDWORD)(n & 0x7F);
		bExtended = (n & 0x80) ? DTRUE : DFALSE;
		return DTRUE;
	}

	static const struct { const char* szKey; DDWORD nScan; int bExt; } s_named[] =
	{
		{ "Left Shift", 0x2A, 0 }, { "Right Shift", 0x36, 0 }, { "Left Ctrl", 0x1D, 0 }, { "Right Ctrl", 0x1D, 1 },
		{ "Left Alt", 0x38, 0 }, { "Right Alt", 0x38, 1 }, { "Space", 0x39, 0 }, { "Enter", 0x1C, 0 },
		{ "Return", 0x1C, 0 }, { "Tab", 0x0F, 0 }, { "Caps Lock", 0x3A, 0 }, { "Backspace", 0x0E, 0 }, { 0, 0, 0 }
	};
	int i;
	for (i = 0; s_named[i].szKey; i++)
	{
		if (_stricmp(szName, s_named[i].szKey) == 0)
		{
			nScan = s_named[i].nScan;
			bExtended = s_named[i].bExt ? DTRUE : DFALSE;
			return DTRUE;
		}
	}

	// Everything else: DirectInput names keys the way Windows does.
	int nExt, nCode;
	char szKey[64];
	for (nExt = 0; nExt < 2; nExt++)
	{
		for (nCode = 1; nCode < 128; nCode++)
		{
			if (GetKeyNameTextA((nCode << 16) | (nExt << 24), szKey, sizeof(szKey)) > 0 && _stricmp(szKey, szName) == 0)
			{
				nScan = (DDWORD)nCode;
				bExtended = nExt ? DTRUE : DFALSE;
				return DTRUE;
			}
		}
	}
	return DFALSE;
}

DBOOL CVRStereo::FindCommandKey(int nCommand, DDWORD &nScan, DBOOL &bExtended)
{
	if (!m_pClientDE) return DFALSE;
	char szName[INPUTNAME_LEN];
	szName[0] = 0;

	DeviceBinding* pBindings = m_pClientDE->GetDeviceBindings(DEVICETYPE_KEYBOARD);
	DeviceBinding* p;
	for (p = pBindings; p && !szName[0]; p = p->pNext)
	{
		GameAction* pAction;
		for (pAction = p->pActionHead; pAction; pAction = pAction->pNext)
		{
			if (pAction->nActionCode == nCommand)
			{
				strncpy(szName, p->strTriggerName, sizeof(szName) - 1);
				szName[sizeof(szName) - 1] = 0;
				break;
			}
		}
	}
	if (pBindings) m_pClientDE->FreeDeviceBindings(pBindings);

	return szName[0] ? VRKeyNameToScan(szName, nScan, bExtended) : DFALSE;
}

void CVRStereo::RequestTransform()
{
	if (!m_pClientDE || m_bTransformKeyDown) return;

	DDWORD nScan = 0;
	DBOOL bExt = DFALSE;
	if (!FindCommandKey(COMMAND_ID_VEHICLETOGGLE, nScan, bExt))
	{
		if (!m_bWarnedNoTransformKey)
		{
			m_pClientDE->CPrint("VR: to transform with the controller, bind a key to 'Vehicle mode toggle' in Options > Keyboard.");
			m_bWarnedNoTransformKey = DTRUE;
		}
		return;
	}

	// Only into Shogo itself, never into another program.
	if (!m_nGameHwnd || GetForegroundWindow() != (HWND)m_nGameHwnd) return;

	VRSendScanCode(nScan, bExt, DFALSE);
	m_bTransformKeyDown	= DTRUE;
	m_nTransformScan	= nScan;
	m_bTransformExt		= bExt;
	m_nTransformUpTick	= GetTickCount() + 150;		// long enough for the game to see it

	Log("VR: transform key down (scan 0x%02X)", (unsigned)nScan);
}

void CVRStereo::ReleaseTransformKey()
{
	if (!m_bTransformKeyDown) return;
	VRSendScanCode(m_nTransformScan, m_bTransformExt, DTRUE);
	m_bTransformKeyDown = DFALSE;
}

// Press a key for the game and release it a few frames later, the way a human
// would.  Some commands (weapon switching, vehicle toggle) are only handled
// when the engine sees a real key press, so the controller taps their key.
void CVRStereo::InjectScanTap(DDWORD nScan, DBOOL bExt)
{
	if (!m_nGameHwnd || GetForegroundWindow() != (HWND)m_nGameHwnd) return;
	if (m_nInjScan) VRSendScanCode(m_nInjScan, m_bInjExt, DTRUE);		// release the previous tap
	VRSendScanCode(nScan, bExt, DFALSE);
	m_nInjScan		= nScan;
	m_bInjExt		= bExt;
	m_nInjUpTick	= GetTickCount() + 60;
}

void CVRStereo::InjectCommandTap(int nCommand)
{
	DDWORD nScan = 0;
	DBOOL bExt = DFALSE;
	if (FindCommandKey(nCommand, nScan, bExt)) InjectScanTap(nScan, bExt);
}

void CVRStereo::ReleaseInjectedKey()
{
	if (!m_nInjScan) return;
	if ((long)(GetTickCount() - m_nInjUpTick) < 0) return;
	VRSendScanCode(m_nInjScan, m_bInjExt, DTRUE);
	m_nInjScan = 0;
}

// Per frame: turn controller edges into game commands (in a level) or into
// menu keys (in the menus).  Continuous actions - fire, jump, crouch, movement
// and turning - are read straight from the buttons/sticks elsewhere.
// A tuning aid, OFF by default (VRNudgeKeys 1 to use it).
//
// Kept rather than deleted: it is how the shotgun's lent hand was seated, and
// the same job will come up again for the knife's off hand and for any weapon
// a later model dump turns out to need.  It costs one GetAsyncKeyState sweep
// per frame while off, and nothing else.
//
// The shotgun's lent hand has to be placed by eye, and the only way to do that
// so far has been to type a number, look, and type another.  These four keys
// move it live: q/e walk it back and forth along the barrel, r/u drop and
// raise it, f/y slide it sideways.  Every change is logged with all three
// values, so the set that lands
// it on the grip can be read straight out of ShogoVR_game.log and baked into
// DomHandSeat.
//
// Polled rather than hooked: CRiotClientShell::OnKeyDown is Monolith source,
// and a debug aid is not worth an engine edit and its modification note.
// GetAsyncKeyState does not consume the key, so whatever q/e/r/u are bound to
// still happens; VRNudgeKeys 0 switches this off.
void CVRStereo::UpdateNudgeKeys()
{
	if (!m_pClientDE || m_vtNudgeKeys.GetFloat(1.0f) == 0.0f) return;

	// The trims used to be cleared when handedness changed, because the
	// seating they rode on was per-mode and carrying a trim across broke the
	// other mode.  Both are a single shared value now that the offset lives in
	// the gun's frame, so a trim tuned in one mode is just as correct in the
	// other - switching keeps it, and a mode change is a free way to check it.
	m_nNudgeLastHand = m_bLeftHanded ? 1 : 0;

	DFLOAT fZ = 0.0f, fY = 0.0f, fX = 0.0f;
	if (GetAsyncKeyState('Q') & 0x8000) fZ -= 1.0f;		// back along the barrel
	if (GetAsyncKeyState('E') & 0x8000) fZ += 1.0f;		// forward
	if (GetAsyncKeyState('R') & 0x8000) fY -= 1.0f;		// down
	if (GetAsyncKeyState('U') & 0x8000) fY += 1.0f;		// up
	// F, not T: T opens the game's "mpmimimi" all-weapons cheat entry, and
	// these keys are polled rather than consumed, so T would do both.
	if (GetAsyncKeyState('F') & 0x8000) fX -= 1.0f;		// sideways, one way
	if (GetAsyncKeyState('Y') & 0x8000) fX += 1.0f;		// and the other

	if (fZ == 0.0f && fY == 0.0f && fX == 0.0f)
	{
		m_nNudgeNextTick = 0;			// released: the next press acts at once
		return;
	}

	DDWORD nNow = GetTickCount();
	if (m_nNudgeNextTick && (long)(nNow - m_nNudgeNextTick) < 0) return;
	m_nNudgeNextTick = nNow + 120;		// then repeats while held

	DFLOAT fStep = m_vtNudgeStep.GetFloat(0.1f);
	DFLOAT fNewZ = m_vtDomHandZ.GetFloat(0.0f) + fZ * fStep;
	DFLOAT fNewY = m_vtDomHandY.GetFloat(0.0f) + fY * fStep;
	DFLOAT fNewX = m_vtDomHandX.GetFloat(0.0f) + fX * fStep;

	char szCmd[96];
	if (fZ != 0.0f)
	{
		_snprintf(szCmd, sizeof(szCmd) - 1, "VRDomHandZ %g", fNewZ);
		szCmd[sizeof(szCmd) - 1] = 0;
		m_pClientDE->RunConsoleString(szCmd);
	}
	if (fY != 0.0f)
	{
		_snprintf(szCmd, sizeof(szCmd) - 1, "VRDomHandY %g", fNewY);
		szCmd[sizeof(szCmd) - 1] = 0;
		m_pClientDE->RunConsoleString(szCmd);
	}
	if (fX != 0.0f)
	{
		_snprintf(szCmd, sizeof(szCmd) - 1, "VRDomHandX %g", fNewX);
		szCmd[sizeof(szCmd) - 1] = 0;
		m_pClientDE->RunConsoleString(szCmd);
	}

	// Handedness is logged with it only as a check: all three axes are the
	// GUN's now, and the gun's frame does not flip between the modes, so a
	// value tuned in one should read correctly in the other.  If it ever does
	// not, the frame assumption is wrong and that is worth knowing.  Both the
	// trim and the seating it rides on are printed, so the log stays readable
	// now that zero no longer means "no offset".
	DFLOAT fSeatX, fSeatY, fSeatZ;
	DomHandSeat(fSeatX, fSeatY, fSeatZ);
	Log("VR nudge (%s-handed): trim X %g Y %g Z %g  ->  seated at X %g Y %g Z %g",
		m_bLeftHanded ? "left" : "right",
		fNewX, fNewY, fNewZ,
		fSeatX + fNewX, fSeatY + fNewY, fSeatZ + fNewZ);
}

// The packet exactly as the bridge sent it, a few seconds apart.  Printed as
// one line so a tester packet can be pasted straight back in, and so a device
// capture can be diffed against one by eye.
void CVRStereo::LogRawPacket()
{
	if (!m_pClientDE || !m_pXrApi) return;
	if (m_vtVerbose.GetFloat(0.0f) == 0.0f) { m_fNextRawLog = 0.0f; return; }

	DFLOAT fNow = m_pClientDE->GetTime();
	if (m_fNextRawLog > 0.0f && fNow < m_fNextRawLog) return;
	m_fNextRawLog = fNow + 5.0f;

	const char* szRaw = m_pXrApi->GetRawPacket();
	if (szRaw && szRaw[0]) Log("VR XrApi raw: %s", szRaw);
}

void CVRStereo::UpdateInput()
{
	ReleaseInjectedKey();
	UpdateNudgeKeys();
	LogRawPacket();
	if (m_bTransformKeyDown && (long)(GetTickCount() - m_nTransformUpTick) >= 0) ReleaseTransformKey();

	UpdateCrouchOffset();

	// Physical motion melee attacks (#3): detect a slash of the dominant hand
	// (any direction) while holding a melee weapon. If a gesture is detected,
	// fire once via COMMAND_ID_FIRING like a normal shot. Otherwise rely on
	// the trigger.
	if (m_bGameplayInput && IsControllerActive() && !m_bPlayerDead && IsMeleeWeaponEquipped())
	{
		DFLOAT fSens = m_vtMeleeSens.GetFloat(60.0f);
		if (fSens <= 0.0f)
		{
			m_bMeleeTrigger = DFALSE;
			m_bMeleeHandPrevValid = DFALSE;
		}
		else
		{
			CClientDE* pClientDE = m_pClientDE;
			DFLOAT fNow = pClientDE ? pClientDE->GetTime() : 0.0f;
			DFLOAT fDt = 0.016666f;
			if (pClientDE)
			{
				static DBOOL bFirstTime = DTRUE;
				static DBOOL bLastFrameValid = DFALSE;
				static DFLOAT fLastTime = 0.0f;
				if (bFirstTime || !bLastFrameValid)
				{
					fLastTime = fNow;
					bLastFrameValid = DTRUE;
					bFirstTime = DFALSE;
				}
				if (fNow >= fLastTime && (fNow - fLastTime) < 1.0f)
					fDt = fNow - fLastTime;
				fLastTime = fNow;
			}

			// Higher percentage = easier to trigger (smaller swing)
			if (fSens <= 10.0f) fSens = 10.0f;
			else if (fSens <= 15.0f) fSens = 15.0f;
			else if (fSens <= 25.0f) fSens = 25.0f;
			else if (fSens <= 30.0f) fSens = 30.0f;
			else if (fSens <= 40.0f) fSens = 40.0f;
			else if (fSens <= 60.0f) fSens = 60.0f;
			else fSens = 80.0f;
			DFLOAT fT = 1.0f - (fSens / 100.0f);
			DFLOAT fThreshVel = 0.7f + (fT * 3.0f);
			DFLOAT fCd = 0.1f + (fT * 0.18f);

			// m_vRightP is ALWAYS the dominant hand: the two controller poses
			// trade places at ingest in left-handed mode (see the swap where the
			// packet is read), so the rest of the mod can just say "right" and
			// mean "gun hand".  Picking m_vLeftP here applied that swap a SECOND
			// time, which is why left-handed players had to slash with their right
			// hand - the gesture was watching the off hand, on foot and in a mech
			// alike, since both go through this one test.
			//
			// The haptic below is different and correctly does test the flag:
			// m_fHapticL/R are physical motor channels on their way back to the
			// runtime, and those are never swapped.
			DVector vHandP;
			VEC_COPY(vHandP, m_vRightP);

			if (!m_bMeleeTrigger && (fNow - m_fMeleeLastFireTime) >= fCd)
			{
				if (m_bMeleeHandPrevValid)
				{
					DVector vVel;
					VEC_SUB(vVel, vHandP, m_vMeleeHandPrevP);
					DFLOAT fVel = VEC_MAG(vVel);
					DFLOAT fVelPerSec = fVel / (fDt > 0.000001f ? fDt : 0.016666f);
					if (fVelPerSec >= fThreshVel)
					{
						m_bMeleeTrigger = DTRUE;
						m_fMeleeLastFireTime = fNow;
						if (m_vtHaptics.GetFloat(1.0f) != 0.0f)
						{
							if (m_bLeftHanded) m_fHapticL = 2.0f;
							else m_fHapticR = 2.0f;
						}
					}
				}
			}

			VEC_COPY(m_vMeleeHandPrevP, vHandP);
			m_bMeleeHandPrevValid = DTRUE;
		}
	}
	else
	{
		m_bMeleeTrigger = DFALSE;
		m_bMeleeHandPrevValid = DFALSE;
	}

	if (!IsControllerActive())
	{
		m_bGameplayInput = DFALSE;
		m_bCrouchOn = DFALSE;
		m_bWantViewToggle = DFALSE;
		return;
	}

	DDWORD nPress = m_nPressed & ~m_nSuppressed;

	// Level start: once a level has loaded, wait for a moment of actually
	// playing (so any opening cutscene has ended), then quicksave so the left
	// trigger can reload it on death (#16).
	if (m_bAutosavePending)
	{
		if (m_bGameplayInput && !m_bPlayerDead)
		{
			if (++m_nAutosaveFrames >= 120)		// ~2 seconds
			{
				InjectCommandTap(COMMAND_ID_QUICKSAVE);
				m_bAutosavePending = DFALSE;
				m_nAutosaveFrames = 0;
			}
		}
		else m_nAutosaveFrames = 0;
	}

	if (m_bGameplayInput)
	{
		// Bring the menu up; from inside the menu the same button closes it.
		if (nPress & SHOGOVR_BTN_MENU) InjectScanTap(0x01, DFALSE);			// Esc

		// Weapon selection: the bridge maps its buttons here; the XrApi path
		// changes weapons with the right stick (PollWeaponCycle below), so no
		// button is mapped to next/previous weapon.
		if (nPress & SHOGOVR_BTN_NEXT_WEAPON) InjectCommandTap(COMMAND_ID_NEXT_WEAPON);

		// Left trigger: while dead it loads the quicksave, which keeps
		// precedence over everything else on this button - losing a save
		// reload to a grip toggle would be a bad trade.  While alive it takes
		// hold of a two-handed weapon, or lets go of one (#8); with the dual
		// pistols it does neither, because the Colt 45 is two separate guns and
		// this trigger is already its off pistol's.
		if ((nPress & SHOGOVR_BTN_LEFT_TRIGGER) && m_bPlayerDead)
			InjectCommandTap(COMMAND_ID_QUICKLOAD);
		else if (nPress & SHOGOVR_BTN_LEFT_TRIGGER)
			ToggleTwoHandGrip();

		// Right A: first/third person view.  The shell performs this directly
		// when it polls ConsumeViewToggle() - don't inject the game's bound key
		// here.  Shogo binds ChaseView to the letter B ("##48"), which is one of
		// the face keys WinlatorXR also leaks; an injected B is swallowed by the
		// face-key filter (so A did nothing) and the leaked B toggled the view
		// on the B button (#23/#31).
		if (nPress & SHOGOVR_BTN_VIEWTOGGLE)	m_bWantViewToggle = DTRUE;
		// Right stick click: recentre the view.
		if (nPress & SHOGOVR_BTN_RECENTER)		RequestRecenter();
		// Right B: transform vehicle/mech.
		if (nPress & SHOGOVR_BTN_TRANSFORM)		RequestTransform();
		// Left X: crouch on/off.  Left stick click is crouch while held
		// (read straight from the buttons in VRCommandOn).
		if (nPress & SHOGOVR_BTN_CROUCH)		m_bCrouchOn = m_bCrouchOn ? DFALSE : DTRUE;

		int nCycle = PollWeaponCycle();
		if (nCycle > 0)			InjectCommandTap(COMMAND_ID_NEXT_WEAPON);
		else if (nCycle < 0)	InjectCommandTap(COMMAND_ID_PREV_WEAPON);
	}
	else
	{
		// Menus: the sticks walk the list (as the arrow keys), the buttons
		// confirm and go back.
		int nDir = PollMenuDirection();
		if (nDir == VK_UP)			InjectScanTap(0x48, DTRUE);
		else if (nDir == VK_DOWN)	InjectScanTap(0x50, DTRUE);
		else if (nDir == VK_LEFT)	InjectScanTap(0x4B, DTRUE);
		else if (nDir == VK_RIGHT)	InjectScanTap(0x4D, DTRUE);

		if (m_nPressed & SHOGOVR_BTN_FIRE)		InjectScanTap(0x1C, DFALSE);	// Enter
		if (nPress & SHOGOVR_BTN_VIEWTOGGLE)	InjectScanTap(0x1C, DFALSE);	// Enter
		if (nPress & SHOGOVR_BTN_CROUCH)		InjectScanTap(0x0E, DFALSE);	// Backspace (back)
		if (nPress & SHOGOVR_BTN_MENU)			InjectScanTap(0x01, DFALSE);	// Esc
	}
}

// Right A: hand the first/third-person view toggle to the shell, which performs
// it directly (see CRiotClientShell::Update).  One-shot: clears on read.
DBOOL CVRStereo::ConsumeViewToggle()
{
	DBOOL b = m_bWantViewToggle;
	m_bWantViewToggle = DFALSE;
	return b;
}

void CVRStereo::SetBodyAnim(const char* szAnim, DBOOL bLoop)
{
	if (!m_hBody || strcmp(szAnim, m_szBodyAnim) == 0) return;

	HMODELANIM hAnim = m_pClientDE->GetAnimIndex(m_hBody, (char*)szAnim);
	if (hAnim == (HMODELANIM)-1)
	{
		// Not on this model - fall back to standing.
		hAnim = m_pClientDE->GetAnimIndex(m_hBody, (char*)"IR1");
		if (hAnim == (HMODELANIM)-1) return;
	}
	m_pClientDE->SetModelAnimation(m_hBody, hAnim);
	m_pClientDE->SetModelLooping(m_hBody, bLoop);
	strncpy(m_szBodyAnim, szAnim, sizeof(m_szBodyAnim) - 1);
	m_szBodyAnim[sizeof(m_szBodyAnim) - 1] = 0;
}

void CVRStereo::HideBody()
{
	if (m_pClientDE && m_hBody) m_pClientDE->SetObjectFlags(m_hBody, 0);
}

// Crouch state for the visible body (#20).  Shogo has no on-foot crouch physics
// (CMoveMgr never ducks), so the crouch controls - Left X toggles, left stick
// click holds - do not lower the camera; feeding this into UpdateBody() at
// least shows the player crouching.  Mirrors VRCommandOn(COMMAND_ID_DUCK).
DBOOL CVRStereo::IsCrouchOn()
{
	if (!m_bGameplayInput || !IsControllerActive()) return DFALSE;
	DDWORD nHeld = m_nButtons & ~m_nSuppressed;
	return (m_bCrouchOn || (nHeld & SHOGOVR_BTN_CROUCH_HOLD)) ? DTRUE : DFALSE;
}

// Crouch eye-height (#20).  The game never ducks on foot, so while the crouch
// controls are on we lower the HMD world position ourselves (applied in
// RenderStereo).  Eased so the drop/lift isn't jarring.
void CVRStereo::UpdateCrouchOffset()
{
	DFLOAT fTarget = IsCrouchOn() ? 1.0f : 0.0f;

	DFLOAT fFrameTime = m_pClientDE ? m_pClientDE->GetFrameTime() : 0.0f;
	if (fFrameTime < 0.0f) fFrameTime = 0.0f;
	if (fFrameTime > 0.1f) fFrameTime = 0.1f;

	DFLOAT k = fFrameTime * 8.0f;
	if (k > 1.0f) k = 1.0f;

	m_fCrouchAmt += (fTarget - m_fCrouchAmt) * k;
	if (m_fCrouchAmt < 0.001f) m_fCrouchAmt = 0.0f;
	if (m_fCrouchAmt > 0.999f) m_fCrouchAmt = 1.0f;
}

void CVRStereo::UpdateBody(HLOCALOBJ hPlayerObj, int nModelId, DBOOL bShow, DBOOL bCrouching)
{
	if (!m_pClientDE) return;
	CClientDE* pClientDE = m_pClientDE;

	bShow = bShow && hPlayerObj && m_vtBody.GetFloat(1.0f) != 0.0f && IsHeadTracking() && nModelId >= 0 &&
			!m_bVehicle;		// in vehicle mode the game shows its own mech model
	if (!bShow)
	{
		HideBody();
		m_bBodyVisYawSet = DFALSE;
		return;
	}

	// (Re)create when the player model changes (on foot, kid, each MCA).
	if (!m_hBody || nModelId != m_nBodyModelId)
	{
		if (m_hBody) pClientDE->DeleteObject(m_hBody);
		m_hBody = DNULL;

		ObjectCreateStruct theStruct;
		INIT_OBJECTCREATESTRUCT(theStruct);
		theStruct.m_ObjectType = OT_MODEL;
		strncpy(theStruct.m_Filename, GetModel((DBYTE)nModelId), sizeof(theStruct.m_Filename) - 1);
		strncpy(theStruct.m_SkinName, GetSkin((DBYTE)nModelId, UCA, MS_NORMAL, DFALSE), sizeof(theStruct.m_SkinName) - 1);
		theStruct.m_Flags = FLAG_VISIBLE | FLAG_MODELGOURAUDSHADE;
		pClientDE->GetObjectPos(hPlayerObj, &theStruct.m_Pos);
		m_hBody = pClientDE->CreateObject(&theStruct);
		if (!m_hBody) return;

		m_nBodyModelId = nModelId;
		m_szBodyAnim[0] = 0;
		VEC_COPY(m_vBodyLastPos, theStruct.m_Pos);
		VEC_INIT(m_vBodyVel);
		m_fBodyMaxSpeed = 1.0f;
		m_bBodyArmsShown = (m_vtBodyArms.GetFloat(0.0f) != 0.0f);
		m_nBodyHidden = HideModelNodes(m_hBody, DTRUE, !m_bBodyArmsShown);
	}

	DBOOL bArms = (m_vtBodyArms.GetFloat(0.0f) != 0.0f);
	if (bArms != m_bBodyArmsShown)
	{
		m_bBodyArmsShown = bArms;
		m_nBodyHidden = HideModelNodes(m_hBody, DTRUE, !bArms);
	}

	// Same size as the real player model.
	DVector vScale;
	VEC_SET(vScale, 1.0f, 1.0f, 1.0f);
	pClientDE->GetObjectScale(hPlayerObj, &vScale);
	pClientDE->SetObjectScale(m_hBody, &vScale);

	// Which way the body faces: follows your head, but only once you've
	// turned it more than ~35 degrees (like turning your shoulders).
	DFLOAT fHeadWorldYaw = m_fBodyYaw + m_fHeadYaw;
	if (!m_bBodyVisYawSet) { m_fBodyVisYaw = fHeadWorldYaw; m_bBodyVisYawSet = DTRUE; }
	DFLOAT fDiff = fHeadWorldYaw - m_fBodyVisYaw;
	while (fDiff >  VR_PI) fDiff -= 2.0f * VR_PI;
	while (fDiff < -VR_PI) fDiff += 2.0f * VR_PI;
	const DFLOAT fFree = 0.6f;
	if (fDiff >  fFree) m_fBodyVisYaw += fDiff - fFree;
	if (fDiff < -fFree) m_fBodyVisYaw += fDiff + fFree;

	// Movement: velocity of the real player object (smoothed).
	DVector vPos, vStep;
	pClientDE->GetObjectPos(hPlayerObj, &vPos);
	DFLOAT fDt = pClientDE->GetFrameTime();
	if (fDt > 0.0001f && fDt < 0.5f)
	{
		VEC_SUB(vStep, vPos, m_vBodyLastPos);
		VEC_MULSCALAR(vStep, vStep, 1.0f / fDt);
		DFLOAT k = fDt * 10.0f;
		if (k > 1.0f) k = 1.0f;
		m_vBodyVel.x += (vStep.x - m_vBodyVel.x) * k;
		m_vBodyVel.y += (vStep.y - m_vBodyVel.y) * k;
		m_vBodyVel.z += (vStep.z - m_vBodyVel.z) * k;
	}
	VEC_COPY(m_vBodyLastPos, vPos);

	DFLOAT fSin = (DFLOAT)sin(m_fBodyVisYaw), fCos = (DFLOAT)cos(m_fBodyVisYaw);
	DFLOAT fFwd = m_vBodyVel.x * fSin + m_vBodyVel.z * fCos;		// along the body's facing
	DFLOAT fSide = m_vBodyVel.x * fCos - m_vBodyVel.z * fSin;		// to its right
	DFLOAT fSpeed = (DFLOAT)sqrt(fFwd * fFwd + fSide * fSide);
	m_fBodyMaxSpeed *= 0.9995f;
	if (fSpeed > m_fBodyMaxSpeed) m_fBodyMaxSpeed = fSpeed;

	// Pick an animation - the same ones the game uses for characters.
	DBOOL bInAir = ((DFLOAT)fabs(m_vBodyVel.y) > 0.6f * m_fBodyMaxSpeed && m_fBodyMaxSpeed > 1.0f);
	DBOOL bMoving = fSpeed > 0.08f * m_fBodyMaxSpeed && fSpeed > 1.0f;
	DBOOL bRunning = fSpeed > 0.65f * m_fBodyMaxSpeed;
	int nDir = 0;									// 0 forward, 1 back, 2 left, 3 right
	if ((DFLOAT)fabs(fSide) > (DFLOAT)fabs(fFwd)) nDir = (fSide > 0.0f) ? 3 : 2;
	else nDir = (fFwd >= 0.0f) ? 0 : 1;

	static const char* s_walk[4]	= { "WR", "WRB", "WSLR", "WSRR" };
	static const char* s_run[4]		= { "RR", "RRB", "RSLR", "RSRR" };
	static const char* s_crouch[4]	= { "CWR", "CWRB", "CSLR", "CSRR" };

	if (bInAir)						SetBodyAnim(m_vBodyVel.y > 0.0f ? "JUMP_UP" : "JUMP_DOWN", DFALSE);
	else if (bCrouching && bMoving)	SetBodyAnim(s_crouch[nDir], DTRUE);
	else if (bCrouching)			SetBodyAnim("CR", DTRUE);
	else if (bMoving && bRunning)	SetBodyAnim(s_run[nDir], DTRUE);
	else if (bMoving)				SetBodyAnim(s_walk[nDir], DTRUE);
	else							SetBodyAnim("IR1", DTRUE);

	// Place it where the real player model is, nudged back so you look
	// down at your chest and legs rather than out of your own neck.
	DFLOAT fBack = m_vtBodyOffset.GetFloat(0.15f) * m_fWorldScaleUsed;
	DVector vBody;
	VEC_COPY(vBody, vPos);
	vBody.x -= fSin * fBack;
	vBody.z -= fCos * fBack;

	DRotation rRot;
	pClientDE->SetupEuler(&rRot, 0.0f, m_fBodyVisYaw, 0.0f);
	pClientDE->SetObjectPos(m_hBody, &vBody);
	pClientDE->SetObjectRotation(m_hBody, &rRot);
	pClientDE->SetObjectFlags(m_hBody, FLAG_VISIBLE | FLAG_MODELGOURAUDSHADE);
}

// "VRBodyInfo": lists the parts (nodes) of your body model and of the gun
// model, so hiding can be tuned for models whose parts are named differently.
static void PrintNodes(CClientDE* pClientDE, HLOCALOBJ hObj, const char* szWhat)
{
	if (!hObj) { pClientDE->CPrint("VR: %s: none", szWhat); return; }
	char szLine[512];
	szLine[0] = 0;
	int nCount = 0;
	HMODELNODE hNode = INVALID_MODEL_NODE;
	while (pClientDE->GetNextModelNode(hObj, hNode, &hNode) == LT_OK)
	{
		char szName[64];
		szName[0] = 0;
		pClientDE->GetModelNodeName(hObj, hNode, szName, sizeof(szName));
		DBOOL bHidden = DFALSE;
		pClientDE->GetModelNodeHideStatus(hObj, szName, &bHidden);
		char szItem[80];
		_snprintf(szItem, sizeof(szItem) - 1, "%s%s%s", nCount ? ", " : "", szName, bHidden ? "(hidden)" : "");
		szItem[sizeof(szItem) - 1] = 0;
		if (strlen(szLine) + strlen(szItem) > 120)
		{
			pClientDE->CPrint("VR: %s: %s", szWhat, szLine);
			szLine[0] = 0;
		}
		strcat(szLine, szItem);
		nCount++;
	}
	pClientDE->CPrint("VR: %s: %s (%d nodes)", szWhat, szLine, nCount);
}

void CVRStereo::PrintBodyInfo(HLOCALOBJ hWeaponModel)
{
	if (!m_pClientDE) return;
	m_pClientDE->CPrint("VR: body %s, animation %s, %d parts hidden, two-handed %s",
						m_hBody ? "on" : "off", m_szBodyAnim[0] ? m_szBodyAnim : "-", m_nBodyHidden,
						m_bTwoHanded ? "yes" : "no");
	PrintNodes(m_pClientDE, m_hBody, "body parts");
	PrintNodes(m_pClientDE, hWeaponModel, "gun parts");
}


// ======================================================================= //
//	Held gun: the third-person weapon model (no arms) in your hand
// ======================================================================= //

// The models other players see you holding (from the game's weapon
// definitions on the server side).  Their origin is the grip.
// Shogo's weapon numbers (RiotWeaponId in Shared/WeaponDefs.h), repeated here
// so this file doesn't need that header - its inline functions don't compile
// the same way in every copy of the source release.
enum
{
	VRGUN_PULSERIFLE      = 0,
	VRGUN_LASERCANNON     = 1,
	VRGUN_SPIDER          = 2,
	VRGUN_BULLGUT         = 3,
	VRGUN_SNIPERRIFLE     = 4,
	VRGUN_JUGGERNAUT      = 5,
	VRGUN_SHREDDER        = 6,
	VRGUN_REDRIOT         = 8,
	VRGUN_ENERGYBATON     = 9,
	VRGUN_ENERGYBLADE     = 10,
	VRGUN_KATANA          = 11,
	VRGUN_MONOKNIFE       = 12,
	VRGUN_COLT45          = 13,
	VRGUN_SHOTGUN         = 14,
	VRGUN_ASSAULTRIFLE    = 15,
	VRGUN_ENERGYGRENADE   = 16,
	VRGUN_KATOGRENADE     = 17,
	VRGUN_MAC10           = 18,
	VRGUN_TOW             = 19,
	VRGUN_TANTO           = 22,
};

struct HeldGunModel { int nId; const char* szModel; const char* szSkin; };
static const HeldGunModel s_HeldGuns[] =
{
	{ VRGUN_PULSERIFLE,	"Models\\Powerups\\PulseRifle.abc",		"Skins\\Powerups\\PulseRifle_a.dtx" },
	{ VRGUN_SHREDDER,		"Models\\Powerups\\Shredder.abc",		"Skins\\Powerups\\Shredder_a.dtx" },
	{ VRGUN_BULLGUT,		"Models\\Powerups\\Bullgut.abc",		"Skins\\Powerups\\Bullgut_a.dtx" },
	{ VRGUN_JUGGERNAUT,	"Models\\Powerups\\Juggernaut.abc",		"Skins\\Powerups\\Juggernaut_a.dtx" },
	{ VRGUN_SPIDER,		"Models\\Powerups\\Spider.abc",			"Skins\\Powerups\\Spider_a.dtx" },
	{ VRGUN_REDRIOT,		"Models\\Powerups\\RedRiot.abc",		"Skins\\Powerups\\RedRiot_a.dtx" },
	{ VRGUN_SNIPERRIFLE,	"Models\\Powerups\\SniperRifle.abc",	"Skins\\Powerups\\SniperRifle_a.dtx" },
	{ VRGUN_ENERGYBATON,	"Models\\Powerups\\EnergyBaton.abc",	"Skins\\Powerups\\EnergyBaton.dtx" },
	{ VRGUN_ENERGYBLADE,	"Models\\Powerups\\EnergyBlade.abc",	"Skins\\Powerups\\EnergyBlade.dtx" },
	{ VRGUN_KATANA,		"Models\\Powerups\\Katana.abc",			"Skins\\Powerups\\Katana.dtx" },
	{ VRGUN_MONOKNIFE,		"Models\\Powerups\\MonoKnife.abc",		"Skins\\Powerups\\MonoKnife.dtx" },
	{ VRGUN_COLT45,		"Models\\Powerups\\Colt45.abc",			"Skins\\Powerups\\Colt45_a.dtx" },
	{ VRGUN_SHOTGUN,		"Models\\Powerups\\Shotgun.abc",		"Skins\\Powerups\\Shotgun_a.dtx" },
	{ VRGUN_MAC10,			"Models\\Powerups\\Machinegun.abc",		"Skins\\Powerups\\Machinegun_a.dtx" },
	{ VRGUN_ASSAULTRIFLE,	"Models\\Powerups\\AssaultRifle.abc",	"Skins\\Powerups\\AssaultRifle_a.dtx" },
	{ VRGUN_ENERGYGRENADE,	"Models\\Powerups\\EnergyGrenade.abc",	"Skins\\Powerups\\EnergyGrenade_a.dtx" },
	{ VRGUN_TOW,			"Models\\Powerups\\TOW.abc",			"Skins\\Powerups\\TOW_a.dtx" },
	{ VRGUN_LASERCANNON,	"Models\\Powerups\\LaserCannon.abc",	"Skins\\Powerups\\LaserCannon_a.dtx" },
	{ VRGUN_KATOGRENADE,	"Models\\Powerups\\KatoGrenade.abc",	"Skins\\Powerups\\KatoGrenade_a.dtx" },
	{ VRGUN_TANTO,			"Models\\Powerups\\Tanto.abc",			"Skins\\Powerups\\Tanto_a.dtx" },
	{ -1, 0, 0 }
};

// A screenshot cannot reliably say which weapon is in shot - the shotgun and
// the assault rifle read as the same gun at the wrong angle, and two fixes
// have already been aimed at the wrong weapon because of it.  So the game says
// so itself: one line each time the weapon changes, naming the model file and
// every decision the mod made about that weapon's hands.
void CVRStereo::LogWeaponHands(HLOCALOBJ hPVModel, int nWeaponId)
{
	// Deliberately checked before m_nLoggedWeapon is updated, so switching
	// VRVerboseDebug on reports the weapon already in hand rather than staying
	// silent until the next swap.
	if (m_vtVerbose.GetFloat(0.0f) == 0.0f) return;

	if (nWeaponId == m_nLoggedWeapon) return;
	m_nLoggedWeapon = nWeaponId;
	if (!hPVModel) return;

	char* szModel = GetPVModelName(nWeaponId);
	DBOOL bSwap   = (DBOOL)(m_vtOffHandSwap.GetFloat(0.0f) != 0.0f);
	int   nFwd    = (m_vtOffHandAuto.GetFloat(1.0f) != 0.0f) ? ForwardHandGroup(hPVModel) : -1;
	if (nFwd >= 0) bSwap = (DBOOL)(nFwd == 0);

	Log("VR hands: weapon %d (%s) - hand groups A=%d B=%d; two-handed=%d split-off-hand=%d lent-dominant=%d; keeping group %c on the gun, moving group %c to the off hand",
		nWeaponId,
		szModel ? szModel : "?",
		CountHandGroupNodes(hPVModel, 0),
		CountHandGroupNodes(hPVModel, 1),
		(int)IsTwoHandWeapon(nWeaponId),
		(int)HasSplitOffHand(nWeaponId),
		(int)(nWeaponId == GUN_SHOTGUN_ID),
		bSwap ? 'B' : 'A',
		bSwap ? 'A' : 'B');
	if (nWeaponId == GUN_SHOTGUN_ID)
	{
		// The lent hand is seated in the GUN's frame, so both handedness modes
		// should want the same offset - yet they do not (right 5.6/-5.4/-3.0,
		// left -7.5/-4.2/-2.2).  If the gun's own basis differs between the
		// modes, that is the reason, and this is the line that will say so.
		DRotation rGun;
		DVector gU, gR, gF;
		m_pClientDE->GetObjectRotation(hPVModel, &rGun);
		m_pClientDE->GetRotationVectors(&rGun, &gU, &gR, &gF);
		Log("VR hands: shotgun basis (%s-handed) R %.3f %.3f %.3f  U %.3f %.3f %.3f  F %.3f %.3f %.3f",
			m_bLeftHanded ? "left" : "right",
			gR.x, gR.y, gR.z, gU.x, gU.y, gU.z, gF.x, gF.y, gF.z);

		// Everything the lent hand's placement depends on, so a desktop run and
		// a device run can be compared line for line.  The seating is in the
		// gun's frame and so should transfer between them; if the hand lands
		// differently, one of these differs and this says which.
		DFLOAT fSX, fSY, fSZ;
		DomHandSeat(fSX, fSY, fSZ);
		DVector vGunPos, vPalm;
		DRotation rTmp;
		m_pClientDE->GetObjectPos(hPVModel, &vGunPos);
		DBOOL bPalm = m_pClientDE->GetModelNodeTransform(hPVModel, (char*)"Right_Palm4", &vPalm, &rTmp);
		Log("VR hands: shotgun seat %.2f %.2f %.2f + trim %.2f %.2f %.2f, gun scale %.3f, gun pos %.1f %.1f %.1f, own palm %s %.1f %.1f %.1f",
			fSX, fSY, fSZ,
			m_vtDomHandX.GetFloat(0.0f), m_vtDomHandY.GetFloat(0.0f), m_vtDomHandZ.GetFloat(0.0f),
			m_fGunScaleUsed, vGunPos.x, vGunPos.y, vGunPos.z,
			bPalm ? "at" : "MISSING", vPalm.x, vPalm.y, vPalm.z);
	}

	Log("VR hands: weapon %d - fore hand %s", nWeaponId,
		(nFwd < 0) ? "could not be measured, using VROffHandSwap" :
		(nFwd == 0) ? "measured as group A" : "measured as group B");
}

void CVRStereo::UpdateHeldGun(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	m_nCurrentWeaponId = nWeaponId;
	LogWeaponHands(hPVModel, nWeaponId);
	if (!m_pClientDE) return;
	CClientDE* pClientDE = m_pClientDE;

	// Only when the hand aims, the option is on, and the game is actually
	// showing a gun right now (not mid-switch, not with view weapons off).
	DDWORD dwPVFlags = hPVModel ? pClientDE->GetObjectFlags(hPVModel) : 0;
	const HeldGunModel* pDef = DNULL;
	int i;
	for (i = 0; s_HeldGuns[i].nId >= 0; i++) if (s_HeldGuns[i].nId == nWeaponId) pDef = &s_HeldGuns[i];

	// The first-person model (with its arms) stays in place and "visible" as
	// far as the game is concerned - it still drives the muzzle flash and
	// where shots come from - but every part of it is hidden.  (Clearing
	// its visible flag instead would also hide the muzzle flash.)
	DBOOL bUseHeld = m_bAimByHand && m_vtGunModel.GetFloat(1.0f) != 0.0f && pDef;
	// Re-applied every frame: the game makes a new model on each weapon
	// switch, and we never touch an old (possibly deleted) one.
	if (bUseHeld)
	{
		if (hPVModel) SetAllNodesHidden(hPVModel, DTRUE);
		m_hHiddenPV = hPVModel;
	}
	else if (m_hHiddenPV)
	{
		if (m_hHiddenPV == hPVModel) SetAllNodesHidden(hPVModel, DFALSE);
		m_hHiddenPV = DNULL;
	}

	// First-person gun: list its parts once (ShogoVR_models.txt), and hide
	// Sanjuro's arms on it unless they're wanted.
	//
	// The dump is part of VRVerboseDebug, so an ordinary session writes no file
	// at all.  The "already dumped" bit is only set when the dump actually
	// happens, so switching verbose on mid-session still lists the weapons from
	// there on rather than finding them all ticked off.
	if (m_vtVerbose.GetFloat(0.0f) != 0.0f &&
		hPVModel && nWeaponId >= 0 && nWeaponId < 64 && !(m_nDumpedWeapons[nWeaponId / 32] & (1u << (nWeaponId % 32))))
	{
		m_nDumpedWeapons[nWeaponId / 32] |= (1u << (nWeaponId % 32));
		DumpModelNodes(hPVModel, nWeaponId);
	}
	// Dual pistols (#5): split the Colt 45's two halves between the hands.
	// Runs after the held-gun hiding above so it can tell whether the
	// first-person model is the one actually on show.
	UpdateDualPistols(hPVModel, nWeaponId, vCamPos);

	// Free off arm (#7): the submachine gun's spare arm follows the off
	// controller.  After the pistols, because the two share one copy object and
	// this one stands down whenever that split is live.
	UpdateOffArm(hPVModel, nWeaponId, vCamPos);

	// Weapons whose model is missing a hand get a borrowed one, so the player
	// never has a controller with nothing on it.
	UpdateDominantHand(hPVModel, nWeaponId, vCamPos);
	UpdateFreeOffHand(hPVModel, nWeaponId, vCamPos);

	// ...and when it is split, the "arms" are half of each pistol's own model:
	// hiding them would take away the hand holding the gun.
	DBOOL bHideArms = !bUseHeld && !m_bPistolSplit && m_bAimByHand && m_vtGunArms.GetFloat(1.0f) == 0.0f;
	if (bHideArms && hPVModel)
	{
		SetArmNodesHidden(hPVModel, DTRUE);				// every frame: new model on each weapon switch
		m_hArmlessPV = hPVModel;
	}
	else if (m_hArmlessPV)
	{
		if (m_hArmlessPV == hPVModel && !bUseHeld) SetArmNodesHidden(hPVModel, DFALSE);
		m_hArmlessPV = DNULL;
	}

	DBOOL bShow = bUseHeld && (dwPVFlags & FLAG_VISIBLE);
	if (!bShow)
	{
		if (m_hHeldGun) pClientDE->SetObjectFlags(m_hHeldGun, 0);
		return;
	}

	if (!m_hHeldGun || nWeaponId != m_nHeldGunId)
	{
		if (m_hHeldGun) pClientDE->DeleteObject(m_hHeldGun);
		m_hHeldGun = DNULL;

		ObjectCreateStruct theStruct;
		INIT_OBJECTCREATESTRUCT(theStruct);
		theStruct.m_ObjectType = OT_MODEL;
		strncpy(theStruct.m_Filename, pDef->szModel, sizeof(theStruct.m_Filename) - 1);
		strncpy(theStruct.m_SkinName, pDef->szSkin, sizeof(theStruct.m_SkinName) - 1);
		theStruct.m_Flags = FLAG_VISIBLE | FLAG_MODELGOURAUDSHADE;
		VEC_COPY(theStruct.m_Pos, vCamPos);
		m_hHeldGun = pClientDE->CreateObject(&theStruct);
		if (!m_hHeldGun) return;
		m_nHeldGunId = nWeaponId;
	}

	// In your hand, pointing where you aim (one or two hands).
	DVector vHandPos, vHandF, vHandU, hU, hR, hF, vT;
	GetHandWorld(vCamPos, vHandPos, vHandF, vHandU);

	DRotation rRot;
	pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
	DFLOAT fPitch = m_vtHeldPitch.GetFloat(0.0f), fYaw = m_vtHeldYaw.GetFloat(0.0f), fRoll = m_vtHeldRoll.GetFloat(0.0f);
	if (fYaw != 0.0f)	pClientDE->EulerRotateY(&rRot, VR_DEG2RAD(fYaw));
	if (fPitch != 0.0f)	pClientDE->EulerRotateX(&rRot, VR_DEG2RAD(fPitch));
	if (fRoll != 0.0f)	pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));

	pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);
	VEC_MULSCALAR(vT, hR, m_vtHeldX.GetFloat(0.0f));	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hU, m_vtHeldY.GetFloat(0.0f));	VEC_ADD(vHandPos, vHandPos, vT);
	VEC_MULSCALAR(vT, hF, m_vtHeldZ.GetFloat(0.0f));	VEC_ADD(vHandPos, vHandPos, vT);

	DFLOAT fScale = m_vtHeldScale.GetFloat(1.0f);
	if (fScale < 0.1f) fScale = 0.1f;
	DVector vScale;
	VEC_SET(vScale, fScale, fScale, fScale);

	pClientDE->SetObjectPos(m_hHeldGun, &vHandPos);
	pClientDE->SetObjectRotation(m_hHeldGun, &rRot);
	pClientDE->SetObjectScale(m_hHeldGun, &vScale);
	pClientDE->SetObjectFlags(m_hHeldGun, FLAG_VISIBLE | FLAG_MODELGOURAUDSHADE);
}

// Muzzle flash placement (#29): the game puts its flash sprite at the
// flat-screen muzzle - up near the camera - but VR holds the gun in the hand,
// so the flash ends up above and behind the barrel.  Work out where the visible
// gun's barrel actually ends, in world space, so the shell can draw the sprite
// there.  Returns DFALSE when VR isn't posing the gun by hand, so the shell
// keeps the game's own position and the flat game is unchanged.
DBOOL CVRStereo::GetMuzzleFlashWorld(int nWeaponId, const DVector &vCamPos, DBOOL bFootScale, DVector &vOut, int nFlashState)
{
	if (!m_pClientDE || !m_bAimByHand) return DFALSE;

	// Dual pistols: the stock pair alternates barrels and reports which one
	// fired as W_FIRED / W_FIRED2, which is why that state is passed in (#41).
	// Note it is the fire ANIMATION that alternates, not the player's hands -
	// so once the pair is split between the hands (#5) the state says nothing
	// about which pistol fired and the mod's own answer is used instead.

	// Is the mod drawing its own third-person gun for this weapon?  (Same test
	// as UpdateHeldGun - keep the two in step.)
	const HeldGunModel* pDef = DNULL;
	int i;
	for (i = 0; s_HeldGuns[i].nId >= 0; i++) if (s_HeldGuns[i].nId == nWeaponId) pDef = &s_HeldGuns[i];
	DBOOL bHeld = m_vtGunModel.GetFloat(1.0f) != 0.0f && pDef;

	DVector vPos, vR, vU, vF, vT;
	DFLOAT fScale;
	DVector vHandPos, vHandF, vHandU;

	// Dual pistols (#5): the flash belongs on the pistol that fired, so it is
	// placed from that hand's own pose.  Hand 0 is the gun hand, whose model
	// the game poses itself; hand 1 is the off hand, holding our copy.
	// Default to the gun hand rather than -1: a flash drawn before the first
	// granted shot must still be treated as a split pistol, or it would get
	// the stock pair's barrel spread applied on top of a hand origin.
	int nPistolHand = -1;
	if (m_bPistolSplit) nPistolHand = (m_nPistolFireHand >= 0) ? m_nPistolFireHand : 0;
	if (nPistolHand == 1)	GetHandWorldFrom(vCamPos, m_vLeftP, m_vLeftF, m_vLeftU, vHandPos, vHandF, vHandU);
	else					GetHandWorld(vCamPos, vHandPos, vHandF, vHandU);

	DRotation rRot;

	if (bHeld)
	{
		// Mirror the placement UpdateHeldGun() gives the held gun.
		m_pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
		DFLOAT fPitch = m_vtHeldPitch.GetFloat(0.0f), fYaw = m_vtHeldYaw.GetFloat(0.0f), fRoll = m_vtHeldRoll.GetFloat(0.0f);
		if (fYaw != 0.0f)	m_pClientDE->EulerRotateY(&rRot, VR_DEG2RAD(fYaw));
		if (fPitch != 0.0f)	m_pClientDE->EulerRotateX(&rRot, VR_DEG2RAD(fPitch));
		if (fRoll != 0.0f)	m_pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fRoll));
		m_pClientDE->GetRotationVectors(&rRot, &vU, &vR, &vF);

		VEC_COPY(vPos, vHandPos);
		VEC_MULSCALAR(vT, vR, m_vtHeldX.GetFloat(0.0f)); VEC_ADD(vPos, vPos, vT);
		VEC_MULSCALAR(vT, vU, m_vtHeldY.GetFloat(0.0f)); VEC_ADD(vPos, vPos, vT);
		VEC_MULSCALAR(vT, vF, m_vtHeldZ.GetFloat(0.0f)); VEC_ADD(vPos, vPos, vT);

		fScale = m_vtHeldScale.GetFloat(1.0f);
		if (fScale < 0.1f) fScale = 0.1f;
	}
	else
	{
		// Mirror the placement AdjustWeaponPose() gives the first-person model -
		// or, for the off-hand pistol, the placement UpdateDualPistols gives
		// our own copy, which is the same plus its own roll and trim.
		m_pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
		if (nPistolHand == 1)
		{
			DFLOAT fOffRoll = m_vtPistolOffRoll.GetFloat(0.0f);
			if (fOffRoll != 0.0f) m_pClientDE->EulerRotateZ(&rRot, VR_DEG2RAD(fOffRoll));
		}
		m_pClientDE->GetRotationVectors(&rRot, &vU, &vR, &vF);

		fScale = bFootScale ? m_vtGunScale.GetFloat(1.5f) : m_vtGunScaleMCA.GetFloat(1.0f);
		if (fScale < 0.2f) fScale = 0.2f;

		DFLOAT fOffX = 0.0f, fOffY = 0.0f, fOffZ = 0.0f;
		if (nPistolHand == 1)
		{
			fOffX = m_vtPistolOffX.GetFloat(0.0f);
			fOffY = m_vtPistolOffY.GetFloat(0.0f);
			fOffZ = m_vtPistolOffZ.GetFloat(0.0f);
		}

		VEC_COPY(vPos, vHandPos);
		VEC_MULSCALAR(vT, vR, (m_vtGunX.GetFloat(0.0f) + fOffX) * fScale); VEC_ADD(vPos, vPos, vT);
		VEC_MULSCALAR(vT, vU, (m_vtGunY.GetFloat(0.0f) + fOffY) * fScale); VEC_ADD(vPos, vPos, vT);
		VEC_MULSCALAR(vT, vF, (m_vtGunZ.GetFloat(0.0f) + fOffZ) * fScale); VEC_ADD(vPos, vPos, vT);
	}

	// The muzzle offset is in the model's own frame (x right, y up, z forward)
	// and the model is scaled about its origin, so scale the offset too.
	// Use the fired state (W_FIRED or W_FIRED2) to position correctly for alternating barrels (#41)
	int nState = (nFlashState == W_FIRED2) ? W_FIRED2 : W_FIRED;
	DVector vMuzzle = GetWeaponMuzzleOffset((RiotWeaponId)nWeaponId, (WeaponState)nState);

	// #41: the held gun is the mod's own model, not the one the game's muzzle
	// table was measured for, so the table's offset is only a starting point.
	// Forward/back (z) and up/down (y) are corrected for the DUAL PISTOLS only
	// (#46) - the flash should sit a little lower and further out than the
	// table puts it, but only just, and far less than the old global nudge
	// did.  Every other weapon keeps the table's own y/z untouched - see the
	// #46 split below.
	// Left/right (x) is different per weapon:
	//   * The **dual pistols** (Colt 45) are the only two-barrel weapon, and the
	//     owner wants **one flash per hand**: the table's x is what puts barrel 1
	//     on the left and barrel 2 on the right, so it is kept and only its
	//     *spread* is scaled down (VRPistolSpread < 1) - on device the mod's held
	//     model put the two flashes a little too far apart.  Scaling both values
	//     about the centre keeps them on either side of the barrel axis; the bug
	//     was feeding both barrels the *same* x, which the spread never does.
	//   * Every other weapon is single-barrel, so its table x was only ever
	//     barrel-selection noise; it is replaced by one constant
	//     (VRMuzzleLateral) so the sprite sits on the barrel axis.
	// NOTE: this applies in **both** branches above.  The mod may be posing the
	// third-person held gun (VRGunModel=1) *or* the first-person model with arms
	// (VRGunModel=0, the default) - either way the flash is drawn on the gun the
	// player sees, so the corrections must not be tied to one of them.  (They
	// used to be, which is why a device run with VRGunModel=0 saw no change.)
	// #5: once the pair is split, the origin above IS the firing hand, so the
	// table's left/right value must NOT be applied on top - doing both puts
	// the left pistol's flash out past the left of the left hand.  A split
	// pistol is therefore treated like every other single-barrel weapon and
	// its flash sits on the barrel axis.  VRPistolSpread still scales the
	// stock pair, where one model holds both barrels and the table's x is
	// what tells them apart.
	if (nWeaponId == GUN_COLT45_ID && nPistolHand < 0)	vMuzzle.x *= m_vtPistolSpread.GetFloat(0.63f);
	else												vMuzzle.x  = m_vtMuzzleLateral.GetFloat(0.0f);

	// #46: only the DUAL PISTOLS get a y/z correction - and a small one
	// (x0.5 of VRFlashLower down, x0.3 of VRFlashOffset forward).  Every OTHER
	// weapon skips the correction entirely and keeps the muzzle table's own
	// y/z: the flash sits exactly where the game puts it, which is how all
	// non-pistol weapons looked before this tuning.  (The old GLOBAL
	// correction - the commented lines below - was what broke them.)
	if (nWeaponId == GUN_COLT45_ID)
	{
		vMuzzle.y = -(m_vtFlashLower.GetFloat(1.16f) * 0.5f);
		vMuzzle.z += (m_vtFlashOffset.GetFloat(0.5f) * 0.3f);
	}
	//vMuzzle.y = -m_vtFlashLower.GetFloat(2.4f);
	//vMuzzle.z += m_vtFlashOffset.GetFloat(2.0f);

	VEC_MULSCALAR(vT, vR, vMuzzle.x * fScale); VEC_ADD(vOut, vPos, vT);
	VEC_MULSCALAR(vT, vU, vMuzzle.y * fScale); VEC_ADD(vOut, vOut, vT);
	VEC_MULSCALAR(vT, vF, vMuzzle.z * fScale); VEC_ADD(vOut, vOut, vT);

	// One line per weapon, with the offset that was applied, so a device run
	// can tune VRFlashOffset / VRFlashLower / VRMuzzleLateral / VRPistolSpread.
	static int s_nLoggedMuzzle = -1;
	if (s_nLoggedMuzzle != nWeaponId)
	{
		s_nLoggedMuzzle = nWeaponId;
		// Log("VR muzzle flash: weapon %d -> %.1f,%.1f,%.1f (%s gun; x/y/z offset %.1f,%.1f,%.1f scale %.2f%s)",
		// 	nWeaponId, (double)vOut.x, (double)vOut.y, (double)vOut.z, bHeld ? "held" : "first-person",
		// 	(double)vMuzzle.x, (double)vMuzzle.y, (double)vMuzzle.z, (double)fScale,
		// 	(nWeaponId == GUN_COLT45_ID)
		// 		? (bHeld ? " [pistols: spread-scaled, held]" : " [pistols: spread-scaled, first-person]")
		// 		: "");
	}

	return DTRUE;
}
