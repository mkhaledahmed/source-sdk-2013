//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: weapon_barnacle — tongue grapple weapon.
// See env_barnacle_anchor.cpp for the placeable .mdl pull-target entity.
//
//  Pull YOU toward them:
//    - All live NPCs EXCEPT headcrabs and metrocops
//    - World geometry (grapple-hook)
//    → On contact: NPC takes damage
//
//  Pull THEM toward you:
//    - Headcrabs (all variants)
//    - Metrocops (npc_metropolice)
//    - Ragdolls (prop_ragdoll)
//    - env_barnacle_anchor entities
//    → On contact: NPC takes damage (headcrab/metrocop only)
//
//  Primary press  : fire tongue
//  Primary hold   : maintain pull
//  Primary release: detach
//
//  All pulls are acceleration-based (a force blended with whatever
//  velocity the puller/pullee already has), not a hard velocity snap
//  toward the target -- existing momentum carries through before,
//  during, and after the grab, so pulls arc/swing instead of yanking
//  in a straight line. Nothing is zeroed on release either.
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "beam_shared.h"
#include "soundent.h"
#include "engine/IEngineSound.h"
#include "player_pickup.h"
#include "ai_basenpc.h"
#include "physics.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static const float TONGUE_RANGE          = 250.0f;
static const float TONGUE_HOLD_DIST      =  80.0f;
static const float TONGUE_MAX_MASS       = 300.0f;
static const float TONGUE_PHYS_SPEED     = 450.0f;  // max speed while reeling a physics object in
static const float TONGUE_PHYS_ACCEL     = 2000.0f; // accel applied to physics objects while reeling in
static const float TONGUE_HOLD_SPRING    =  12.0f;
static const float TONGUE_HOLD_DAMP      =   2.5f;
static const float TONGUE_HOLD_MAXSPD    = 350.0f;
static const float TONGUE_NPC_PULL_SPEED = 350.0f;  // max speed when pulling NPC toward player
static const float TONGUE_NPC_PULL_ACCEL = 1600.0f; // accel applied to NPC toward player
static const float TONGUE_PLAYER_SPEED   = 500.0f;  // max speed when player is pulled
static const float TONGUE_PLAYER_ACCEL   = 1800.0f; // accel applied to player toward target
static const float TONGUE_CONTACT_DIST   =  55.0f;  // distance considered "touching"
static const float TONGUE_CONTACT_DAMAGE =  40.0f;  // damage on contact

//=============================================================================
// Helpers
//=============================================================================
static bool IsHeadcrab( CBaseEntity *pEnt )
{
	return FClassnameIs( pEnt, "npc_headcrab" )
	    || FClassnameIs( pEnt, "npc_headcrab_fast" )
	    || FClassnameIs( pEnt, "npc_headcrab_black" );
}

static bool IsPhysicsPullTarget( CBaseEntity *pEnt )
{
	return FClassnameIs( pEnt, "prop_ragdoll" );
}

//=============================================================================
// weapon_barnacle
//=============================================================================
class CWeaponBarnacle : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponBarnacle, CBaseHLCombatWeapon );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
public:
	CWeaponBarnacle();

	void Precache() override;
	void PrimaryAttack() override;
	void ItemPostFrame() override;
	bool Holster( CBaseCombatWeapon *pSwitchingTo ) override;

	float WeaponAutoAimScale() { return 0.6f; }
	int   CapabilitiesGet()    { return bits_CAP_WEAPON_RANGE_ATTACK1; }

