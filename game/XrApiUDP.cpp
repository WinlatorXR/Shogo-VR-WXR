// ======================================================================= //
//
// MODULE  : XrApiUDP.cpp
//
// PURPOSE : In-process WinlatorXR XrApi client (protocol 0.4) for Shogo.
//           See XrApiUDP.h for the protocol notes.
//
// ======================================================================= //

#include <winsock2.h>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XrApiUDP.h"

#define XR_PI	3.141592653589793f

#define XR_UDP_RECV_PORT		7872
#define XR_UDP_FALLBACK_PORT	7873
#define XR_UDP_SEND_PORT		7278

// HMD_SYNC hold (VRSyncHold): the socket-wait slice used while blocking for a
// fresh HMD_SYNC.  The wait itself is unbounded on purpose (see Poll): if the
// runtime stops sending, the game holds its last frame until data returns; the
// WinlatorXR sandbox can always be torn down if the runtime is gone for good.
#define XR_SYNC_HOLD_SLICE_MS	4

// ----------------------------------------------------------------------- //
//  Quaternion / vector helpers.
//
//  XrApi is NOT OpenXR.  It hands us raw HMD/hand quaternions + positions.
//  Raw positions are right-handed (x right, y up, -z forward); the raw
//  quaternions live in that same frame and pass through unchanged.  (The
//  code once negated their Z component to match HWXR/opentrackWXR, but on
//  device that reversed the head's roll - leaning left rolled the view
//  right - while leaving yaw and pitch untouched, because a pure yaw or
//  pitch quaternion has Z = 0.  Dropping the negation fixes the roll and
//  cannot change the other two axes.)
// ----------------------------------------------------------------------- //

// Raw XrApi quaternions already match the pipeline frame (see above).
static void XrQuatToOpenXR(const DFLOAT qIn[4], DFLOAT qOut[4])
{
	qOut[0] = qIn[0]; qOut[1] = qIn[1]; qOut[2] = qIn[2]; qOut[3] = qIn[3];
}

