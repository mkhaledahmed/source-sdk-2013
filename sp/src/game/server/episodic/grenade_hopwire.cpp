//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Gravity well device
//
//=====================================================================================//

#include "cbase.h"
#include "grenade_hopwire.h"
#include "rope.h"
#include "rope_shared.h"
#include "beam_shared.h"
#include "physics.h"
#include "physics_saverestore.h"
#include "explode.h"
#include "physics_prop_ragdoll.h"
#include "movevars_shared.h"
#include "SpriteTrail.h"		// for CSpriteTrail, used by CreateEffects()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar hopwire_vortex("hopwire_vortex", "1");	// was "0" -- vortex is now the default core behavior, not a debug toggle
ConVar hopwire_trap("hopwire_trap", "0");		// was "1" -- default is now a normal timed grenade; set to 1 to restore old manual-trigger trap mode

ConVar hopwire_beep_interval_far("hopwire_beep_interval_far", "0.5");		// seconds between beeps while far from detonating
ConVar hopwire_beep_interval_near("hopwire_beep_interval_near", "0.1");		// seconds between beeps once close to detonating
ConVar hopwire_beep_near_threshold("hopwire_beep_near_threshold", "0.75");	// seconds-remaining cutoff between "far" and "near"

ConVar hopwire_max_pickups("hopwire_max_pickups", "10");	// cap on how many health/armor pairs a single vortex can drop
ConVar hopwire_strider_kill_dist_h("hopwire_strider_kill_dist_h", "300");
ConVar hopwire_strider_kill_dist_v("hopwire_strider_kill_dist_v", "256");
ConVar hopwire_strider_hits("hopwire_strider_hits", "1");
ConVar hopwire_hopheight("hopwire_hopheight", "400");

ConVar g_debug_hopwire("g_debug_hopwire", "0");

#define	DENSE_BALL_MODEL	"models/props_junk/metal_paintcan001b.mdl"

#define	MAX_HOP_HEIGHT		(hopwire_hopheight.GetFloat())		// Maximum amount the grenade will "hop" upwards when detonated

class CGravityVortexController : public CBaseEntity
{
	DECLARE_CLASS(CGravityVortexController, CBaseEntity);
	DECLARE_DATADESC();

public:

	CGravityVortexController(void) : m_flEndTime(0.0f), m_flRadius(256), m_flStrength(256), m_flMass(0.0f) {}
	float	GetConsumedMass(void) const;

	static CGravityVortexController* Create(const Vector& origin, float radius, float strength, float duration);

private:

	void	ConsumeEntity(CBaseEntity* pEnt);
	void	PullPlayersInRange(void);
	bool	KillNPCInRange(CBaseEntity* pVictim, IPhysicsObject** pPhysObj);
	void	CreateDenseBall(void);
	void	PullThink(void);
	void	StartPull(const Vector& origin, float radius, float strength, float duration);

	float	m_flMass;		// Mass consumed by the vortex
	float	m_flEndTime;	// Time when the vortex will stop functioning
	float	m_flRadius;		// Area of effect for the vortex
	float	m_flStrength;	// Pulling strength of the vortex

	// Positions where something was consumed -- pickups are spawned here
	// only once the vortex is fully done pulling (see PullThink()), NOT
	// immediately in ConsumeEntity(). Spawning immediately caused an
	// infinite feedback loop: item_healthkit/item_battery are physics
	// props, so a pickup spawned mid-vortex would just get sucked back in
	// and consumed again, spawning more pickups, forever.
	CUtlVector<Vector>	m_ConsumedPositions;
};

//-----------------------------------------------------------------------------
// Purpose: Returns the amount of mass consumed by the vortex
//-----------------------------------------------------------------------------
float CGravityVortexController::GetConsumedMass(void) const
{
	return m_flMass;
}

//-----------------------------------------------------------------------------
// Purpose: Drops a health kit + armor battery at the spot an entity was
// consumed by the vortex. CBaseEntity::Create() already calls DispatchSpawn
// internally (same pattern used elsewhere in this codebase, e.g.
// CMissile::Create in weapon_rpg.cpp), so no separate ->Spawn() call is needed.
//-----------------------------------------------------------------------------
static void SpawnVortexPickups(const Vector& origin)
{
	CBaseEntity::Create("item_healthkit", origin, vec3_angle);
	CBaseEntity::Create("item_battery", origin + Vector(0, 0, 4), vec3_angle);	// slight vertical offset so they don't spawn perfectly overlapping
}

