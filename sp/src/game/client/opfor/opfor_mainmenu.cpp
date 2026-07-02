//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Custom main menu.
//
//   TOP BAR  Accent colour fades right → translucent. Nav buttons inside.
//   LEFT     Save info (one line) + RESUME button — positions from
//            resource/opfor_mainmenu.res so you can move them without
//            recompiling.
//   BOTTOM   Quit button left-aligned. Live date/time at the bottom right.
//
//=============================================================================

#include "cbase.h"
#include "opfor_mainmenu.h"

#include <vgui/VGUI.h>
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Label.h>
#include "ienginevgui.h"
#include "interface.h"
#include "GameUI/IGameUI.h"
#include "filesystem.h"
#include "KeyValues.h"
#include <time.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static IGameUI *g_pGameUI = NULL;

//-----------------------------------------------------------------------------
// Colors
//-----------------------------------------------------------------------------
static const Color ACCENT      ( 180, 240,   0, 255 );
static const Color TEXT_IDLE   ( 200, 200, 200, 180 );
static const Color TEXT_HOVER  ( 255, 255, 255, 255 );
static const Color TEXT_PRESS  ( 180, 240,   0, 255 );
static const Color BG_CLEAR    (   0,   0,   0,   0 );
static const Color BG_HOVER    (   0,   0,   0,  55 );
static const Color BAR_BG      (   0,   0,   0, 255 );
static const Color SAVE_INFO_C ( 200, 200, 200, 220 );
static const Color DATETIME_C  ( 160, 160, 160, 200 );

//-----------------------------------------------------------------------------
// Layout constants
//-----------------------------------------------------------------------------
static const int ACCENT_LINE  = 2;
static const int TOP_BAR_H    = 50;
static const int BOT_BAR_H    = 50;
static const int FADE_PCT     = 65;   // where the accent tint starts fading
static const int LEFT_MARGIN  = 40;

//-----------------------------------------------------------------------------
// Default positions used when the .res file is absent or a key is missing.
// Change these by editing resource/opfor_mainmenu.res instead.
//-----------------------------------------------------------------------------
static const int DEF_SAVEINFO_X = 40;
static const int DEF_SAVEINFO_Y = 400;
static const int DEF_SAVEINFO_W = 600;
static const int DEF_SAVEINFO_H = 26;

static const int DEF_RESUME_X   = 40;
static const int DEF_RESUME_Y   = 440;
static const int DEF_RESUME_W   = 180;
static const int DEF_RESUME_H   = 50;

//-----------------------------------------------------------------------------
// Latest-save helper — returns a single combined display string.
//-----------------------------------------------------------------------------
static bool GetLatestSaveInfo( char *pOut, int outLen )
{
	pOut[0] = '\0';

	FileFindHandle_t fh;
	const char *pFile = g_pFullFileSystem->FindFirstEx( "save/*.sav", "MOD", &fh );

	long latestTime = 0;
	char latestName[MAX_PATH] = "";

	while ( pFile )
	{
		char rel[MAX_PATH];
		Q_snprintf( rel, sizeof( rel ), "save/%s", pFile );
		long t = g_pFullFileSystem->GetFileTime( rel, "MOD" );
		if ( t > latestTime ) { latestTime = t; Q_strncpy( latestName, pFile, sizeof( latestName ) ); }
		pFile = g_pFullFileSystem->FindNext( fh );
	}
	g_pFullFileSystem->FindClose( fh );

	if ( !latestName[0] || latestTime == 0 )
		return false;

	// Clean up the filename
	char name[MAX_PATH];
	Q_strncpy( name, latestName, sizeof( name ) );
	char *ext = Q_stristr( name, ".sav" );
	if ( ext ) *ext = '\0';
	bool cap = true;
	for ( char *c = name; *c; c++ )
	{
		if ( *c == '_' || *c == '-' ) { *c = ' '; cap = true; }
		else if ( cap ) { *c = (char)toupper( (unsigned char)*c ); cap = false; }
	}

	// Format the date
	char dateBuf[64];
	time_t ts = (time_t)latestTime;
	struct tm *ti = localtime( &ts );
	if ( ti ) strftime( dateBuf, sizeof( dateBuf ), "%d %b %Y  %H:%M", ti );
	else       Q_strncpy( dateBuf, "Unknown date", sizeof( dateBuf ) );

	Q_snprintf( pOut, outLen, "%s   |   %s", name, dateBuf );
	return true;
}

