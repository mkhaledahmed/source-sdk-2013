#ifndef NPC_CRABSYNTH_H
#define NPC_CRABSYNTH_H

#include "ai_basenpc.h"
#include "ai_blended_movement.h"

#define	CRABSYNTH_MELEE1_CONE		0.7f

#define	CRABSYNTH_MODEL	"models/synth.mdl"
#define	CRABSYNTH_FOV_NORMAL			-0.4f

#define	CRABSYNTH_CHARGE_MIN			256
#define CRABSYNTH_CHARGE_MAX			2048
#define CRABSYNTH_CHARGE_MAX_HEIGHT_DIFF	64.0f

ConVar	sk_crabsynth_health("sk_crabsynth_health", "500");
ConVar	sk_crabsynth_dmg_shove("sk_crabsynth_dmg_shove", "0");
ConVar	sk_crabsynth_dmg_charge("sk_crabsynth_dmg_charge", "0");

ConVar	g_debug_crabsynth("g_debug_crabsynth", "0");

//==================================================
// AntlionGuardSchedules
//==================================================

enum
{
	SCHED_CRABSYNTH_CHARGE = LAST_SHARED_SCHEDULE,
	SCHED_CRABSYNTH_CHARGE_TARGET,
	SCHED_CRABSYNTH_CHASE_ENEMY
};

//==================================================
// AntlionGuardConditions
//==================================================

enum
{
	COND_CRABSYNTH_CAN_CHARGE = LAST_SHARED_CONDITION,
	COND_CRABSYNTH_HAS_CHARGE_TARGET
};

int AE_CRABSYNTH_CHARGE_HIT;
int AE_CRABSYNTH_CHARGE_START;

enum
{
	TASK_CRABSYNTH_CHARGE = LAST_SHARED_TASK,
	TASK_CRABSYNTH_GET_PATH_TO_CHARGE_POSITION,
	TASK_CRABSYNTH_GET_PATH_TO_NEAREST_NODE,
	TASK_CRABSYNTH_GET_CHASE_PATH_ENEMY_TOLERANCE

};

Activity ACT_CRABSYNTH_CHARGE_START;
Activity ACT_CRABSYNTH_CHARGE_RUN;
Activity ACT_CRABSYNTH_CHARGE_STOP;
Activity ACT_CRABSYNTH_CHARGE_CRASH;
Activity ACT_CRABSYNTH_CHARGE_HIT;
Activity ACT_CRABSYNTH_CHARGE_ANTICIPATION;
Activity ACT_CRABSYNTH_RUN_HURT;

class CNPC_CrabSynth : public CAI_BlendedNPC

{

	DECLARE_CLASS(CNPC_CrabSynth, CAI_BlendedNPC);
	DECLARE_DATADESC();

public:

	Class_T	Classify(void) { return CLASS_CRAB_SYNTH; }

	CNPC_CrabSynth(void);

	const impactdamagetable_t& GetPhysicsImpactDamageTable(void);

	void Spawn(void);
	void Precache(void);
	void	ImpactShock(const Vector& origin, float radius, float magnitude, CBaseEntity* pIgnored = NULL);

	virtual int OnTakeDamage_Alive(const CTakeDamageInfo& info);

	virtual float MaxYawSpeed(void);

	virtual void GatherConditions(void);
	virtual int	SelectSchedule(void);
	virtual int TranslateSchedule(int scheduleType);
	virtual int SelectCombatSchedule(void);

	virtual Activity NPC_TranslateActivity(Activity baseAct);

	bool	ShouldCharge(const Vector& startPos, const Vector& endPos, bool useTime, bool bCheckForCancel);

	virtual void StartTask(const Task_t* pTask);
	virtual void RunTask(const Task_t* pTask);

	bool	HandleChargeImpact(Vector vecImpact, CBaseEntity* pEntity);
	void	ChargeDamage(CBaseEntity* pTarget);


	virtual int MeleeAttack1Conditions(float flDot, float flDist);
	virtual void	TraceAttack(const CTakeDamageInfo& info, const Vector& vecDir, trace_t* ptr, CDmgAccumulator* pAccumulator);

	EHANDLE			m_hChargeTarget;
	EHANDLE			m_hChargeTargetPosition;
	EHANDLE			m_hOldTarget;

	virtual void HandleAnimEvent(animevent_t* pEvent);

	virtual void UpdateOnRemove(void);

	float	ChargeSteer(void);
	void	ChargeLookAhead(void);

	bool	EnemyIsRightInFrontOfMe(CBaseEntity** pEntity);

	//void PainSound(void);
	//void AlertSound(void);
	//void IdleSound(void);
	//void AttackSound(void);

	void TraceHull_SkipPhysics(const Vector& vecAbsStart, const Vector& vecAbsEnd, const Vector& hullMin,
		const Vector& hullMax, unsigned int mask, const CBaseEntity* ignore,
		int collisionGroup, trace_t* ptr, float minMass);

	DEFINE_CUSTOM_AI;

private:

	bool		m_CiChargeMisses;
	bool		m_CbDecidedNotToStop;
	float		m_CnextCharge;
	float		m_CflNextRangeAttackTime;
	float		m_CflNextMelee2AttackTime;
	float		m_CflNextRoarTime;
	float		m_CflChargeTime;
};

#endif // NPC_CRABSYNTH_H

