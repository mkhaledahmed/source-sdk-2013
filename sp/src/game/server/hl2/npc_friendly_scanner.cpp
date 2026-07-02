//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Friendly Scanner - follows the player around and shoots their enemies.
//
//=============================================================================

#include "cbase.h"
#include "npc_friendly_scanner.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( npc_friendly_scanner, CNPC_FriendlyScanner );

BEGIN_DATADESC( CNPC_FriendlyScanner )
	DEFINE_FIELD( m_flNextAttackTime, FIELD_TIME ),
END_DATADESC()

//-----------------------------------------------------------------------------
CNPC_FriendlyScanner::CNPC_FriendlyScanner()
{
	m_flNextAttackTime = 0.0f;
}

//-----------------------------------------------------------------------------
void CNPC_FriendlyScanner::Precache( void )
{
	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
void CNPC_FriendlyScanner::Spawn( void )
{
	BaseClass::Spawn();

	m_iHealth    = 100;
	m_iMaxHealth = 100;
}

//-----------------------------------------------------------------------------
void CNPC_FriendlyScanner::Activate( void )
{
	BaseClass::Activate();

	// Start following the player immediately
	CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
	if ( pPlayer )
		SetTarget( pPlayer );

	m_flNextAttackTime = gpGlobals->curtime + 1.0f;
}

//-----------------------------------------------------------------------------
float CNPC_FriendlyScanner::GetMaxSpeed( void )
{
	return 300.0f;
}

//-----------------------------------------------------------------------------
// Skip CNPC_CScanner's override which treats players as enemies.
// With Classify() returning CLASS_PLAYER_ALLY, the default relationship
// table already gives us D_LI toward players/allies and D_HT toward Combine.
//-----------------------------------------------------------------------------
Disposition_t CNPC_FriendlyScanner::IRelationType( CBaseEntity *pTarget )
{
	return CAI_BaseNPC::IRelationType( pTarget );
}

//-----------------------------------------------------------------------------
// Follow the player when peaceful, chase and kill enemies when they appear.
//-----------------------------------------------------------------------------
int CNPC_FriendlyScanner::SelectSchedule( void )
{
	if ( IsHeldByPhyscannon() )
		return SCHED_SCANNER_HELD_BY_PHYSCANNON;

	if ( GetEnemy() && GetEnemy()->IsAlive() )
		return SCHED_SCANNER_CHASE_ENEMY;

	if ( GetTarget() )
		return SCHED_SCANNER_FOLLOW_HOVER;

	return SCHED_SCANNER_PATROL;
}

//-----------------------------------------------------------------------------
// Keep the player as the follow target every think, and fire at enemies.
//-----------------------------------------------------------------------------
void CNPC_FriendlyScanner::PrescheduleThink( void )
{
	BaseClass::PrescheduleThink();

	CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
	if ( pPlayer )
		SetTarget( pPlayer );

	// Shoot at detected enemies on a timer
	if ( GetEnemy() && GetEnemy()->IsAlive() && m_flNextAttackTime <= gpGlobals->curtime )
	{
		Vector vecSrc = GetAbsOrigin();
		Vector vecDir = GetEnemy()->WorldSpaceCenter() - vecSrc;
		VectorNormalize( vecDir );

		FireBulletsInfo_t info;
		info.m_vecSrc         = vecSrc;
		info.m_vecDirShooting = vecDir;
		info.m_iShots         = 1;
		info.m_vecSpread      = VECTOR_CONE_2DEGREES;
		info.m_flDistance     = MAX_TRACE_LENGTH;
		info.m_iAmmoType      = GetAmmoDef()->Index( "Pistol" );
		info.m_iTracerFreq    = 1;
		info.m_flDamage       = 10;
		info.m_pAttacker      = this;
		FireBullets( info );

		m_flNextAttackTime = gpGlobals->curtime + random->RandomFloat( 0.8f, 1.5f );
	}
}