//-----------------------------------------------------------------------------
// Flat button — optional inverted hover (white bg, black text).
//-----------------------------------------------------------------------------
class CMenuButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( CMenuButton, vgui::Button );
public:
	CMenuButton( vgui::Panel *parent, const char *name, const char *text,
	             vgui::Panel *target, const char *cmd, bool bInvertHover = false )
		: BaseClass( parent, name, text, target, cmd )
		, m_bInvertHover( bInvertHover )
	{
		SetContentAlignment( vgui::Label::a_center );
	}

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		vgui::HFont h = pScheme->GetFont( "MenuButtonFont", false );
		if ( h != vgui::INVALID_FONT ) SetFont( h );

		if ( m_bInvertHover )
		{
			SetDefaultColor  ( TEXT_IDLE,                BG_CLEAR               );
			SetArmedColor    ( Color(   0,   0,   0, 255 ), Color( 255, 255, 255, 255 ) );
			SetDepressedColor( Color(   0,   0,   0, 255 ), Color( 220, 220, 220, 255 ) );
		}
		else
		{
			SetDefaultColor  ( TEXT_IDLE,  BG_CLEAR );
			SetArmedColor    ( TEXT_HOVER, BG_CLEAR );
			SetDepressedColor( TEXT_PRESS, BG_CLEAR );
		}
		SetBorder( NULL ); SetDefaultBorder( NULL );
		SetDepressedBorder( NULL ); SetKeyFocusBorder( NULL );
	}

	virtual void PaintBackground()
	{
		BaseClass::PaintBackground();   // must call — sets fg colour for text

		if ( !IsArmed() && !IsDepressed() )
			return;

		int w, h; GetSize( w, h );
		if ( m_bInvertHover )
		{
			Color bg = IsDepressed() ? Color( 220, 220, 220, 255 ) : Color( 255, 255, 255, 255 );
			vgui::surface()->DrawSetColor( bg );
			vgui::surface()->DrawFilledRect( 0, 0, w, h );
		}
		else
		{
			vgui::surface()->DrawSetColor( BG_HOVER );
			vgui::surface()->DrawFilledRect( 0, 0, w, h );
			vgui::surface()->DrawSetColor( ACCENT );
			vgui::surface()->DrawFilledRect( 0, 0, w, ACCENT_LINE );
		}
	}

private:
	bool m_bInvertHover;
};

//-----------------------------------------------------------------------------
// Menu entries
//-----------------------------------------------------------------------------
struct MenuEntry { const char *label, *command; bool inGameOnly, outOfGameOnly; };

static const MenuEntry k_Top[] =
{
	{ "NEW GAME",     "OpenNewGameDialog",      false, true  },
	{ "LOAD",         "OpenLoadGameDialog",     false, false },
	{ "SAVE",         "OpenSaveGameDialog",     true,  false },
	{ "ACHIEVEMENTS", "OpenAchievementsDialog", false, false },
	{ "OPTIONS",      "OpenOptionsDialog",      false, false },
};
static const MenuEntry k_Bot[] =
{
	{ "QUIT", "Quit", false, false },
};

//-----------------------------------------------------------------------------
// Read layout values from the .res file, falling back to defaults.
//-----------------------------------------------------------------------------
struct PanelRect { int x, y, w, h; };

static PanelRect ReadRect( KeyValues *pKV, const char *key,
                            int dx, int dy, int dw, int dh )
{
	PanelRect r = { dx, dy, dw, dh };
	if ( !pKV ) return r;
	KeyValues *pSub = pKV->FindKey( key );
	if ( !pSub ) return r;
	r.x = pSub->GetInt( "xpos", dx );
	r.y = pSub->GetInt( "ypos", dy );
	r.w = pSub->GetInt( "wide", dw );
	r.h = pSub->GetInt( "tall", dh );
	return r;
}

