//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A placeable machine entity that starts broken/off and can be
// switched on -- either via Hammer I/O (TurnOn input) or by the wrench's
// mouse3 fixup attack tracing onto it directly (see weapon_wrench.cpp).
//
//=============================================================================//

#ifndef FIXABLE_ENTITY_H
#define FIXABLE_ENTITY_H
#ifdef _WIN32
#pragma once
#endif

#include "baseanimating.h"
#include "entityoutput.h"

class CFixableEntity : public CBaseAnimating
{
	DECLARE_CLASS(CFixableEntity, CBaseAnimating);
	DECLARE_DATADESC();

public:
	void	Spawn(void);
	void	Precache(void);

	bool	IsOn(void) const { return m_bOn; }

	// Called directly by anything that wants to turn it on/off
	// programmatically (e.g. the wrench used to call TurnOn() only --
	// see ToggleFixed() below for what it calls now).
	// Returns false if it was already in that state -- nothing happened.
	bool	TurnOn(CBaseEntity* pActivator);

	// Called by the wrench's fixup attack -- flips whichever state it's
	// currently in. Returns the resulting on/off state.
	bool	ToggleFixed(CBaseEntity* pActivator);

	// Hammer I/O
	void	InputTurnOn(inputdata_t& inputdata);
	void	InputTurnOff(inputdata_t& inputdata);

	COutputEvent	m_OnFixed;
	COutputEvent	m_OnUnfixed;

private:
	void	SetFixedState(bool bOn, CBaseEntity* pActivator);

	bool		m_bOn;
	string_t	m_iszModelName;
};

#endif // FIXABLE_ENTITY_H