//-----------------------------------------------------------------------------
// Purpose: Adds the entity's mass to the aggregate mass consumed
//-----------------------------------------------------------------------------
void CGravityVortexController::ConsumeEntity(CBaseEntity* pEnt)
{
	// Get our base physics object
	IPhysicsObject* pPhysObject = pEnt->VPhysicsGetObject();
	if (pPhysObject == NULL)
		return;

	// Ragdolls need to report the sum of all their parts
	CRagdollProp* pRagdoll = dynamic_cast<CRagdollProp*>(pEnt);
	if (pRagdoll != NULL)
	{
		// Find the aggregate mass of the whole ragdoll
		ragdoll_t* pRagdollPhys = pRagdoll->GetRagdoll();
		for (int j = 0; j < pRagdollPhys->listCount; ++j)
		{
			m_flMass += pRagdollPhys->list[j].pObject->GetMass();
		}
	}
	else
	{
		// Otherwise we just take the normal mass
		m_flMass += pPhysObject->GetMass();
	}

	// Record where this was consumed -- capped so an especially large pull
	// doesn't spawn an unbounded number of pickups. Anything beyond the cap
	// is still consumed normally (mass tallied, entity removed) below --
	// it just won't produce a pickup of its own.
	if (m_ConsumedPositions.Count() < hopwire_max_pickups.GetInt())
	{
		m_ConsumedPositions.AddToTail(pEnt->GetAbsOrigin());
	}

	// Destroy the entity
	UTIL_Remove(pEnt);
}

