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
	m_nPressed				= 0;
	m_nReleased				= 0;
	m_fMoveX = m_fMoveY		= 0.0f;
	m_fTurnX = m_fTurnY		= 0.0f;
	m_fHapticL = m_fHapticR	= 0.0f;
	VEC_SET(m_vRightF, 0.0f, 0.0f, 1.0f);	VEC_SET(m_vRightU, 0.0f, 1.0f, 0.0f);	VEC_INIT(m_vRightP);
	VEC_SET(m_vLeftF, 0.0f, 0.0f, 1.0f);	VEC_SET(m_vLeftU, 0.0f, 1.0f, 0.0f);	VEC_INIT(m_vLeftP);
	m_bGameplayInput		= DFALSE;
	m_nSuppressed			= 0;
	m_bAimByHand			= DFALSE;
	m_bPlayerDead			= DFALSE;
	m_bCrouchOn				= DFALSE;
	m_bAutosavePending		= DFALSE;
	m_nAutosaveFrames		= 0;
	m_bHaveBody				= DFALSE;
	m_fLastOutYaw			= 0.0f;
	m_bSnapLatched			= DFALSE;
	m_nWeaponLatch			= 0;
	m_nWeaponId				= -1;
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
	m_bCaptureHook			= DFALSE;
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
	m_vtSnapTurn.Init		(pClientDE, "VRSnapTurn",		NULL, 45.0f);
	m_vtTurnSpeed.Init		(pClientDE, "VRTurnSpeed",		NULL, 150.0f);
	m_vtGunX.Init			(pClientDE, "VRGunX",			NULL, 0.0f);
	m_vtGunY.Init			(pClientDE, "VRGunY",			NULL, 0.0f);
	m_vtGunZ.Init			(pClientDE, "VRGunZ",			NULL, 0.0f);
	m_vtAimMarker.Init		(pClientDE, "VRAimMarker",		NULL, 1.0f);
	m_vtAimStyle.Init		(pClientDE, "VRAimStyle",		NULL, 3.0f);
	m_vtGameCrosshair.Init	(pClientDE, "VRGameCrosshair",	NULL, 0.0f);
	m_vtAimDotSize.Init		(pClientDE, "VRAimDotSize",		NULL, 1.0f);
	m_vtGunScale.Init		(pClientDE, "VRGunScale",		NULL, 1.5f);
	m_vtGunScaleMCA.Init	(pClientDE, "VRGunScaleMCA",	NULL, 1.0f);
	m_vtFixRenderer.Init	(pClientDE, "VRFixRenderer",	NULL, 1.0f);
	m_vtVRFullscreen.Init	(pClientDE, "VRFullscreen",		NULL, 1.0f);
	m_vtMoveDir.Init		(pClientDE, "VRMoveDir",		NULL, 0.0f);
	m_vtTwoHand.Init		(pClientDE, "VRTwoHand",		NULL, 1.0f);
	m_vtHandFlip.Init		(pClientDE, "VRHandFlip",		NULL, 0.0f);
	m_vtBody.Init			(pClientDE, "VRBody",			NULL, 1.0f);
	m_vtBodyArms.Init		(pClientDE, "VRBodyArms",		NULL, 0.0f);
	m_vtBodyOffset.Init		(pClientDE, "VRBodyOffset",		NULL, 0.15f);
	m_vtGunModel.Init		(pClientDE, "VRGunModel",		NULL, 0.0f);
	m_vtGunArms.Init		(pClientDE, "VRGunArms",		NULL, 1.0f);
	m_vtEasyLadders.Init	(pClientDE, "VREasyLadders",	NULL, 1.0f);
	m_vtAutoBridge.Init		(pClientDE, "VRAutoBridge",		NULL, 1.0f);
	m_vtHaptics.Init		(pClientDE, "VRHaptics",		NULL, 1.0f);
	m_vtSyncHold.Init		(pClientDE, "VRSyncHold",		NULL, 1.0f);
	m_vtDirectCapture.Init	(pClientDE, "VRDirectCapture",	NULL, 1.0f);
	m_vtHeldPitch.Init		(pClientDE, "VRHeldPitch",		NULL, 0.0f);
	m_vtHeldYaw.Init		(pClientDE, "VRHeldYaw",		NULL, 0.0f);
	m_vtHeldRoll.Init		(pClientDE, "VRHeldRoll",		NULL, 0.0f);
	m_vtHeldX.Init			(pClientDE, "VRHeldX",			NULL, 0.0f);
	m_vtHeldY.Init			(pClientDE, "VRHeldY",			NULL, 0.0f);
	m_vtHeldZ.Init			(pClientDE, "VRHeldZ",			NULL, 0.0f);
	m_vtHeldScale.Init		(pClientDE, "VRHeldScale",		NULL, 1.0f);

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
	m_fMenuVirtW = (DFLOAT)VRRound(fMenuLayout);
	m_fMenuVirtH = (DFLOAT)VRRound(fMenuLayout * 0.75f);

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