private:
	enum TongueState_t
	{
		TONGUE_IDLE         = 0,
		TONGUE_PULLING_PHYS,   // ragdoll pulled toward player (physics)
		TONGUE_HOLDING_PHYS,   // held at arm's length
		TONGUE_NPC_TO_PLAYER,  // headcrab / metrocop pulled toward player (velocity)
		TONGUE_PLAYER_TO_ENT,  // player pulled toward NPC or env_barnacle_anchor
	};

	bool TraceForTarget( trace_t &outTr );
	void StartGrab( const trace_t &tr );
	void UpdatePullPhys();
	void UpdateHoldPhys();
	void UpdateNPCToPlayer();
	void UpdatePlayerToEnt();
	void DamageNPC( CBaseEntity *pNPC );
	void ReleaseGrab();
	void UpdateBeam();
	void DestroyBeam();

	TongueState_t        m_nTongueState;
	CHandle<CBaseEntity> m_hGrabbedEnt;
	CHandle<CBeam>       m_hBeam;
	bool                 m_bAttackHeld;
};

LINK_ENTITY_TO_CLASS( weapon_barnacle, CWeaponBarnacle );
PRECACHE_WEAPON_REGISTER( weapon_barnacle );

IMPLEMENT_SERVERCLASS_ST( CWeaponBarnacle, DT_WeaponBarnacle )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponBarnacle )
	DEFINE_FIELD( m_nTongueState, FIELD_INTEGER ),
	DEFINE_FIELD( m_hGrabbedEnt,  FIELD_EHANDLE ),
	DEFINE_FIELD( m_bAttackHeld,  FIELD_BOOLEAN ),
END_DATADESC()

CWeaponBarnacle::CWeaponBarnacle()
	: m_nTongueState( TONGUE_IDLE ), m_bAttackHeld( false ) {}

void CWeaponBarnacle::Precache()
{
	BaseClass::Precache();
	PrecacheModel( "sprites/physbeam.vmt" );
	PrecacheScriptSound( "NPC_Barnacle.Growl" );
	PrecacheScriptSound( "NPC_Barnacle.FinalBite" );
	PrecacheScriptSound( "Weapon_PhysCannon.Pickup" );
	PrecacheScriptSound( "Weapon_PhysCannon.Drop" );
	PrecacheScriptSound( "Weapon_PhysCannon.DryFire" );
}

//-----------------------------------------------------------------------------
bool CWeaponBarnacle::TraceForTarget( trace_t &outTr )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer ) return false;
	Vector vFwd; pPlayer->EyeVectors( &vFwd );
	Vector vStart = pPlayer->EyePosition();
	UTIL_TraceLine( vStart, vStart + vFwd * TONGUE_RANGE,
	                MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &outTr );
	return outTr.DidHit();
}

//-----------------------------------------------------------------------------
void CWeaponBarnacle::StartGrab( const trace_t &tr )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer ) return;

	CBaseEntity *pEnt = tr.m_pEnt;

	// ── World surface → do nothing ───────────────────────────────────────
	if ( !pEnt || pEnt->IsWorld() )
	{
		EmitSound( "Weapon_PhysCannon.DryFire" );
		return;
	}

	// ── env_barnacle_anchor → pull PLAYER toward it ──────────────────────
	if ( FClassnameIs( pEnt, "env_barnacle_anchor" ) )
	{
		m_hGrabbedEnt  = pEnt;
		m_nTongueState = TONGUE_PLAYER_TO_ENT;
		EmitSound( "NPC_Barnacle.Growl" );
		return;
	}

	// ── Ragdoll → pull toward player ─────────────────────────────────────
	if ( IsPhysicsPullTarget( pEnt ) )
	{
		IPhysicsObject *pPhys = pEnt->VPhysicsGetObject();
		if ( pPhys && pPhys->IsMoveable() && pPhys->GetMass() <= TONGUE_MAX_MASS )
		{
			Pickup_OnPhysGunPickup( pEnt, pPlayer, PICKED_UP_BY_CANNON );
			m_hGrabbedEnt  = pEnt;
			m_nTongueState = TONGUE_PULLING_PHYS;
			pPhys->EnableGravity( false );
			EmitSound( "NPC_Barnacle.Growl" );
			m_iPrimaryAttacks++;
			gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );
			return;
		}
	}

	// ── Live NPC checks ──────────────────────────────────────────────────
	if ( pEnt->GetFlags() & FL_NPC )
	{
		CAI_BaseNPC *pNPC = dynamic_cast<CAI_BaseNPC *>( pEnt );
		if ( pNPC && pNPC->IsAlive() )
		{
			if ( IsHeadcrab( pEnt ) || FClassnameIs( pEnt, "npc_metropolice" ) )
			{
				// Headcrab / metrocop → pull THEM toward player
				m_hGrabbedEnt  = pEnt;
				m_nTongueState = TONGUE_NPC_TO_PLAYER;
			}
			else
			{
				// All other NPCs → pull PLAYER toward them
				m_hGrabbedEnt  = pEnt;
				m_nTongueState = TONGUE_PLAYER_TO_ENT;
			}
			EmitSound( "NPC_Barnacle.Growl" );
			m_iPrimaryAttacks++;
			gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );
			return;
		}
	}

	EmitSound( "Weapon_PhysCannon.DryFire" );
}

