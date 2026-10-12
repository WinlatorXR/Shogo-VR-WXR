// ======================================================================= //
//
// MODULE  : ShogoVRShared.h
//
// PURPOSE : Shared memory between the game (CShell.dll, 32-bit) and the
//           ShogoVR bridge (ShogoVRBridge.exe, 64-bit).
//
//           The SAME file is used by both projects - keep them identical.
//           Only fixed-size types are used so both sides agree on the
//           layout.  Each direction is protected by a sequence counter
//           (odd while the writer is busy) so neither side reads a
//           half-written pose.
//
//           Coordinates handed to the game are already in LithTech space:
//           x = right, y = up, z = forward (left-handed), metres.
//
// ======================================================================= //

#ifndef __SHOGOVR_SHARED_H__
#define __SHOGOVR_SHARED_H__

#define SHOGOVR_SHARED_NAME		"Local\\ShogoVR_Shared_v3"
#define SHOGOVR_MAGIC			0x52564F53	/* 'SOVR' */
#define SHOGOVR_VERSION			3

// bridgeFlags
#define SHOGOVR_BRIDGE_HEADSET_ACTIVE	0x00000001	// headset session is running and showing us
#define SHOGOVR_BRIDGE_LAYERS			0x00000002	// bridge can show the HUD strip and flat screens as panels

// controllerFlags
#define SHOGOVR_CTRL_ACTIVE			0x00000001	// controller input is live (headset focused)
#define SHOGOVR_CTRL_RIGHT_POSE		0x00000002	// right hand pose is valid
#define SHOGOVR_CTRL_LEFT_POSE		0x00000004	// left hand pose is valid

// buttons (what each one does is decided by the game)
#define SHOGOVR_BTN_FIRE			0x00000001	// right trigger - shoot
#define SHOGOVR_BTN_JUMP			0x00000002	// left grip - jump (regular); the bridge's A
#define SHOGOVR_BTN_CROUCH			0x00000004	// left X - crouch toggle
#define SHOGOVR_BTN_MENU			0x00000008	// left menu button
#define SHOGOVR_BTN_LOG				0x00000010	// (spare; the bridge's X)
#define SHOGOVR_BTN_WEAPONS			0x00000020	// (spare; the bridge's Y)
#define SHOGOVR_BTN_NEXT_WEAPON		0x00000040	// (spare in XrApi; the bridge's next weapon)
#define SHOGOVR_BTN_ALT				0x00000080	// (spare)
#define SHOGOVR_BTN_TRANSFORM		0x00000100	// right B - transform vehicle/mech
#define SHOGOVR_BTN_DOUBLEJUMP		0x00000200	// right grip - double jump
#define SHOGOVR_BTN_LEFT_TRIGGER	0x00000400	// left trigger - F9 (quick load) when dead
#define SHOGOVR_BTN_ZOOM			0x00000800	// left Y - weapon zoom (re-select the equipped weapon)
#define SHOGOVR_BTN_VIEWTOGGLE		0x00001000	// right A - first/third person view
#define SHOGOVR_BTN_RECENTER		0x00002000	// right stick click - recentre the view
#define SHOGOVR_BTN_CROUCH_HOLD		0x00004000	// left stick click - crouch (held)

// viewMode
#define SHOGOVR_VIEW_OFF		0	// stereo off - nothing to show
#define SHOGOVR_VIEW_TRACKED	1	// immersive, rendered from the reported head pose
#define SHOGOVR_VIEW_SCREEN		2	// floating screen, stereo halves (older versions)
#define SHOGOVR_VIEW_FLAT		3	// plain flat game picture (menus, cutscenes) for a floating screen

// layoutFlags
#define SHOGOVR_LAYOUT_HUD		0x00000001	// the HUD is drawn once into its own strip (hudX/Y/W/H)

#pragma pack(push, 4)

typedef struct ShogoVRShared_t
{
	unsigned int	magic;				// SHOGOVR_MAGIC
	unsigned int	version;			// SHOGOVR_VERSION
	unsigned int	structSize;			// sizeof(ShogoVRShared)

	// ---- written by the bridge -----------------------------------------

	volatile long	bridgeSeq;			// odd while the bridge is writing this block
	volatile long	bridgeHeartbeat;	// increases every bridge frame
	long			bridgeFlags;		// SHOGOVR_BRIDGE_ flags

	long			poseId;				// increases with every new pose (never 0)
	float			headForward[3];		// unit vector, LithTech space
	float			headUp[3];			// unit vector, LithTech space
	float			headPos[3];			// metres from the recentre point, LithTech space
	float			headYaw;			// radians, positive = turned right
	float			headPitch;			// radians, positive = looking down

	float			needTanX;			// each eye's image should cover at least
	float			needTanY;			// tan(half-FOV) this wide / tall

	long			controllerFlags;	// SHOGOVR_CTRL_
	long			buttons;			// SHOGOVR_BTN_ currently held
	float			moveX, moveY;		// left stick, -1..1 (x right, y forward)
	float			turnX, turnY;		// right stick, -1..1
	float			rightForward[3];	// right hand aim ray, LithTech space, same frame as the head
	float			rightUp[3];
	float			rightPos[3];		// metres
	float			leftForward[3];
	float			leftUp[3];
	float			leftPos[3];

	// ---- written by the game -------------------------------------------

	volatile long	gameSeq;			// odd while the game is writing this block
	volatile long	gameHeartbeat;		// increases every game frame
	unsigned int	gameHwnd;			// game's main window (HWNDs fit in 32 bits)

	long			usedPoseId;			// pose the last frame was rendered from (0 = none)
	long			viewMode;			// SHOGOVR_VIEW_
	long			swapEyes;			// 1 = right eye is on the left half
	float			renderTanX;			// tan(half-FOV) actually rendered per eye
	float			renderTanY;
	float			eyeSeparation;		// metres between the two rendered eye positions

	volatile long	recenterRequest;	// game increments this to ask for a recentre

	long			renderWidth;		// screen mode the game renders at
	long			renderHeight;
	long			clientWidth;		// game window's current client area, as the game sees it
	long			clientHeight;

	long			surfaceWidth;		// size the game is actually drawing at (its screen surface)
	long			surfaceHeight;

	long			layoutFlags;		// SHOGOVR_LAYOUT_
	long			eyeHeight;			// eye pictures use rows [0, eyeHeight) of the window (0 = all)
	long			hudX, hudY;			// the HUD strip, in window pixels
	long			hudW, hudH;
	float			hudAngle;			// HUD panel width, radians
	float			hudDistance;		// metres
	float			screenAngle;		// flat-screen width, radians

	// Direct capture: the game's renderer copies each finished frame into a
	// shared D3D11 texture (keyed mutex: game writes with key 0 -> 1, the
	// bridge reads with key 1 -> 0).  0 = not available.
	long			captureHandle;		// shared texture handle (32-bit value, valid across processes)
	long			captureWidth;
	long			captureHeight;
	long			captureFormat;		// DXGI_FORMAT
	long			captureFrame;		// counts copied frames
	long			capturePoseId;		// the pose the copied frame was rendered from - written with
										// the frame, under the keyed mutex, so the two always match
} ShogoVRShared;

#pragma pack(pop)

#endif // __SHOGOVR_SHARED_H__
