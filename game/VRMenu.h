// ======================================================================= //
//
// MODULE  : VRMenu.h
//
// PURPOSE : Options > vr settings - the VR mod's settings inside the game,
//           usable with the motion controllers (stick up/down to choose,
//           left/right to change).  Shares ShogoVR\ShogoVR.ini with the
//           desktop "Shogo VR Settings" window and the headset bridge.
//
//           Two pages: the everyday list, and "VR ADVANCED" behind it for the
//           per-weapon hand settings that were previously ini-only.
//
// ======================================================================= //

#ifndef __VRMENU_H
#define __VRMENU_H

#include "BaseMenu.h"

// The menu has two pages, drawn by the one object: the everyday list and a
// short "VR ADVANCED" page holding the two master switches for the per-weapon
// hand features.  Both pages share the item, value and surface arrays, so
// these must be sized for the LONGER of the two - the everyday page today.
#define VRMENU_PAGE_MAIN	0
#define VRMENU_PAGE_ADV		1
#define VRMENU_PAGES		2

#define VRMENU_ITEMS	13

#if VRMENU_ITEMS > MAX_GENERIC_ITEMS
#error VRMenu has more items than CBaseMenu::m_GenericItem can hold
#endif

class CVRMenu : public CBaseMenu
{
public:

	CVRMenu();

	virtual DBOOL		Init (CClientDE* pClientDE, CRiotMenu* pRiotMenu, CBaseMenu* pParent, int nScreenWidth, int nScreenHeight);
	virtual void		ScreenDimsChanged (int nScreenWidth, int nScreenHeight);
	virtual void		Reset();

	virtual DBOOL		LoadAllSurfaces()		{ return LoadSurfaces(); }
	virtual void		UnloadAllSurfaces()		{ UnloadSurfaces(); }

	virtual void		Left();
	virtual void		Right();
	virtual void		Return();
	virtual void		Esc();

	virtual void		Draw (HSURFACE hScreen, int nScreenWidth, int nScreenHeight, int nTextOffset = 0);

protected:

	virtual DBOOL		LoadSurfaces();
	virtual void		UnloadSurfaces();
	virtual void		PostCalculateMenuDims();

	// How many rows the page being shown actually has - every loop that used to
	// run to VRMENU_ITEMS runs to this instead, so the shorter page leaves the
	// rows above it empty and CBaseMenu's own "is there a surface here" item
	// count skips them.
	int					ItemCount() const;

	// Switches page in place: frees this page's surfaces, builds the other
	// page's, and lets CBaseMenu re-measure and re-centre the result.
	void				SetPage (int nPage);

	void				ReadValues();
	void				Change (int nItem, int nDir);
	void				WriteValue (int nItem);
	void				FormatValue (int nItem, char* szOut, int nOutSize);
	DBOOL				MakeValueSurface (int nItem);
	const char*			IniPath();

protected:

	int					m_nPage;
	int					m_nSecondColumn;
	GENERIC_ITEM		m_Values[VRMENU_ITEMS];
	float				m_fValue[VRMENU_ITEMS];
	char				m_szIni[300];
};

#endif
