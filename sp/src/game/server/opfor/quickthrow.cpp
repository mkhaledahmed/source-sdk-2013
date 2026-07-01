//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Q/E quick-throw system -- lets the player throw a frag grenade
// or hopwire instantly while holding a completely different weapon,
// provided they already own it and have ammo. Doesn't switch weapons or
// viewmodels; just fires the grenade weapon's throw directly.
//
// The player needs to actually bind these, e.g.:
//   bind Q "quickthrow_frag"
//   bind E "quickthrow_hopwire"
//
//=============================================================================//

#include "cbase.h"
#include "player.h"
#include "basecombatweapon.h"
#include "weapon_quickthrow.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Shared handler for both quick-throw commands.
//-----------------------------------------------------------------------------
static void QuickThrowGrenade(CBasePlayer* pPlayer, const char* pszWeaponClassname)
{
	if (!pPlayer)
		return;

	// Must already own the weapon -- this is a shortcut for something the
	// player has, not a free/unlimited grenade source.
	CBaseCombatWeapon* pWeapon = pPlayer->Weapon_OwnsThisType(pszWeaponClassname);
	if (!pWeapon)
		return;

	IQuickThrowable* pThrowable = dynamic_cast<IQuickThrowable*>(pWeapon);
	if (!pThrowable)
		return;	// shouldn't happen unless something's misconfigured -- the classname matched but it doesn't implement the interface

	if (pThrowable->QuickThrow(pPlayer))
	{
		// One-handed toss gesture layered on top of whatever weapon is
		// actually out. NOTE: PLAYER_ATTACK2 is the same generic
		// attack-animation state weapon_frag.cpp's own lob/roll already
		// use -- there's no distinct "one-handed toss while holding
		// something else" animation confirmed to exist in this codebase.
		// This plays correctly mechanically, but the actual visual will
		// only look distinct once/if a dedicated PLAYER_ANIM value and
		// matching animation exist for it.
		pPlayer->SetAnimation(PLAYER_ATTACK2);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Q -- quick-throw a frag grenade.
//-----------------------------------------------------------------------------
CON_COMMAND(quickthrow_frag, "Quick-throw a frag grenade without switching weapons")
{
	CBasePlayer* pPlayer = ToBasePlayer(UTIL_GetCommandClient());
	QuickThrowGrenade(pPlayer, "weapon_frag");
}

//-----------------------------------------------------------------------------
// Purpose: E -- quick-throw a hopwire.
//-----------------------------------------------------------------------------
CON_COMMAND(quickthrow_hopwire, "Quick-throw a hopwire grenade without switching weapons")
{
	CBasePlayer* pPlayer = ToBasePlayer(UTIL_GetCommandClient());
	QuickThrowGrenade(pPlayer, "weapon_hopwire");
}