//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hud_numericdisplay.h"
#include "iclientmode.h"

#include <Color.h>
#include <KeyValues.h>
#include <vgui/ISurface.h>
#include <vgui/ISystem.h>
#include <vgui/IVGui.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudNumericDisplay::CHudNumericDisplay(vgui::Panel *parent, const char *name) : BaseClass(parent, name)
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iValue = 0;
	m_LabelText[0] = 0;
	m_iSecondaryValue = 0;
	m_bDisplayValue = true;
	m_bDisplaySecondaryValue = false;
	m_bIndent = false;
	m_bIsTime = false;
}

//-----------------------------------------------------------------------------
// Purpose: Resets values on restore/new map
//-----------------------------------------------------------------------------
void CHudNumericDisplay::Reset()
{
	m_flBlur = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetDisplayValue(int value)
{
	m_iValue = value;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetSecondaryValue(int value)
{
	m_iSecondaryValue = value;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetShouldDisplayValue(bool state)
{
	m_bDisplayValue = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetShouldDisplaySecondaryValue(bool state)
{
	m_bDisplaySecondaryValue = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetLabelText(const wchar_t *text)
{
	wcsncpy(m_LabelText, text, sizeof(m_LabelText) / sizeof(wchar_t));
	m_LabelText[(sizeof(m_LabelText) / sizeof(wchar_t)) - 1] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetIndent(bool state)
{
	m_bIndent = state;
}

//-----------------------------------------------------------------------------
// Purpose: data accessor
//-----------------------------------------------------------------------------
void CHudNumericDisplay::SetIsTime(bool state)
{
	m_bIsTime = state;
}

//-----------------------------------------------------------------------------
// Purpose: paints a number at the specified position
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintNumbers(HFont font, int xpos, int ypos, int value)
{
	surface()->DrawSetTextFont(font);
	wchar_t unicode[6];
	if ( !m_bIsTime )
	{
		V_snwprintf(unicode, ARRAYSIZE(unicode), L"%d", value);
	}
	else
	{
		int iMinutes = value / 60;
		int iSeconds = value - iMinutes * 60;
#ifdef PORTAL
		// portal uses a normal font for numbers so we need the seperate to be a renderable ':' char
		if ( iSeconds < 10 )
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d:0%d", iMinutes, iSeconds );
		else
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d:%d", iMinutes, iSeconds );		
#else
		if ( iSeconds < 10 )
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d`0%d", iMinutes, iSeconds );
		else
			V_snwprintf( unicode, ARRAYSIZE(unicode), L"%d`%d", iMinutes, iSeconds );
#endif
	}

	// adjust the position to take into account 3 characters
	int charWidth = surface()->GetCharacterWidth(font, '0');
	if (value < 100 && m_bIndent)
	{
		xpos += charWidth;
	}
	if (value < 10 && m_bIndent)
	{
		xpos += charWidth;
	}

	surface()->DrawSetTextPos(xpos, ypos);
	surface()->DrawUnicodeString( unicode );
}

//-----------------------------------------------------------------------------
// Purpose: draws the text
//-----------------------------------------------------------------------------
void CHudNumericDisplay::PaintLabel( void )
{
	surface()->DrawSetTextFont(m_hTextFont);
	surface()->DrawSetTextColor(GetFgColor());
	surface()->DrawSetTextPos(text_xpos, text_ypos);
	surface()->DrawUnicodeString( m_LabelText );
}
#ifdef OPFOR_DLL
// Draw faint "000" and return the slot width (width of a single '0')
static int PaintGhostTriplet(HFont font, int xpos, int ypos, const Color& ghostColor)
{
	surface()->DrawSetTextFont(font);
	surface()->DrawSetTextColor(ghostColor);
	surface()->DrawSetTextPos(xpos, ypos);

	static const wchar_t kGhost[] = L"000";
	surface()->DrawUnicodeString(kGhost);

	int w0, h0;
	surface()->GetTextSize(font, L"0", w0, h0);
	return w0; // slot width = width of one '0'
}

// Draw number into fixed slots, right-aligning each digit inside its slot.
static void PaintDigitsRightAlignedInSlots(HFont font, int xpos, int ypos, int value, int numSlots)
{
	surface()->DrawSetTextFont(font);

	// Prepare right-justified digit buffer (no sign). Clamp negatives to 0 for HUD.
	if (value < 0) value = 0;

	wchar_t buf[32];
	V_snwprintf(buf, ARRAYSIZE(buf), L"%d", value);

	// Get slot width from '0'
	int slotW, slotH;
	surface()->GetTextSize(font, L"0", slotW, slotH);

	// Place digits into slots from right to left
	int len = V_wcslen(buf);
	for (int slot = 0; slot < numSlots; ++slot)
	{
		int srcIdx = len - 1 - slot; // rightmost digit into rightmost slot
		if (srcIdx < 0) continue;    // nothing for this slot

		wchar_t d[2] = { buf[srcIdx], 0 };

		int gw, gh;
		surface()->GetTextSize(font, d, gw, gh);

		int slotLeft = xpos + (numSlots - 1 - slot) * slotW;
		int slotRight = slotLeft + slotW;
		int drawX = slotRight - gw;  // right-align inside the slot

		surface()->DrawSetTextPos(drawX, ypos);
		surface()->DrawUnicodeString(d);
	}
}
#endif

//-----------------------------------------------------------------------------
// Purpose: renders the vgui panel
//-----------------------------------------------------------------------------
void CHudNumericDisplay::Paint()
{
#ifdef OPFOR_DLL
	// --- PRIMARY (e.g., Health) ---
	if (m_bDisplayValue)
	{
		// 1) Ghost "000"
		Color ghost = GetFgColor();
		ghost[0] = ghost[1] = ghost[2] = 128; // grey
		ghost[3] = 80;                        // low alpha
		// Draw faint "000" behind the number
		PaintGhostTriplet(m_hNumberFont, digit_xpos, digit_ypos, ghost);

		// Draw the actual number, right-aligned inside each ghost slot
		surface()->DrawSetTextColor(GetFgColor());
		PaintDigitsRightAlignedInSlots(m_hNumberFont, digit_xpos, digit_ypos, m_iValue, 3);

		// 2) Glow/blur: redraw using glow font, same per-slot logic
		for (float fl = m_flBlur; fl > 0.0f; fl -= 1.0f)
		{
			if (fl >= 1.0f)
			{
				PaintDigitsRightAlignedInSlots(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue, 3);
			}
			else
			{
				Color col = GetFgColor();
				col[3] = (unsigned char)(col[3] * fl);
				surface()->DrawSetTextColor(col);
				PaintDigitsRightAlignedInSlots(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue, 3);
			}
		}
	}

	// total ammo
	if (m_bDisplaySecondaryValue)
	{
		// 1) Draw ghost "000" behind the reserve ammo (using the small font)
		Color ghost = GetFgColor();
		ghost[0] = ghost[1] = ghost[2] = 128; // grey
		ghost[3] = 80;                        // low alpha
		PaintGhostTriplet(m_hSmallNumberFont, digit2_xpos, digit2_ypos, ghost);

		// 2) Draw actual ammo digits, right-aligned per ghost slot
		surface()->DrawSetTextColor(GetFgColor());
		PaintDigitsRightAlignedInSlots(m_hSmallNumberFont, digit2_xpos, digit2_ypos, m_iSecondaryValue, /*numSlots=*/3);
	}

#endif OPFOR_DLL

#ifdef HL2_DLL
	if (m_bDisplayValue)
	{
		// draw our numbers
		surface()->DrawSetTextColor(GetFgColor());
		PaintNumbers(m_hNumberFont, digit_xpos, digit_ypos, m_iValue);

		// draw the overbright blur
		for (float fl = m_flBlur; fl > 0.0f; fl -= 1.0f)
		{
			if (fl >= 1.0f)
			{
				PaintNumbers(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue);
			}
			else
			{
				// draw a percentage of the last one
				Color col = GetFgColor();
				col[3] *= fl;
				surface()->DrawSetTextColor(col);
				PaintNumbers(m_hNumberGlowFont, digit_xpos, digit_ypos, m_iValue);
			}
		}
	}

	// total ammo
	if (m_bDisplaySecondaryValue)
	{
		surface()->DrawSetTextColor(GetFgColor());
		PaintNumbers(m_hSmallNumberFont, digit2_xpos, digit2_ypos, m_iSecondaryValue);
	}
#endif 

	// Label as before
	PaintLabel();
}