void CVRStereo::RenderStereo(HLOCALOBJ hCamera, HLOCALOBJ* pObjects, int nObjects,
							 DFLOAT fBaseFovX, int nMode3D, DBOOL bFootScale)
{
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

	// Zoom: only a real zoom IN (the sniper scope) narrows the view.  The
	// camera's FOV changes for other reasons too - the server sends one made
	// for a 4:3 screen on every player-mode change (each level start, each
	// transform) and a wider one in a mech's vehicle mode - and in a headset
	// none of that may change what you see.  So: horizontal angle only (the
	// server's vertical one assumes 4:3), and never wider than normal.

	DFLOAT fZoom = 1.0f;
	if (fOrigFovX > 0.001f && m_fLastBaseFovX > 0.001f) fZoom = fOrigFovX / m_fLastBaseFovX;
	if (fZoom > 0.97f) fZoom = 1.0f;
	if (fZoom < 0.01f) fZoom = 0.01f;

	if (fZoom >= 1.0f && ((DFLOAT)fabs(fOrigFovX - m_fLoggedCamFovX) > 0.01f || (DFLOAT)fabs(fOrigFovY - m_fLoggedCamFovY) > 0.01f))
	{
		Log("Game camera FOV is now %.1f x %.1f degrees (headset view %s)", VR_RAD2DEG(fOrigFovX), VR_RAD2DEG(fOrigFovY),
			fZoom < 1.0f ? "zoomed in to match" : "unchanged");
		m_fLoggedCamFovX = fOrigFovX;
		m_fLoggedCamFovY = fOrigFovY;
	}
	if ((DFLOAT)fabs(fZoom - m_fLoggedZoom) > 0.05f)
	{
		Log("Zoom %.2fx", fZoom);
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

			fEyeFovY = ClampFov(BaseEyeFovY(m_fLastBaseFovX) * fZoom);
			fEyeFovX = ClampFov(FovXFromFovY(fEyeFovY, EyeAspect()));

			// Shrink the eye distance while zoomed so the scope doesn't
			// turn into binoculars with extreme depth.
			fSep = fIpdWorld * (fZoom < 1.0f ? fZoom : 1.0f);
			fReportSep = GetIPDMeters() * (fZoom < 1.0f ? fZoom : 1.0f);

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
	m_bSkipNonCross = DFALSE;
	m_fVirtW = (DFLOAT)m_nScreenW;
	m_fVirtH = (DFLOAT)m_nScreenH;
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
		}
	}
	if (!m_hMovieSurface)
	{
		m_hMovieSurface = m_pClientDE->CreateSurface(nW, nH);
		if (!m_hMovieSurface) return;
	}

	// Some renderers only accept these blits inside the optimized-2D block.
	m_pClientDE->StartOptimized2D();
	// 1) copy the whole window into the scratch surface
	m_pfnDrawSurfaceToSurface(m_hMovieSurface, hScreen, DNULL, 0, 0);

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
		m_pfnDrawSurfaceToSurface(hScreen, m_hMovieSurface, &m_rcMovieSrc, xL, 0);
		m_pfnDrawSurfaceToSurface(hScreen, m_hMovieSurface, &m_rcMovieSrc, xR, 0);
	}
	else
	{
		// Couldn't find the movie: keep the plain right-half slide.
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
	int nMinX = nR, nMaxX = nL, nMinY = (int)nH, nMaxY = 0;

	HDECOLOR hBlack = m_pClientDE->SetupColor1(0.0f, 0.0f, 0.0f, DFALSE);
	HDECOLOR c = 0;
	DDWORD x, y;
	int rows[3] = { (int)nH / 4, (int)nH / 2, (int)nH * 3 / 4 };
	int cols[3] = { nL, (int)nW / 2, nR };

	for (int r = 0; r < 3; r++)
	{
		for (x = (DDWORD)nL; x < (DDWORD)nR; x++)
		{
			if (m_pClientDE->GetPixel(m_hMovieSurface, x, (DDWORD)rows[r], &c) == DE_OK && c != hBlack)
			{
				if ((int)x < nMinX) nMinX = (int)x;
				if ((int)x > nMaxX) nMaxX = (int)x;
			}
		}
	}
	for (int ci = 0; ci < 3; ci++)
	{
		for (y = 0; y < nH; y++)
		{
			if (m_pClientDE->GetPixel(m_hMovieSurface, (DDWORD)cols[ci], y, &c) == DE_OK && c != hBlack)
			{
				if ((int)y < nMinY) nMinY = (int)y;
				if ((int)y > nMaxY) nMaxY = (int)y;
			}
		}
	}

	if (nMaxX > nMinX && nMaxY > nMinY)
	{
		m_rcMovieSrc.left	= nMinX;
		m_rcMovieSrc.right	= nMaxX + 1;
		m_rcMovieSrc.top	= nMinY;
		m_rcMovieSrc.bottom	= nMaxY + 1;
		m_bMovieSrcKnown	= DTRUE;
		Log("movie source detected: %d,%d - %d,%d", m_rcMovieSrc.left, m_rcMovieSrc.top, m_rcMovieSrc.right, m_rcMovieSrc.bottom);
	}
}

