#ifndef NPC_CRABSYNTH_H
#define NPC_CRABSYNTH_H

#include "ai_basenpc.h"
#include "ai_blended_movement.h"

#define	CRABSYNTH_MELEE1_CONE		0.7f
#define	CRABSYNTH_MELEE1_REACH		100.0f		// max distance to allow a melee attack -- tune to match the model's actual swing reach

#define	CRABSYNTH_MODEL	"models/synth.mdl"
#define	CRABSYNTH_FOV_NORMAL			-0.4f

#define	CRABSYNTH_CHARGE_MIN			256
#define CRABSYNTH_CHARGE_MAX			2048
#define CRABSYNTH_CHARGE_MAX_HEIGHT_DIFF	64.0f

ConVar	sk_crabsynth_health("sk_crabsynth_health", "500");
ConVar	sk_crabsynth_dmg_shove("sk_crabsynth_dmg_shove", "0");
ConVar	sk_crabsynth_dmg_charge("sk_crabsynth_dmg_charge", "0");
ConVar	sk_crabsynth_dmg_melee("sk_crabsynth_dmg_melee", "0");

ConVar	g_debug_crabsynth("g_debug_crabsynth", "0");

ConVar	sk_crabsynth_charge_ready_time("sk_crabsynth_charge_ready_time", "0.");	// seconds to hold the "ready" telegraph pose before charging

// Enemy must be at least this far away (and visible) for the CrabSynth to
// advance on foot while firing its minigun instead of charging/meleeing.
// NOTE: kept for compatibility; the new range-band logic below drives behavior.
ConVar	sk_crabsynth_minigun_range("sk_crabsynth_minigun_range", "512");

// ---------------------------------------------------------------------------
// Range bands (closest -> farthest). Three thresholds carve distance into four
// behavior zones:
//     dist <  melee_range                       -> chase in and melee
//     melee_range   <= dist < standgun_range    -> stand ground + minigun
//     standgun_range <= dist < charge_range     -> walk-and-gun (advance firing)
//     dist >= charge_range                      -> charge the enemy
// (If a charge isn't viable at long range -- blocked lane, big height diff,
//  cooldown -- he falls back to walk-and-gun to close the gap.)
// ---------------------------------------------------------------------------
ConVar	sk_crabsynth_melee_range("sk_crabsynth_melee_range", "400");	// below this: chase in for melee
ConVar	sk_crabsynth_standgun_range("sk_crabsynth_standgun_range", "800");	// below this (and >= melee): stand and gun
ConVar	sk_crabsynth_charge_range("sk_crabsynth_charge_range", "1400");	// at/above this: charge
ConVar	sk_crabsynth_standgun_duration("sk_crabsynth_standgun_duration", "2.0");	// length of one stand-and-gun burst before re-deciding

// ---------------------------------------------------------------------------
// Hunter-style mobility.
// ---------------------------------------------------------------------------
// When the enemy is within this range, keep the body pointed at him while
// moving. This is what makes the directional walk/run blends (side- and
// back-strafes) actually play instead of always running face-first.
ConVar	sk_crabsynth_face_enemy_dist("sk_crabsynth_face_enemy_dist", "1200");
// Chance (0-100) that, instead of planting to stand-and-gun at medium-close
// range, he strafes to a fresh position first -- keeps him dancing like a Hunter.
ConVar	sk_crabsynth_reposition_pct("sk_crabsynth_reposition_pct", "40");

// If the enemy is at least this far ABOVE the crab and can't be pathed to
// (e.g. perched on a ledge), the crab plants and lays down suppressing fire
// with the telegraphed stand-ground minigun attack instead of milling around.
ConVar	sk_crabsynth_above_height("sk_crabsynth_above_height", "64");

//==================================================
// AntlionGuardSchedules
//==================================================

enum
{
	SCHED_CRABSYNTH_CHARGE = LAST_SHARED_SCHEDULE,
	SCHED_CRABSYNTH_CHARGE_TARGET,
	SCHED_CRABSYNTH_CHASE_ENEMY,
	SCHED_CRABSYNTH_RANGE_WALK,		// advance on a distant enemy while firing the minigun
	SCHED_CRABSYNTH_STAND_AND_GUN,	// hold position and fire the minigun at a medium-close enemy
	SCHED_CRABSYNTH_CHANGE_POSITION,		// Hunter-style: strafe to a new spot while facing the enemy
	SCHED_CRABSYNTH_CHANGE_POSITION_FINISH,	// settle and re-face after repositioning
	SCHED_CRABSYNTH_SIDESTEP,		// quick lateral dodge (e.g. after taking heavy damage)
	SCHED_CRABSYNTH_STANDGROUND_FIRE	// enemy is above & unreachable -> telegraph + sustained stationary minigun
};

//==================================================
// AntlionGuardConditions
//==================================================