//-----------------------------------------------------------------------------
// Physics pull (ragdoll / anchor → toward player)
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdatePullPhys()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	CBaseEntity *pEnt   = m_hGrabbedEnt;
	if ( !pPlayer || !pEnt ) { ReleaseGrab(); return; }

	IPhysicsObject *pPhys = pEnt->VPhysicsGetObject();
	if ( !pPhys ) { ReleaseGrab(); return; }

	Vector vFwd; pPlayer->EyeVectors( &vFwd );
	Vector vTarget = pPlayer->EyePosition() + vFwd * TONGUE_HOLD_DIST;
	Vector vPos; QAngle vAng; pPhys->GetPosition( &vPos, &vAng );
	Vector vDelta = vTarget - vPos;
	float  flDist = vDelta.Length();

	if ( flDist > TONGUE_RANGE * 1.5f ) { ReleaseGrab(); return; }
	if ( flDist < 20.0f ) { EmitSound( "Weapon_PhysCannon.Pickup" ); m_nTongueState = TONGUE_HOLDING_PHYS; return; }

	// Accelerate toward the hold point instead of snapping straight onto
	// it -- blending with the object's existing velocity keeps whatever
	// momentum it already had (falling, sliding, a shove) instead of
	// yanking it down a rail.
	Vector vCurVel; pPhys->GetVelocity( &vCurVel, NULL );
	Vector vNewVel = vCurVel + vDelta.Normalized() * ( TONGUE_PHYS_ACCEL * gpGlobals->frametime );
	float  flSpeed = vNewVel.Length();
	if ( flSpeed > TONGUE_PHYS_SPEED ) vNewVel *= TONGUE_PHYS_SPEED / flSpeed;
	pPhys->SetVelocity( &vNewVel, NULL );
}

//-----------------------------------------------------------------------------
// Physics hold (arm's-length spring)
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateHoldPhys()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	CBaseEntity *pEnt   = m_hGrabbedEnt;
	if ( !pPlayer || !pEnt ) { ReleaseGrab(); return; }

	IPhysicsObject *pPhys = pEnt->VPhysicsGetObject();
	if ( !pPhys ) { ReleaseGrab(); return; }

	Vector vFwd; pPlayer->EyeVectors( &vFwd );
	Vector vTarget = pPlayer->EyePosition() + vFwd * TONGUE_HOLD_DIST;
	Vector vPos; QAngle vAng; pPhys->GetPosition( &vPos, &vAng );
	Vector vDelta = vTarget - vPos;
	if ( vDelta.Length() > TONGUE_RANGE ) { ReleaseGrab(); return; }

	Vector vCurVel; pPhys->GetVelocity( &vCurVel, NULL );
	Vector vDesired = vDelta * TONGUE_HOLD_SPRING - vCurVel * TONGUE_HOLD_DAMP;
	float  flSpeed  = vDesired.Length();
	if ( flSpeed > TONGUE_HOLD_MAXSPD ) vDesired *= TONGUE_HOLD_MAXSPD / flSpeed;
	pPhys->SetVelocity( &vDesired, NULL );

	AngularImpulse vAng2; pPhys->GetVelocity( NULL, &vAng2 );
	AngularImpulse vDamp = vAng2 * -0.4f; pPhys->AddVelocity( NULL, &vDamp );
}