static void QMul(const DFLOAT a[4], const DFLOAT b[4], DFLOAT r[4])
{
	r[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
	r[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
	r[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
	r[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
}

static void QRotate(const DFLOAT q[4], const DFLOAT v[3], DFLOAT r[3])
{
	// v' = v + 2w(u x v) + 2(u x (u x v)),  u = q.xyz
	DFLOAT u[3] = { q[0], q[1], q[2] };
	DFLOAT t[3] = { 2.0f*(u[1]*v[2] - u[2]*v[1]),
					2.0f*(u[2]*v[0] - u[0]*v[2]),
					2.0f*(u[0]*v[1] - u[1]*v[0]) };
	r[0] = v[0] + q[3]*t[0] + (u[1]*t[2] - u[2]*t[1]);
	r[1] = v[1] + q[3]*t[1] + (u[2]*t[0] - u[0]*t[2]);
	r[2] = v[2] + q[3]*t[2] + (u[0]*t[1] - u[1]*t[0]);
}

static void QYaw(DFLOAT angle, DFLOAT q[4])		// rotation about +Y
{
	q[0] = 0.0f;
	q[1] = (DFLOAT)sin(angle * 0.5f);
	q[2] = 0.0f;
	q[3] = (DFLOAT)cos(angle * 0.5f);
}

static DFLOAT YawOf(const DFLOAT q[4])
{
	DFLOAT zf[3] = { 0.0f, 0.0f, -1.0f };
	DFLOAT fwd[3];
	QRotate(q, zf, fwd);
	return (DFLOAT)atan2(-fwd[0], -fwd[2]);
}

static DBOOL IsNumberToken(const char* s)
{
	if (!s || !*s) return DFALSE;
	return (*s == '-' || *s == '+' || *s == '.' || (*s >= '0' && *s <= '9'));
}

static void TrimEol(char* s)
{
	if (!s) return;
	int n = (int)strlen(s);
	while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

// Create a (possibly nested) directory.  Returns DTRUE if it exists afterwards.
static DBOOL EnsureDir(const char* szPath)
{
	char szTmp[260];
	strncpy(szTmp, szPath, sizeof(szTmp) - 1);
	szTmp[sizeof(szTmp) - 1] = 0;

	for (char* p = szTmp + 1; *p; p++)
	{
		if (*p == '\\' || *p == '/')
		{
			char c = *p;
			*p = 0;
			CreateDirectoryA(szTmp, NULL);
			*p = c;
		}
	}
	if (CreateDirectoryA(szTmp, NULL)) return DTRUE;
	return (GetFileAttributesA(szTmp) != INVALID_FILE_ATTRIBUTES) ? DTRUE : DFALSE;
}


// ======================================================================= //
//	Construction / setup
// ======================================================================= //

CXrApiUDP::CXrApiUDP()
{
	m_sockRecv			= 0;
	m_bRunning			= DFALSE;
	m_bWsaStarted		= DFALSE;
	m_bHaveSend			= DFALSE;
	memset(m_sendAddr, 0, sizeof(m_sendAddr));

	m_bHaveRaw			= DFALSE;
	memset(m_lQuat, 0, sizeof(m_lQuat));
	memset(m_rQuat, 0, sizeof(m_rQuat));
	memset(m_hmdQuat, 0, sizeof(m_hmdQuat));
	m_hmdQuat[3] = 1.0f;
	memset(m_lPos, 0, sizeof(m_lPos));
	memset(m_rPos, 0, sizeof(m_rPos));
	memset(m_hmdPos, 0, sizeof(m_hmdPos));
	memset(m_lStick, 0, sizeof(m_lStick));
	memset(m_rStick, 0, sizeof(m_rStick));
	m_ipd = 0.064f;
	m_fovX = 0.0f;
	m_fovY = 0.0f;
	m_szBools[0] = 0;
	m_bPrevStickClick = DFALSE;

	m_bHaveCenter		= DFALSE;
	m_bRecenterPending	= DFALSE;
	memset(m_centerInv, 0, sizeof(m_centerInv));
	m_centerInv[3] = 1.0f;
	memset(m_centerPos, 0, sizeof(m_centerPos));

	m_fLastSync			= 0.0f;
	m_bSyncHold			= DFALSE;
	m_bHavePrevSync		= DFALSE;
	m_fPrevFrameSync	= 0.0f;

	m_dirPath[0] = 0;
	strcpy(m_szMake, "META");
	strcpy(m_szModel, "QUEST 3");
}

CXrApiUDP::~CXrApiUDP()
{
	Stop();
}

void CXrApiUDP::CloseSockets()
{
	if (m_sockRecv)
	{
		closesocket((SOCKET)m_sockRecv);
		m_sockRecv = 0;
	}
}

void CXrApiUDP::Start()
{
	if (m_bRunning) return;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
	m_bWsaStarted = DTRUE;

	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET)
	{
		WSACleanup();
		m_bWsaStarted = DFALSE;
		return;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(XR_UDP_RECV_PORT);

	if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		// Port busy (a second instance?) - use the documented fallback port.
		addr.sin_port = htons(XR_UDP_FALLBACK_PORT);
		if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			closesocket(s);
			WSACleanup();
			m_bWsaStarted = DFALSE;
			return;
		}
	}

	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);		// non-blocking: Poll() drains what's there

	m_sockRecv = (DDWORD)s;

	// Where we send: localhost:7278.
	{
		struct sockaddr_in* pSend = (struct sockaddr_in*)m_sendAddr;
		pSend->sin_family = AF_INET;
		pSend->sin_port = htons(XR_UDP_SEND_PORT);
		pSend->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		m_bHaveSend = DTRUE;
	}

	m_bRunning = DTRUE;
	WriteStartupFiles();
}

void CXrApiUDP::Stop()
{
	if (m_dirPath[0])
	{
		char szFile[96];
		_snprintf(szFile, sizeof(szFile) - 1, "%s\\vr", m_dirPath);
		szFile[sizeof(szFile) - 1] = 0;
		DeleteFileA(szFile);
	}

	CloseSockets();

	if (m_bWsaStarted)
	{
		WSACleanup();
		m_bWsaStarted = DFALSE;
	}
	m_bRunning = DFALSE;
}

// Write Z:\tmp\xr\version + vr, and read Z:\tmp\xr\system for the headset name.
void CXrApiUDP::WriteStartupFiles()
{
	strcpy(m_dirPath, "Z:\\tmp\\xr");
	if (GetFileAttributesA("Z:\\") == INVALID_FILE_ATTRIBUTES)
		strcpy(m_dirPath, "D:\\xrtemp");

	if (!EnsureDir(m_dirPath)) return;

	char szFile[96];

	_snprintf(szFile, sizeof(szFile) - 1, "%s\\version", m_dirPath);
	szFile[sizeof(szFile) - 1] = 0;
	FILE* f = fopen(szFile, "w");
	if (f) { fputs("0.4", f); fclose(f); }

	_snprintf(szFile, sizeof(szFile) - 1, "%s\\vr", m_dirPath);
	szFile[sizeof(szFile) - 1] = 0;
	f = fopen(szFile, "w");
	if (f) { fputs("VR", f); fclose(f); }

	_snprintf(szFile, sizeof(szFile) - 1, "%s\\system", m_dirPath);
	szFile[sizeof(szFile) - 1] = 0;
	f = fopen(szFile, "r");
	if (f)
	{
		char szLine[128];
		if (fgets(szLine, sizeof(szLine), f))
		{
			TrimEol(szLine);
			strncpy(m_szMake, szLine, sizeof(m_szMake) - 1);
			m_szMake[sizeof(m_szMake) - 1] = 0;
		}
		if (fgets(szLine, sizeof(szLine), f))
		{
			TrimEol(szLine);
			strncpy(m_szModel, szLine, sizeof(m_szModel) - 1);
			m_szModel[sizeof(m_szModel) - 1] = 0;
		}
		fclose(f);
	}

	if (!m_szMake[0])  strcpy(m_szMake, "META");
	if (!m_szModel[0]) strcpy(m_szModel, "QUEST 3");
}

// ======================================================================= //
//	Receive / parse
// ======================================================================= //

void CXrApiUDP::Drain()
{
	for (int i = 0; i < 64; i++)		// drain the backlog, keep the newest
	{
		char buf[1024];
		struct sockaddr_in from;
		int fromLen = sizeof(from);
		int n = recvfrom((SOCKET)m_sockRecv, buf, sizeof(buf) - 1, 0,
						 (struct sockaddr*)&from, &fromLen);
		if (n <= 0) break;
		if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
		buf[n] = 0;
		Parse(buf);
	}
}

static int SocketReadable(DDWORD sock, int nMs)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET((SOCKET)sock, &fds);
	struct timeval tv;
	tv.tv_sec  = nMs / 1000;
	tv.tv_usec = (nMs % 1000) * 1000;
	return select(0, &fds, NULL, NULL, &tv);
}

DBOOL CXrApiUDP::Poll(XrApiPose &out)
{
	memset(&out, 0, sizeof(out));
	out.bValid = DFALSE;

	if (m_bRunning)
	{
		Drain();

		// Keep the app in step with the headset: block (sleeping in select, no
		// CPU) until the runtime reports a fresh HMD_SYNC.  This is deliberately
		// unbounded - if the runtime stops sending, the game holds its last frame
		// until data returns.  Only active when VRSyncHold is on and we've already
		// seen a packet (so startup without a headset never blocks).
		if (m_bSyncHold && m_bHaveRaw && m_bHavePrevSync)
		{
			while (m_bRunning && m_fLastSync == m_fPrevFrameSync)
			{
				int nReady = SocketReadable(m_sockRecv, XR_SYNC_HOLD_SLICE_MS);
				if (nReady < 0) break;			// socket error: stop waiting rather than spin
				if (nReady > 0) Drain();
			}
		}
	}

	if (!m_bHaveRaw) return DFALSE;

	Convert(out);
	out.bValid = DTRUE;
	m_fPrevFrameSync = m_fLastSync;
	m_bHavePrevSync  = DTRUE;
	return DTRUE;
}

DBOOL CXrApiUDP::Parse(const char* szData)
{
	if (!szData || !*szData) return DFALSE;

	// Split into whitespace-separated tokens.
	char tok[64][32];
	int nTok = 0;
	const char* p = szData;
	while (*p && nTok < 64)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
		if (!*p) break;
		int k = 0;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && k < 31)
			tok[nTok][k++] = *p++;
		tok[nTok][k] = 0;
		nTok++;
	}

	// The packet leads with "client"; skip it if present.  If it's missing,
	// the first token is just the first number.
	int i0 = (nTok > 0 && !IsNumberToken(tok[0])) ? 1 : 0;
	if (nTok < i0 + 29) return DFALSE;

	DFLOAT f[29];
	for (int i = 0; i < 29; i++) f[i] = (DFLOAT)atof(tok[i0 + i]);

	m_lQuat[0] = f[0];  m_lQuat[1] = f[1];  m_lQuat[2] = f[2];  m_lQuat[3] = f[3];
	m_lStick[0] = f[4]; m_lStick[1] = f[5];
	m_lPos[0] = f[6];   m_lPos[1] = f[7];   m_lPos[2] = f[8];

	m_rQuat[0] = f[9];  m_rQuat[1] = f[10]; m_rQuat[2] = f[11]; m_rQuat[3] = f[12];
	m_rStick[0] = f[13]; m_rStick[1] = f[14];
	m_rPos[0] = f[15];  m_rPos[1] = f[16];  m_rPos[2] = f[17];

	m_hmdQuat[0] = f[18]; m_hmdQuat[1] = f[19]; m_hmdQuat[2] = f[20]; m_hmdQuat[3] = f[21];
	m_hmdPos[0] = f[22];  m_hmdPos[1] = f[23];  m_hmdPos[2] = f[24];

	m_ipd	= f[25];
	m_fovX	= f[26];
	m_fovY	= f[27];
	m_fLastSync = f[28];

	m_szBools[0] = 0;
	if (i0 + 29 < nTok)
	{
		strncpy(m_szBools, tok[i0 + 29], sizeof(m_szBools) - 1);
		m_szBools[sizeof(m_szBools) - 1] = 0;
	}

	m_bHaveRaw = DTRUE;

	// Recentre on the first packet or on demand (the mod's recentre button
	// calls RequestRecenter when the right stick is clicked).
	if (!m_bHaveCenter || m_bRecenterPending)
		Recenter();
	m_bRecenterPending = DFALSE;

	return DTRUE;
}

