//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Friendly Scanner - follows the player around and shoots their enemies.
//
//=============================================================================

#include "cbase.h"
#include "npc_friendly_scanner.h"
#include "ammodef.h"
#include "nav_mesh.h"
#include "nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( npc_friendly_scanner, CNPC_FriendlyScanner );

BEGIN_DATADESC( CNPC_FriendlyScanner )
	DEFINE_FIELD( m_flNextAttackTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextPathRefresh, FIELD_TIME ),
END_DATADESC()

//-----------------------------------------------------------------------------
CNPC_FriendlyScanner::CNPC_FriendlyScanner()
{
	m_flNextAttackTime = 0.0f;
	m_flNextPathRefresh = 0.0f;
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
	return 450.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Immune to bullet/explosion damage specifically when it comes
// from the player (directly, or from something the player owns -- a
// thrown grenade, a fired rocket, etc). Everything else -- Combine fire,
// scripted damage, physics, and so on -- still works normally, so this
// can still die, just never to the player's own gunfire or explosives.
//-----------------------------------------------------------------------------
static bool IsDamageFromPlayer( CBaseEntity *pEnt )
{
	if ( !pEnt )
		return false;

	if ( pEnt->IsPlayer() )
		return true;

	CBaseEntity *pOwner = pEnt->GetOwnerEntity();
	return ( pOwner && pOwner->IsPlayer() );
}

int CNPC_FriendlyScanner::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	if ( ( info.GetDamageType() & ( DMG_BULLET | DMG_BLAST ) ) &&
		 ( IsDamageFromPlayer( info.GetAttacker() ) || IsDamageFromPlayer( info.GetInflictor() ) ) )
	{
		return 0;
	}

	return BaseClass::OnTakeDamage_Alive( info );
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

	// SCHED_SCANNER_FOLLOW_HOVER's task list is just an idle hover (it's
	// meant for the stock scanner's photograph/inspect behavior) -- it has
	// no pathing/movement tasks at all, so using it here is why the scanner
	// used to just sit there until something gave it an enemy to chase.
	// SCHED_SCANNER_CHASE_TARGET does the same job but actually paths to
	// and runs toward its target.
	if ( GetTarget() )
		return SCHED_SCANNER_CHASE_TARGET;

	return SCHED_SCANNER_PATROL;
}

//-----------------------------------------------------------------------------
// Purpose: This SDK branch has no general-purpose server-side query for
// "is it dark here" -- engine->GetLightForPoint only exists on the client
// engine interface, and there's no server-side equivalent. The nav mesh's
// per-area lighting data (baked in by nav_generate, which samples real
// engine lighting while running as a listen server) is the one real,
// position-accurate light value available on the server, so that's what
// this reads instead of proxying off anything the player is doing.
//
// NOTE: this only has data on maps where someone has run nav_generate +
// nav_save at least once -- on a map with no .nav file (or no nav mesh
// covering the scanner's position) GetNearestNavArea() returns NULL and
// the torch just stays off.
//
// EF_BRIGHTLIGHT is the same effect flag the engine uses for things like
// the flare/brightlight dlight (color 250,250,250, radius ~400-431) --
// noticeably stronger than EF_DIMLIGHT (100,100,100, ~200-231), so it
// reads as a proper bright torch.
//-----------------------------------------------------------------------------
static const float SCANNER_DARKNESS_THRESHOLD = 0.3f;

void CNPC_FriendlyScanner::UpdateTorch( void )
{
	bool bWantTorch = false;

	if ( TheNavMesh )
	{
		// anyZ + no ground check -- the scanner flies, so it won't
		// necessarily have solid ground directly beneath it the way
		// GetNearestNavArea()'s default ground-height check assumes.
		CNavArea *pArea = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), true, 10000.0f, false, false );
		if ( pArea )
			bWantTorch = ( pArea->GetLightIntensity( GetAbsOrigin() ) < SCANNER_DARKNESS_THRESHOLD );
	}

	if ( bWantTorch && !IsEffectActive( EF_BRIGHTLIGHT ) )
		AddEffects( EF_BRIGHTLIGHT );
	else if ( !bWantTorch && IsEffectActive( EF_BRIGHTLIGHT ) )
		RemoveEffects( EF_BRIGHTLIGHT );
}

//-----------------------------------------------------------------------------
// Keep the player as the follow target every think, and fire at enemies.
//-----------------------------------------------------------------------------
void CNPC_FriendlyScanner::PrescheduleThink( void )
{
	BaseClass::PrescheduleThink();

#ifdef GLOWS_ENABLE
	// Soft white outline, same as the squad companions -- this scanner is
	// always a friendly ally from the moment it spawns, so it's simpler
	// than the squad-membership check: just glow whenever it's alive.
	if ( IsAlive() != IsGlowEffectActive() )
	{
		if ( IsAlive() )
		{
			AddGlowEffect();
			SetGlowColor( 1.0f, 1.0f, 1.0f, 0.5f );
		}
		else
		{
			RemoveGlowEffect();
		}
	}
#endif // GLOWS_ENABLE

	CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
	if ( pPlayer )
		SetTarget( pPlayer );

	// SCHED_SCANNER_CHASE_TARGET (used below in SelectSchedule when we're
	// just following, not fighting) only computes its route once, to a
	// single snapshot of the player's position, and has no fail schedule
	// (stock code even has "//FIXME: This is wrong!" written right above
	// it). If that one route attempt fails -- easy to hit at long range
	// through several rooms -- it falls into the engine's default fail
	// schedule, SelectSchedule() immediately hands back the same chase
	// schedule, and it can loop failing in place instead of ever reaching
	// the player. Periodically clearing the nav goal forces a brand new
	// route attempt with the player's current (not stale) position, so a
	// bad attempt gets retried instead of stuck on repeat.
	if ( !( GetEnemy() && GetEnemy()->IsAlive() ) && pPlayer && m_flNextPathRefresh <= gpGlobals->curtime )
	{
		GetNavigator()->ClearGoal();
		m_flNextPathRefresh = gpGlobals->curtime + 2.0f;
	}

	UpdateTorch();

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
		info.m_flDamage       = 15;
		info.m_pAttacker      = this;
		FireBullets( info );

		m_flNextAttackTime = gpGlobals->curtime + random->RandomFloat( 0.3f, 0.6f );
	}
}