//-----------------------------------------------------------------------------
// NPC (headcrab / metrocop) pulled toward player — velocity-based.
// On contact: NPC takes damage.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateNPCToPlayer()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	CBaseEntity *pEnt   = m_hGrabbedEnt;
	if ( !pPlayer || !pEnt ) { ReleaseGrab(); return; }

	Vector vDir = pPlayer->WorldSpaceCenter() - pEnt->GetAbsOrigin();
	float  flDist = vDir.Length();

	if ( flDist < TONGUE_CONTACT_DIST )
	{
		DamageNPC( pEnt );
		ReleaseGrab();
		return;
	}

	VectorNormalize( vDir );
	// Accelerate toward the player, blended with the NPC's existing
	// velocity, so it swings/arcs in rather than being pinned to a
	// straight line toward you.
	Vector vNewVel = pEnt->GetAbsVelocity() + vDir * ( TONGUE_NPC_PULL_ACCEL * gpGlobals->frametime );
	float  flSpeed = vNewVel.Length();
	if ( flSpeed > TONGUE_NPC_PULL_SPEED ) vNewVel *= TONGUE_NPC_PULL_SPEED / flSpeed;
	pEnt->SetAbsVelocity( vNewVel );
}

//-----------------------------------------------------------------------------
// Player pulled toward an NPC or env_barnacle_anchor.
// On contact with NPC: NPC takes damage. Anchor: just release.
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdatePlayerToEnt()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	CBaseEntity *pEnt   = m_hGrabbedEnt;
	if ( !pPlayer || !pEnt ) { ReleaseGrab(); return; }

	Vector vDir = pEnt->WorldSpaceCenter() - pPlayer->GetAbsOrigin();
	float  flDist = vDir.Length();

	bool bIsAnchor = FClassnameIs( pEnt, "env_barnacle_anchor" );

	if ( flDist < TONGUE_CONTACT_DIST )
	{
		if ( !bIsAnchor )
		{
			// NPC contact — deal damage and release
			if ( pEnt->GetFlags() & FL_NPC )
				DamageNPC( pEnt );
			ReleaseGrab();
			return;
		}
		// Anchor contact — stay connected, stop driving velocity so
		// the player's built-up momentum carries them naturally
		return;
	}

	VectorNormalize( vDir );
	// Accelerate toward the target, blended with the player's existing
	// velocity, so falling/strafing/jump momentum carries through and
	// the pull arcs in instead of snapping onto a direct line.
	Vector vNewVel = pPlayer->GetAbsVelocity() + vDir * ( TONGUE_PLAYER_ACCEL * gpGlobals->frametime );
	float  flSpeed = vNewVel.Length();
	if ( flSpeed > TONGUE_PLAYER_SPEED ) vNewVel *= TONGUE_PLAYER_SPEED / flSpeed;
	pPlayer->SetAbsVelocity( vNewVel );
}

//-----------------------------------------------------------------------------
void CWeaponBarnacle::DamageNPC( CBaseEntity *pNPC )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	CTakeDamageInfo info( this, pPlayer, TONGUE_CONTACT_DAMAGE, DMG_CLUB );
	pNPC->TakeDamage( info );
	EmitSound( "NPC_Barnacle.FinalBite" );
}

