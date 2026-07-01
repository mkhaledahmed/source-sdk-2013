//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A placeable machine entity that starts broken/off and can be
// switched on -- either via Hammer I/O (TurnOn input) or by the wrench's
// mouse3 fixup attack tracing onto it directly.
//
//=============================================================================//

#include "cbase.h"
#include "fixable_entity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS(fixable_entity, CFixableEntity);

BEGIN_DATADESC(CFixableEntity)
DEFINE_KEYFIELD(m_bOn, FIELD_BOOLEAN, "StartOn"),
DEFINE_KEYFIELD(m_iszModelName, FIELD_STRING, "FixableModel"),

DEFINE_INPUTFUNC(FIELD_VOID, "TurnOn", InputTurnOn),
DEFINE_INPUTFUNC(FIELD_VOID, "TurnOff", InputTurnOff),

DEFINE_OUTPUT(m_OnFixed, "OnFixed"),
DEFINE_OUTPUT(m_OnUnfixed, "OnUnfixed"),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFixableEntity::Precache(void)
{
	// TODO: point "model" (set via Hammer, or this fallback) at whatever
	// machine prop model you actually want -- this placeholder almost
	// certainly won't exist in your content.
	if (!m_iszModelName)
	{
		m_iszModelName = AllocPooledString("models/props_c17/consolebox01a.mdl");
	}

	PrecacheModel(STRING(m_iszModelName));

	// TODO: needs real soundscript entries for these -- placeholders for now.
	PrecacheScriptSound("FixableEntity.TurnOn");
	PrecacheScriptSound("FixableEntity.TurnOff");
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFixableEntity::Spawn(void)
{
	Precache();

	SetModel(STRING(m_iszModelName));
	SetSolid(SOLID_BBOX);
	SetMoveType(MOVETYPE_NONE);

	// Skin 0 = off/broken, skin 1 = on/fixed. TODO: if your model doesn't
	// have two skins set up for this, swap to a body group or two separate
	// models instead -- whichever your actual art supports.
	m_nSkin = m_bOn ? 1 : 0;
}

//-----------------------------------------------------------------------------
// Purpose: Shared logic for any state change -- handles the skin swap,
// sound, and firing the correct output, regardless of what triggered it.
//-----------------------------------------------------------------------------
void CFixableEntity::SetFixedState(bool bOn, CBaseEntity* pActivator)
{
	if (m_bOn == bOn)
		return;	// already in that state -- nothing to do

	m_bOn = bOn;
	m_nSkin = bOn ? 1 : 0;

	EmitSound(bOn ? "FixableEntity.TurnOn" : "FixableEntity.TurnOff");

	if (bOn)
	{
		m_OnFixed.FireOutput(pActivator, this);
	}
	else
	{
		m_OnUnfixed.FireOutput(pActivator, this);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Turns the machine on specifically. Kept for anything that wants
// a one-directional "make sure this is on" call rather than a toggle.
//-----------------------------------------------------------------------------
bool CFixableEntity::TurnOn(CBaseEntity* pActivator)
{
	if (m_bOn)
		return false;

	SetFixedState(true, pActivator);
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Flips the current state -- this is what the wrench's fixup
// attack calls now, so hitting an already-fixed entity breaks it again.
//-----------------------------------------------------------------------------
bool CFixableEntity::ToggleFixed(CBaseEntity* pActivator)
{
	SetFixedState(!m_bOn, pActivator);
	return m_bOn;
}

//-----------------------------------------------------------------------------
// Purpose: Hammer input version of TurnOn -- lets other map logic (buttons,
// triggers, logic_relay, etc.) turn this on too, not just the wrench.
//-----------------------------------------------------------------------------
void CFixableEntity::InputTurnOn(inputdata_t& inputdata)
{
	SetFixedState(true, inputdata.pActivator);
}

//-----------------------------------------------------------------------------
// Purpose: Hammer input to reset it back to the off/broken state.
//-----------------------------------------------------------------------------
void CFixableEntity::InputTurnOff(inputdata_t& inputdata)
{
	SetFixedState(false, inputdata.pActivator);
}