enum
{
	COND_CRABSYNTH_CAN_CHARGE = LAST_SHARED_CONDITION,
	COND_CRABSYNTH_HAS_CHARGE_TARGET,
	COND_CRABSYNTH_CAN_RANGE_WALK,	// enemy is far away and visible -> walk-and-gun
	COND_CRABSYNTH_CAN_STAND_GUN,	// enemy is at medium-close range and visible -> stand and gun
	COND_CRABSYNTH_ENEMY_ABOVE_UNREACHABLE	// enemy is above us and can't be pathed to -> stationary suppressing fire
};

int AE_CRABSYNTH_CHARGE_HIT;
int AE_CRABSYNTH_CHARGE_START;
int AE_CRABSYNTH_MELEE_HIT;
int AE_CRABSYNTH_SHOOT;

enum
{
	TASK_CRABSYNTH_CHARGE = LAST_SHARED_TASK,
	TASK_CRABSYNTH_CHARGE_READY,
	TASK_CRABSYNTH_GET_PATH_TO_CHARGE_POSITION,
	TASK_CRABSYNTH_GET_PATH_TO_NEAREST_NODE,
	TASK_CRABSYNTH_GET_CHASE_PATH_ENEMY_TOLERANCE,
	TASK_CRABSYNTH_STAND_AND_GUN,
	TASK_CRABSYNTH_WAIT_FOR_MOVEMENT_FACING_ENEMY,	// wait out movement while keeping the enemy in our sights (drives the strafe blends)
	TASK_CRABSYNTH_FIND_SIDESTEP_POSITION,			// pick a point directly left/right of us to dodge to
	TASK_CRABSYNTH_STANDGROUND_FIRE					// telegraph (start) -> sustained fire (loop) -> spin-down (end) state machine
};

Activity ACT_CRABSYNTH_CHARGE_START;
Activity ACT_CRABSYNTH_CHARGE_RUN;
Activity ACT_CRABSYNTH_CHARGE_STOP;
Activity ACT_CRABSYNTH_CHARGE_CRASH;
Activity ACT_CRABSYNTH_CHARGE_HIT;
Activity ACT_CRABSYNTH_CHARGE_ANTICIPATION;
Activity ACT_CRABSYNTH_CHARGE_READY;
Activity ACT_CRABSYNTH_RUN_HURT;
Activity ACT_CRABSYNTH_WALK_FIRE;	// walk-toward-enemy cycle with minigun shoot events baked in

// Stationary "stand-ground" minigun attack. Names MUST match the activity
// strings bound in the model QC (fire01_start / fire01_loop / fire01_end).
Activity ACT_SYNTH_STANDGROUND_RANGEATTACK_START;	// spin-up telegraph
Activity ACT_SYNTH_STANDGROUND_RANGEATTACK_FIRE;	// sustained fire loop
Activity ACT_SYNTH_STANDGROUND_RANGEATTACK_END;		// spin-down

// Phases of a stationary minigun attack: wind up the barrels, fire, wind down.
// Tracked explicitly so the spin-up/spin-down always play and firing never
// starts before the wind-up animation has finished.
enum
{
	CRABSYNTH_STANDFIRE_WINDUP = 0,
	CRABSYNTH_STANDFIRE_FIRING,
	CRABSYNTH_STANDFIRE_WINDDOWN
};

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

	// Keep facing the enemy while moving so the directional walk/run blends play.
	virtual bool OverrideMoveFacing(const AILocalMoveGoal_t& move, float flInterval);

	bool	ShouldCharge(const Vector& startPos, const Vector& endPos, bool useTime, bool bCheckForCancel);

	// True if the enemy is meaningfully above us and can't be pathed to.
	bool	IsEnemyAboveAndUnreachable(void);

	// True if the enemy simply can't be pathed to right now (any direction --
	// ledge, gap, blocked lane). Drives "deploy the minigun and suppress"
	// instead of spamming failed GetPathToEnemy while trying to walk in.
	bool	IsEnemyUnreachable(void);

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

	virtual void NPCThink(void); // Used to process burst fire

	float	ChargeSteer(void);
	void	ChargeLookAhead(void);

	bool	EnemyIsRightInFrontOfMe(CBaseEntity** pEntity);

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
	float		m_CflChargeReadyEndTime;	// when the "ready" telegraph pose stops holding and the charge begins
	int			m_miniGunAmmo;

	bool		m_bIsFiring;
	float		m_flNextGunTime;
	float		m_flGunBurstEnd;

	int			m_nStandFirePhase;	// CRABSYNTH_STANDFIRE_* -- current phase of a stationary minigun attack
	float		m_flStandFireUntil;	// when the FIRING phase should stop and wind down (stand-and-gun)

	// Hunter-style mobility state. NOTE: m_vSavePosition (the dodge destination
	// read by TASK_GET_PATH_TO_SAVEPOSITION) is inherited from CAI_BaseNPC --
	// don't redeclare it here or it would shadow the base member.
	float		m_flNextSideStepTime;	// cooldown so damage doesn't make him sidestep every frame

	bool		m_bWasInGreenZone;		// Tracks if the player has entered the 800-1400 range band
};

#endif // NPC_CRABSYNTH_H