// ======================================================================= //
//
// MODULE  : VRMenu.cpp
//
// PURPOSE : Options > vr settings - the VR mod's settings inside the game.
//
//           Up/down picks a setting, left/right changes it (Enter also
//           steps forward).  Changes are written to ShogoVR\ShogoVR.ini and
//           applied immediately: game settings via the console, headset
//           settings by the bridge, which watches the same file.
//
// ======================================================================= //

#include <windows.h>
#include "cpp_client_de.h"
#include "VRMenu.h"
#include "TextHelper.h"
#include "RiotMenu.h"
#include "VRStereo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VRS_CHOICE		0
#define VRS_RANGE		1
#define VRS_TOGGLE		2
#define VRS_ACTION		3

#define VRM_RECENTER	6
#define VRM_DEFAULTS	7
#define VRM_BACK		8

struct VRMenuSetting
{
	const char*	szLabel;
	const char*	szSection;		// ShogoVR.ini section ("Game" = console variable)
	const char*	szKey;
	int			nType;
	float		fDefault;
	float		fMin, fMax, fStep, fShow;	// ranges: fShow multiplies for display (100 for %)
	const char*	szUnit;
	int			nChoices;
	const char*	szChoice[8];
	float		fChoice[8];
};

// Trimmed to a short list (2026-10-13, #30); the removed rows are pinned to
// fixed defaults in the CVRStereo VarTrack Init calls / the bridge's own
// defaults.  Same settings and defaults as the desktop "Shogo VR Settings"
// window where a row still exists.
static VRMenuSetting s_Settings[VRMENU_ITEMS] =
{
	{ "Dominant hand",			"Controls",	"LeftHanded",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			2, { "right-handed", "left-handed" }, { 0, 1 } },
	{ "Hands flip",			"Game",		"VRHandFlip",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			2, { "off", "on" }, { 0, 1 } },
	{ "Turning",				"Game",		"VRSnapTurn",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			6, { "snap 15 deg", "snap 30 deg", "snap 45 deg", "snap 60 deg", "snap 90 deg", "smooth" }, { 15, 30, 45, 60, 90, 0 } },
	{ "Smooth turn speed",		"Game",		"VRTurnSpeed",		VRS_RANGE,	150,	45, 360, 15, 1,		" deg/s",	0, { 0 }, { 0 } },
	{ "Move dead zone",			"Game",		"VRMoveDeadZone",	VRS_RANGE,	0.15f,	0, 0.5f, 0.01f,	100,	"%",	0, { 0 }, { 0 } },
	{ "Swap wep threshold",	"Game",		"VRWeaponSwapDead",	VRS_RANGE,	0.8f,	0, 1.0f, 0.01f,	100,	"%",	0, { 0 }, { 0 } },
	{ "Gesture melee sens.",	"Game",		"VRMeleeSens",		VRS_CHOICE,	15,		0, 0, 0, 1,			"",			8, { "OFF", "10%", "15%", "25%", "30%", "40%", "60%", "80%" }, { 0, 10, 15, 25, 30, 40, 60, 80 } },
	{ "Recentre view",			"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Reset to defaults",		"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Back",					"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
};

static int ChoiceIndex(const VRMenuSetting& s, float v)
{
	int i, nBest = 0;
	float fBest = 1e9f;
	for (i = 0; i < s.nChoices; i++)
	{
		float d = (float)fabs(s.fChoice[i] - v);
		if (d < fBest) { fBest = d; nBest = i; }
	}
	return nBest;
}

// ======================================================================= //

CVRMenu::CVRMenu()
{
	m_nSecondColumn = 0;
	m_szIni[0] = 0;
	int i;
	for (i = 0; i < VRMENU_ITEMS; i++) m_fValue[i] = s_Settings[i].fDefault;
}

DBOOL CVRMenu::Init (CClientDE* pClientDE, CRiotMenu* pRiotMenu, CBaseMenu* pParent, int nScreenWidth, int nScreenHeight)
{
	DBOOL bSuccess = CBaseMenu::Init (pClientDE, pRiotMenu, pParent, nScreenWidth, nScreenHeight);
	m_nSecondColumn = (nScreenWidth < 640) ? 170 : 200;
	return bSuccess;
}

void CVRMenu::ScreenDimsChanged (int nScreenWidth, int nScreenHeight)
{
	m_nSecondColumn = (nScreenWidth < 640) ? 170 : 200;
	CBaseMenu::ScreenDimsChanged (nScreenWidth, nScreenHeight);
}

const char* CVRMenu::IniPath()
{
	if (!m_szIni[0])
	{
		const char* p = g_VRStereo.GetIniPath();
		if (p && p[0])
		{
			strncpy (m_szIni, p, sizeof(m_szIni) - 1);
		}
		else
		{
			char szDir[MAX_PATH];
			szDir[0] = 0;
			GetCurrentDirectoryA (MAX_PATH, szDir);
			_snprintf (m_szIni, sizeof(m_szIni) - 1, "%s\\ShogoVR\\ShogoVR.ini", szDir);
		}
		m_szIni[sizeof(m_szIni) - 1] = 0;
	}
	return m_szIni;
}

void CVRMenu::ReadValues()
{
	int i;
	for (i = 0; i < VRMENU_ITEMS; i++)
	{
		const VRMenuSetting& s = s_Settings[i];
		if (s.nType == VRS_ACTION) continue;

		char szDef[32], szBuf[64];
		_snprintf (szDef, sizeof(szDef) - 1, "%g", s.fDefault);
		szDef[sizeof(szDef) - 1] = 0;
		GetPrivateProfileStringA (s.szSection, s.szKey, szDef, szBuf, sizeof(szBuf), IniPath());
		m_fValue[i] = (float)atof (szBuf);
	}
}

void CVRMenu::FormatValue (int nItem, char* szOut, int nOutSize)
{
	const VRMenuSetting& s = s_Settings[nItem];
	float v = m_fValue[nItem];
	szOut[0] = 0;

	if (s.nType == VRS_CHOICE)
	{
		_snprintf (szOut, nOutSize - 1, "%s", s.szChoice[ChoiceIndex(s, v)]);
	}
	else if (s.nType == VRS_TOGGLE)
	{
		_snprintf (szOut, nOutSize - 1, "%s", v != 0.0f ? "on" : "off");
	}
	else if (s.nType == VRS_RANGE)
	{
		float fShown = v * s.fShow;
		if (s.fStep * s.fShow < 0.999f) _snprintf (szOut, nOutSize - 1, "%.1f%s", fShown, s.szUnit);
		else							_snprintf (szOut, nOutSize - 1, "%.0f%s", fShown, s.szUnit);
	}
	szOut[nOutSize - 1] = 0;
}

DBOOL CVRMenu::MakeValueSurface (int nItem)
{
	if (!m_pClientDE || !m_pRiotMenu) return DFALSE;

	if (m_Values[nItem].hMenuItem) m_pClientDE->DeleteSurface (m_Values[nItem].hMenuItem);
	if (m_Values[nItem].hMenuItemSelected) m_pClientDE->DeleteSurface (m_Values[nItem].hMenuItemSelected);
	m_Values[nItem].hMenuItem = DNULL;
	m_Values[nItem].hMenuItemSelected = DNULL;

	if (s_Settings[nItem].nType == VRS_ACTION) return DTRUE;

	char szText[64];
	FormatValue (nItem, szText, sizeof(szText));
	m_Values[nItem].hMenuItem = CTextHelper::CreateSurfaceFromString (m_pClientDE, m_pRiotMenu->GetFont12n(), szText);
	m_Values[nItem].hMenuItemSelected = CTextHelper::CreateSurfaceFromString (m_pClientDE, m_pRiotMenu->GetFont12s(), szText);
	return (m_Values[nItem].hMenuItem && m_Values[nItem].hMenuItemSelected);
}

// Writes one setting to ShogoVR.ini and, for game settings, applies it now.
void CVRMenu::WriteValue (int nItem)
{
	const VRMenuSetting& s = s_Settings[nItem];
	if (s.nType == VRS_ACTION || !m_pClientDE) return;

	char szVal[32];
	_snprintf (szVal, sizeof(szVal) - 1, "%g", m_fValue[nItem]);
	szVal[sizeof(szVal) - 1] = 0;
	WritePrivateProfileStringA (s.szSection, s.szKey, szVal, IniPath());

	if (strcmp (s.szSection, "Game") == 0)
	{
		char szCmd[96];
		_snprintf (szCmd, sizeof(szCmd) - 1, "%s %s", s.szKey, szVal);
		szCmd[sizeof(szCmd) - 1] = 0;
		m_pClientDE->RunConsoleString (szCmd);
	}
}

void CVRMenu::Change (int nItem, int nDir)
{
	const VRMenuSetting& s = s_Settings[nItem];
	float& v = m_fValue[nItem];

	if (s.nType == VRS_CHOICE)
	{
		int n = ChoiceIndex (s, v) + nDir;
		if (n < 0) n = s.nChoices - 1;
		if (n >= s.nChoices) n = 0;
		v = s.fChoice[n];
	}
	else if (s.nType == VRS_TOGGLE)
	{
		v = (v != 0.0f) ? 0.0f : 1.0f;
	}
	else if (s.nType == VRS_RANGE)
	{
		// Snap to the step grid so repeated presses don't drift.
		float fSteps = (float)floor ((v - s.fMin) / s.fStep + 0.5f) + (float)nDir;
		v = s.fMin + fSteps * s.fStep;
		if (v < s.fMin) v = s.fMin;
		if (v > s.fMax) v = s.fMax;
	}
	else return;

	WriteValue (nItem);
	MakeValueSurface (nItem);
	if (nDir < 0) PlayLeftSound(); else PlayRightSound();
}

void CVRMenu::Reset()
{
	ReadValues();
	int i;
	for (i = 0; i < VRMENU_ITEMS; i++) MakeValueSurface (i);
	CBaseMenu::Reset();
}

void CVRMenu::Left()
{
	if (m_nSelection >= 0 && m_nSelection < VRMENU_ITEMS) Change (m_nSelection, -1);
}

void CVRMenu::Right()
{
	if (m_nSelection >= 0 && m_nSelection < VRMENU_ITEMS) Change (m_nSelection, +1);
}

void CVRMenu::Return()
{
	if (!m_pRiotMenu) return;

	if (m_nSelection == VRM_BACK)
	{
		m_pRiotMenu->SetCurrentMenu (m_pParent);
		CBaseMenu::Return();
	}
	else if (m_nSelection == VRM_RECENTER)
	{
		g_VRStereo.RequestRecenter();
		PlayReturnSound();
	}
	else if (m_nSelection == VRM_DEFAULTS)
	{
		int i;
		for (i = 0; i < VRMENU_ITEMS; i++)
		{
			if (s_Settings[i].nType == VRS_ACTION) continue;
			m_fValue[i] = s_Settings[i].fDefault;
			WriteValue (i);
			MakeValueSurface (i);
		}
		PlayReturnSound();
	}
	else if (m_nSelection >= 0 && m_nSelection < VRMENU_ITEMS)
	{
		Change (m_nSelection, +1);		// Enter / A steps forward too
	}
}

void CVRMenu::Draw (HSURFACE hScreen, int nScreenWidth, int nScreenHeight, int nTextOffset)
{
	if (!m_pClientDE) return;

	CBaseMenu::Draw (hScreen, nScreenWidth, nScreenHeight, nTextOffset);

	// Values in a second column, lined up with the (possibly scrolled) labels.
	int y = m_nMenuY + m_szMenuTitle.cy + m_nMenuTitleSpacing;
	int i;
	for (i = m_nTopItem; i < VRMENU_ITEMS; i++)
	{
		if (m_Values[i].hMenuItem)
		{
			m_pClientDE->DrawSurfaceToSurfaceTransparent (hScreen, m_nSelection == i ? m_Values[i].hMenuItemSelected : m_Values[i].hMenuItem,
														  DNULL, m_nMenuX + m_nSecondColumn, y, DNULL);
		}
		int nRowH = (int)m_GenericItem[i].szMenuItem.cy;
		y += nRowH + m_nMenuSpacing;
		if (y + nRowH > GetMenuAreaBottom()) break;		// the rest is scrolled off
	}
}

DBOOL CVRMenu::LoadSurfaces()
{
	if (!m_pClientDE || !m_pRiotMenu) return DFALSE;

	// Labels in the menu's small font: plain, and highlighted when selected.
	CBitmapFont* pLabelFont = m_pRiotMenu->GetFont12n();
	CBitmapFont* pHighlightFont = m_pRiotMenu->GetFont12s();

	int i;
	for (i = 0; i < VRMENU_ITEMS; i++)
	{
		m_GenericItem[i].hMenuItem = CTextHelper::CreateSurfaceFromString (m_pClientDE, pLabelFont, (char*)s_Settings[i].szLabel);
		m_GenericItem[i].hMenuItemSelected = CTextHelper::CreateSurfaceFromString (m_pClientDE, pHighlightFont, (char*)s_Settings[i].szLabel);
	}

	m_hMenuTitle = CTextHelper::CreateSurfaceFromString (m_pClientDE, pLabelFont, (char*)"VR SETTINGS");
	m_pClientDE->GetSurfaceDims (m_hMenuTitle, &m_szMenuTitle.cx, &m_szMenuTitle.cy);

	ReadValues();
	for (i = 0; i < VRMENU_ITEMS; i++)
	{
		if (!m_GenericItem[i].hMenuItem || !m_GenericItem[i].hMenuItemSelected || !MakeValueSurface (i))
		{
			UnloadSurfaces();
			return DFALSE;
		}
		m_pClientDE->GetSurfaceDims (m_GenericItem[i].hMenuItem, &m_GenericItem[i].szMenuItem.cx, &m_GenericItem[i].szMenuItem.cy);
	}

	return CBaseMenu::LoadSurfaces();
}

static void VRFreeItemSurfaces(CClientDE* pClientDE, GENERIC_ITEM& item)
{
	HSURFACE* pSurfaces[2] = { &item.hMenuItem, &item.hMenuItemSelected };
	int k;
	for (k = 0; k < 2; k++)
	{
		if (*pSurfaces[k]) pClientDE->DeleteSurface(*pSurfaces[k]);
		*pSurfaces[k] = DNULL;
	}
}

void CVRMenu::UnloadSurfaces()
{
	if (!m_pClientDE) return;

	int i;
	for (i = 0; i < VRMENU_ITEMS; i++)
	{
		VRFreeItemSurfaces(m_pClientDE, m_GenericItem[i]);
		VRFreeItemSurfaces(m_pClientDE, m_Values[i]);
		m_GenericItem[i].szMenuItem.cx = 0;
		m_GenericItem[i].szMenuItem.cy = 0;
	}
	if (m_hMenuTitle)
	{
		m_pClientDE->DeleteSurface(m_hMenuTitle);
		m_hMenuTitle = DNULL;
	}

	CBaseMenu::UnloadSurfaces();
}

void CVRMenu::PostCalculateMenuDims()
{
	if (!m_pClientDE) return;

	// Centre labels + values as one block.
	int nMenuMaxWidth = 0, i;
	for (i = 0; i < VRMENU_ITEMS; i++)
	{
		int w = (int)m_GenericItem[i].szMenuItem.cx;
		if (m_Values[i].hMenuItem)
		{
			DDWORD nW = 0, nH = 0;
			m_pClientDE->GetSurfaceDims (m_Values[i].hMenuItem, &nW, &nH);
			if (m_nSecondColumn + (int)nW > w) w = m_nSecondColumn + (int)nW;
		}
		if (w > nMenuMaxWidth) nMenuMaxWidth = w;
	}
	m_nMenuX = GetMenuAreaLeft() + ((int)m_szMenuArea.cx - nMenuMaxWidth) / 2;
}
