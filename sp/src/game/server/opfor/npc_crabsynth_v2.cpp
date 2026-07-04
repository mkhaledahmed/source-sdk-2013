//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:	Antlion Guard
//
//=============================================================================//

#include "cbase.h"
#include "ai_memory.h"
#include "ai_basenpc.h"
#include "ai_behavior.h"
#include "player_pickup.h"
#include "npcevent.h"
#include "iservervehicle.h"
#include "ai_network.h"
#include "ai_node.h"
#include "ai_moveprobe.h"
#include "ai_squad.h"
#include "npc_crabsynth_v2.h"
#include "ammodef.h"

//-----------------------------------------------------------------------------
// Purpose: Calculate & apply damage & force for a charge to a target.
//			Done outside of the guard because we need to do this inside a trace filter.
//-----------------------------------------------------------------------------
static void ApplyChargeDamage(CBaseEntity* pCrabSynth, CBaseEntity* pTarget, float flDamage)
{
	Vector attackDir = (pTarget->WorldSpaceCenter() - pCrabSynth->WorldSpaceCenter());
	VectorNormalize(attackDir);
	Vector offset = RandomVector(-32, 32) + pTarget->WorldSpaceCenter();

	// Generate enough force to make a 75kg guy move away at 700 in/sec
	Vector vecForce = attackDir * ImpulseScale(75, 700);

	// Deal the damage
	CTakeDamageInfo	info(pCrabSynth, pCrabSynth, vecForce, offset, flDamage, DMG_CLUB);
	pTarget->TakeDamage(info);

}

//==================================================
// CNPC_AntlionGuard::m_DataDesc
//==================================================

BEGIN_DATADESC(CNPC_CrabSynth)

DEFINE_FIELD(m_CnextCharge, FIELD_BOOLEAN),
DEFINE_FIELD(m_CflNextRangeAttackTime, FIELD_TIME),
DEFINE_FIELD(m_CflNextMelee2AttackTime, FIELD_TIME),
DEFINE_FIELD(m_CflNextRoarTime, FIELD_TIME),
DEFINE_FIELD(m_flNextSideStepTime, FIELD_TIME),
DEFINE_FIELD(m_bWasInGreenZone, FIELD_BOOLEAN), // Tracking variable for the green zone

END_DATADESC()

//==============================================================================================
// ANTLION GUARD PHYSICS DAMAGE TABLE
//==============================================================================================
static impactentry_t crabSynthLinearTable[] =
{
	{ 100 * 100,	10 },
	{ 250 * 250,	25 },
	{ 350 * 350,	50 },
	{ 500 * 500,	75 },
	{ 1000 * 1000,100 },
};

static impactentry_t crabSynthAngularTable[] =
{
	{  50 * 50, 10 },
	{ 100 * 100, 25 },
	{ 150 * 150, 50 },
	{ 200 * 200, 75 },
};

impactdamagetable_t gcrabSynthImpactDamageTable =
{
	crabSynthLinearTable,
	crabSynthAngularTable,

	ARRAYSIZE(crabSynthLinearTable),
	ARRAYSIZE(crabSynthAngularTable),

	200 * 200,// minimum linear speed squared
	180 * 180,// minimum angular speed squared (360 deg/s to cause spin/slice damage)
	15,		// can't take damage from anything under 15kg

	10,		// anything less than 10kg is "small"
	5,		// never take more than 1 pt of damage from anything under 15kg
	128 * 128,// <15kg objects must go faster than 36 in/s to do damage

	45,		// large mass in kg 
	2,		// large mass scale (anything over 500kg does 4X as much energy to read from damage table)
	1,		// large mass falling scale
	0,		// my min velocity
};

//-----------------------------------------------------------------------------
// Purpose: 
// Output : const impactdamagetable_t
//-----------------------------------------------------------------------------
const impactdamagetable_t& CNPC_CrabSynth::GetPhysicsImpactDamageTable(void)
{
	return gcrabSynthImpactDamageTable;
}

//==================================================
// CNPC_AntlionGuard
//==================================================
CNPC_CrabSynth::CNPC_CrabSynth(void)
{
	m_bIsFiring = false;
	m_flNextGunTime = 0.0f;
	m_flGunBurstEnd = 0.0f;
	m_flNextSideStepTime = 0.0f;
	m_nStandFirePhase = CRABSYNTH_STANDFIRE_WINDUP;
	m_flStandFireUntil = 0.0f;
	m_bWasInGreenZone = false; // Initialize the zone memory
}

