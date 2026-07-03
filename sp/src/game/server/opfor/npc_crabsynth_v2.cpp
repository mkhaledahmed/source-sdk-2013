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
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::Precache(void)
{
	PrecacheModel(DefaultOrCustomModel(CRABSYNTH_MODEL));;

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
	Vector absMin = -Vector(100, 100, 0);
	Vector absMax = Vector(100, 100, 128);

	CollisionProp()->SetSurroundingBoundsType(USE_SPECIFIED_BOUNDS, &absMin, &absMax);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::GatherConditions(void)
{
	BaseClass::GatherConditions();

	// See if we can charge the target
	if (GetEnemy())
	{
		if (ShouldCharge(GetAbsOrigin(), GetEnemy()->GetAbsOrigin(), true, false))
		{
			SetCondition(COND_CRABSYNTH_CAN_CHARGE);
		}
		else
		{
			ClearCondition(COND_CRABSYNTH_CAN_CHARGE);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CNPC_CrabSynth::SelectCombatSchedule(void)
{
	ClearHintGroup();

	// Attack if we can
	if (HasCondition(COND_CAN_MELEE_ATTACK1))
		return SCHED_MELEE_ATTACK1;

	// Charging
	if (HasCondition(COND_CRABSYNTH_CAN_CHARGE))
	{
		return SCHED_CRABSYNTH_CHARGE;
	}

	return BaseClass::SelectSchedule();
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

		if (m_hChargeTarget->IsAlive() == false)
		{
			m_hChargeTarget = NULL;
			m_hChargeTargetPosition = NULL;
			SetEnemy(m_hOldTarget);

			if (m_hOldTarget == NULL)
			{
				m_NPCState = NPC_STATE_ALERT;
			}
		}
		else
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
	Activity eActivity = GetActivity();

	// Stay still
	if ((eActivity == ACT_MELEE_ATTACK1))
		return 0.0f;

	CBaseEntity* pEnemy = GetEnemy();

	if (pEnemy != NULL && pEnemy->IsPlayer() == false)
		return 16.0f;

	// Turn slowly when you're charging
	if (eActivity == ACT_CRABSYNTH_CHARGE_START)
		return 4.0f;

	// Turn more slowly as we close in on our target
	if (eActivity == ACT_CRABSYNTH_CHARGE_RUN)
	{
		if (pEnemy == NULL)
			return 2.0f;

		float dist = UTIL_DistApprox2D(GetEnemy()->WorldSpaceCenter(), WorldSpaceCenter());

		if (dist > 512)
			return 16.0f;

		//FIXME: Alter by skill level
		float yawSpeed = RemapVal(dist, 0, 512, 1.0f, 2.0f);
		yawSpeed = clamp(yawSpeed, 1.0f, 2.0f);

		return yawSpeed;
	}

	if (eActivity == ACT_CRABSYNTH_CHARGE_STOP)
		return 8.0f;

	switch (eActivity)
	{
	case ACT_TURN_LEFT:
	case ACT_TURN_RIGHT:
		return 40.0f;
		break;

	case ACT_RUN:
	default:
		return 20.0f;
		break;
	}
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
		m_CflChargeTime = gpGlobals->curtime + 4.0f;
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
	trace_t	tr;
	Vector	testPos, steer, forward, right;
	QAngle	angles;
	const float	testLength = m_flGroundSpeed * 0.15f;

	//Get our facing
	GetVectors(&forward, &right, NULL);

	steer = forward;

	const float faceYaw = UTIL_VecToYaw(forward);

	//Offset right
	VectorAngles(forward, angles);
	angles[YAW] += 45.0f;
	AngleVectors(angles, &forward);

	//Probe out
	testPos = GetAbsOrigin() + (forward * testLength);

	//Offset by step height
	Vector testHullMins = GetHullMins();
	testHullMins.z += (StepHeight() * 2);

	//Probe
	TraceHull_SkipPhysics(GetAbsOrigin(), testPos, testHullMins, GetHullMaxs(), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr, VPhysicsGetObject()->GetMass() * 0.5f);

	//Debug info
	if (g_debug_crabsynth.GetInt() == 1)
	{
		if (tr.fraction == 1.0f)
		{
			NDebugOverlay::BoxDirection(GetAbsOrigin(), testHullMins, GetHullMaxs() + Vector(testLength, 0, 0), forward, 0, 255, 0, 8, 0.1f);
		}
		else
		{
			NDebugOverlay::BoxDirection(GetAbsOrigin(), testHullMins, GetHullMaxs() + Vector(testLength, 0, 0), forward, 255, 0, 0, 8, 0.1f);
		}
	}

	//Add in this component
	steer += (right * 0.5f) * (1.0f - tr.fraction);

	//Offset left
	angles[YAW] -= 90.0f;
	AngleVectors(angles, &forward);

	//Probe out
	testPos = GetAbsOrigin() + (forward * testLength);

	// Probe
	TraceHull_SkipPhysics(GetAbsOrigin(), testPos, testHullMins, GetHullMaxs(), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr, VPhysicsGetObject()->GetMass() * 0.5f);

	//Debug
	if (g_debug_crabsynth.GetInt() == 1)
	{
		if (tr.fraction == 1.0f)
		{
			NDebugOverlay::BoxDirection(GetAbsOrigin(), testHullMins, GetHullMaxs() + Vector(testLength, 0, 0), forward, 0, 255, 0, 8, 0.1f);
		}
		else
		{
			NDebugOverlay::BoxDirection(GetAbsOrigin(), testHullMins, GetHullMaxs() + Vector(testLength, 0, 0), forward, 255, 0, 0, 8, 0.1f);
		}
	}

	//Add in this component
	steer -= (right * 0.5f) * (1.0f - tr.fraction);

	//Debug
	if (g_debug_crabsynth.GetInt() == 1)
	{
		NDebugOverlay::Line(GetAbsOrigin(), GetAbsOrigin() + (steer * 512.0f), 255, 255, 0, true, 0.1f);
		NDebugOverlay::Cross3D(GetAbsOrigin() + (steer * 512.0f), Vector(2, 2, 2), -Vector(2, 2, 2), 255, 255, 0, true, 0.1f);

		NDebugOverlay::Line(GetAbsOrigin(), GetAbsOrigin() + (BodyDirection3D() * 256.0f), 255, 0, 255, true, 0.1f);
		NDebugOverlay::Cross3D(GetAbsOrigin() + (BodyDirection3D() * 256.0f), Vector(2, 2, 2), -Vector(2, 2, 2), 255, 0, 255, true, 0.1f);
	}

	return UTIL_AngleDiff(UTIL_VecToYaw(steer), faceYaw);
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  : *pTask -
//-----------------------------------------------------------------------------
void CNPC_CrabSynth::RunTask(const Task_t* pTask)
{
	switch (pTask->iTask)
	{
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

					if (DotProduct(BodyDirection2D(), goalDir) < 0.25f)
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
		if (AutoMovement(GetEnemy(), &moveTrace) == false)
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
// Purpose: 
// Input  : baseAct - 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CNPC_CrabSynth::NPC_TranslateActivity(Activity baseAct)
{
#ifdef MAPBASE
	baseAct = BaseClass::NPC_TranslateActivity(baseAct);
#endif

	if ((baseAct == ACT_RUN) && IsCurSchedule(SCHED_CRABSYNTH_CHARGE))
		return (Activity)ACT_CRABSYNTH_CHARGE_RUN;

	if ((baseAct == ACT_RUN) && (m_iHealth <= (m_iMaxHealth / 4)))
		return (Activity)ACT_CRABSYNTH_RUN_HURT;

	// Stopgap: no distinct run animation exists on this model yet --
	// reuse the walk cycle so the NPC actually moves during chase
	// schedules, instead of requesting an activity with zero matching
	// sequences. Remove this once a real ACT_RUN sequence is authored.
	if (baseAct == ACT_RUN)
		return ACT_WALK;

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
DECLARE_TASK(TASK_CRABSYNTH_GET_PATH_TO_CHARGE_POSITION)
DECLARE_TASK(TASK_CRABSYNTH_GET_PATH_TO_NEAREST_NODE)
DECLARE_TASK(TASK_CRABSYNTH_GET_CHASE_PATH_ENEMY_TOLERANCE)


//Activities
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_START)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_RUN)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_STOP)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_CRASH)
DECLARE_ACTIVITY(ACT_CRABSYNTH_CHARGE_ANTICIPATION)
DECLARE_ACTIVITY(ACT_CRABSYNTH_RUN_HURT)


//Adrian: events go here
DECLARE_ANIMEVENT(AE_CRABSYNTH_CHARGE_HIT)
DECLARE_ANIMEVENT(AE_CRABSYNTH_CHARGE_START)

DECLARE_CONDITION(COND_CRABSYNTH_HAS_CHARGE_TARGET)
DECLARE_CONDITION(COND_CRABSYNTH_CAN_CHARGE)

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
	"		TASK_STOP_MOVING				0"
	"		TASK_GET_CHASE_PATH_TO_ENEMY	300"
	"		TASK_RUN_PATH					0"
	"		TASK_WAIT_FOR_MOVEMENT			0"
	"		TASK_FACE_ENEMY			0"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_ENEMY_DEAD"
	"		COND_ENEMY_UNREACHABLE"
	"		COND_CAN_RANGE_ATTACK1"
	// "		COND_CAN_MELEE_ATTACK1"
	"		COND_CAN_RANGE_ATTACK2"
	"		COND_CAN_MELEE_ATTACK2"
	"		COND_TOO_CLOSE_TO_ATTACK"
	"		COND_TASK_FAILED"
	"		COND_LOST_ENEMY"
	"		COND_HEAVY_DAMAGE"
	"		COND_CRABSYNTH_CAN_CHARGE"
)

AI_END_CUSTOM_NPC()
