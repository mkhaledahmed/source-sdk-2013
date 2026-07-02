//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Aggressive Scanner - hunts the player and fires flares at them.
//
//=============================================================================

#include "cbase.h"
#include "npc_aggressive_scanner.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( npc_aggressive_scanner, CNPC_AggressiveScanner );

BEGIN_DATADESC( CNPC_AggressiveScanner )
	DEFINE_FIELD( m_flNextFlareTime, FIELD_TIME ),
END_DATADESC()

//-----------------------------------------------------------------------------
CNPC_AggressiveScanner::CNPC_AggressiveScanner()
{
	m_flNextFlareTime = 0.0f;
}

//-----------------------------------------------------------------------------
void CNPC_AggressiveScanner::Precache( void )
{
	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
void CNPC_AggressiveScanner::Spawn( void )
{
	BaseClass::Spawn();

	// Slightly tougher than a stock scanner
	m_iHealth    = 50;
	m_iMaxHealth = 50;
}

//-----------------------------------------------------------------------------
void CNPC_AggressiveScanner::Activate( void )
{
	BaseClass::Activate();
	m_flNextFlareTime = gpGlobals->curtime + 2.0f;
}

//-----------------------------------------------------------------------------
float CNPC_AggressiveScanner::GetMaxSpeed( void )
{
	return 300.0f;
}

//-----------------------------------------------------------------------------
// Always chase the player; skip the CScanner's inspect/photograph logic.
//-----------------------------------------------------------------------------
int CNPC_AggressiveScanner::SelectSchedule( void )
{
	if ( IsHeldByPhyscannon() )
		return SCHED_SCANNER_HELD_BY_PHYSCANNON;

	if ( GetEnemy() && GetEnemy()->IsAlive() )
		return SCHED_SCANNER_CHASE_ENEMY;

	return SCHED_SCANNER_PATROL;
}

//-----------------------------------------------------------------------------
// Keep the player as our permanent enemy and fire flares at them on a timer.
//-----------------------------------------------------------------------------
void CNPC_AggressiveScanner::PrescheduleThink( void )
{
	BaseClass::PrescheduleThink();

	// Always treat the player as the enemy
	CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
	if ( pPlayer && !GetEnemy() )
		SetEnemy( pPlayer );

	// Fire a bullet at the enemy periodically
	if ( GetEnemy() && GetEnemy()->IsAlive() && m_flNextFlareTime <= gpGlobals->curtime )
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

		EmitSound( "NPC_BaseScanner.Alert" );

		// Tip off nearby allies
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		int nAIs = g_AI_Manager.NumAIs();
		for ( int i = 0; i < nAIs; i++ )
		{
			CAI_BaseNPC *pAI = ppAIs[i];
			if ( pAI && pAI != this )
			{
				if ( FClassnameIs( pAI, "npc_strider" )
				  || FClassnameIs( pAI, "npc_cscanner" )
				  || FClassnameIs( pAI, "npc_aggressive_scanner" ) )
				{
					pAI->UpdateEnemyMemory( GetEnemy(), GetEnemy()->GetAbsOrigin(), this );
				}
			}
		}

		m_flNextFlareTime = gpGlobals->curtime + random->RandomFloat( 0.8f, 1.5f );
	}
}