//-----------------------------------------------------------------------------
// The panel
//-----------------------------------------------------------------------------
class COpforMainMenu : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( COpforMainMenu, vgui::EditablePanel );
public:
	COpforMainMenu()
		: BaseClass( NULL, "OpforMainMenu" )
		, m_bWasInGame( false ), m_bHasSave( false )
		, m_flNextSaveRefresh( 0.f ), m_flNextClockRefresh( 0.f )
	{
		SetScheme( vgui::scheme()->LoadSchemeFromFile(
			"resource/SourceScheme.res", "SourceScheme" ) );

		for ( int i = 0; i < ARRAYSIZE( k_Top ); i++ )
			m_TopBtns.AddToTail( new CMenuButton( this, k_Top[i].label,
				k_Top[i].label, this, k_Top[i].command ) );
		for ( int i = 0; i < ARRAYSIZE( k_Bot ); i++ )
			m_BotBtns.AddToTail( new CMenuButton( this, k_Bot[i].label,
				k_Bot[i].label, this, k_Bot[i].command ) );

		m_pSaveInfo  = new vgui::Label( this, "SaveInfo",  "" );
		m_pResumeBtn = new CMenuButton( this, "Resume",    "RESUME", this, "ResumeGame", true );
		m_pDateTime  = new vgui::Label( this, "DateTime",  "" );

		SetPaintBackgroundEnabled( true );
		SetPaintBorderEnabled( false );
		vgui::ivgui()->AddTickSignal( GetVPanel() );
	}

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );
		SetBgColor( BG_CLEAR );
		SetBorder( NULL );

		auto StyleLabel = [&]( vgui::Label *l, Color col )
		{
			// Font follows the scheme, same as the rest of the UI.
			l->SetFgColor( col );
			l->SetPaintBackgroundEnabled( false );
		};

		StyleLabel( m_pSaveInfo, SAVE_INFO_C );
		m_pSaveInfo->SetContentAlignment( vgui::Label::a_west );

		StyleLabel( m_pDateTime, DATETIME_C );
		m_pDateTime->SetContentAlignment( vgui::Label::a_east );
	}

	virtual void OnTick()
	{
		bool bInGame = engine->IsInGame() && !engine->IsLevelMainMenuBackground();
		if ( bInGame != m_bWasInGame ) { m_bWasInGame = bInGame; InvalidateLayout(); }

		if ( gpGlobals->curtime >= m_flNextSaveRefresh )
		{
			m_flNextSaveRefresh = gpGlobals->curtime + 5.f;
			RefreshSaveInfo();
		}

		if ( gpGlobals->curtime >= m_flNextClockRefresh )
		{
			m_flNextClockRefresh = gpGlobals->curtime + 1.f;
			RefreshClock();
		}
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int sw, sh;
		vgui::surface()->GetScreenSize( sw, sh );
		SetBounds( 0, 0, sw, sh );

		bool bInGame  = m_bWasInGame;
		int  btnAreaW = sw * FADE_PCT / 100;

		// Load layout overrides from the res file (no caching — PerformLayout
		// isn't called every frame, only on InvalidateLayout).
		KeyValues *pLayout = new KeyValues( "OpforMainMenu" );
		bool bLoaded = pLayout->LoadFromFile( g_pFullFileSystem,
		                                      "resource/opfor_mainmenu.res", "GAME" );
		KeyValues *pKV = bLoaded ? pLayout : NULL;

		// --- Top buttons -------------------------------------------------
		int nTopVis = 0;
		for ( int i = 0; i < m_TopBtns.Count(); i++ )
		{
			const MenuEntry &e = k_Top[i];
			bool vis = !( e.inGameOnly && !bInGame ) && !( e.outOfGameOnly && bInGame );
			m_TopBtns[i]->SetVisible( vis );
			if ( vis ) nTopVis++;
		}
		if ( nTopVis > 0 )
		{
			int btnW = btnAreaW / nTopVis, x = 0;
			for ( int i = 0; i < m_TopBtns.Count(); i++ )
			{
				if ( !m_TopBtns[i]->IsVisible() ) continue;
				m_TopBtns[i]->SetBounds( x, 0, btnW, TOP_BAR_H );
				x += btnW;
			}
		}

		// --- Save info + Resume (positions from .res) --------------------
		bool showResume = m_bHasSave || bInGame;
		m_pSaveInfo->SetVisible( m_bHasSave );
		m_pResumeBtn->SetVisible( showResume );

		if ( m_bHasSave )
		{
			PanelRect r = ReadRect( pKV, "SaveInfo",
			    DEF_SAVEINFO_X, DEF_SAVEINFO_Y, DEF_SAVEINFO_W, DEF_SAVEINFO_H );
			m_pSaveInfo->SetBounds( r.x, r.y, r.w, r.h );
		}
		if ( showResume )
		{
			PanelRect r = ReadRect( pKV, "Resume",
			    DEF_RESUME_X, DEF_RESUME_Y, DEF_RESUME_W, DEF_RESUME_H );
			m_pResumeBtn->SetBounds( r.x, r.y, r.w, r.h );
		}

		pLayout->deleteThis();

		// --- Bottom buttons — left-aligned, one slot wide ----------------
		int nBotVis = 0;
		for ( int i = 0; i < m_BotBtns.Count(); i++ )
		{
			const MenuEntry &e = k_Bot[i];
			bool vis = !( e.inGameOnly && !bInGame ) && !( e.outOfGameOnly && bInGame );
			m_BotBtns[i]->SetVisible( vis );
			if ( vis ) nBotVis++;
		}
		if ( nBotVis > 0 && nTopVis > 0 )
		{
			int slotW = btnAreaW / nTopVis, x = 0;
			for ( int i = 0; i < m_BotBtns.Count(); i++ )
			{
				if ( !m_BotBtns[i]->IsVisible() ) continue;
				m_BotBtns[i]->SetBounds( x, sh - BOT_BAR_H, slotW, BOT_BAR_H );
				x += slotW;
			}
		}

		// --- Date/time — right side of bottom bar ------------------------
		const int kClockW = 300, kClockH = BOT_BAR_H;
		m_pDateTime->SetBounds( sw - kClockW - 12, sh - kClockH, kClockW, kClockH );
	}

	virtual void PaintBackground()
	{
		int sw, sh;
		vgui::surface()->GetScreenSize( sw, sh );

		// Top bar
		vgui::surface()->DrawSetColor( BAR_BG );
		vgui::surface()->DrawFilledRectFade( 0, 0, sw, TOP_BAR_H, 255, 80, true );

		vgui::surface()->DrawSetColor( ACCENT );
		vgui::surface()->DrawFilledRectFade( 0, 0, sw, TOP_BAR_H, 90, 30, true );

		vgui::surface()->DrawSetColor( ACCENT );
		vgui::surface()->DrawFilledRectFade( 0, TOP_BAR_H - ACCENT_LINE, sw, TOP_BAR_H, 255, 80, true );

		// Bottom bar
		vgui::surface()->DrawSetColor( BAR_BG );
		vgui::surface()->DrawFilledRectFade( 0, sh - BOT_BAR_H, sw, sh, 255, 80, true );

		vgui::surface()->DrawSetColor( ACCENT );
		vgui::surface()->DrawFilledRectFade( 0, sh - BOT_BAR_H, sw, sh, 90, 30, true );

		vgui::surface()->DrawSetColor( ACCENT );
		vgui::surface()->DrawFilledRectFade( 0, sh - BOT_BAR_H, sw,
		                                     sh - BOT_BAR_H + ACCENT_LINE, 255, 80, true );
	}

	virtual void OnCommand( const char *cmd )
	{
		if ( g_pGameUI ) g_pGameUI->SendMainMenuCommand( cmd );
	}

