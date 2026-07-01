//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared interface implemented by weapon_frag.cpp's CWeaponFrag
// and weapon_hopwire.cpp's CWeaponHopwire, so the Q/E quick-throw system
// (quickgrenade.cpp) can trigger an instant throw on an owned-but-inactive
// weapon without needing either class's full definition visible -- both
// classes are defined entirely inside their own .cpp files with no header.
//
//=============================================================================//

#ifndef WEAPON_QUICKTHROW_H
#define WEAPON_QUICKTHROW_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;

class IQuickThrowable
{
public:
	// Returns true if the throw actually happened (false if out of ammo, etc).
	// Implementations should NOT touch weapon-switching/viewmodel state --
	// this is meant to fire while a completely different weapon is active.
	virtual bool QuickThrow(CBasePlayer* pPlayer) = 0;
};

#endif // WEAPON_QUICKTHROW_H