// ======================================================================= //
//
// MODULE  : XrApiUDP.h
//
// PURPOSE : In-process WinlatorXR XrApi client (protocol 0.4) for Shogo.
//
//           Replaces the old out-of-process OpenXR bridge (ShogoVRBridge.exe
//           + shared memory).  Adapted from WinlatorXR/HaloCEWXR
//           (WinXrApiUDP / WinXrApi) and the XrApi 0.4/0.5 specification.
//
//   out -> localhost:7278  "<Lhaptic> <Rhaptic> <MODE_VR> <MODE_3D> <FOVH> <FOVV>"
//   in  <- localhost:7872  (fallback localhost:7873)
//          "client <28 floats> <frame id> <21 T/F> <HMD string>"
//          e.g. "client <LQx4> <Lstick2> <Lpos3> <RQx4> <Rstick2> <Rpos3>
//                <HQx4> <Hpos3> <IPD fovX fovY> <frame id> <21 chars> META"
//          The frame id doubles as HMD_SYNC (the red sync pixel).  Trailing
//          tokens after the bool string (the HMD string) are ignored, and a
//          shorter/older (19-char) bool string is tolerated.  Bool chars 19/20
//          are the runtime's IS_IMMERSIVE / IS_SBS hints, which this client
//          does not need.
//
//           Poses arrive in the XrApi/OpenXR frame (right-handed, +Y up,
//           -Z forward, metres) and are converted here into LithTech space
//           (x right, y up, z forward, left-handed, metres) relative to the
//           recentre point - the same conversion the OpenXR bridge used.
//
// ======================================================================= //

#ifndef __XRAPIUDP_H__
#define __XRAPIUDP_H__

#include "client_de.h"
#include "ShogoVRShared.h"

// One converted frame, ready to drop into CVRStereo's members.
struct XrApiPose
{
	DBOOL	bValid;			// a packet has been parsed and converted
	DDWORD	buttons;		// SHOGOVR_BTN_* currently held
	DBOOL	bCtrlActive;	// controller input is live
	DBOOL	bRightPose;		// right (gun) hand pose is valid
	DBOOL	bLeftPose;		// left hand pose is valid

	DFLOAT	headF[3];		// head forward, LithTech space
	DFLOAT	headU[3];		// head up, LithTech space
	DFLOAT	headP[3];		// head position, metres from the recentre point
	DFLOAT	headYaw;		// radians, positive = turned right
	DFLOAT	headPitch;		// radians, positive = looking down
	DFLOAT	rF[3], rU[3], rP[3];	// right (gun) hand
	DFLOAT	lF[3], lU[3], lP[3];	// left hand

	DFLOAT	moveX, moveY;	// left stick (x right, y forward)
	DFLOAT	turnX, turnY;	// right stick
	DFLOAT	tanX, tanY;		// tan(half-FOV) the runtime wants covered
	DFLOAT	ipd;			// metres
	DFLOAT	sync;			// HMD_SYNC value (draw as shade of red, top-left)
};

class CXrApiUDP
{
	public:

		CXrApiUDP();
		~CXrApiUDP();

		void	Start();			// open sockets, announce VR via Z:\tmp\xr
		void	Stop();				// close sockets, remove the vr file
		DBOOL	IsRunning()			{ return m_bRunning; }

		// Wait (blocking, no CPU) for a fresh HMD_SYNC before returning from
		// Poll.  Unbounded by design: a stalled runtime holds the game on its
		// last frame until data returns.  Off by default at the API level; the
		// mod turns it on via VRSyncHold.
		void	SetSyncHold(DBOOL b)	{ m_bSyncHold = b; }

		// Drain any incoming packets and fill out the newest converted pose.
		// Returns DFALSE until the first packet has ever arrived.
		DBOOL	Poll(XrApiPose &out);

		void	SendFrame(DFLOAT fLHaptic, DFLOAT fRHaptic, int nModeVR, int nMode3D,
							 DFLOAT fFovX, DFLOAT fFovY);
		void	RequestRecenter();

		const char* GetHmdMake()	{ return m_szMake; }
		const char* GetHmdModel()	{ return m_szModel; }
		DFLOAT	LastSync()			{ return m_fLastSync; }

	private:

		void	Drain();					// read every queued packet
		DBOOL	Parse(const char* szData);	// packet -> raw storage
		void	Convert(XrApiPose &out);	// raw storage -> LithTech pose
		DDWORD	ButtonBits();				// the 19 T/F chars -> SHOGOVR_BTN_*
		void	ToGameSpace(const DFLOAT q[4], const DFLOAT pos[3],
							DFLOAT f[3], DFLOAT u[3], DFLOAT p[3]);
		void	Recenter();
		void	WriteStartupFiles();
		void	CloseSockets();

		DDWORD	m_sockRecv;
		DBOOL	m_bRunning;
		DBOOL	m_bWsaStarted;
		DBOOL	m_bHaveSend;
		char	m_sendAddr[16];		// opaque sockaddr_in for 127.0.0.1:7278

		// Raw tracking-space values from the last packet.
		DBOOL	m_bHaveRaw;
		DFLOAT	m_lQuat[4], m_rQuat[4], m_hmdQuat[4];
		DFLOAT	m_lPos[3],  m_rPos[3],  m_hmdPos[3];
		DFLOAT	m_lStick[2], m_rStick[2];
		DFLOAT	m_ipd, m_fovX, m_fovY;
		char	m_szBools[24];
		DBOOL	m_bPrevStickClick;	// for button-edge recentre

		// Recentre point (yaw-inverse quaternion + position).
		DBOOL	m_bHaveCenter;
		DBOOL	m_bRecenterPending;
		DFLOAT	m_centerInv[4];
		DFLOAT	m_centerPos[3];

		DFLOAT	m_fLastSync;

		// VRSyncHold: block for a new HMD_SYNC value each frame (unbounded).
		DBOOL	m_bSyncHold;
		DBOOL	m_bHavePrevSync;
		DFLOAT	m_fPrevFrameSync;

		char	m_dirPath[64];
		char	m_szMake[64];
		char	m_szModel[64];
};

#endif // __XRAPIUDP_H__
