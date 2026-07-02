//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: env_barnacle_anchor — a placeable .mdl pull target for
// weapon_barnacle. The tongue treats it like a ragdoll and pulls the
// player toward it, preserving momentum on release.
//
//=============================================================================//

#ifndef ENV_BARNACLE_ANCHOR_H
#define ENV_BARNACLE_ANCHOR_H
#ifdef _WIN32
#pragma once
#endif

#include "baseanimating.h"

class CEnvBarnacleAnchor : public CBaseAnimating
{
	DECLARE_CLASS( CEnvBarnacleAnchor, CBaseAnimating );
	DECLARE_DATADESC();
public:
	void Spawn() override;
	void Precache() override;
};

#endif // ENV_BARNACLE_ANCHOR_H