DDWORD CXrApiUDP::ButtonBits()
{
	int n = (int)strlen(m_szBools);
	#define XB(i)  (((i) < n && m_szBools[(i)] == 'T') ? 1 : 0)
	DDWORD b = 0;
	if (XB(18))			b |= SHOGOVR_BTN_FIRE;			// right trigger - shoot
	if (XB(12))			b |= SHOGOVR_BTN_DOUBLEJUMP;	// right grip - double jump
	if (XB(0))			b |= SHOGOVR_BTN_JUMP;			// left grip - jump (regular)
	if (XB(10))			b |= SHOGOVR_BTN_VIEWTOGGLE;	// A - first/third person view
	if (XB(11))			b |= SHOGOVR_BTN_TRANSFORM;		// B - transform vehicle/mech
	if (XB(9))			b |= SHOGOVR_BTN_ZOOM;			// Y - weapon zoom
	if (XB(8))			b |= SHOGOVR_BTN_CROUCH;		// X - crouch toggle
	if (XB(2))			b |= SHOGOVR_BTN_CROUCH_HOLD;	// left stick click - crouch (held)
	if (XB(7))			b |= SHOGOVR_BTN_LEFT_TRIGGER;	// left trigger - F9 (quick load) when dead
	if (XB(1))			b |= SHOGOVR_BTN_MENU;			// left menu
	if (XB(13))			b |= SHOGOVR_BTN_RECENTER;		// right stick click - recentre
	#undef XB
	return b;
}

