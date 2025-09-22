//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#ifndef OPFOR_SHAREDDEFS_H
#define OPFOR_SHAREDDEFS_H

#ifdef _WIN32
#pragma once
#endif

#include "const.h"


//--------------------------------------------------------------------------
// Collision groups
//--------------------------------------------------------------------------

enum
{
	OPFORCOLLISION_GROUP_PLASMANODE = LAST_SHARED_COLLISION_GROUP,
	OPFORCOLLISION_GROUP_SPIT,
	OPFORCOLLISION_GROUP_HOMING_MISSILE,
	OPFORCOLLISION_GROUP_COMBINE_BALL,

	OPFORCOLLISION_GROUP_FIRST_NPC,
	OPFORCOLLISION_GROUP_HOUNDEYE,
	OPFORCOLLISION_GROUP_CROW,
	OPFORCOLLISION_GROUP_HEADCRAB,
	OPFORCOLLISION_GROUP_STRIDER,
	OPFORCOLLISION_GROUP_GUNSHIP,
	OPFORCOLLISION_GROUP_ANTLION,
	OPFORCOLLISION_GROUP_LAST_NPC,
	OPFORCOLLISION_GROUP_COMBINE_BALL_NPC,
};


//--------------
// HL2 SPECIFIC
//--------------
#define DMG_SNIPER			(DMG_LASTGENERICFLAG<<1)	// This is sniper damage
#define DMG_MISSILEDEFENSE	(DMG_LASTGENERICFLAG<<2)	// The only kind of damage missiles take. (special missile defense)


#endif // OPFOR_SHAREDDEFS_H