private:
	void RefreshSaveInfo()
	{
		char buf[256];
		m_bHasSave = GetLatestSaveInfo( buf, sizeof( buf ) );
		if ( m_bHasSave ) m_pSaveInfo->SetText( buf );
		InvalidateLayout();
	}

	void RefreshClock()
	{
		time_t now = time( NULL );
		struct tm *ti = localtime( &now );
		if ( !ti ) return;
		char buf[64];
		strftime( buf, sizeof( buf ), "%d %b %Y   %H:%M:%S", ti );
		m_pDateTime->SetText( buf );
	}

	CUtlVector<CMenuButton *>  m_TopBtns;
	CUtlVector<CMenuButton *>  m_BotBtns;
	vgui::Label               *m_pSaveInfo;
	CMenuButton               *m_pResumeBtn;
	vgui::Label               *m_pDateTime;
	bool   m_bWasInGame;
	bool   m_bHasSave;
	float  m_flNextSaveRefresh;
	float  m_flNextClockRefresh;
};

//-----------------------------------------------------------------------------
void InstallOpforMainMenu()
{
	CreateInterfaceFn gameUIFactory = Sys_GetFactory( "gameui" DLL_EXT_STRING );
	if ( !gameUIFactory ) return;
	g_pGameUI = (IGameUI *)gameUIFactory( GAMEUI_INTERFACE_VERSION, NULL );
	if ( !g_pGameUI ) return;
	COpforMainMenu *pMenu = new COpforMainMenu();
	g_pGameUI->SetMainMenuOverride( pMenu->GetVPanel() );
}