void CXrApiUDP::ToGameSpace(const DFLOAT qIn[4], const DFLOAT pos[3],
							DFLOAT f[3], DFLOAT u[3], DFLOAT p[3])
{
	DFLOAT q[4];
	QMul(m_centerInv, qIn, q);

	DFLOAT d[3] = { pos[0] - m_centerPos[0], pos[1] - m_centerPos[1], pos[2] - m_centerPos[2] };
	DFLOAT pp[3];
	QRotate(m_centerInv, d, pp);

	DFLOAT zf[3] = { 0.0f, 0.0f, -1.0f };
	DFLOAT yu[3] = { 0.0f, 1.0f, 0.0f };
	DFLOAT fw[3], up[3];
	QRotate(q, zf, fw);
	QRotate(q, yu, up);

	// OpenXR (right-handed) -> LithTech (left-handed): flip z.
	f[0] = fw[0]; f[1] = fw[1]; f[2] = -fw[2];
	u[0] = up[0]; u[1] = up[1]; u[2] = -up[2];
	p[0] = pp[0]; p[1] = pp[1]; p[2] = -pp[2];
}

void CXrApiUDP::Convert(XrApiPose &out)
{
	DFLOAT hq[4], rq[4], lq[4];
	XrQuatToOpenXR(m_hmdQuat, hq);
	XrQuatToOpenXR(m_rQuat, rq);
	XrQuatToOpenXR(m_lQuat, lq);

	ToGameSpace(hq, m_hmdPos, out.headF, out.headU, out.headP);
	out.headYaw   = (DFLOAT)atan2(out.headF[0], out.headF[2]);
	out.headPitch = (DFLOAT)-atan2(out.headF[1],
						(DFLOAT)sqrt(out.headF[0]*out.headF[0] + out.headF[2]*out.headF[2]));

	ToGameSpace(rq, m_rPos, out.rF, out.rU, out.rP);
	ToGameSpace(lq, m_lPos, out.lF, out.lU, out.lP);

	out.moveX = m_lStick[0];	out.moveY = m_lStick[1];
	out.turnX = m_rStick[0];	out.turnY = m_rStick[1];

	out.ipd  = m_ipd;
	out.tanX = (DFLOAT)tan(m_fovX * XR_PI / 360.0f);
	out.tanY = (DFLOAT)tan(m_fovY * XR_PI / 360.0f);
	out.sync = m_fLastSync;

	out.bCtrlActive = DTRUE;
	out.bRightPose  = DTRUE;
	out.bLeftPose   = DTRUE;
	out.buttons     = ButtonBits();
}

