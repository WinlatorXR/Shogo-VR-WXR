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
//           Two pages share this one object: "VR SETTINGS" (the everyday list)
//           and "VR ADVANCED" behind its "Advanced settings" row, which holds
//           the dual-pistol, free-off-hand, lent-hand and two-handed-grip
//           settings that could previously only be changed by editing the ini.
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

// The action rows (#49): these used to be three fixed indices, which silently
// went stale when VRMENU_ITEMS grew to 12 - Return() then matched the three
// SETTINGS rows above them, so "Wep Swap Via RS" reset every VR setting and
// "Recentre view" did nothing at all.  The actions are matched by label now,
// so adding or moving a row can never point them at the wrong thing again.
#define VRM_ACT_ADVANCED	"Advanced settings"
#define VRM_ACT_RECENTER	"Recentre view"
#define VRM_ACT_DEFAULTS	"Reset to defaults"
#define VRM_ACT_BACK		"Back"

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
static VRMenuSetting s_Main[] =
{
	{ "Dominant hand",			"Controls",	"LeftHanded",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			2, { "right-handed", "left-handed" }, { 0, 1 } },
	{ "Hands flip",			"Game",		"VRHandFlip",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			2, { "off", "on" }, { 0, 1 } },
	{ "Turning",				"Game",		"VRSnapTurn",		VRS_CHOICE,	0,		0, 0, 0, 1,			"",			6, { "snap 15 deg", "snap 30 deg", "snap 45 deg", "snap 60 deg", "snap 90 deg", "smooth" }, { 15, 30, 45, 60, 90, 0 } },
	{ "Smooth turn speed",		"Game",		"VRTurnSpeed",		VRS_RANGE,	150,	45, 360, 15, 1,		" deg/s",	0, { 0 }, { 0 } },
	{ "Move dead zone",			"Game",		"VRMoveDeadZone",	VRS_RANGE,	0.15f,	0, 0.5f, 0.01f,	100,	"%",	0, { 0 }, { 0 } },
	{ "Swap wep threshold",	"Game",		"VRWeaponSwapDead",	VRS_RANGE,	0.8f,	0, 1.0f, 0.01f,	100,	"%",	0, { 0 }, { 0 } },
	{ "Physical wep swap",		"Game",		"VRPhysicalWepSwap",	VRS_TOGGLE,	0,		0, 1, 1, 1,			"",			0, { 0 }, { 0 } },
	{ "Wep Swap Via RS",		"Game",		"VRWepSwapThumbstick",VRS_TOGGLE,	1,		0, 1, 1, 1,			"",			0, { 0 }, { 0 } },
	{ "Gesture melee sens.",	"Game",		"VRMeleeSens",		VRS_CHOICE,	15,		0, 0, 0, 1,			"",			8, { "OFF", "10%", "15%", "25%", "30%", "40%", "60%", "80%" }, { 0, 10, 15, 25, 30, 40, 60, 80 } },
	{ "Advanced settings",		"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Recentre view",			"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Reset to defaults",		"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Back",					"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
};

// Page 2 - "VR ADVANCED".  Deliberately two rows: the owner asked for the two
// decisions, not the knobs behind them.  Each is a master switch whose "off"
// is the behaviour from before the feature existed, so a player who dislikes
// either gets V1.0 back without knowing a CVar name.
//
// HUD size is here rather than on the everyday page because it exists for one
// situation: a WinlatorXR container run above the headset's own resolution for
// supersampling (#6), where the HUD is 2D art blitted at fixed sizes and so
// covers a different share of the view.  VRHudAutoScale already corrects for
// that automatically; this row trims around the result, and its range reaches
// far enough to undo the correction entirely.
//
// Everything finer - which half of a model is the off hand, the seating trims,
// the roll corrections, the grip reach and angle, the lent hands - stays in
// ShogoVR.ini. Those were all settled on device once and should not be sitting
// in front of a player who has no way to tell what they do.
//
// Defaults here MUST match the VarTrack defaults in CVRStereo::Init, or "Reset
// to defaults" would quietly move the mod off its tested settings.
static VRMenuSetting s_Adv[] =
{
	{ "Dual pistols",			"Game",		"VRDualPistols",		VRS_TOGGLE,	1,		0, 1, 1, 1,			"",			0, { 0 }, { 0 } },
	{ "Two handed weps",		"Game",		"VRTwoHandWeapons",		VRS_TOGGLE,	1,		0, 1, 1, 1,			"",			0, { 0 }, { 0 } },
	{ "HUD size",				"Game",		"VRHudScale",			VRS_RANGE,	1.0f,	0.5f, 1.5f, 0.05f, 100,	"%",	0, { 0 }, { 0 } },
	{ "Verbose debug",			"Game",		"VRVerboseDebug",		VRS_TOGGLE,	0,		0, 1, 1, 1,			"",			0, { 0 }, { 0 } },
	{ "Reset to defaults",		"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
	{ "Back",					"",			"",					VRS_ACTION,	0,		0, 0, 0, 1,			"",			0, { 0 }, { 0 } },
};

static VRMenuSetting* const s_Page[VRMENU_PAGES]	= { s_Main, s_Adv };
static const int s_PageItems[VRMENU_PAGES]			= { sizeof(s_Main) / sizeof(s_Main[0]),
													    sizeof(s_Adv)  / sizeof(s_Adv[0]) };
static const char* const s_PageTitle[VRMENU_PAGES]	= { "VR SETTINGS", "VR ADVANCED" };

// The row being talked about, on whichever page is showing.
#define ROW(i)	(s_Page[m_nPage][i])

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
	m_nPage = VRMENU_PAGE_MAIN;		// before ItemCount()/ROW(), both of which read it
	m_nSecondColumn = 0;
	m_szIni[0] = 0;
	int i;
	for (i = 0; i < ItemCount(); i++) m_fValue[i] = ROW(i).fDefault;
}

int CVRMenu::ItemCount() const
{
	if (m_nPage < 0 || m_nPage >= VRMENU_PAGES) return s_PageItems[VRMENU_PAGE_MAIN];
	return s_PageItems[m_nPage];
}

// Swapping page is a full surface rebuild: the labels, the values, the title
// and every measurement belong to the page, and CBaseMenu counts its items by
// walking m_GenericItem for non-null surfaces - so the old page's rows have to
// be freed, not just drawn over, or the shorter page would keep the longer
// one's leftovers and the selection could land on a row that is not there.
//
// LoadSurfaces() ends in CBaseMenu::LoadSurfaces(), which recounts the items
// and calls CalculateMenuDims(), so the new page re-centres itself.
void CVRMenu::SetPage (int nPage)
{
	if (nPage < 0 || nPage >= VRMENU_PAGES) return;
	if (nPage == m_nPage) return;

	UnloadSurfaces();
	m_nPage = nPage;
	m_nSelection = 0;
	m_nTopItem = 0;
	LoadSurfaces();
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
	for (i = 0; i < ItemCount(); i++)
	{
		const VRMenuSetting& s = ROW(i);
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
	const VRMenuSetting& s = ROW(nItem);
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

	if (ROW(nItem).nType == VRS_ACTION) return DTRUE;

	char szText[64];
	FormatValue (nItem, szText, sizeof(szText));
	m_Values[nItem].hMenuItem = CTextHelper::CreateSurfaceFromString (m_pClientDE, m_pRiotMenu->GetFont12n(), szText);
	m_Values[nItem].hMenuItemSelected = CTextHelper::CreateSurfaceFromString (m_pClientDE, m_pRiotMenu->GetFont12s(), szText);
	return (m_Values[nItem].hMenuItem && m_Values[nItem].hMenuItemSelected);
}

// Writes one setting to ShogoVR.ini and, for game settings, applies it now.
void CVRMenu::WriteValue (int nItem)
{
	const VRMenuSetting& s = ROW(nItem);
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
	const VRMenuSetting& s = ROW(nItem);
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

	// Physical motion weapon swapping (#4): mutual exclusion - cannot have both off
	if (strcmp(s.szKey, "VRPhysicalWepSwap") == 0 || strcmp(s.szKey, "VRWepSwapThumbstick") == 0)
	{
		// find the other
		for (int i = 0; i < ItemCount(); i++)
		{
			if (ROW(i).nType == VRS_ACTION) continue;
			if (strcmp(ROW(i).szKey, "VRPhysicalWepSwap") == 0 && strcmp(s.szKey, "VRWepSwapThumbstick") == 0)
			{
				// ensure at least one is on
				if (m_fValue[i] == 0.0f && m_fValue[nItem] == 0.0f)
				{
					m_fValue[i] = 1.0f;
					WriteValue(i);
					MakeValueSurface(i);
				}
			}
			else if (strcmp(ROW(i).szKey, "VRWepSwapThumbstick") == 0 && strcmp(s.szKey, "VRPhysicalWepSwap") == 0)
			{
				if (m_fValue[i] == 0.0f && m_fValue[nItem] == 0.0f)
				{
					m_fValue[i] = 1.0f;
					WriteValue(i);
					MakeValueSurface(i);
				}
			}
		}
	}

	WriteValue (nItem);
	MakeValueSurface (nItem);
	if (nDir < 0) PlayLeftSound(); else PlayRightSound();
}

void CVRMenu::Reset()
{
	// Re-entering the menu always starts on the everyday page.  SetPage both
	// re-reads the ini and rebuilds the surfaces, so that path needs no more.
	if (m_nPage != VRMENU_PAGE_MAIN)
	{
		SetPage (VRMENU_PAGE_MAIN);
		CBaseMenu::Reset();
		return;
	}

	ReadValues();
	int i;
	for (i = 0; i < ItemCount(); i++) MakeValueSurface (i);
	CBaseMenu::Reset();
}

void CVRMenu::Left()
{
	if (m_nSelection >= 0 && m_nSelection < ItemCount()) Change (m_nSelection, -1);
}

void CVRMenu::Right()
{
	if (m_nSelection >= 0 && m_nSelection < ItemCount()) Change (m_nSelection, +1);
}

void CVRMenu::Return()
{
	if (!m_pRiotMenu) return;

	if (m_nSelection < 0 || m_nSelection >= ItemCount()) return;

	const VRMenuSetting& sel = ROW(m_nSelection);
	DBOOL bAction = (DBOOL)(sel.nType == VRS_ACTION);

	if (bAction && strcmp (sel.szLabel, VRM_ACT_ADVANCED) == 0)
	{
		SetPage (VRMENU_PAGE_ADV);
		PlayReturnSound();
	}
	else if (bAction && strcmp (sel.szLabel, VRM_ACT_BACK) == 0)
	{
		// Back off the advanced page returns to the everyday one; only Back on
		// that page leaves the VR menu altogether.
		if (m_nPage != VRMENU_PAGE_MAIN)
		{
			SetPage (VRMENU_PAGE_MAIN);
			PlayReturnSound();
		}
		else
		{
			m_pRiotMenu->SetCurrentMenu (m_pParent);
			CBaseMenu::Return();
		}
	}
	else if (bAction && strcmp (sel.szLabel, VRM_ACT_RECENTER) == 0)
	{
		g_VRStereo.RequestRecenter();
		PlayReturnSound();
	}
	else if (bAction && strcmp (sel.szLabel, VRM_ACT_DEFAULTS) == 0)
	{
		int i;
		for (i = 0; i < ItemCount(); i++)
		{
			if (ROW(i).nType == VRS_ACTION) continue;
			m_fValue[i] = ROW(i).fDefault;
			WriteValue (i);
			MakeValueSurface (i);
		}
		// Enforce mutual exclusion after reset
		{
			float fPhys = 1.0f;
			float fThumb = 1.0f;
			int iPhys = -1, iThumb = -1;
			for (i = 0; i < ItemCount(); i++)
			{
				if (strcmp(ROW(i).szKey, "VRPhysicalWepSwap") == 0) { fPhys = m_fValue[i]; iPhys = i; }
				if (strcmp(ROW(i).szKey, "VRWepSwapThumbstick") == 0) { fThumb = m_fValue[i]; iThumb = i; }
			}
			if (iPhys >= 0 && iThumb >= 0 && fPhys == 0.0f && fThumb == 0.0f)
			{
				m_fValue[iThumb] = 1.0f;
				WriteValue(iThumb);
				MakeValueSurface(iThumb);
			}
		}
		PlayReturnSound();
	}
	else if (!bAction)
	{
		Change (m_nSelection, +1);		// Enter / A steps forward too
	}
}

// Esc / B on the advanced page steps back a page rather than closing the whole
// VR menu, which is what every other nested page in this game does.
void CVRMenu::Esc()
{
	if (m_nPage != VRMENU_PAGE_MAIN)
	{
		SetPage (VRMENU_PAGE_MAIN);
		PlayEscSound();
		return;
	}
	CBaseMenu::Esc();
}

void CVRMenu::Draw (HSURFACE hScreen, int nScreenWidth, int nScreenHeight, int nTextOffset)
{
	if (!m_pClientDE) return;

	CBaseMenu::Draw (hScreen, nScreenWidth, nScreenHeight, nTextOffset);

	// Values in a second column, lined up with the (possibly scrolled) labels.
	int y = m_nMenuY + m_szMenuTitle.cy + m_nMenuTitleSpacing;
	int i;
	for (i = m_nTopItem; i < ItemCount(); i++)
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
	for (i = 0; i < ItemCount(); i++)
	{
		m_GenericItem[i].hMenuItem = CTextHelper::CreateSurfaceFromString (m_pClientDE, pLabelFont, (char*)ROW(i).szLabel);
		m_GenericItem[i].hMenuItemSelected = CTextHelper::CreateSurfaceFromString (m_pClientDE, pHighlightFont, (char*)ROW(i).szLabel);
	}

	m_hMenuTitle = CTextHelper::CreateSurfaceFromString (m_pClientDE, pLabelFont, (char*)s_PageTitle[m_nPage]);
	m_pClientDE->GetSurfaceDims (m_hMenuTitle, &m_szMenuTitle.cx, &m_szMenuTitle.cy);

	ReadValues();
	for (i = 0; i < ItemCount(); i++)
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
