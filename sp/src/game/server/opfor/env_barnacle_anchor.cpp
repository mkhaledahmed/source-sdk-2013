//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: env_barnacle_anchor — a placeable .mdl pull target for
// weapon_barnacle. See weapon_barnacle.cpp for the weapon logic that
// pulls the player toward this entity.
//
//=============================================================================//

#include "cbase.h"
#include "env_barnacle_anchor.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Default model — a small cone from the editor folder, always in HL2 content,
// clearly visible and dev-textured. Mappers can override with any .mdl.
#define BARNACLE_ANCHOR_DEFAULT_MODEL "models/editor/cone.mdl"

LINK_ENTITY_TO_CLASS( env_barnacle_anchor, CEnvBarnacleAnchor );
BEGIN_DATADESC( CEnvBarnacleAnchor ) END_DATADESC()

void CEnvBarnacleAnchor::Spawn()
{
	Precache();
	SetModel( GetModelName() != NULL_STRING
	          ? STRING( GetModelName() )
	          : BARNACLE_ANCHOR_DEFAULT_MODEL );

	// Static anchor — it stays in place, the player is pulled toward it.
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_BBOX );
	UTIL_SetSize( this, Vector( -16, -16, -16 ), Vector( 16, 16, 16 ) );
	AddEFlags( EF_NOSHADOW );
}

void CEnvBarnacleAnchor::Precache()
{
	PrecacheModel( GetModelName() != NULL_STRING
	               ? STRING( GetModelName() )
	               : BARNACLE_ANCHOR_DEFAULT_MODEL );
}