void CXrApiUDP::Recenter()
{
	if (!m_bHaveRaw) return;

	DFLOAT hq[4];
	XrQuatToOpenXR(m_hmdQuat, hq);
	QYaw(-YawOf(hq), m_centerInv);
	m_centerPos[0] = m_hmdPos[0];
	m_centerPos[1] = m_hmdPos[1];
	m_centerPos[2] = m_hmdPos[2];
	m_bHaveCenter = DTRUE;
}

void CXrApiUDP::RequestRecenter()
{
	m_bRecenterPending = DTRUE;
	Recenter();				// take effect immediately with the last pose
}

void CXrApiUDP::SendFrame(DFLOAT fLHaptic, DFLOAT fRHaptic, int nModeVR, int nMode3D,
						  DFLOAT fFovX, DFLOAT fFovY)
{
	if (!m_bRunning || !m_bHaveSend) return;

	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return;

	char buf[160];
	_snprintf(buf, sizeof(buf) - 1, "%g %g %d %d %g %g",
			  (double)fLHaptic, (double)fRHaptic, nModeVR, nMode3D,
			  (double)fFovX, (double)fFovY);
	buf[sizeof(buf) - 1] = 0;

	sendto(s, buf, (int)strlen(buf), 0, (struct sockaddr*)m_sendAddr, sizeof(struct sockaddr_in));
	closesocket(s);
}