void CVRStereo::ResetMovieSource()
{
	m_bMovieSrcKnown = DFALSE;
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
	if (s.m_bVirtualHud && hSurf == s.m_hScreen)
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
	}
	else if (!m_bBridgeAlive)
	{
		m_nCtrlFlags = 0;
		m_nButtons = 0;
	}

	UpdateTwoHanded();

	// Button edges for this frame
	if (!IsControllerActive()) m_nButtons = 0;
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

	// Controller haptics: a short vibration pulse on the fire edge (and a tiny
	// one when switching weapons).  The runtime expects a duration in frames,
	// so we send a small countdown and let it fall back to zero.
	if (m_fHapticL > 0.0f) m_fHapticL -= 1.0f;
	if (m_fHapticR > 0.0f) m_fHapticR -= 1.0f;
	if (m_vtHaptics.GetFloat(1.0f) != 0.0f)
	{
		if (m_nPressed & SHOGOVR_BTN_FIRE)			m_fHapticR = 4.0f;
		if (m_nPressed & SHOGOVR_BTN_NEXT_WEAPON)	m_fHapticR = 2.0f;
	}
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

DBOOL CVRStereo::VRCommandOn(int nCommand)
{
	if (!m_bGameplayInput || !IsControllerActive()) return DFALSE;

	DDWORD nHeld = m_nButtons & ~m_nSuppressed;
	switch (nCommand)
	{
		case COMMAND_ID_FIRING:	return (nHeld & SHOGOVR_BTN_FIRE) ? DTRUE : DFALSE;
		case COMMAND_ID_JUMP:	return (nHeld & (SHOGOVR_BTN_JUMP | SHOGOVR_BTN_DOUBLEJUMP)) ? DTRUE : DFALSE;
		case COMMAND_ID_DOUBLEJUMP:	return (nHeld & SHOGOVR_BTN_DOUBLEJUMP) ? DTRUE : DFALSE;
		case COMMAND_ID_DUCK:	return (m_bCrouchOn || (nHeld & SHOGOVR_BTN_CROUCH_HOLD)) ? DTRUE : DFALSE;

		// Walking.  The engine has no "set command" call exposed to the shell,
		// so movement flags are driven through this query instead:
		// CMoveMgr::UpdateControlFlags() reads IsCommandOn(FORWARD/REVERSE/
		// STRAFE_*) each frame to build BC_CFLG_*, and the analog axis values
		// stored by UpdateAxisMovement cap the speed.  Turning stays on the
		// right stick (StickTurn).  GetMoveAxes() applies the dead-zone and the
		// same rotate-to-aim transform the speed caps use, so direction and
		// speed agree.
		case COMMAND_ID_FORWARD:
		case COMMAND_ID_REVERSE:
		case COMMAND_ID_STRAFE_RIGHT:
		case COMMAND_ID_STRAFE_LEFT:
		{
			DFLOAT fFwd = 0.0f, fRight = 0.0f;
			GetMoveAxes(fFwd, fRight);
			if (nCommand == COMMAND_ID_FORWARD)      return (fFwd  > 0.0f) ? DTRUE : DFALSE;
			if (nCommand == COMMAND_ID_REVERSE)      return (fFwd  < 0.0f) ? DTRUE : DFALSE;
			if (nCommand == COMMAND_ID_STRAFE_RIGHT) return (fRight > 0.0f) ? DTRUE : DFALSE;
			return (fRight < 0.0f) ? DTRUE : DFALSE;
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

	// Round dead zone, then rescale so movement starts gently at its edge.
	const DFLOAT fDead = 0.15f;
	DFLOAT fLen = (DFLOAT)sqrt(m_fMoveX * m_fMoveX + m_fMoveY * m_fMoveY);
	if (fLen <= fDead) return DFALSE;

	DFLOAT fScale = ((fLen > 1.0f ? 1.0f : fLen) - fDead) / (1.0f - fDead) / fLen;
	fForward	= m_fMoveY * fScale;
	fRight		= m_fMoveX * fScale;

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

	WIN32_FILE_ATTRIBUTE_DATA fa;
	if (!GetFileAttributesExA(m_szIniPath, GetFileExInfoStandard, &fa)) return;
	m_nIniTimeLo = fa.ftLastWriteTime.dwLowDateTime;
	m_nIniTimeHi = fa.ftLastWriteTime.dwHighDateTime;

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

	DFLOAT y = m_fTurnY, ax = (DFLOAT)fabs(m_fTurnX), ay = (DFLOAT)fabs(m_fTurnY);
	if (m_nWeaponLatch == 0 && ay > 0.7f && ay > ax)
	{
		m_nWeaponLatch = (y > 0.0f) ? 1 : -1;
		return m_nWeaponLatch;
	}
	if (ay < 0.3f) m_nWeaponLatch = 0;
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
	CClientDE* pClientDE = m_pClientDE;

	DRotation rBody;
	DVector bU, bR, bF, vT;
	pClientDE->SetupEuler(&rBody, 0.0f, m_fBodyYaw, 0.0f);
	pClientDE->GetRotationVectors(&rBody, &bU, &bR, &bF);

	DFLOAT fScale = m_fWorldScaleUsed;
	VEC_MULSCALAR(vPos, bR, m_vRightP.x * fScale);
	VEC_MULSCALAR(vT,   bU, m_vRightP.y * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT,   bF, m_vRightP.z * fScale);	VEC_ADD(vPos, vPos, vT);
	VEC_ADD(vPos, vPos, vCamPos);

	VEC_MULSCALAR(vF, bR, m_vAimF.x);
	VEC_MULSCALAR(vT, bU, m_vAimF.y);	VEC_ADD(vF, vF, vT);
	VEC_MULSCALAR(vT, bF, m_vAimF.z);	VEC_ADD(vF, vF, vT);

	VEC_MULSCALAR(vU, bR, m_vAimU.x);
	VEC_MULSCALAR(vT, bU, m_vAimU.y);	VEC_ADD(vU, vU, vT);
	VEC_MULSCALAR(vT, bF, m_vAimU.z);	VEC_ADD(vU, vU, vT);

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

	// vPos arrives as the camera position; the gun goes to the right hand,
	// pointing where the hand points.
	DVector vHandPos, vHandF, vHandU, hU, hR, hF, vT;
	GetHandWorld(vPos, vHandPos, vHandF, vHandU);
	m_pClientDE->AlignRotation(&rRot, &vHandF, &vHandU);
	m_pClientDE->GetRotationVectors(&rRot, &hU, &hR, &hF);

	VEC_COPY(vPos, vHandPos);
	VEC_MULSCALAR(vT, hR, m_vtGunX.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hU, m_vtGunY.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);
	VEC_MULSCALAR(vT, hF, m_vtGunZ.GetFloat(0.0f) * fGunScale);	VEC_ADD(vPos, vPos, vT);
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

	if (m_vtTwoHand.GetFloat(1.0f) == 0.0f) return;
	if (!(m_nCtrlFlags & SHOGOVR_CTRL_RIGHT_POSE) || !(m_nCtrlFlags & SHOGOVR_CTRL_LEFT_POSE)) return;

	DVector vD;
	VEC_SUB(vD, m_vLeftP, m_vRightP);
	DFLOAT fDist = VEC_MAG(vD);
	if (fDist < 0.12f || fDist > 0.9f) return;		// metres

	DVector vDir;
	VEC_MULSCALAR(vDir, vD, 1.0f / fDist);
	DFLOAT fCos = VEC_DOT(vDir, m_vRightF);
	DFLOAT fNeed = bWas ? 0.766f : 0.906f;			// enter within 25 degrees, stay within 40
	if (fCos < fNeed) return;

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

// Appends a weapon model's part names to ShogoVR\ShogoVR_models.txt - which
// parts exist decides what can be hidden; send this file in if the arms on
// some gun don't disappear.
void CVRStereo::DumpModelNodes(HLOCALOBJ hObj, int nWeaponId)
{
	if (!m_pClientDE || !hObj || !m_szIniPath[0]) return;
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
void CVRStereo::UpdateInput()
{
	ReleaseInjectedKey();
	if (m_bTransformKeyDown && (long)(GetTickCount() - m_nTransformUpTick) >= 0) ReleaseTransformKey();

	if (!IsControllerActive())
	{
		m_bGameplayInput = DFALSE;
		m_bCrouchOn = DFALSE;
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

		// Left trigger: nothing while alive; while dead it loads the quicksave.
		if ((nPress & SHOGOVR_BTN_LEFT_TRIGGER) && m_bPlayerDead)
			InjectCommandTap(COMMAND_ID_QUICKLOAD);

		// Left Y: re-select the equipped weapon, which toggles its zoom (#18).
		if (nPress & SHOGOVR_BTN_ZOOM)
		{
			int nCmd = GetCommandId(m_nWeaponId);
			if (nCmd > 0) InjectCommandTap(nCmd);
		}
		// Right A: first/third person view.
		if (nPress & SHOGOVR_BTN_VIEWTOGGLE)	InjectCommandTap(COMMAND_ID_CHASEVIEWTOGGLE);
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

void CVRStereo::UpdateHeldGun(HLOCALOBJ hPVModel, int nWeaponId, const DVector &vCamPos)
{
	if (!m_pClientDE) return;
	CClientDE* pClientDE = m_pClientDE;

	// Remember the equipped weapon so Left-Y can re-select it (zoom toggle).
	m_nWeaponId = nWeaponId;

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
	if (hPVModel && nWeaponId >= 0 && nWeaponId < 64 && !(m_nDumpedWeapons[nWeaponId / 32] & (1u << (nWeaponId % 32))))
	{
		m_nDumpedWeapons[nWeaponId / 32] |= (1u << (nWeaponId % 32));
		DumpModelNodes(hPVModel, nWeaponId);
	}
	DBOOL bHideArms = !bUseHeld && m_bAimByHand && m_vtGunArms.GetFloat(1.0f) == 0.0f;
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
