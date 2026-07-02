//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Friendly Scanner - follows and protects the player.
//          Inherits the full npc_cscanner movement, sounds, and model.
//
//=============================================================================

#ifndef NPC_FRIENDLY_SCANNER_H
#define NPC_FRIENDLY_SCANNER_H
#ifdef _WIN32
#pragma once
#endif

#include "npc_scanner.h"

//-----------------------------------------------------------------------------
// Purpose: Player-allied scanner that follows and defends the player.
//-----------------------------------------------------------------------------
class CNPC_FriendlyScanner : public CNPC_CScanner
{
	DECLARE_CLASS( CNPC_FriendlyScanner, CNPC_CScanner );

public:
	CNPC_FriendlyScanner();

	virtual void		Precache( void );
	virtual void		Spawn( void );
	virtual void		Activate( void );

	virtual Class_T		Classify( void ) { return CLASS_PLAYER_ALLY; }
	virtual Disposition_t IRelationType( CBaseEntity *pTarget );

	virtual int			SelectSchedule( void );
	virtual void		PrescheduleThink( void );

	// Turns its own light on/off based on the real ambient light level at
	// its position (read from the nav mesh's precomputed lighting data --
	// see UpdateTorch()'s comment for why).
	void				UpdateTorch( void );

	// Immune to the player's own bullets/explosions specifically -- still
	// killable by everything else (Combine, scripted damage, etc).
	virtual int			OnTakeDamage_Alive( const CTakeDamageInfo &info );

	float				GetMaxSpeed( void );

private:
	float				m_flNextAttackTime;
	float				m_flNextPathRefresh;

	DECLARE_DATADESC();
};

#endif // NPC_FRIENDLY_SCANNER_H