//-----------------------------------------------------------------------------
void CWeaponBarnacle::ReleaseGrab()
{
	// Momentum is never zeroed here -- whatever velocity the pull built up
	// (on the player, the NPC, or the physics object) carries through past
	// the release, same as it would from a real swing.
	if ( m_hGrabbedEnt && ( m_nTongueState == TONGUE_PULLING_PHYS || m_nTongueState == TONGUE_HOLDING_PHYS ) )
	{
		IPhysicsObject *pPhys = m_hGrabbedEnt->VPhysicsGetObject();
		if ( pPhys ) pPhys->EnableGravity( true );
		Pickup_OnPhysGunDrop( m_hGrabbedEnt, ToBasePlayer( GetOwner() ), DROPPED_BY_CANNON );
	}

	EmitSound( "Weapon_PhysCannon.Drop" );
	m_hGrabbedEnt  = NULL;
	m_nTongueState = TONGUE_IDLE;
	DestroyBeam();
}

//-----------------------------------------------------------------------------
// Tongue beam
//-----------------------------------------------------------------------------
void CWeaponBarnacle::UpdateBeam()
{
	if ( m_nTongueState == TONGUE_IDLE ) { DestroyBeam(); return; }

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer ) return;

	if ( !m_hBeam )
	{
		m_hBeam = CBeam::BeamCreate( "sprites/physbeam.vmt", 4 );
		if ( !m_hBeam ) return;
		m_hBeam->SetColor( 160, 100, 70 );
		m_hBeam->SetBrightness( 220 );
		m_hBeam->SetNoise( 1.5f );
		m_hBeam->SetWidth( 3.0f );
		m_hBeam->SetEndWidth( 7.0f );
	}

	if ( !m_hGrabbedEnt ) { DestroyBeam(); return; }
	Vector vEnd = m_hGrabbedEnt->WorldSpaceCenter();
	m_hBeam->PointsInit( pPlayer->Weapon_ShootPosition(), vEnd );
}

void CWeaponBarnacle::DestroyBeam()
{
	if ( m_hBeam ) { UTIL_Remove( m_hBeam ); m_hBeam = NULL; }
}

//-----------------------------------------------------------------------------
void CWeaponBarnacle::PrimaryAttack()
{
	if ( m_nTongueState != TONGUE_IDLE ) return;
	trace_t tr;
	if ( TraceForTarget( tr ) )
	{
		StartGrab( tr );
	}
	else if ( !m_bAttackHeld )
	{
		// Only dry-fire on the initial press -- while the button stays held
		// we keep silently re-checking so walking into range auto-grabs.
		EmitSound( "Weapon_PhysCannon.DryFire" );
	}
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
}

bool CWeaponBarnacle::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	ReleaseGrab();
	return BaseClass::Holster( pSwitchingTo );
}

void CWeaponBarnacle::ItemPostFrame()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner ) { BaseClass::ItemPostFrame(); return; }

	bool bAttackDown = ( pOwner->m_nButtons & IN_ATTACK ) != 0;

	// Keep retrying (throttled) while the button is held, not just on the
	// initial press -- lets the tongue auto-grab once a target walks
	// into range without needing to release and re-press attack.
	if ( bAttackDown && m_nTongueState == TONGUE_IDLE && gpGlobals->curtime >= m_flNextPrimaryAttack )
		PrimaryAttack();

	if ( bAttackDown && m_nTongueState != TONGUE_IDLE )
	{
		switch ( m_nTongueState )
		{
		case TONGUE_PULLING_PHYS:   UpdatePullPhys();       break;
		case TONGUE_HOLDING_PHYS:   UpdateHoldPhys();       break;
		case TONGUE_NPC_TO_PLAYER:  UpdateNPCToPlayer();  break;
		case TONGUE_PLAYER_TO_ENT:  UpdatePlayerToEnt();  break;
		default: break;
		}
	}

	if ( !bAttackDown && m_nTongueState != TONGUE_IDLE )
		ReleaseGrab();

	m_bAttackHeld = bAttackDown;
	UpdateBeam();
	WeaponIdle();
}