LINK_ENTITY_TO_CLASS(npc_crabsynth, CNPC_CrabSynth);

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::UpdateOnRemove(void)
{
	// Chain to the base class
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: Catch think to handle continuous burst firing
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::NPCThink(void)
{
	BaseClass::NPCThink();

	if (m_bIsFiring)
	{
		// Stop firing if the burst time is up, or the enemy is gone/dead
		if (gpGlobals->curtime > m_flGunBurstEnd || !GetEnemy() || !GetEnemy()->IsAlive())
		{
			m_bIsFiring = false;
		}
		else if (gpGlobals->curtime >= m_flNextGunTime)
		{
			// Calculate the gun position relative to the CrabSynth's current facing direction
			Vector forward, right, up;
			GetVectors(&forward, &right, &up);
			Vector vecShootOrigin = GetAbsOrigin() + (forward * m_HackedGunPos.x) + (right * m_HackedGunPos.y) + (up * m_HackedGunPos.z);

			// Calculate the direction to the enemy
			Vector vecShootDir = GetEnemy()->BodyTarget(vecShootOrigin) - vecShootOrigin;
			VectorNormalize(vecShootDir);

			// Fire the hitscan bullet with a 5-degree cone for a Strider-like spread
			FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_5DEGREES, 8192, m_miniGunAmmo, 1);

			// Play the Strider's minigun sound
			EmitSound("NPC_Strider.FireMinigun");

			// Schedule the next bullet in the burst
			m_flNextGunTime = gpGlobals->curtime + 0.1f;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::Precache(void)
{
	// Fetch the Strider's minigun ammo definition
	m_miniGunAmmo = GetAmmoDef()->Index("StriderMinigun");

	PrecacheModel(DefaultOrCustomModel(CRABSYNTH_MODEL));;
	PrecacheScriptSound("NPC_Strider.FireMinigun");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::Spawn(void)
{
	Precache();

	SetModel(DefaultOrCustomModel(CRABSYNTH_MODEL));

	SetHullType(HULL_LARGE);
	SetHullSizeNormal();
	SetDefaultEyeOffset();

	SetSolid(SOLID_BBOX);
	AddSolidFlags(FSOLID_NOT_STANDABLE);
	SetMoveType(MOVETYPE_STEP);

	SetNavType(NAV_GROUND);
	SetBloodColor(BLOOD_COLOR_YELLOW);

	SetCollisionGroup(COLLISION_GROUP_NPC);

	m_iHealth = sk_crabsynth_health.GetFloat();
	m_iMaxHealth = m_iHealth;
	m_flFieldOfView = CRABSYNTH_FOV_NORMAL;


	m_CflNextRoarTime = 0;

	ClearHintGroup();

	m_HackedGunPos.x = 10;
	m_HackedGunPos.y = 0;
	m_HackedGunPos.z = 30;

	CapabilitiesClear();
	CapabilitiesAdd(bits_CAP_MOVE_GROUND | bits_CAP_INNATE_MELEE_ATTACK1 | bits_CAP_SQUAD);
	CapabilitiesAdd(bits_CAP_SKIP_NAV_GROUND_CHECK);

	NPCInit();

	BaseClass::Spawn();

	// Do not dissolve
	AddEFlags(EFL_NO_DISSOLVE);

	// We get a minute of free knowledge about the target
	GetEnemies()->SetEnemyDiscardTime(120.0f);
	GetEnemies()->SetFreeKnowledgeDuration(60.0f);

	// We need to bloat the absbox to encompass all the hitboxes
	Vector absMin = Vector(-140, -140, 0);
	Vector absMax = Vector(140, 140, 160);

	CollisionProp()->SetSurroundingBoundsType(USE_SPECIFIED_BOUNDS, &absMin, &absMax);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::IsEnemyAboveAndUnreachable(void)
{
	CBaseEntity* pEnemy = GetEnemy();
	if (!pEnemy)
		return false;

	// Must be meaningfully above us.
	float flZDiff = pEnemy->GetAbsOrigin().z - GetAbsOrigin().z;
	if (flZDiff < sk_crabsynth_above_height.GetFloat())
		return false;

	// ...and something we can't currently walk to. RememberUnreachable() (called
	// when a chase path fails) feeds this, so a player who hops onto a ledge the
	// crab can't climb naturally trips it after the first failed approach.
	return IsUnreachable(pEnemy) || HasCondition(COND_ENEMY_UNREACHABLE);
}

//-----------------------------------------------------------------------------
// Purpose: Reachability, regardless of direction. Unlike IsEnemyAboveAndUnreachable
//			this doesn't care whether he's above, below, or across a gap -- only
//			whether we can currently path to him. Populated the same way (a failed
//			chase path calls RememberUnreachable / sets COND_ENEMY_UNREACHABLE),
//			with a built-in timeout so we periodically re-test and resume closing.
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::IsEnemyUnreachable(void)
{
	CBaseEntity* pEnemy = GetEnemy();
	if (!pEnemy)
		return false;

	return IsUnreachable(pEnemy) || HasCondition(COND_ENEMY_UNREACHABLE);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::GatherConditions(void)
{
	BaseClass::GatherConditions();

	// Re-evaluate our range-band conditions from scratch each think.
	ClearCondition(COND_CRABSYNTH_CAN_CHARGE);
	ClearCondition(COND_CRABSYNTH_CAN_STAND_GUN);
	ClearCondition(COND_CRABSYNTH_CAN_RANGE_WALK);
	ClearCondition(COND_CRABSYNTH_ENEMY_ABOVE_UNREACHABLE);

	if (GetEnemy())
	{
		const float flMeleeRange = sk_crabsynth_melee_range.GetFloat();
		const float flStandGunRange = sk_crabsynth_standgun_range.GetFloat();
		const float flChargeRange = sk_crabsynth_charge_range.GetFloat();

		if (g_debug_crabsynth.GetInt() > 0)
		{
			int segments = 32;
			float flStep = (2.0f * 3.14159f) / segments;
			Vector vecPos = GetAbsOrigin();
			vecPos.z += 10.0f; // Offset slightly above ground to avoid z-fighting

			for (int i = 0; i < segments; i++)
			{
				float theta1 = i * flStep;
				float theta2 = (i + 1) * flStep;

				Vector p1(cos(theta1), sin(theta1), 0);
				Vector p2(cos(theta2), sin(theta2), 0);

				// Melee range (Red)
				NDebugOverlay::Line(vecPos + p1 * flMeleeRange, vecPos + p2 * flMeleeRange, 255, 0, 0, true, 0.1f);
				// Stand and Gun range (Yellow)
				NDebugOverlay::Line(vecPos + p1 * flStandGunRange, vecPos + p2 * flStandGunRange, 255, 255, 0, true, 0.1f);
				// Charge range (Green)
				NDebugOverlay::Line(vecPos + p1 * flChargeRange, vecPos + p2 * flChargeRange, 0, 255, 0, true, 0.1f);
			}
		}

		float flDist = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length2D();
		bool  bCanSee = HasCondition(COND_SEE_ENEMY);

		// Visible but we can't path to him (perched on a ledge, across a gap, or
		// behind geometry the nav can't route around -- any direction, not just
		// above): plant and deploy the minigun and suppress, rather than trying
		// to walk in and spamming failed GetPathToEnemy. He periodically re-tests
		// reachability (the unreachable memory times out) and resumes closing.
		if (bCanSee && IsEnemyUnreachable())
		{
			SetCondition(COND_CRABSYNTH_ENEMY_ABOVE_UNREACHABLE);
		}

		// Determine the player's current zone based purely on the numerical boundaries
		// Yellow Zone: Between 400 and 800
		bool bInYellowZone = (flDist >= flMeleeRange && flDist < flStandGunRange);
		// Green Zone: Between 800 and 1400
		bool bInGreenZone = (flDist >= flStandGunRange && flDist <= flChargeRange);

		// Track if the player has been in the green zone
		if (bInGreenZone)
		{
			m_bWasInGreenZone = true;
		}
		else if (flDist > flChargeRange || flDist < flMeleeRange)
		{
			// Reset the memory if the player escapes beyond the 1400 range or enters melee under 400
			m_bWasInGreenZone = false;
		}

		// CHARGE: Only trigger if the player is currently in the yellow zone (400-800)
		// AND previously in the green zone (800-1400)
		if (bInYellowZone && m_bWasInGreenZone &&
			ShouldCharge(GetAbsOrigin(), GetEnemy()->GetAbsOrigin(), true, false))
		{
			SetCondition(COND_CRABSYNTH_CAN_CHARGE);
		}

		// YELLOW ZONE: Plant and deploy the minigun (stand-and-gun burst).
		// Only do this if we haven't decided to charge based on the memory logic above.
		if (bCanSee && bInYellowZone && !HasCondition(COND_CRABSYNTH_CAN_CHARGE))
		{
			SetCondition(COND_CRABSYNTH_CAN_STAND_GUN);
		}

		// GREEN ZONE: Advance while firing to close (walk-and-gun).
		// Crab fires WHILST walking exclusively in the Green zone (800-1400).
		if (bCanSee && bInGreenZone)
		{
			SetCondition(COND_CRABSYNTH_CAN_RANGE_WALK);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::SelectCombatSchedule(void)
{
	ClearHintGroup();

	// Highest priority: if he's already in melee reach, swing.
	if (HasCondition(COND_CAN_MELEE_ATTACK1))
		return SCHED_MELEE_ATTACK1;

	if (!GetEnemy())
		return BaseClass::SelectSchedule();

	// Enemy is perched above us where we can't path to him (e.g. on a ledge).
	// Plant and lay down suppressing fire rather than uselessly trying to close.
	if (HasCondition(COND_CRABSYNTH_ENEMY_ABOVE_UNREACHABLE))
		return SCHED_CRABSYNTH_STANDGROUND_FIRE;

	// Hunter-style: when we take a heavy hit, jink sideways instead of standing
	// there and eating the follow-up. Cooldown-gated so it doesn't spam.
	if (HasCondition(COND_HEAVY_DAMAGE) && gpGlobals->curtime > m_flNextSideStepTime)
	{
		m_flNextSideStepTime = gpGlobals->curtime + RandomFloat(1.5f, 3.0f);
		return SCHED_CRABSYNTH_SIDESTEP;
	}

	const float flDist = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length2D();
	const float flMeleeRange = sk_crabsynth_melee_range.GetFloat();

	// CLOSE (but not yet in reach): run him down to land the melee.
	if (flDist < flMeleeRange)
		return SCHED_CRABSYNTH_CHASE_ENEMY;

	// CHARGE whenever a viable lane exists. Evaluated strictly by our GatheringConditions.
	if (HasCondition(COND_CRABSYNTH_CAN_CHARGE))
		return SCHED_CRABSYNTH_CHARGE;

	// He can't charge right now (blocked lane, uneven ground, or on cooldown).
	if (HasCondition(COND_SEE_ENEMY))
	{
		// YELLOW ZONE: plant and unload the deployed minigun -- or, Hunter-style,
		// strafe to a fresh spot first (facing you, so the side/back blends play)
		// rather than rooting every time.
		if (HasCondition(COND_CRABSYNTH_CAN_STAND_GUN))
		{
			if (RandomInt(1, 100) <= sk_crabsynth_reposition_pct.GetInt())
				return SCHED_CRABSYNTH_CHANGE_POSITION;

			return SCHED_CRABSYNTH_STAND_AND_GUN;
		}

		// GREEN ZONE: advance while firing to close (walk-and-gun). 
		// If it still fails he degrades to firing in place (RANGE_WALK's fail schedule).
		if (HasCondition(COND_CRABSYNTH_CAN_RANGE_WALK))
			return SCHED_CRABSYNTH_RANGE_WALK;

		// BEYOND GREEN ZONE (>= 1400): fire in place. The deployed minigun only needs line of
		// sight, not a nav path, so he engages from range like a Combine soldier.
		return SCHED_CRABSYNTH_STAND_AND_GUN;
	}

	// No line of sight: reposition (facing the enemy) to re-establish one or to
	// stumble into a charge lane, instead of standing there blind.
	return SCHED_CRABSYNTH_CHANGE_POSITION;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::SelectSchedule(void)
{
	// Charge after a target if it's set
	if (m_hChargeTarget && m_hChargeTargetPosition)
	{
		ClearCondition(COND_CRABSYNTH_HAS_CHARGE_TARGET);
		ClearHintGroup();

		if (!m_hChargeTarget->IsAlive())
		{
			m_hChargeTarget = NULL;
			m_hChargeTargetPosition = NULL;

			// ONLY restore enemy if valid
			if (m_hOldTarget && m_hOldTarget->IsAlive())
			{
				SetEnemy(m_hOldTarget);
			}

			return SCHED_CRABSYNTH_CHASE_ENEMY;
		}

		// DO NOT override enemy during active chase schedule
		if (!IsCurSchedule(SCHED_CRABSYNTH_CHASE_ENEMY))
		{
			m_hOldTarget = GetEnemy();
			SetEnemy(m_hChargeTarget);
			UpdateEnemyMemory(m_hChargeTarget, m_hChargeTarget->GetAbsOrigin());

			return SCHED_CRABSYNTH_CHARGE_TARGET;
		}
	}

	//Only do these in combat states
	if (m_NPCState == NPC_STATE_COMBAT && GetEnemy())
		return SelectCombatSchedule();

	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::MeleeAttack1Conditions(float flDot, float flDist)
{
	// Don't attack again too soon
	if (GetNextAttack() > gpGlobals->curtime)
		return 0;

	// While charging, we can't melee attack
	if (IsCurSchedule(SCHED_CRABSYNTH_CHARGE))
		return 0;

	// Must actually be in reach -- this was missing entirely before,
	// which let the hull trace below grant a melee attack from any
	// distance if it happened to clip terrain/geometry along the way.
	if (flDist > CRABSYNTH_MELEE1_REACH)
		return COND_TOO_FAR_TO_ATTACK;

	// Must be within a viable cone
	if (flDot < CRABSYNTH_MELEE1_CONE)
		return COND_NOT_FACING_ATTACK;

	// If the enemy is on top of me, I'm allowed to hit the sucker
	if (GetEnemy()->GetGroundEntity() == this)
		return COND_CAN_MELEE_ATTACK1;

	trace_t	tr;
	TraceHull_SkipPhysics(WorldSpaceCenter(), GetEnemy()->WorldSpaceCenter(), Vector(-10, -10, -10), Vector(10, 10, 10), MASK_SHOT_HULL, this, COLLISION_GROUP_NONE, &tr, VPhysicsGetObject()->GetMass() * 0.5);

	// If we hit anything, go for it
	if (tr.fraction < 1.0f)
		return COND_CAN_MELEE_ATTACK1;

	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CNPC_CrabSynth::MaxYawSpeed(void)
{
	Activity act = GetActivity();
	CBaseEntity* pEnemy = GetEnemy();

	// MELEE = locked in place
	if (act == ACT_MELEE_ATTACK1)
		return 0.0f;

	// CHARGE START = fast acquisition
	if (act == ACT_CRABSYNTH_CHARGE_START)
		return 25.0f;

	// CHARGE RUN = full aggression tracking
	if (act == ACT_CRABSYNTH_CHARGE_RUN)
		return 35.0f;

	// CHARGE STOP = still responsive
	if (act == ACT_CRABSYNTH_CHARGE_STOP)
		return 20.0f;

	// Default behaviour
	if (pEnemy && !pEnemy->IsPlayer())
		return 20.0f;

	return 20.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::ShouldCharge(const Vector& startPos, const Vector& endPos, bool useTime, bool bCheckForCancel)
{
	// Must have a target
	if (!GetEnemy())
		return false;

	// Don't check the distance once we start charging
	if (!bCheckForCancel)
	{
		// Don't allow use to charge again if it's been too soon
		if (useTime && (m_CflChargeTime > gpGlobals->curtime))
			return false;

		// Must be around the same level
		if (fabs(startPos.z - endPos.z) > CRABSYNTH_CHARGE_MAX_HEIGHT_DIFF)
			return false;

		float distance = UTIL_DistApprox2D(startPos, endPos);

		// Must be within our tolerance range
		if ((distance < CRABSYNTH_CHARGE_MIN) || (distance > CRABSYNTH_CHARGE_MAX))
			return false;
	}

	if (GetSquad())
	{
		// If someone in our squad is closer to the enemy, then don't charge (we end up hitting them more often than not!)
		float flOurDistToEnemySqr = (GetAbsOrigin() - GetEnemy()->GetAbsOrigin()).LengthSqr();
		AISquadIter_t iter;
		for (CAI_BaseNPC* pSquadMember = GetSquad()->GetFirstMember(&iter); pSquadMember; pSquadMember = GetSquad()->GetNextMember(&iter))
		{
			if (pSquadMember->IsAlive() == false || pSquadMember == this)
				continue;

			if ((pSquadMember->GetAbsOrigin() - GetEnemy()->GetAbsOrigin()).LengthSqr() < flOurDistToEnemySqr)
				return false;
		}
	}

	//FIXME: We'd like to exclude small physics objects from this check!

	// We only need to hit the endpos with the edge of our bounding box
	Vector vecDir = endPos - startPos;
	VectorNormalize(vecDir);
	float flWidth = WorldAlignSize().x * 0.5;
	Vector vecTargetPos = endPos - (vecDir * flWidth);

	// See if we can directly move there
	AIMoveTrace_t moveTrace;
	GetMoveProbe()->MoveLimit(NAV_GROUND, startPos, vecTargetPos, MASK_NPCSOLID_BRUSHONLY, GetEnemy(), &moveTrace);

	// Draw the probe
	if (g_debug_crabsynth.GetInt() == 1)
	{
		Vector	enemyDir = (vecTargetPos - startPos);
		float	enemyDist = VectorNormalize(enemyDir);

		NDebugOverlay::BoxDirection(startPos, GetHullMins(), GetHullMaxs() + Vector(enemyDist, 0, 0), enemyDir, 0, 255, 0, 8, 1.0f);
	}

	// If we're not blocked, charge
	if (IsMoveBlocked(moveTrace))
	{
		// Don't allow it if it's too close to us
		if (UTIL_DistApprox(WorldSpaceCenter(), moveTrace.vEndPosition) < CRABSYNTH_CHARGE_MIN)
			return false;

		// Allow some special cases to not block us
		if (moveTrace.pObstruction != NULL)
		{
			// If we've hit the world, see if it's a cliff
			if (moveTrace.pObstruction == GetContainingEntity(INDEXENT(0)))
			{
				// Can't be too far above/below the target
				if (fabs(moveTrace.vEndPosition.z - vecTargetPos.z) > StepHeight())
					return false;

				// Allow it if we got pretty close
				if (UTIL_DistApprox(moveTrace.vEndPosition, vecTargetPos) < 64)
					return true;
			}

			// Hit things that will take damage
			if (moveTrace.pObstruction->m_takedamage != DAMAGE_NO)
				return true;

			// Hit things that will move
			if (moveTrace.pObstruction->GetMoveType() == MOVETYPE_VPHYSICS)
				return true;
		}

		return false;
	}

	// Only update this if we've requested it
	if (useTime)
	{
		// INCREASED FROM 4.0 TO 8.0 TO MAKE THE NPC GENERALLY LESS EAGER TO CHARGE
		m_CflChargeTime = gpGlobals->curtime + 8.0f;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: A simple trace filter class to skip small moveable physics objects
//-----------------------------------------------------------------------------
class CTraceFilterSkipPhysics : public CTraceFilter
{
public:
	// It does have a base, but we'll never network anything below here..
	DECLARE_CLASS_NOBASE(CTraceFilterSkipPhysics);

	CTraceFilterSkipPhysics(const IHandleEntity* passentity, int collisionGroup, float minMass)
		: m_pPassEnt(passentity), m_collisionGroup(collisionGroup), m_minMass(minMass)
	{
	}
	virtual bool ShouldHitEntity(IHandleEntity* pHandleEntity, int contentsMask)
	{
		if (!StandardFilterRules(pHandleEntity, contentsMask))
			return false;

		if (!PassServerEntityFilter(pHandleEntity, m_pPassEnt))
			return false;

		// Don't test if the game code tells us we should ignore this collision...
		CBaseEntity* pEntity = EntityFromEntityHandle(pHandleEntity);
		if (pEntity)
		{
			if (!pEntity->ShouldCollide(m_collisionGroup, contentsMask))
				return false;

			if (!g_pGameRules->ShouldCollide(m_collisionGroup, pEntity->GetCollisionGroup()))
				return false;

			// don't test small moveable physics objects (unless it's an NPC)
			if (!pEntity->IsNPC() && pEntity->GetMoveType() == MOVETYPE_VPHYSICS)
			{
				IPhysicsObject* pPhysics = pEntity->VPhysicsGetObject();
#ifdef MAPBASE
				// A MOVETYPE_VPHYSICS object without a VPhysics object is an odd edge case, but it's evidently possible
				// since my game crashed after an antlion guard tried to see me through an EP2 jalopy.
				// Perhaps that's a sign of an underlying issue?
				if (pPhysics && pPhysics->IsMoveable() && pPhysics->GetMass() < m_minMass)
#else
				Assert(pPhysics);
				if (pPhysics->IsMoveable() && pPhysics->GetMass() < m_minMass)
#endif
					return false;
			}

			// If we hit an antlion, don't stop, but kill it
			if (pEntity->Classify() == CLASS_ANTLION)
			{
				CBaseEntity* pGuard = (CBaseEntity*)EntityFromEntityHandle(m_pPassEnt);
				ApplyChargeDamage(pGuard, pEntity, pEntity->GetHealth());
				return false;
			}
		}

		return true;
	}



private:
	const IHandleEntity* m_pPassEnt;
	int m_collisionGroup;
	float m_minMass;
};

void CNPC_CrabSynth::TraceHull_SkipPhysics(const Vector& vecAbsStart, const Vector& vecAbsEnd, const Vector& hullMin,
	const Vector& hullMax, unsigned int mask, const CBaseEntity* ignore,
	int collisionGroup, trace_t* ptr, float minMass)
{
	Ray_t ray;
	ray.Init(vecAbsStart, vecAbsEnd, hullMin, hullMax);
	CTraceFilterSkipPhysics traceFilter(ignore, collisionGroup, minMass);
	enginetrace->TraceRay(ray, mask, &traceFilter, ptr);
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CTraceFilterCharge : public CTraceFilterEntitiesOnly
{
public:
	// It does have a base, but we'll never network anything below here..
	DECLARE_CLASS_NOBASE(CTraceFilterCharge);

	CTraceFilterCharge(const IHandleEntity* passentity, int collisionGroup, CNPC_CrabSynth* pAttacker)
		: m_pPassEnt(passentity), m_collisionGroup(collisionGroup), m_pAttacker(pAttacker)
	{
	}

	virtual bool ShouldHitEntity(IHandleEntity* pHandleEntity, int contentsMask)
	{
		if (!StandardFilterRules(pHandleEntity, contentsMask))
			return false;

		if (!PassServerEntityFilter(pHandleEntity, m_pPassEnt))
			return false;

		// Don't test if the game code tells us we should ignore this collision...
		CBaseEntity* pEntity = EntityFromEntityHandle(pHandleEntity);

		if (pEntity)
		{
			if (!pEntity->ShouldCollide(m_collisionGroup, contentsMask))
				return false;

			if (!g_pGameRules->ShouldCollide(m_collisionGroup, pEntity->GetCollisionGroup()))
				return false;

			if (pEntity->m_takedamage == DAMAGE_NO)
				return false;

			// Translate the vehicle into its driver for damage
			if (pEntity->GetServerVehicle() != NULL)
			{
				CBaseEntity* pDriver = pEntity->GetServerVehicle()->GetPassenger();

				if (pDriver != NULL)
				{
					pEntity = pDriver;
				}
			}

			Vector	attackDir = pEntity->WorldSpaceCenter() - m_pAttacker->WorldSpaceCenter();
			VectorNormalize(attackDir);

			float	flDamage = (pEntity->IsPlayer()) ? sk_crabsynth_dmg_shove.GetFloat() : 250;;

			CTakeDamageInfo info(m_pAttacker, m_pAttacker, flDamage, DMG_CRUSH);
			CalculateMeleeDamageForce(&info, attackDir, info.GetAttacker()->WorldSpaceCenter(), 4.0f);

			CBaseCombatCharacter* pVictimBCC = pEntity->MyCombatCharacterPointer();

			// Only do these comparisons between NPCs
			if (pVictimBCC)
			{
				// Can only damage other NPCs that we hate
#ifdef MAPBASE
				if (m_pAttacker->IRelationType(pEntity) <= D_FR)
#else
				if (m_pAttacker->IRelationType(pEntity) == D_HT)
#endif
				{
					pEntity->TakeDamage(info);
					return true;
				}
			}
			else
			{
				// Otherwise just damage passive objects in our way
				pEntity->TakeDamage(info);
				Pickup_ForcePlayerToDropThisObject(pEntity);
			}
		}

		return false;
	}

public:
	const IHandleEntity* m_pPassEnt;
	int					m_collisionGroup;
	CNPC_CrabSynth* m_pAttacker;
};

#define	MIN_FOOTSTEP_NEAR_DIST	Square( 80*12.0f )// ft

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEvent - 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::HandleAnimEvent(animevent_t* pEvent)
{
	// Don't handle anim events after death
	if (m_NPCState == NPC_STATE_DEAD)
	{
		BaseClass::HandleAnimEvent(pEvent);
		return;
	}

	if (pEvent->event == AE_CRABSYNTH_SHOOT)
	{
		if (GetEnemy())
		{
			// Start a rapid-fire burst lasting 1.5 seconds
			m_bIsFiring = true;
			m_flGunBurstEnd = gpGlobals->curtime + 1.5f;
			m_flNextGunTime = gpGlobals->curtime;
		}
		return;
	}

	if (pEvent->event == AE_CRABSYNTH_MELEE_HIT)
	{
		CBaseEntity* pHit = CheckTraceHullAttack(
			CRABSYNTH_MELEE1_REACH,
			Vector(-32, -32, -32),
			Vector(32, 32, 32),
			sk_crabsynth_dmg_melee.GetFloat(),
			DMG_CLUB
		);

		if (pHit)
		{
			Vector forward;
			AngleVectors(GetAbsAngles(), &forward);

			pHit->ApplyAbsVelocityImpulse(forward * 300 + Vector(0, 0, 120));

			if (pHit->IsPlayer())
			{
				ToBasePlayer(pHit)->ViewPunch(QAngle(-12, RandomFloat(-5, 5), 0));
			}
		}

		return;
	}

	if (pEvent->event == AE_CRABSYNTH_CHARGE_HIT)
	{
		UTIL_ScreenShake(GetAbsOrigin(), 32.0f, 4.0f, 1.0f, 512, SHAKE_START);
		EmitSound("NPC_AntlionGuard.HitHard");

		Vector	startPos = GetAbsOrigin();
		float	checkSize = (CollisionProp()->BoundingRadius() + 8.0f);
		Vector	endPos = startPos + (BodyDirection3D() * checkSize);

		CTraceFilterCharge traceFilter(this, COLLISION_GROUP_NONE, this);

		Ray_t ray;
		ray.Init(startPos, endPos, GetHullMins(), GetHullMaxs());

		trace_t tr;
		enginetrace->TraceRay(ray, MASK_SHOT, &traceFilter, &tr);

		if (g_debug_crabsynth.GetInt() == 1)
		{
			Vector hullMaxs = GetHullMaxs();
			hullMaxs.x += checkSize;

			NDebugOverlay::BoxDirection(startPos, GetHullMins(), hullMaxs, BodyDirection2D(), 100, 255, 255, 20, 1.0f);
		}

		//NDebugOverlay::Box3D( startPos, endPos, BodyDirection2D(),
		if (m_hChargeTarget && m_hChargeTarget->IsAlive() == false)
		{
			m_hChargeTarget = NULL;
			m_hChargeTargetPosition = NULL;
		}

		// Cause a shock wave from this point which will distrupt nearby physics objects
		ImpactShock(tr.endpos, 200, 500);
		return;
	}
	BaseClass::HandleAnimEvent(pEvent);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &info - 
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::OnTakeDamage_Alive(const CTakeDamageInfo& info)
{
	CTakeDamageInfo dInfo = info;

	// Don't take damage from another antlion guard!
	if (dInfo.GetAttacker() && dInfo.GetAttacker() != this && FClassnameIs(dInfo.GetAttacker(), "npc_crabsynth"))
		return 0;

	if ((dInfo.GetDamageType() & DMG_CRUSH) && !(dInfo.GetDamageType() & DMG_VEHICLE))
	{
		// Don't take damage from physics objects that weren't thrown by the player.
		CBaseEntity* pInflictor = dInfo.GetInflictor();

		IPhysicsObject* pObj = pInflictor->VPhysicsGetObject();
		if (!pObj || !(pObj->GetGameFlags() & FVPHYSICS_WAS_THROWN))
		{
			return 0;
		}
	}

	// Hack to make antlion guard harder in HARD
	if (g_pGameRules->IsSkillLevel(SKILL_HARD) && !(info.GetDamageType() & DMG_CRUSH))
	{
		dInfo.SetDamage(dInfo.GetDamage() * 0.75);
	}

	// Cap damage taken by crushing (otherwise we can get crushed oddly)
	if ((dInfo.GetDamageType() & DMG_CRUSH) && dInfo.GetDamage() > 100)
	{
		dInfo.SetDamage(100);
	}

	int nDamageTaken = BaseClass::OnTakeDamage_Alive(dInfo);

	return nDamageTaken;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pAttacker - 
//			flDamage - 
//			&vecDir - 
//			*ptr - 
//			bitsDamageType - 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::TraceAttack(const CTakeDamageInfo& inputInfo, const Vector& vecDir, trace_t* ptr, CDmgAccumulator* pAccumulator)
{
	CTakeDamageInfo info = inputInfo;

	// Bullets are weak against us, buckshot less so
	if (info.GetDamageType() & DMG_BUCKSHOT)
	{
		info.ScaleDamage(0.5f);
	}
	else if (info.GetDamageType() & DMG_BULLET)
	{
		info.ScaleDamage(0.25f);
	}

	// Make sure we haven't rounded down to a minimal amount
	if (info.GetDamage() < 1.0f)
	{
		info.SetDamage(1.0f);
	}

	BaseClass::TraceAttack(info, vecDir, ptr, pAccumulator);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTask - 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::StartTask(const Task_t* pTask)
{
	switch (pTask->iTask)
	{
	case TASK_CRABSYNTH_CHARGE_READY:
	{
		// Stop moving and play the "ready" telegraph pose -- signals to the
		// player that a charge is coming before the lunge actually starts.
		GetMotor()->MoveStop();

		SetIdealActivity(ACT_CRABSYNTH_CHARGE_READY);

		// Record when the fixed-duration hold ends (see RunTask).
		m_CflChargeReadyEndTime = gpGlobals->curtime + sk_crabsynth_charge_ready_time.GetFloat();
	}
	break;

	case TASK_CRABSYNTH_CHARGE:
	{
		// HACK: Because the guard stops running his normal blended movement 
		//		 here, he also needs to remove his blended movement layers!
		GetMotor()->MoveStop();

		SetIdealActivity(ACT_CRABSYNTH_CHARGE_START);
	}
	break;


	case TASK_CRABSYNTH_GET_PATH_TO_CHARGE_POSITION:
	{
		if (!m_hChargeTargetPosition)
		{
			TaskFail("Tried to find a charge position without one specified.\n");
			break;
		}

		// Move to the charge position
		AI_NavGoal_t goal(GOALTYPE_LOCATION, m_hChargeTargetPosition->GetAbsOrigin(), ACT_RUN);
		if (GetNavigator()->SetGoal(goal))
		{
			// We want to face towards the charge target
			Vector vecDir = m_hChargeTarget->GetAbsOrigin() - m_hChargeTargetPosition->GetAbsOrigin();
			VectorNormalize(vecDir);
			vecDir.z = 0;
			GetNavigator()->SetArrivalDirection(vecDir);
			TaskComplete();
		}
		else
		{
			m_hChargeTarget = NULL;
			m_hChargeTargetPosition = NULL;
			TaskFail(FAIL_NO_ROUTE);
		}
	}
	break;

	case TASK_CRABSYNTH_GET_PATH_TO_NEAREST_NODE:
	{
		if (!GetEnemy())
		{
			TaskFail(FAIL_NO_ENEMY);
			break;
		}

		// Find the nearest node to the enemy
		int node = GetNavigator()->GetNetwork()->NearestNodeToPoint(this, GetEnemy()->GetAbsOrigin(), false);
		CAI_Node* pNode = GetNavigator()->GetNetwork()->GetNode(node);
		if (pNode == NULL)
		{
			TaskFail(FAIL_NO_ROUTE);
			break;
		}

		Vector vecNodePos = pNode->GetPosition(GetHullType());
		AI_NavGoal_t goal(GOALTYPE_LOCATION, vecNodePos, ACT_RUN);
		if (GetNavigator()->SetGoal(goal))
		{
			GetNavigator()->SetArrivalDirection(GetEnemy());
			TaskComplete();
			break;
		}


		TaskFail(FAIL_NO_ROUTE);
		break;
	}
	break;

	case TASK_CRABSYNTH_GET_CHASE_PATH_ENEMY_TOLERANCE:
	{
		// Chase the enemy, but allow local navigation to succeed if it gets within the goal tolerance
		GetNavigator()->SetLocalSucceedOnWithinTolerance(true);

		if (GetNavigator()->SetGoal(GOALTYPE_ENEMY))
		{
			TaskComplete();
		}
		else
		{
			RememberUnreachable(GetEnemy());
			TaskFail(FAIL_NO_ROUTE);
		}

		GetNavigator()->SetLocalSucceedOnWithinTolerance(false);
	}
	break;

	case TASK_CRABSYNTH_STAND_AND_GUN:
	{
		if (!GetEnemy())
		{
			TaskFail(FAIL_NO_ENEMY);
			break;
		}

		// Plant and begin the spin-up telegraph. NO bullets fly until the
		// wind-up finishes (handled in RunTask); firing then lasts for
		// sk_crabsynth_standgun_duration before the barrels spin back down.
		GetMotor()->MoveStop();
		m_bIsFiring = false;
		m_nStandFirePhase = CRABSYNTH_STANDFIRE_WINDUP;
		SetIdealActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_START);
	}
	break;

	case TASK_CRABSYNTH_WAIT_FOR_MOVEMENT_FACING_ENEMY:
	{
		// Same as a normal "wait for movement", but OverrideMoveFacing + the
		// facing target added in RunTask keep us strafing toward the enemy.
		ChainStartTask(TASK_WAIT_FOR_MOVEMENT, pTask->flTaskData);
	}
	break;

	case TASK_CRABSYNTH_FIND_SIDESTEP_POSITION:
	{
		if (GetEnemy() == NULL)
		{
			TaskFail(FAIL_NO_ENEMY);
			break;
		}

		Vector vecUp;
		GetVectors(NULL, NULL, &vecUp);

		// Perpendicular to the enemy direction = straight left/right of us.
		Vector vecEnemyDir = GetEnemy()->GetAbsOrigin() - GetAbsOrigin();
		Vector vecDir = CrossProduct(vecEnemyDir, vecUp);
		VectorNormalize(vecDir);

		// Dodge left or right at random.
		if (RandomInt(0, 1) == 0)
			vecDir *= -1.0f;

		// Start high and trace down so it works on uneven ground.
		Vector vecPos = GetAbsOrigin() + Vector(0, 0, 64) + RandomFloat(150, 260) * vecDir;

		trace_t tr;
		UTIL_TraceLine(vecPos, vecPos + Vector(0, 0, -128), MASK_NPCSOLID, this, COLLISION_GROUP_NONE, &tr);
		if (tr.fraction < 1.0f)
		{
			m_vSavePosition = tr.endpos;
			TaskComplete();
		}
		else
		{
			TaskFail("Couldn't find sidestep position\n");
		}
	}
	break;

	case TASK_CRABSYNTH_STANDGROUND_FIRE:
	{
		// Plant and play the spin-up telegraph. RunTask advances the phase
		// machine: WINDUP (start) -> FIRING (loop) -> WINDDOWN (end).
		GetMotor()->MoveStop();
		m_bIsFiring = false;
		m_nStandFirePhase = CRABSYNTH_STANDFIRE_WINDUP;
		SetIdealActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_START);
	}
	break;

	default:
		BaseClass::StartTask(pTask);
		break;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pTarget -
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::ChargeDamage(CBaseEntity* pTarget)
{
	if (pTarget == NULL)
		return;

	CBasePlayer* pPlayer = ToBasePlayer(pTarget);

	if (pPlayer != NULL)
	{
		//Kick the player angles
		pPlayer->ViewPunch(QAngle(20, 20, -30));

		Vector	dir = pPlayer->WorldSpaceCenter() - WorldSpaceCenter();
		VectorNormalize(dir);
		dir.z = 0.0f;

		Vector vecNewVelocity = dir * 250.0f;
		vecNewVelocity[2] += 128.0f;
		pPlayer->SetAbsVelocity(vecNewVelocity);

		color32 red = { 128,0,0,128 };
		UTIL_ScreenFade(pPlayer, red, 1.0f, 0.1f, FFADE_IN);
	}

	// Player takes less damage
	float flDamage = (pPlayer == NULL) ? 250 : sk_crabsynth_dmg_charge.GetFloat();

	// If it's being held by the player, break that bond
	Pickup_ForcePlayerToDropThisObject(pTarget);

	// Calculate the physics force
	ApplyChargeDamage(this, pTarget, flDamage);
}

//-----------------------------------------------------------------------------
// Purpose: Handles the guard charging into something. Returns true if it hit the world.
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::HandleChargeImpact(Vector vecImpact, CBaseEntity* pEntity)
{
	// Cause a shock wave from this point which will disrupt nearby physics objects
	ImpactShock(vecImpact, 128, 350);

	// Did we hit anything interesting?
	if (!pEntity || pEntity->IsWorld())
	{
		// Robin: Due to some of the finicky details in the motor, the guard will hit
		//		  the world when it is blocked by our enemy when trying to step up 
		//		  during a moveprobe. To get around this, we see if the enemy's within
		//		  a volume in front of the guard when we hit the world, and if he is,
		//		  we hit him anyway.
		EnemyIsRightInFrontOfMe(&pEntity);

		// Did we manage to find him? If not, increment our charge miss count and abort.
		if (pEntity->IsWorld())
		{
			m_CiChargeMisses++;
			return true;
		}
	}

	// Hit anything we don't like
#ifdef MAPBASE
	if (IRelationType(pEntity) <= D_FR && (GetNextAttack() < gpGlobals->curtime))
#else
	if (IRelationType(pEntity) == D_HT && (GetNextAttack() < gpGlobals->curtime))
#endif
	{
		EmitSound("NPC_AntlionGuard.Shove");

		if (!IsPlayingGesture(ACT_CRABSYNTH_CHARGE_HIT))
		{
			RestartGesture(ACT_CRABSYNTH_CHARGE_HIT);
		}

		ChargeDamage(pEntity);

		pEntity->ApplyAbsVelocityImpulse((BodyDirection2D() * 400) + Vector(0, 0, 200));

		if (!pEntity->IsAlive() && GetEnemy() == pEntity)
		{
			SetEnemy(NULL);
		}

		SetNextAttack(gpGlobals->curtime + 2.0f);
		SetActivity(ACT_CRABSYNTH_CHARGE_STOP);

		// We've hit something, so clear our miss count
		m_CiChargeMisses = 0;
		return false;
	}

	// Hit something we don't hate. If it's not moveable, crash into it.
	if (pEntity->GetMoveType() == MOVETYPE_NONE || pEntity->GetMoveType() == MOVETYPE_PUSH)
		return true;

	// If it's a vphysics object that's too heavy, crash into it too.
	if (pEntity->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		IPhysicsObject* pPhysics = pEntity->VPhysicsGetObject();
		if (pPhysics)
		{
			// If the object is being held by the player, knock it out of his hands
			if (pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
			{
				Pickup_ForcePlayerToDropThisObject(pEntity);
				return false;
			}

			if ((!pPhysics->IsMoveable() || pPhysics->GetMass() > VPhysicsGetObject()->GetMass() * 0.5f))
				return true;
		}
	}

	return false;

	/*

	ROBIN: Wrote & then removed this. If we want to have large rocks that the guard
		   should smack around, then we should enable it.

	else
	{
		// If we hit a physics prop, smack the crap out of it. (large rocks)
		// Factor the object mass into it, because we want to move it no matter how heavy it is.
		if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS )
		{
			CTakeDamageInfo info( this, this, 250, DMG_BLAST );
			info.SetDamagePosition( vecImpact );
			float flForce = ImpulseScale( pEntity->VPhysicsGetObject()->GetMass(), 250 );
			flForce *= random->RandomFloat( 0.85, 1.15 );

			// Calculate the vector and stuff it into the takedamageinfo
			Vector vecForce = BodyDirection3D();
			VectorNormalize( vecForce );
			vecForce *= flForce;
			vecForce *= phys_pushscale.GetFloat();
			info.SetDamageForce( vecForce );

			pEntity->VPhysicsTakeDamage( info );
		}
	}
	*/
}

//-----------------------------------------------------------------------------
// Purpose: While charging, look ahead and see if we're going to run into anything.
//			If we are, start the gesture so it looks like we're anticipating the hit.
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::ChargeLookAhead(void)
{
	trace_t	tr;
	Vector vecForward;
	GetVectors(&vecForward, NULL, NULL);
	Vector vecTestPos = GetAbsOrigin() + (vecForward * m_flGroundSpeed * 0.75);
	Vector testHullMins = GetHullMins();
	testHullMins.z += (StepHeight() * 2);
	TraceHull_SkipPhysics(GetAbsOrigin(), vecTestPos, testHullMins, GetHullMaxs(), MASK_SHOT_HULL, this, COLLISION_GROUP_NONE, &tr, VPhysicsGetObject()->GetMass() * 0.5);

	//NDebugOverlay::Box( tr.startpos, testHullMins, GetHullMaxs(), 0, 255, 0, true, 0.1f );
	//NDebugOverlay::Box( vecTestPos, testHullMins, GetHullMaxs(), 255, 0, 0, true, 0.1f );

	if (tr.fraction != 1.0)
	{
		// Start playing the hit animation
		AddGesture(ACT_CRABSYNTH_CHARGE_ANTICIPATION);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CNPC_CrabSynth::ChargeSteer(void)
{
	Vector forward, right;
	GetVectors(&forward, &right, NULL);

	const float testLength = m_flGroundSpeed * 0.2f;

	Vector steer = forward;

	// RIGHT PROBE
	Vector testPos = GetAbsOrigin() + (forward + right * 0.6f) * testLength;

	Vector mins = GetHullMins();
	mins.z += StepHeight() * 2;

	trace_t trRight;
	TraceHull_SkipPhysics(GetAbsOrigin(), testPos, mins, GetHullMaxs(),
		MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE,
		&trRight, VPhysicsGetObject()->GetMass() * 0.5f);

	steer += right * (1.0f - trRight.fraction);

	// LEFT PROBE
	testPos = GetAbsOrigin() + (forward - right * 0.6f) * testLength;

	trace_t trLeft;
	TraceHull_SkipPhysics(GetAbsOrigin(), testPos, mins, GetHullMaxs(),
		MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE,
		&trLeft, VPhysicsGetObject()->GetMass() * 0.5f);

	steer -= right * (1.0f - trLeft.fraction);

	// Convert to yaw difference
	float desiredYaw = UTIL_VecToYaw(steer);
	float currentYaw = UTIL_VecToYaw(forward);

	return AngleDiff(desiredYaw, currentYaw);
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pTask -
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::RunTask(const Task_t* pTask)
{
	switch (pTask->iTask)
	{
	case TASK_CRABSYNTH_CHARGE_READY:
	{
		// Keep facing the enemy while telegraphing.
		if (GetEnemy())
		{
			GetMotor()->SetIdealYawToTargetAndUpdate(GetEnemy()->GetAbsOrigin());
		}

		// Hold the ready pose for the fixed telegraph duration, then move on
		// to the actual charge.
		if (gpGlobals->curtime >= m_CflChargeReadyEndTime)
		{
			TaskComplete();
		}
	}
	break;

	case TASK_CRABSYNTH_CHARGE:
	{
		Activity eActivity = GetActivity();

		// See if we're trying to stop after hitting/missing our target
		if (eActivity == ACT_CRABSYNTH_CHARGE_STOP || eActivity == ACT_CRABSYNTH_CHARGE_CRASH)
		{
			if (IsActivityFinished())
			{
				TaskComplete();
				return;
			}

			// Still in the process of slowing down. Run movement until it's done.
			AutoMovement();
			return;
		}

		// Check for manual transition
		if ((eActivity == ACT_CRABSYNTH_CHARGE_START) && (IsActivityFinished()))
		{
			SetActivity(ACT_CRABSYNTH_CHARGE_RUN);
		}

		// See if we're still running
		if (eActivity == ACT_CRABSYNTH_CHARGE_RUN || eActivity == ACT_CRABSYNTH_CHARGE_START)
		{
			if (HasCondition(COND_NEW_ENEMY) || HasCondition(COND_LOST_ENEMY) || HasCondition(COND_ENEMY_DEAD))
			{
				SetActivity(ACT_CRABSYNTH_CHARGE_STOP);
				return;
			}
			else
			{
				if (GetEnemy() != NULL)
				{
					Vector	goalDir = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin());
					VectorNormalize(goalDir);

					if (DotProduct(BodyDirection2D(), goalDir) < 0.5f)
					{
						if (!m_CbDecidedNotToStop)
						{
							// We've missed the target. Randomly decide not to stop, which will cause
							// the guard to just try and swing around for another pass.
							m_CbDecidedNotToStop = true;
							if (RandomFloat(0, 1) > 0.3)
							{
								m_CiChargeMisses++;
								SetActivity(ACT_CRABSYNTH_CHARGE_STOP);
							}
						}
					}
					else
					{
						m_CbDecidedNotToStop = false;
					}
				}
			}
		}

		// Steer towards our target
		float idealYaw;
		if (GetEnemy() == NULL)
		{
			idealYaw = GetMotor()->GetIdealYaw();
		}
		else
		{
			idealYaw = CalcIdealYaw(GetEnemy()->GetAbsOrigin());
		}

		// Add in our steering offset
		idealYaw += ChargeSteer();

		// Turn to face
		GetMotor()->SetIdealYawAndUpdate(idealYaw);

		// See if we're going to run into anything soon
		ChargeLookAhead();

		// Let our animations simply move us forward. Keep the result
		// of the movement so we know whether we've hit our target.
		AIMoveTrace_t moveTrace;

		Vector vChargeDebugBefore = GetAbsOrigin();
		bool bChargeMoved = AutoMovement(GetEnemy(), &moveTrace);

		if (g_debug_crabsynth.GetInt() == 2)
		{
			float flActualMove = (GetAbsOrigin() - vChargeDebugBefore).Length();
			Msg("[crabsynth] charge: activity=%d  AutoMovement=%s  movedThisFrame=%.2f  traceDist=%.2f\n",
				(int)eActivity, bChargeMoved ? "TRUE" : "FALSE", flActualMove, moveTrace.flTotalDist);
		}

		if (bChargeMoved == false)
		{
			// Only stop if we hit the world
			if (HandleChargeImpact(moveTrace.vEndPosition, moveTrace.pObstruction))
			{
				// If we're starting up, this is an error
				if (eActivity == ACT_CRABSYNTH_CHARGE_START)
				{
					TaskFail("Unable to make initial movement of charge\n");
					return;
				}

				// Crash unless we're trying to stop already
				if (eActivity != ACT_CRABSYNTH_CHARGE_STOP)
				{
					if (moveTrace.fStatus == AIMR_BLOCKED_WORLD && moveTrace.vHitNormal == vec3_origin)
					{
						SetActivity(ACT_CRABSYNTH_CHARGE_STOP);
					}
					else
					{
						SetActivity(ACT_CRABSYNTH_CHARGE_CRASH);
					}
				}
			}
			else if (moveTrace.pObstruction)
			{
				// If we hit an antlion, don't stop, but kill it
				if (moveTrace.pObstruction->Classify() == CLASS_ANTLION)
				{
					if (FClassnameIs(moveTrace.pObstruction, "npc_crabsynth"))
					{
						// Crash unless we're trying to stop already
						if (eActivity != ACT_CRABSYNTH_CHARGE_STOP)
						{
							SetActivity(ACT_CRABSYNTH_CHARGE_STOP);
						}
					}
					else
					{
						ApplyChargeDamage(this, moveTrace.pObstruction, moveTrace.pObstruction->GetHealth());
					}
				}
			}
		}
	}
	break;

	case TASK_CRABSYNTH_STAND_AND_GUN:
	{
		if (!GetEnemy())
		{
			m_bIsFiring = false;
			TaskComplete();
			return;
		}

		// Keep the minigun trained on the enemy while we hold ground.
		GetMotor()->SetIdealYawToTargetAndUpdate(GetEnemy()->GetAbsOrigin());

		switch (m_nStandFirePhase)
		{
		case CRABSYNTH_STANDFIRE_WINDUP:
			// Hold the spin-up telegraph. Not a single bullet until it finishes.
			if (GetActivity() == ACT_SYNTH_STANDGROUND_RANGEATTACK_START && IsActivityFinished())
			{
				m_nStandFirePhase = CRABSYNTH_STANDFIRE_FIRING;
				SetActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_FIRE);
				m_bIsFiring = true;
				m_flNextGunTime = gpGlobals->curtime;
				m_flStandFireUntil = gpGlobals->curtime + sk_crabsynth_standgun_duration.GetFloat();
			}
			break;

		case CRABSYNTH_STANDFIRE_FIRING:
			// Sustain fire (only while we have a shot). NPCThink() spits the
			// bullets as long as m_bIsFiring is set and the window is open.
			m_bIsFiring = HasCondition(COND_SEE_ENEMY);
			if (m_bIsFiring)
				m_flGunBurstEnd = gpGlobals->curtime + 0.5f;

			// Re-latch the loop if the fire01_loop $sequence isn't flagged "loop".
			if (IsActivityFinished())
			{
				SetCycle(0.0f);
				ResetSequenceInfo();
			}

			// Fired long enough -> spin the barrels down before finishing.
			if (gpGlobals->curtime >= m_flStandFireUntil)
			{
				m_bIsFiring = false;
				m_nStandFirePhase = CRABSYNTH_STANDFIRE_WINDDOWN;
				SetActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_END);
			}
			break;

		case CRABSYNTH_STANDFIRE_WINDDOWN:
			// Let the spin-down play out, THEN hand control back so
			// SelectCombatSchedule() can re-check the range band.
			m_bIsFiring = false;
			if (IsActivityFinished())
				TaskComplete();
			break;
		}
	}
	break;

	case TASK_CRABSYNTH_STANDGROUND_FIRE:
	{
		if (!GetEnemy())
		{
			m_bIsFiring = false;
			TaskComplete();
			return;
		}

		// Always keep the minigun trained on the target.
		GetMotor()->SetIdealYawToTargetAndUpdate(GetEnemy()->GetAbsOrigin());

		switch (m_nStandFirePhase)
		{
		case CRABSYNTH_STANDFIRE_WINDUP:
			// Hold the spin-up telegraph until it finishes -- no firing yet.
			if (GetActivity() == ACT_SYNTH_STANDGROUND_RANGEATTACK_START && IsActivityFinished())
			{
				m_nStandFirePhase = CRABSYNTH_STANDFIRE_FIRING;
				SetActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_FIRE);
				m_flNextGunTime = gpGlobals->curtime;
			}
			break;

		case CRABSYNTH_STANDFIRE_FIRING:
			// Sustain fire while we can see him. NPCThink() does the shooting as
			// long as m_bIsFiring is set and the window is open.
			m_bIsFiring = HasCondition(COND_SEE_ENEMY);
			if (m_bIsFiring)
				m_flGunBurstEnd = gpGlobals->curtime + 0.5f;

			// Re-latch the loop if fire01_loop isn't flagged "loop" in the QC.
			if (IsActivityFinished())
			{
				SetCycle(0.0f);
				ResetSequenceInfo();
			}

			// Enemy is reachable again (came down off the ledge, we got a path
			// around, or the unreachable memory timed out) -> spin the barrels
			// down before doing anything else; the transition after the spin-down
			// is decided on completion (chase / walk-gun / charge on reselect).
			if (!IsEnemyUnreachable())
			{
				m_bIsFiring = false;
				m_nStandFirePhase = CRABSYNTH_STANDFIRE_WINDDOWN;
				SetActivity(ACT_SYNTH_STANDGROUND_RANGEATTACK_END);
			}
			break;

		case CRABSYNTH_STANDFIRE_WINDDOWN:
			// Let the spin-down finish, then complete. Reselect routes to a
			// chase if he's close, or walk-and-gun if he's farther out.
			m_bIsFiring = false;
			if (IsActivityFinished())
				TaskComplete();
			break;
		}
	}
	break;

	case TASK_CRABSYNTH_WAIT_FOR_MOVEMENT_FACING_ENEMY:
	{
		// Keep pulling our facing toward the enemy for the whole move so the
		// directional walk/run blends stay engaged (side/back strafing).
		if (GetEnemy())
		{
			AddFacingTarget(GetEnemy(), GetEnemyLKP(), 1.0f, 0.8f);
		}
		ChainRunTask(TASK_WAIT_FOR_MOVEMENT, pTask->flTaskData);
	}
	break;

	case TASK_WAIT_FOR_MOVEMENT:
	{
		BaseClass::RunTask(pTask);

	}
	break;

	default:
		BaseClass::RunTask(pTask);
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Return true if our charge target is right in front of the guard
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::EnemyIsRightInFrontOfMe(CBaseEntity** pEntity)
{
	if (!GetEnemy())
		return false;

	if ((GetEnemy()->WorldSpaceCenter() - WorldSpaceCenter()).LengthSqr() < (156 * 156))
	{
		Vector vecLOS = (GetEnemy()->GetAbsOrigin() - GetAbsOrigin());
		vecLOS.z = 0;
		VectorNormalize(vecLOS);
		Vector vBodyDir = BodyDirection2D();
		if (DotProduct(vecLOS, vBodyDir) > 0.8)
		{
			// He's in front of me, and close. Make sure he's not behind a wall.
			trace_t tr;
			UTIL_TraceLine(WorldSpaceCenter(), GetEnemy()->EyePosition(), MASK_SOLID, this, COLLISION_GROUP_NONE, &tr);
			if (tr.m_pEnt == GetEnemy())
			{
				*pEntity = tr.m_pEnt;
				return true;
			}
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &origin - 
//			radius - 
//			magnitude - 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::ImpactShock(const Vector& origin, float radius, float magnitude, CBaseEntity* pIgnored)
{
	// Also do a local physics explosion to push objects away
	float	adjustedDamage, flDist;
	Vector	vecSpot;
	float	falloff = 1.0f / 2.5f;

	CBaseEntity* pEntity = NULL;

	// Find anything within our radius
	while ((pEntity = gEntList.FindEntityInSphere(pEntity, origin, radius)) != NULL)
	{
		// Don't affect the ignored target
		if (pEntity == pIgnored)
			continue;
		if (pEntity == this)
			continue;

		// UNDONE: Ask the object if it should get force if it's not MOVETYPE_VPHYSICS?
		if (pEntity->GetMoveType() == MOVETYPE_VPHYSICS || (pEntity->VPhysicsGetObject() && pEntity->IsPlayer() == false))
		{
			vecSpot = pEntity->BodyTarget(GetAbsOrigin());

			// decrease damage for an ent that's farther from the bomb.
			flDist = (GetAbsOrigin() - vecSpot).Length();

			if (radius == 0 || flDist <= radius)
			{
				adjustedDamage = flDist * falloff;
				adjustedDamage = magnitude - adjustedDamage;

				if (adjustedDamage < 1)
				{
					adjustedDamage = 1;
				}

				CTakeDamageInfo info(this, this, adjustedDamage, DMG_BLAST);
				CalculateExplosiveDamageForce(&info, (vecSpot - GetAbsOrigin()), GetAbsOrigin());

				pEntity->VPhysicsTakeDamage(info);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scheduleType - 
// Output : int
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::TranslateSchedule(int scheduleType)
{
	switch (scheduleType)
	{
	case SCHED_CHASE_ENEMY:
		return SCHED_CRABSYNTH_CHASE_ENEMY;
		break;
	}

	return BaseClass::TranslateSchedule(scheduleType);
}

//-----------------------------------------------------------------------------
// Purpose: While moving with an enemy nearby, keep the body trained on him.
//			This is the Hunter's trick: the navigator moves the feet along the
//			path while the move_yaw pose parameter blends in the correct
//			directional cycle, so he side- and back-strafes (using the
//			walk_blended / run_blended sequences) instead of always running
//			face-first.
//-----------------------------------------------------------------------------
bool CNPC_CrabSynth::OverrideMoveFacing(const AILocalMoveGoal_t& move, float flInterval)
{
	// Don't fight the charge steering or the melee lock.
	if (IsCurSchedule(SCHED_CRABSYNTH_CHARGE) ||
		IsCurSchedule(SCHED_CRABSYNTH_CHARGE_TARGET) ||
		GetActivity() == ACT_MELEE_ATTACK1)
	{
		return BaseClass::OverrideMoveFacing(move, flInterval);
	}

	bool bSideStepping = IsCurSchedule(SCHED_CRABSYNTH_SIDESTEP);

	Activity moveActivity = GetNavigator()->GetMovementActivity();

	if (GetEnemy() &&
		(bSideStepping || moveActivity == ACT_RUN || moveActivity == ACT_WALK))
	{
		Vector vecEnemyLKP = GetEnemyLKP();

		// Face the enemy while sidestepping, or whenever he's close enough that
		// strafing reads better than just charging straight at him.
		if (bSideStepping ||
			UTIL_DistApprox(vecEnemyLKP, GetAbsOrigin()) < sk_crabsynth_face_enemy_dist.GetFloat())
		{
			AddFacingTarget(GetEnemy(), vecEnemyLKP, 1.0f, 0.2f);
		}
	}

	return BaseClass::OverrideMoveFacing(move, flInterval);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : baseAct - 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CNPC_CrabSynth::NPC_TranslateActivity(Activity baseAct)
{
#ifdef MAPBASE
	// Needed for VScript NPC_TranslateActiviy hook
	baseAct = BaseClass::NPC_TranslateActivity(baseAct);
#endif

	//See which run to use
	if ((baseAct == ACT_RUN) && IsCurSchedule(SCHED_CRABSYNTH_CHARGE))
		return (Activity)ACT_CRABSYNTH_CHARGE_RUN;

	// While advancing on a distant enemy, use the gundown walk cycle so the
	// baked-in minigun shoot events fire as he closes the distance.
	if ((baseAct == ACT_WALK) && IsCurSchedule(SCHED_CRABSYNTH_RANGE_WALK))
		return (Activity)ACT_CRABSYNTH_WALK_FIRE;

	if ((baseAct == ACT_RUN) && (m_iHealth <= (m_iMaxHealth / 4)))
		return (Activity)ACT_CRABSYNTH_RUN_HURT;

	return baseAct;
}

//-----------------------------------------------------------------------------
//
// Schedules
//
//-----------------------------------------------------------------------------

AI_BEGIN_CUSTOM_NPC(npc_crabsynth, CNPC_CrabSynth)


//Tasks
DECLARE_TASK(TASK_CRABSYNTH_CHARGE)
DECLARE_TASK(TASK_CRABSYNTH_CHARGE_READY)
DECLARE_TASK(TASK_CRABSYNTH_GET_PATH_TO_CHARGE_POSITION)
DECLARE_TASK(TASK_CRABSYNTH_GET_PATH_TO_NEAREST_NODE)
DECLARE_TASK(TASK_CRABSYNTH_GET_CHASE_PATH_ENEMY_TOLERANCE)
DECLARE_TASK(TASK_CRABSYNTH_STAND_AND_GUN)
DECLARE_TASK(TASK_CRABSYNTH_WAIT_FOR_MOVEMENT_FACING_ENEMY)
DECLARE_TASK(TASK_CRABSYNTH_FIND_SIDESTEP_POSITION)
DECLARE_TASK(TASK_CRABSYNTH_STANDGROUND_FIRE)


//Activities
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_START)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_READY)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_RUN)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_STOP)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_CRASH)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_ANTICIPATION)
DECLARE_ACTIVITY(ACT_CRABSYNTH_RUN_HURT)
DECLARE_ACTIVITY(ACT_CRABSYNTH_WALK_FIRE)
DECLARE_ACTIVITY(ACT_SYNTH_STANDGROUND_RANGEATTACK_START)
DECLARE_ACTIVITY(ACT_SYNTH_STANDGROUND_RANGEATTACK_FIRE)
DECLARE_ACTIVITY(ACT_SYNTH_STANDGROUND_RANGEATTACK_END)


//Adrian: events go here
DECLARE_ANIMEVENT(AE_CRABSYNTH_CHARGE_HIT)
DECLARE_ANIMEVENT(AE_CRABSYNTH_CHARGE_START)
DECLARE_ANIMEVENT(AE_CRABSYNTH_MELEE_HIT)
DECLARE_ANIMEVENT(AE_CRABSYNTH_SHOOT)

DECLARE_CONDITION(COND_CRABSYNTH_HAS_CHARGE_TARGET)
DECLARE_CONDITION(COND_CRABSYNTH_CAN_CHARGE)
DECLARE_CONDITION(COND_CRABSYNTH_CAN_RANGE_WALK)
DECLARE_CONDITION(COND_CRABSYNTH_CAN_STAND_GUN)
DECLARE_CONDITION(COND_CRABSYNTH_ENEMY_ABOVE_UNREACHABLE)

//==================================================
// SCHED_ANTLIONGUARD_CHARGE
//==================================================

DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_CHARGE,

	"	Tasks"
	"		TASK_STOP_MOVING					0"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_CRABSYNTH_CHASE_ENEMY"
	"		TASK_FACE_ENEMY						0"
	"		TASK_CRABSYNTH_CHARGE_READY		0"
	"		TASK_CRABSYNTH_CHARGE			0"
	""
	"	Interrupts"
	"		COND_TASK_FAILED"
	"		COND_HEAVY_DAMAGE"

	// These are deliberately left out so they can be detected during the 
	// charge Task and correctly play the charge stop animation.
	//"		COND_NEW_ENEMY"
	//"		COND_ENEMY_DEAD"
	//"		COND_LOST_ENEMY"
)

//==================================================
// SCHED_ANTLIONGUARD_CHARGE_TARGET
//==================================================

DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_CHARGE_TARGET,

	"	Tasks"
	"		TASK_STOP_MOVING					0"
	"		TASK_FACE_ENEMY						0"
	"		TASK_CRABSYNTH_CHARGE_READY		0"
	"		TASK_CRABSYNTH_CHARGE			0"
	""
	"	Interrupts"
	"		COND_TASK_FAILED"
	"		COND_ENEMY_DEAD"
	"		COND_HEAVY_DAMAGE"
)

//=========================================================
// SCHED_ANTLIONGUARD_CHASE_ENEMY
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_CHASE_ENEMY,

	"	Tasks"
	// Path failure here (e.g. the big hull can't finish the route the small
	// player walked) drops to firing in place rather than a dead stall.
	"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_CRABSYNTH_STAND_AND_GUN"
	"		TASK_STOP_MOVING				0"
	// Tolerance tightened to melee reach so he actually closes the final gap
	// instead of stopping short and re-deciding.
	"		TASK_GET_CHASE_PATH_TO_ENEMY	64"
	"		TASK_RUN_PATH					0"
	"		TASK_WAIT_FOR_MOVEMENT			0"
	"		TASK_FACE_ENEMY			0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_ENEMY_UNREACHABLE"
	// Break the instant he's in reach so the close-range melee actually lands.
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_CAN_MELEE_ATTACK2"
	"		COND_TOO_CLOSE_TO_ATTACK"
	"		COND_TASK_FAILED"
	"		COND_LOST_ENEMY"
	"		COND_HEAVY_DAMAGE"
	// If the enemy retreats out of the close band, re-decide (stand-gun /
	// walk-gun / charge). These conditions are only set beyond melee range, so
	// they won't thrash the chase while he's genuinely closing in.
	"		COND_CRABSYNTH_CAN_STAND_GUN"
	"		COND_CRABSYNTH_CAN_RANGE_WALK"
	"		COND_CRABSYNTH_CAN_CHARGE"
)

//=========================================================
// SCHED_CRABSYNTH_RANGE_WALK
//
// Advance on a distant enemy on foot while firing the minigun.
// The firing itself comes from the AE_CRABSYNTH_SHOOT events baked into the
// "walk_blended_gundown" sequence (ACT_CRABSYNTH_WALK_FIRE), which ACT_WALK
// is translated to for the duration of this schedule.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_RANGE_WALK,

	"	Tasks"
	// If we can't path even at medium range (big hull vs. sparse nav), don't
	// stall in the default 1-second SCHED_FAIL and don't wander -- just plant and
	// fire. The deployed minigun needs only line of sight, so he keeps fighting.
	"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_CRABSYNTH_STAND_AND_GUN"
	"		TASK_STOP_MOVING				0"
	"		TASK_GET_CHASE_PATH_TO_ENEMY	300"
	"		TASK_WALK_PATH					0"
	"		TASK_WAIT_FOR_MOVEMENT			0"
	"		TASK_FACE_ENEMY					0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_ENEMY_UNREACHABLE"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_CRABSYNTH_CAN_CHARGE"
	// Break to a stationary minigun burst once he's closed into medium-close range.
	"		COND_CRABSYNTH_CAN_STAND_GUN"
	"		COND_TOO_CLOSE_TO_ATTACK"
	"		COND_TASK_FAILED"
	"		COND_LOST_ENEMY"
	"		COND_HEAVY_DAMAGE"
)

//=========================================================
// SCHED_CRABSYNTH_STAND_AND_GUN
//
// Medium-close range behavior: stop, face the enemy, and fire the minigun in
// place for one burst (sk_crabsynth_standgun_duration). When the burst ends the
// task completes, forcing a fresh range-band decision.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_STAND_AND_GUN,

	"	Tasks"
	"		TASK_STOP_MOVING				0"
	"		TASK_FACE_ENEMY					0"
	"		TASK_CRABSYNTH_STAND_AND_GUN	0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_LOST_ENEMY"
	// Enemy rushed into melee reach -> drop the gun and swing.
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_TOO_CLOSE_TO_ATTACK"
	"		COND_HEAVY_DAMAGE"
	"		COND_TASK_FAILED"
)

//=========================================================
// SCHED_CRABSYNTH_CHANGE_POSITION
//
// Hunter-style "make busy": wander to a nearby spot while keeping the enemy in
// view (so the side/back walk-run blends play), then settle and re-face. Breaks
// out the instant an attack becomes available.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_CHANGE_POSITION,

	"	Tasks"
	"		TASK_STOP_MOVING									0"
	"		TASK_WANDER											720432"	// 6ft..36ft (min 72, max 432 units)
	"		TASK_RUN_PATH										0"
	"		TASK_CRABSYNTH_WAIT_FOR_MOVEMENT_FACING_ENEMY		0"
	"		TASK_STOP_MOVING									0"
	"		TASK_SET_SCHEDULE									SCHEDULE:SCHED_CRABSYNTH_CHANGE_POSITION_FINISH"
	""
	"	Interrupts"
	"		COND_ENEMY_DEAD"
	"		COND_NEW_ENEMY"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_CRABSYNTH_CAN_CHARGE"
	"		COND_HEAVY_DAMAGE"
	"		COND_TASK_FAILED"
)

//=========================================================
// SCHED_CRABSYNTH_CHANGE_POSITION_FINISH
//
// Settle after repositioning: face the enemy briefly, then re-decide. Attack
// conditions interrupt immediately so he snaps back into the fight.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_CHANGE_POSITION_FINISH,

	"	Tasks"
	"		TASK_FACE_ENEMY					0"
	"		TASK_WAIT_FACE_ENEMY_RANDOM		1"
	""
	"	Interrupts"
	"		COND_ENEMY_DEAD"
	"		COND_NEW_ENEMY"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_CRABSYNTH_CAN_CHARGE"
	"		COND_CRABSYNTH_CAN_STAND_GUN"
	"		COND_CRABSYNTH_CAN_RANGE_WALK"
	"		COND_HEAVY_DAMAGE"
)

//=========================================================
// SCHED_CRABSYNTH_SIDESTEP
//
// Quick lateral dodge (used after heavy damage). Runs to a point directly
// left/right, facing the enemy the whole way via OverrideMoveFacing.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_SIDESTEP,

	"	Tasks"
	"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_CRABSYNTH_CHANGE_POSITION"
	"		TASK_STOP_MOVING					0"
	"		TASK_CRABSYNTH_FIND_SIDESTEP_POSITION	0"
	"		TASK_GET_PATH_TO_SAVEPOSITION		0"
	"		TASK_RUN_PATH						0"
	"		TASK_WAIT_FOR_MOVEMENT				0"
	"		TASK_FACE_ENEMY						0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_CAN_MELEE_ATTACK1"
)

//=========================================================
// SCHED_CRABSYNTH_STANDGROUND_FIRE
//
// Enemy is above us and unreachable (e.g. up on a ledge). Face him, telegraph
// with the spin-up, then pour on sustained minigun fire. The task itself drives
// the START -> FIRE -> END animation state machine and decides when to bail:
//   * enemy drops down close   -> spin down (END), then chase (via reselect)
//   * enemy drops down far      -> hand off to walk-and-gun
// So most transitions are handled in-task rather than by interrupts here.
//=========================================================
DEFINE_SCHEDULE
(
	SCHED_CRABSYNTH_STANDGROUND_FIRE,

	"	Tasks"
	"		TASK_STOP_MOVING				0"
	"		TASK_FACE_ENEMY					0"
	"		TASK_CRABSYNTH_STANDGROUND_FIRE	0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_LOST_ENEMY"
	"		COND_CAN_MELEE_ATTACK1"
	"		COND_TASK_FAILED"
)

AI_END_CUSTOM_NPC()