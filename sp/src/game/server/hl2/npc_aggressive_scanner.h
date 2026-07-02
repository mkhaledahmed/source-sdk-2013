//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Aggressive Scanner - hunts the player and fires flares at them.
//          Inherits the full npc_cscanner movement, sounds, and model.
//
//=============================================================================

#ifndef NPC_AGGRESSIVE_SCANNER_H
#define NPC_AGGRESSIVE_SCANNER_H
#ifdef _WIN32
#pragma once
#endif

#include "npc_scanner.h"

//-----------------------------------------------------------------------------
// Purpose: Scanner variant that always pursues the player and fires flares.
//-----------------------------------------------------------------------------
class CNPC_AggressiveScanner : public CNPC_CScanner
{
	DECLARE_CLASS( CNPC_AggressiveScanner, CNPC_CScanner );

public:
	CNPC_AggressiveScanner();

	virtual void	Precache( void );
	virtual void	Spawn( void );
	virtual void	Activate( void );
	virtual int		SelectSchedule( void );
	virtual void	PrescheduleThink( void );

	float			GetMaxSpeed( void );

private:
	float			m_flNextFlareTime;

	DECLARE_DATADESC();
};

#endif // NPC_AGGRESSIVE_SCANNER_H