//-----------------------------------------------------------------------------
// Purpose: Causes players within the radius to be sucked in
//-----------------------------------------------------------------------------
void CGravityVortexController::PullPlayersInRange(void)
{
	CBasePlayer* pPlayer = UTIL_GetLocalPlayer();

	Vector	vecForce = GetAbsOrigin() - pPlayer->WorldSpaceCenter();
	float	dist = VectorNormalize(vecForce);

	// FIXME: Need a more deterministic method here
	if (dist < 128.0f)
	{
		// Kill the player (with falling death sound and effects)
		CTakeDamageInfo deathInfo(this, this, GetAbsOrigin(), GetAbsOrigin(), 200, DMG_FALL);
		pPlayer->TakeDamage(deathInfo);

		if (pPlayer->IsAlive() == false)
		{
			color32 black = { 0, 0, 0, 255 };
			UTIL_ScreenFade(pPlayer, black, 0.1f, 0.0f, (FFADE_OUT | FFADE_STAYOUT));
			return;
		}
	}

	// Must be within the radius
	if (dist > m_flRadius)
		return;

	float mass = pPlayer->VPhysicsGetObject()->GetMass();
	float playerForce = m_flStrength * 0.05f;

	// Find the pull force
	// NOTE: We might want to make this non-linear to give more of a "grace distance"
	vecForce *= (1.0f - (dist / m_flRadius)) * playerForce * mass;
	vecForce[2] *= 0.025f;

	pPlayer->SetBaseVelocity(vecForce);
	pPlayer->AddFlag(FL_BASEVELOCITY);

	// Make sure the player moves
	if (vecForce.z > 0 && (pPlayer->GetFlags() & FL_ONGROUND))
	{
		pPlayer->SetGroundEntity(NULL);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Attempts to kill an NPC if it's within range and other criteria
// Input  : *pVictim - NPC to assess
//			**pPhysObj - pointer to the ragdoll created if the NPC is killed
// Output :	bool - whether or not the NPC was killed and the returned pointer is valid
//-----------------------------------------------------------------------------
bool CGravityVortexController::KillNPCInRange(CBaseEntity* pVictim, IPhysicsObject** pPhysObj)
{
	CBaseCombatCharacter* pBCC = pVictim->MyCombatCharacterPointer();

	// See if we can ragdoll
	if (pBCC != NULL && pBCC->CanBecomeRagdoll())
	{
		// Don't bother with striders
		if (FClassnameIs(pBCC, "npc_strider"))
			return false;

		// TODO: Make this an interaction between the NPC and the vortex

		// Become ragdoll
		CTakeDamageInfo info(this, this, 1.0f, DMG_GENERIC);
		CBaseEntity* pRagdoll = CreateServerRagdoll(pBCC, 0, info, COLLISION_GROUP_INTERACTIVE_DEBRIS, true);
		pRagdoll->SetCollisionBounds(pVictim->CollisionProp()->OBBMins(), pVictim->CollisionProp()->OBBMaxs());

		// Necessary to cause it to do the appropriate death cleanup
		CTakeDamageInfo ragdollInfo(this, this, 10000.0, DMG_GENERIC | DMG_REMOVENORAGDOLL);
		pVictim->TakeDamage(ragdollInfo);

		// Return the pointer to the ragdoll
		*pPhysObj = pRagdoll->VPhysicsGetObject();
		return true;
	}

	// Wasn't able to ragdoll this target
	*pPhysObj = NULL;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Creates a dense ball with a mass equal to the aggregate mass consumed by the vortex
//-----------------------------------------------------------------------------
void CGravityVortexController::CreateDenseBall(void)
{
	CBaseEntity* pBall = CreateEntityByName("prop_physics");

	pBall->SetModel(DENSE_BALL_MODEL);
	pBall->SetAbsOrigin(GetAbsOrigin());
	pBall->Spawn();

	IPhysicsObject* pObj = pBall->VPhysicsGetObject();
	if (pObj != NULL)
	{
		pObj->SetMass(GetConsumedMass());
	}
}

//-----------------------------------------------------------------------------
// Purpose: Pulls physical objects towards the vortex center, killing them if they come too near
//-----------------------------------------------------------------------------
void CGravityVortexController::PullThink(void)
{
	// Pull any players close enough to us
	PullPlayersInRange();

	Vector mins, maxs;
	mins = GetAbsOrigin() - Vector(m_flRadius, m_flRadius, m_flRadius);
	maxs = GetAbsOrigin() + Vector(m_flRadius, m_flRadius, m_flRadius);

	// Draw debug information
	if (g_debug_hopwire.GetBool())
	{
		NDebugOverlay::Box(GetAbsOrigin(), mins - GetAbsOrigin(), maxs - GetAbsOrigin(), 0, 255, 0, 16, 4.0f);
	}

	CBaseEntity* pEnts[128];
	int numEnts = UTIL_EntitiesInBox(pEnts, 128, mins, maxs, 0);

	for (int i = 0; i < numEnts; i++)
	{
		IPhysicsObject* pPhysObject = NULL;

		// Attempt to kill and ragdoll any victims in range
		if (KillNPCInRange(pEnts[i], &pPhysObject) == false)
		{
			// If we didn't have a valid victim, see if we can just get the vphysics object
			pPhysObject = pEnts[i]->VPhysicsGetObject();
			if (pPhysObject == NULL)
				continue;
		}

		float mass;

		CRagdollProp* pRagdoll = dynamic_cast<CRagdollProp*>(pEnts[i]);
		if (pRagdoll != NULL)
		{
			ragdoll_t* pRagdollPhys = pRagdoll->GetRagdoll();
			mass = 0.0f;

			// Find the aggregate mass of the whole ragdoll
			for (int j = 0; j < pRagdollPhys->listCount; ++j)
			{
				mass += pRagdollPhys->list[j].pObject->GetMass();
			}
		}
		else
		{
			mass = pPhysObject->GetMass();
		}

		Vector	vecForce = GetAbsOrigin() - pEnts[i]->WorldSpaceCenter();
		Vector	vecForce2D = vecForce;
		vecForce2D[2] = 0.0f;
		float	dist2D = VectorNormalize(vecForce2D);
		float	dist = VectorNormalize(vecForce);

		// FIXME: Need a more deterministic method here
		if (dist < 48.0f)
		{
			ConsumeEntity(pEnts[i]);
			continue;
		}

		// Must be within the radius
		if (dist > m_flRadius)
			continue;

		// Find the pull force
		vecForce *= (1.0f - (dist2D / m_flRadius)) * m_flStrength * mass;

		if (pEnts[i]->VPhysicsGetObject())
		{
			// Pull the object in
			pEnts[i]->VPhysicsTakeDamage(CTakeDamageInfo(this, this, vecForce, GetAbsOrigin(), m_flStrength, DMG_BLAST));
		}
	}

	// Keep going if need-be
	if (m_flEndTime > gpGlobals->curtime)
	{
		SetThink(&CGravityVortexController::PullThink);
		SetNextThink(gpGlobals->curtime + 0.1f);
	}
	else
	{
		//Msg( "Consumed %.2f kilograms\n", m_flMass );
		//CreateDenseBall();

		// The vortex is done pulling entirely -- safe to spawn pickups now,
		// since nothing further will be sucked in to re-consume them.
		for (int i = 0; i < m_ConsumedPositions.Count(); i++)
		{
			SpawnVortexPickups(m_ConsumedPositions[i]);
		}
		m_ConsumedPositions.RemoveAll();

		// The controller entity itself was never being cleaned up before --
		// remove it now that it's done its job.
		UTIL_Remove(this);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Starts the vortex working
//-----------------------------------------------------------------------------
void CGravityVortexController::StartPull(const Vector& origin, float radius, float strength, float duration)
{
	SetAbsOrigin(origin);
	m_flEndTime = gpGlobals->curtime + duration;
	m_flRadius = radius;
	m_flStrength = strength;

	SetThink(&CGravityVortexController::PullThink);
	SetNextThink(gpGlobals->curtime + 0.1f);
}

//-----------------------------------------------------------------------------
// Purpose: Creation utility
//-----------------------------------------------------------------------------
CGravityVortexController* CGravityVortexController::Create(const Vector& origin, float radius, float strength, float duration)
{
	// Create an instance of the vortex
	CGravityVortexController* pVortex = (CGravityVortexController*)CreateEntityByName("vortex_controller");
	if (pVortex == NULL)
		return NULL;

	// Start the vortex working
	pVortex->StartPull(origin, radius, strength, duration);

	return pVortex;
}

BEGIN_DATADESC(CGravityVortexController)
DEFINE_FIELD(m_flMass, FIELD_FLOAT),
DEFINE_FIELD(m_flEndTime, FIELD_TIME),
DEFINE_FIELD(m_flRadius, FIELD_FLOAT),
DEFINE_FIELD(m_flStrength, FIELD_FLOAT),

DEFINE_THINKFUNC(PullThink),
END_DATADESC()

LINK_ENTITY_TO_CLASS(vortex_controller, CGravityVortexController);

#define GRENADE_MODEL	"models/Weapons/w_grenade.mdl"		// same model grenade_frag.cpp uses

BEGIN_DATADESC(CGrenadeHopwire)
DEFINE_FIELD(m_hVortexController, FIELD_EHANDLE),
DEFINE_FIELD(m_pMainGlow, FIELD_EHANDLE),
DEFINE_FIELD(m_pGlowTrail, FIELD_EHANDLE),
DEFINE_FIELD(m_flDetonateTime, FIELD_TIME),

DEFINE_THINKFUNC(EndThink),
DEFINE_THINKFUNC(CombatThink),
DEFINE_THINKFUNC(BeepThink),
END_DATADESC()

LINK_ENTITY_TO_CLASS(npc_grenade_hopwire, CGrenadeHopwire);

IMPLEMENT_SERVERCLASS_ST(CGrenadeHopwire, DT_GrenadeHopwire)
END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::Spawn(void)
{
	Precache();

	SetModel(GRENADE_MODEL);
	SetCollisionGroup(COLLISION_GROUP_PROJECTILE);

	CreateVPhysics();

	CreateEffects();
}

//-----------------------------------------------------------------------------
// Purpose: Cyan glow + light trail, following the exact same pattern
// grenade_frag.cpp uses for its own (red/green) glow -- CSprite for the
// glow, CSpriteTrail for the trail. "laserbeam.vmt" is used (rather than
// grenade_frag's "bluelaser1.vmt") because it's a neutral white sprite
// meant to be tinted, whereas bluelaser1 is pre-colored and would mix
// into a muddy tone under a cyan tint instead of reading as clean cyan.
//-----------------------------------------------------------------------------
void CGrenadeHopwire::CreateEffects(void)
{
	m_pMainGlow = CSprite::SpriteCreate("sprites/redglow1.vmt", GetLocalOrigin(), false);

	if (m_pMainGlow != NULL)
	{
		m_pMainGlow->FollowEntity(this);
		m_pMainGlow->SetTransparency(kRenderGlow, 0, 255, 255, 200, kRenderFxNoDissipation);	// cyan
		m_pMainGlow->SetScale(0.2f);
		m_pMainGlow->SetGlowProxySize(4.0f);
	}

	m_pGlowTrail = CSpriteTrail::SpriteTrailCreate("sprites/laserbeam.vmt", GetLocalOrigin(), false);

	if (m_pGlowTrail != NULL)
	{
		m_pGlowTrail->FollowEntity(this);
		m_pGlowTrail->SetTransparency(kRenderTransAdd, 0, 255, 255, 255, kRenderFxNone);	// cyan
		m_pGlowTrail->SetStartWidth(8.0f);
		m_pGlowTrail->SetEndWidth(1.0f);
		m_pGlowTrail->SetLifeTime(0.5f);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Recreate the glow/trail after a save/restore, same reasoning
// grenade_frag.cpp uses for its own effects surviving a save game.
//-----------------------------------------------------------------------------
void CGrenadeHopwire::OnRestore(void)
{
	CreateEffects();

	BaseClass::OnRestore();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CGrenadeHopwire::CreateVPhysics()
{
	// Create the object in the physics system
	VPhysicsInitNormal(SOLID_BBOX, 0, false);
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::Precache(void)
{
	// FIXME: Replace
	//PrecacheSound("NPC_Strider.Shoot");
	//PrecacheSound("d3_citadel.weapon_zapper_beam_loop2");

	PrecacheModel(GRENADE_MODEL);

	PrecacheModel(DENSE_BALL_MODEL);

	// Cyan glow + trail sprites (see CreateEffects())
	PrecacheModel("sprites/redglow1.vmt");
	PrecacheModel("sprites/laserbeam.vmt");

	// Reused from grenade_frag.cpp's own beep sound.
	PrecacheScriptSound("Grenade.Blip");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : timer - 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::SetTimer(float timer)
{
	SetThink(&CBaseGrenade::PreDetonate);
	SetNextThink(gpGlobals->curtime + timer);

	// Remembered so BeepThink() can compute how much time is left and
	// speed up as it approaches zero.
	m_flDetonateTime = gpGlobals->curtime + timer;

	// Runs as an independent named-context think, in parallel with the
	// main PreDetonate think above -- same pattern CLaserDot uses for its
	// own separate think schedule elsewhere in this codebase.
	SetContextThink(&CGrenadeHopwire::BeepThink, gpGlobals->curtime, "BeepThink");
}

//-----------------------------------------------------------------------------
// Purpose: Beeps on an accelerating schedule as detonation approaches --
// spaced out while there's still plenty of time left, rapid once close.
// Runs independently of the main detonate countdown via a named context,
// and gets explicitly stopped in Detonate() once the hop actually begins.
//-----------------------------------------------------------------------------
void CGrenadeHopwire::BeepThink(void)
{
	// Reusing the frag grenade's real beep sound (see grenade_frag.cpp's
	// BlipSound()) instead of a made-up placeholder name.
	EmitSound("Grenade.Blip");

	float flTimeLeft = m_flDetonateTime - gpGlobals->curtime;

	float flInterval = (flTimeLeft <= hopwire_beep_near_threshold.GetFloat())
		? hopwire_beep_interval_near.GetFloat()
		: hopwire_beep_interval_far.GetFloat();

	SetContextThink(&CGrenadeHopwire::BeepThink, gpGlobals->curtime + flInterval, "BeepThink");
}

#define	MAX_STRIDER_KILL_DISTANCE_HORZ	(hopwire_strider_kill_dist_h.GetFloat())		// Distance a Strider will be killed if within
#define	MAX_STRIDER_KILL_DISTANCE_VERT	(hopwire_strider_kill_dist_v.GetFloat())		// Distance a Strider will be killed if within

#define MAX_STRIDER_STUN_DISTANCE_HORZ	(MAX_STRIDER_KILL_DISTANCE_HORZ*2)	// Distance a Strider will be stunned if within
#define MAX_STRIDER_STUN_DISTANCE_VERT	(MAX_STRIDER_KILL_DISTANCE_VERT*2)	// Distance a Strider will be stunned if within

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::KillStriders(void)
{
	CBaseEntity* pEnts[128];
	Vector	mins, maxs;

	ClearBounds(mins, maxs);
	AddPointToBounds(-Vector(MAX_STRIDER_STUN_DISTANCE_HORZ, MAX_STRIDER_STUN_DISTANCE_HORZ, MAX_STRIDER_STUN_DISTANCE_HORZ), mins, maxs);
	AddPointToBounds(Vector(MAX_STRIDER_STUN_DISTANCE_HORZ, MAX_STRIDER_STUN_DISTANCE_HORZ, MAX_STRIDER_STUN_DISTANCE_HORZ), mins, maxs);
	AddPointToBounds(-Vector(MAX_STRIDER_STUN_DISTANCE_VERT, MAX_STRIDER_STUN_DISTANCE_VERT, MAX_STRIDER_STUN_DISTANCE_VERT), mins, maxs);
	AddPointToBounds(Vector(MAX_STRIDER_STUN_DISTANCE_VERT, MAX_STRIDER_STUN_DISTANCE_VERT, MAX_STRIDER_STUN_DISTANCE_VERT), mins, maxs);

	// FIXME: It's probably much faster to simply iterate over the striders in the map, rather than any entity in the radius - jdw

	// Find any striders in range of us
	int numTargets = UTIL_EntitiesInBox(pEnts, ARRAYSIZE(pEnts), GetAbsOrigin() + mins, GetAbsOrigin() + maxs, FL_NPC);
	float targetDistHorz, targetDistVert;

	for (int i = 0; i < numTargets; i++)
	{
		// Only affect striders
		if (FClassnameIs(pEnts[i], "npc_strider") == false)
			continue;

		// We categorize our spatial relation to the strider in horizontal and vertical terms, so that we can specify both parameters separately
		targetDistHorz = UTIL_DistApprox2D(pEnts[i]->GetAbsOrigin(), GetAbsOrigin());
		targetDistVert = fabs(pEnts[i]->GetAbsOrigin()[2] - GetAbsOrigin()[2]);

		if (targetDistHorz < MAX_STRIDER_KILL_DISTANCE_HORZ && targetDistHorz < MAX_STRIDER_KILL_DISTANCE_VERT)
		{
			// Kill the strider
			float fracDamage = (pEnts[i]->GetMaxHealth() / hopwire_strider_hits.GetFloat()) + 1.0f;
			CTakeDamageInfo killInfo(this, this, fracDamage, DMG_GENERIC);
			Vector	killDir = pEnts[i]->GetAbsOrigin() - GetAbsOrigin();
			VectorNormalize(killDir);

			killInfo.SetDamageForce(killDir * -1000.0f);
			killInfo.SetDamagePosition(GetAbsOrigin());

			pEnts[i]->TakeDamage(killInfo);
		}
		else if (targetDistHorz < MAX_STRIDER_STUN_DISTANCE_HORZ && targetDistHorz < MAX_STRIDER_STUN_DISTANCE_VERT)
		{
			// Stun the strider
			CTakeDamageInfo killInfo(this, this, 200.0f, DMG_GENERIC);
			pEnts[i]->TakeDamage(killInfo);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::EndThink(void)
{
	if (hopwire_vortex.GetBool())
	{
		EntityMessageBegin(this, true);
		WRITE_BYTE(1);
		MessageEnd();
	}

	SetThink(&CBaseEntity::SUB_Remove);
	SetNextThink(gpGlobals->curtime + 1.0f);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::CombatThink(void)
{
	// Stop the grenade from moving
	AddEFlags(EF_NODRAW);
	AddFlag(FSOLID_NOT_SOLID);
	VPhysicsDestroyObject();
	SetAbsVelocity(vec3_origin);
	SetMoveType(MOVETYPE_NONE);

	// The vortex effect takes over visually from here -- clean up the
	// cyan glow/trail rather than leaving them floating through it.
	if (m_pMainGlow != NULL)
	{
		UTIL_Remove(m_pMainGlow);
		m_pMainGlow = NULL;
	}

	if (m_pGlowTrail != NULL)
	{
		UTIL_Remove(m_pGlowTrail);
		m_pGlowTrail = NULL;
	}

	// Do special behaviors if there are any striders in the area
	KillStriders();

	// FIXME: Replace
	//EmitSound("NPC_Strider.Shoot");
	//EmitSound("d3_citadel.weapon_zapper_beam_loop2");

	// Quick screen flash
	CBasePlayer* pPlayer = ToBasePlayer(GetThrower());
	color32 white = { 255,255,255,255 };
	UTIL_ScreenFade(pPlayer, white, 0.2f, 0.0f, FFADE_IN);

	// Create the vortex controller to pull entities towards us
	if (hopwire_vortex.GetBool())
	{
		m_hVortexController = CGravityVortexController::Create(GetAbsOrigin(), 512, 150, 3.0f);

		// Start our client-side effect
		EntityMessageBegin(this, true);
		WRITE_BYTE(0);
		MessageEnd();

		// Begin to stop in two seconds
		SetThink(&CGrenadeHopwire::EndThink);
		SetNextThink(gpGlobals->curtime + 2.0f);
	}
	else
	{
		// Remove us immediately
		SetThink(&CBaseEntity::SUB_Remove);
		SetNextThink(gpGlobals->curtime + 0.1f);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGrenadeHopwire::SetVelocity(const Vector& velocity, const AngularImpulse& angVelocity)
{
	IPhysicsObject* pPhysicsObject = VPhysicsGetObject();

	if (pPhysicsObject != NULL)
	{
		pPhysicsObject->AddVelocity(&velocity, &angVelocity);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hop off the ground to start deployment
//-----------------------------------------------------------------------------
void CGrenadeHopwire::Detonate(void)
{
	// Stop beeping -- it's actually going off now, no need to keep ticking.
	SetContextThink(NULL, TICK_NEVER_THINK, "BeepThink");

	AngularImpulse	hopAngle = RandomAngularImpulse(-300, 300);

	//Find out how tall the ceiling is and always try to hop halfway
	trace_t	tr;
	UTIL_TraceLine(GetAbsOrigin(), GetAbsOrigin() + Vector(0, 0, MAX_HOP_HEIGHT * 2), MASK_SOLID, this, COLLISION_GROUP_NONE, &tr);

	// Jump half the height to the found ceiling
	float hopHeight = MIN(MAX_HOP_HEIGHT, (MAX_HOP_HEIGHT * tr.fraction));

	//Add upwards velocity for the "hop"
	Vector hopVel(0.0f, 0.0f, hopHeight);
	SetVelocity(hopVel, hopAngle);

	// Get the time until the apex of the hop
	float apexTime = sqrt(hopHeight / GetCurrentGravity());

	// Explode at the apex
	SetThink(&CGrenadeHopwire::CombatThink);
	SetNextThink(gpGlobals->curtime + apexTime);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseGrenade* HopWire_Create(const Vector& position, const QAngle& angles, const Vector& velocity, const AngularImpulse& angVelocity, CBaseEntity* pOwner, float timer)
{
	CGrenadeHopwire* pGrenade = (CGrenadeHopwire*)CBaseEntity::Create("npc_grenade_hopwire", position, angles, pOwner);

	// Only set ourselves to detonate on a timer if we're not a trap hopwire
	if (hopwire_trap.GetBool() == false)
	{
		pGrenade->SetTimer(timer);
	}

	pGrenade->SetVelocity(velocity, angVelocity);
	pGrenade->SetThrower(ToBaseCombatCharacter(pOwner));

	return pGrenade;
}
