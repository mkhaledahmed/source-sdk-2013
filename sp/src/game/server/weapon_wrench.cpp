#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "gamerules.h"
#include "ammodef.h"
#include "mathlib/mathlib.h"
#include "in_buttons.h"
#include "soundent.h"
#include "basebludgeonweapon.h"
#include "vstdlib/random.h"
#include "npcevent.h"
#include "ai_basenpc.h"
#include "weapon_wrench.h"
#include "fixable_entity.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar    sk_plr_dmg_wrench("sk_plr_dmg_wrench", "0");
ConVar    sk_npc_dmg_wrench("sk_npc_dmg_wrench", "0");

// Charge attack (mouse2, hold-and-release)
ConVar    sk_wrench_charge_rate("sk_wrench_charge_rate", "25.0");			// bonus damage per second held
ConVar    sk_wrench_charge_damage_max("sk_wrench_charge_damage_max", "50.0");	// cap on bonus damage
ConVar    sk_wrench_charge_min_time("sk_wrench_charge_min_time", "0.2");		// below this, release counts as a whiff

// NEW: Shake effect ConVars
ConVar    sk_wrench_shake_speed("sk_wrench_shake_speed", "50.0");			// oscillation speed of the max-charge screen shake -- fast
ConVar    sk_wrench_shake_amplitude("sk_wrench_shake_amplitude", "0.3");	// degrees -- tiny

ConVar    wrench_debug("wrench_debug", "0", FCVAR_NONE, "1 = print charge/fixup debug info to console");
ConVar    sk_wrench_fixup_range("sk_wrench_fixup_range", "100.0");	// how far the mouse3 fixup trace reaches

//-----------------------------------------------------------------------------
// CWeaponWrench
//-----------------------------------------------------------------------------

IMPLEMENT_SERVERCLASS_ST(CWeaponWrench, DT_WeaponWrench)
END_SEND_TABLE()

#ifndef HL2MP
LINK_ENTITY_TO_CLASS(weapon_wrench, CWeaponWrench);
PRECACHE_WEAPON_REGISTER(weapon_wrench);
#endif

acttable_t CWeaponWrench::m_acttable[] =
{
	{ ACT_MELEE_ATTACK1,	ACT_MELEE_ATTACK_SWING, true },
	{ ACT_IDLE,				ACT_IDLE_ANGRY_MELEE,	false },
	{ ACT_IDLE_ANGRY,		ACT_IDLE_ANGRY_MELEE,	false },
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_RUN,				ACT_RUN_MELEE,			false },
	{ ACT_WALK,				ACT_WALK_MELEE,			false },

	{ ACT_ARM,				ACT_ARM_MELEE,			false },
	{ ACT_DISARM,			ACT_DISARM_MELEE,		false },
#endif

#ifdef MAPBASE
	// HL2:DM activities (for third-person animations in SP)
	{ ACT_RANGE_ATTACK1,                ACT_RANGE_ATTACK_SLAM, true },
	{ ACT_HL2MP_IDLE,                    ACT_HL2MP_IDLE_MELEE,                    false },
	{ ACT_HL2MP_RUN,                    ACT_HL2MP_RUN_MELEE,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,            ACT_HL2MP_IDLE_CROUCH_MELEE,            false },
	{ ACT_HL2MP_WALK_CROUCH,            ACT_HL2MP_WALK_CROUCH_MELEE,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,    ACT_HL2MP_GESTURE_RANGE_ATTACK_MELEE,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,            ACT_HL2MP_GESTURE_RELOAD_MELEE,            false },
	{ ACT_HL2MP_JUMP,                    ACT_HL2MP_JUMP_MELEE,                    false },
#if EXPANDED_HL2DM_ACTIVITIES
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK2,	ACT_HL2MP_GESTURE_RANGE_ATTACK2_MELEE,		false },
	{ ACT_HL2MP_WALK,					ACT_HL2MP_WALK_MELEE,						false },
#endif
#endif
};

IMPLEMENT_ACTTABLE(CWeaponWrench);

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponWrench::CWeaponWrench(void)
{
	m_bChargingAttack = false;
	m_flChargeStartTime = 0.0f;
	m_flAccumulatedChargeDamage = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Get the damage amount for the animation we're doing
// Input  : hitActivity - currently played activity
// Output : Damage amount
//-----------------------------------------------------------------------------
float CWeaponWrench::GetDamageForActivity(Activity hitActivity)
{
	if ((GetOwner() != NULL) && (GetOwner()->IsPlayer()))
		return sk_plr_dmg_wrench.GetFloat();

	return sk_npc_dmg_wrench.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: Add in a view kick for this weapon
//-----------------------------------------------------------------------------
void CWeaponWrench::AddViewKick(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());

	if (pPlayer == NULL)
		return;

	QAngle punchAng;

	punchAng.x = random->RandomFloat(1.0f, 2.0f);
	punchAng.y = random->RandomFloat(-2.0f, -1.0f);
	punchAng.z = 0.0f;

	pPlayer->ViewPunch(punchAng);
}

//-----------------------------------------------------------------------------
// Attempt to lead the target (needed because citizens can't hit manhacks with the crowbar!)
//-----------------------------------------------------------------------------
ConVar sk_wrench_lead_time("sk_wrench_lead_time", "0.9");

int CWeaponWrench::WeaponMeleeAttack1Condition(float flDot, float flDist)
{
	// Attempt to lead the target (needed because citizens can't hit manhacks with the crowbar!)
	CAI_BaseNPC* pNPC = GetOwner()->MyNPCPointer();
	CBaseEntity* pEnemy = pNPC->GetEnemy();
	if (!pEnemy)
		return COND_NONE;

	Vector vecVelocity;
	vecVelocity = pEnemy->GetSmoothedVelocity();

	// Project where the enemy will be in a little while
	float dt = sk_wrench_lead_time.GetFloat();
	dt += random->RandomFloat(-0.3f, 0.2f);
	if (dt < 0.0f)
		dt = 0.0f;

	Vector vecExtrapolatedPos;
	VectorMA(pEnemy->WorldSpaceCenter(), dt, vecVelocity, vecExtrapolatedPos);

	Vector vecDelta;
	VectorSubtract(vecExtrapolatedPos, pNPC->WorldSpaceCenter(), vecDelta);

	if (fabs(vecDelta.z) > 70)
	{
		return COND_TOO_FAR_TO_ATTACK;
	}

	Vector vecForward = pNPC->BodyDirection2D();
	vecDelta.z = 0.0f;
	float flExtrapolatedDist = Vector2DNormalize(vecDelta.AsVector2D());
	if ((flDist > 64) && (flExtrapolatedDist > 64))
	{
		return COND_TOO_FAR_TO_ATTACK;
	}

	float flExtrapolatedDot = DotProduct2D(vecDelta.AsVector2D(), vecForward.AsVector2D());
	if ((flDot < 0.7) && (flExtrapolatedDot < 0.7))
	{
		return COND_NOT_FACING_ATTACK;
	}

	return COND_CAN_MELEE_ATTACK1;
}

//-----------------------------------------------------------------------------
// Animation event handlers
//-----------------------------------------------------------------------------
void CWeaponWrench::HandleAnimEventMeleeHit(animevent_t* pEvent, CBaseCombatCharacter* pOperator)
{
	// Trace up or down based on where the enemy is...
	// But only if we're basically facing that direction
	Vector vecDirection;
	AngleVectors(GetAbsAngles(), &vecDirection);

	CBaseEntity* pEnemy = pOperator->MyNPCPointer() ? pOperator->MyNPCPointer()->GetEnemy() : NULL;
	if (pEnemy)
	{
		Vector vecDelta;
		VectorSubtract(pEnemy->WorldSpaceCenter(), pOperator->Weapon_ShootPosition(), vecDelta);
		VectorNormalize(vecDelta);

		Vector2D vecDelta2D = vecDelta.AsVector2D();
		Vector2DNormalize(vecDelta2D);
		if (DotProduct2D(vecDelta2D, vecDirection.AsVector2D()) > 0.8f)
		{
			vecDirection = vecDelta;
		}
	}

	Vector vecEnd;
	VectorMA(pOperator->Weapon_ShootPosition(), 50, vecDirection, vecEnd);
	CBaseEntity* pHurt = pOperator->CheckTraceHullAttack(pOperator->Weapon_ShootPosition(), vecEnd,
		Vector(-16, -16, -16), Vector(36, 36, 36), sk_npc_dmg_wrench.GetFloat(), DMG_CLUB, 0.75);

	// did I hit someone?
	if (pHurt)
	{
		// play sound
		WeaponSound(MELEE_HIT);

		// Fake a trace impact, so the effects work out like a player's crowbaw
		trace_t traceHit;
		UTIL_TraceLine(pOperator->Weapon_ShootPosition(), pHurt->GetAbsOrigin(), MASK_SHOT_HULL, pOperator, COLLISION_GROUP_NONE, &traceHit);
		ImpactEffect(traceHit);
	}
	else
	{
		WeaponSound(MELEE_MISS);
	}
}

//-----------------------------------------------------------------------------
// Animation event
//-----------------------------------------------------------------------------
void CWeaponWrench::Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator)
{
	switch (pEvent->event)
	{
	case EVENT_WEAPON_MELEE_HIT:
		HandleAnimEventMeleeHit(pEvent, pOperator);
		break;

	default:
		BaseClass::Operator_HandleAnimEvent(pEvent, pOperator);
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drives the mouse2 charge attack and mouse3 fixup stand-in.
//
// NOTE: mouse3 is not bound to anything by default in Source -- whoever
// plays this needs "bind mouse3 +attack3" (or an equivalent default bind
// added client-side) for IN_ATTACK3 to ever actually fire.
//-----------------------------------------------------------------------------
void CWeaponWrench::ItemPostFrame(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
	{
		BaseClass::ItemPostFrame();
		return;
	}

	// ---- mouse3: fixup stand-in (fires once per press, not held) ----
	if (pOwner->m_afButtonPressed & IN_ATTACK3)
	{
		if (wrench_debug.GetBool())
		{
			Msg("[wrench] mouse3 fixup triggered (no accumulated value -- single-press action)\n");
		}
		FixupAttack();
	}

	// ---- mouse2: hold to charge, release to swing ----
	bool bAttack2Held = (pOwner->m_nButtons & IN_ATTACK2) != 0;

	if (bAttack2Held && !m_bChargingAttack)
	{
		StartChargeAttack();
	}
	else if (!bAttack2Held && m_bChargingAttack)
	{
		ReleaseChargeAttack();
	}
	else if (bAttack2Held && m_bChargingAttack)
	{
		float flChargeTime = gpGlobals->curtime - m_flChargeStartTime;
		m_flAccumulatedChargeDamage = MIN(flChargeTime * sk_wrench_charge_rate.GetFloat(),
			sk_wrench_charge_damage_max.GetFloat());

		// NEW: Apply screen shake when at maximum charge
		if (m_flAccumulatedChargeDamage >= sk_wrench_charge_damage_max.GetFloat())
		{
			float flPhase = flChargeTime * sk_wrench_shake_speed.GetFloat();
			float flRoll = sk_wrench_shake_amplitude.GetFloat() * sin(flPhase);
			pOwner->ViewPunch(QAngle(0, 0, flRoll));

			if (wrench_debug.GetBool())
			{
				Msg("[wrench] MAX CHARGE -- shake roll %.3f\n", flRoll);
			}
		}
		else if (wrench_debug.GetBool())
		{
			Msg("[wrench] charging: held %.2fs, accumulated bonus damage = %.1f / %.1f\n",
				flChargeTime, m_flAccumulatedChargeDamage, sk_wrench_charge_damage_max.GetFloat());
		}
	}

	// While charging, don't let the base class also process a normal
	// primary swing this frame -- holding mouse2 should just hold the
	// wind-up pose until release, not also let mouse1 interrupt it.
	if (m_bChargingAttack)
	{
		WeaponIdle();
		return;
	}

	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose: Begin the mouse2 hold -- play the wind-up/held-back animation
// and start the charge timer.
//-----------------------------------------------------------------------------
void CWeaponWrench::StartChargeAttack(void)
{
	m_bChargingAttack = true;
	m_flChargeStartTime = gpGlobals->curtime;
	m_flAccumulatedChargeDamage = 0.0f;

	SendWeaponAnim(ACT_VM_PULLBACK);

	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (pOwner)
	{
		pOwner->SetAnimation(PLAYER_ATTACK1);
	}
}

//-----------------------------------------------------------------------------
// Purpose: mouse2 released -- swing with the accumulated bonus damage from
// however long it was held, or whiff if released too quickly to count.
//-----------------------------------------------------------------------------
void CWeaponWrench::ReleaseChargeAttack(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());

	float flChargeTime = gpGlobals->curtime - m_flChargeStartTime;
	float flBonusDamage = m_flAccumulatedChargeDamage;

	m_bChargingAttack = false;
	m_flAccumulatedChargeDamage = 0.0f;

	if (!pOwner)
		return;

	if (flChargeTime < sk_wrench_charge_min_time.GetFloat())
	{
		if (wrench_debug.GetBool())
		{
			Msg("[wrench] released after %.2fs -- below min charge time (%.2fs), whiff\n",
				flChargeTime, sk_wrench_charge_min_time.GetFloat());
		}

		// released too fast to count as a real charged swing
		WeaponSound(MELEE_MISS);
		SendWeaponAnim(ACT_VM_MISSCENTER);
		return;
	}

	if (wrench_debug.GetBool())
	{
		Msg("[wrench] released after %.2fs -- final bonus damage = %.1f\n", flChargeTime, flBonusDamage);
	}

	Vector vecSrc = pOwner->Weapon_ShootPosition();
	Vector vecAiming;
	pOwner->EyeVectors(&vecAiming);

	Vector vecEnd;
	VectorMA(vecSrc, 50, vecAiming, vecEnd);

	CBaseEntity* pHurt = pOwner->CheckTraceHullAttack(vecSrc, vecEnd, Vector(-16, -16, -16), Vector(36, 36, 36),
		sk_plr_dmg_wrench.GetFloat() + flBonusDamage, DMG_CLUB, 0.75);

	if (pHurt)
	{
		WeaponSound(MELEE_HIT);
		SendWeaponAnim(ACT_VM_HITCENTER);

		trace_t traceHit;
		UTIL_TraceLine(vecSrc, pHurt->GetAbsOrigin(), MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit);
		ImpactEffect(traceHit);

		AddViewKick();
	}
	else
	{
		WeaponSound(MELEE_MISS);
		SendWeaponAnim(ACT_VM_MISSCENTER);
	}
}

//-----------------------------------------------------------------------------
// Purpose: mouse3 -- traces forward and, if the player is looking directly
// at a fixable_entity within range, turns it on. Still plays the
// draw animation as the visual for the interaction; the actual gameplay
// effect now happens via the trace below instead of being a pure stand-in.
//-----------------------------------------------------------------------------
void CWeaponWrench::FixupAttack(void)
{
	SendWeaponAnim(ACT_VM_DRAW);

	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
		return;

	Vector vecSrc = pOwner->Weapon_ShootPosition();
	Vector vecAiming;
	pOwner->EyeVectors(&vecAiming);

	Vector vecEnd;
	VectorMA(vecSrc, sk_wrench_fixup_range.GetFloat(), vecAiming, vecEnd);

	trace_t tr;
	UTIL_TraceLine(vecSrc, vecEnd, MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &tr);

	if (wrench_debug.GetBool())
	{
		Msg("[wrench] fixup trace hit: %s\n", tr.m_pEnt ? tr.m_pEnt->GetClassname() : "(nothing)");
	}

	if (!tr.m_pEnt)
		return;

	CFixableEntity* pFixable = dynamic_cast<CFixableEntity*>(tr.m_pEnt);
	if (!pFixable)
		return;	// hit something, but not a fixable_entity -- no interaction

	bool bIsOnNow = pFixable->ToggleFixed(pOwner);

	if (wrench_debug.GetBool())
	{
		Msg("[wrench] fixable_entity toggled %s\n", bIsOnNow ? "ON" : "OFF");
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs every frame while charging. Handles the max-charge screen
// shake -- interpreted as a continuous sine-wave roll oscillation between
// +amplitude and -amplitude degrees, applied every frame via ViewPunch() until release.
//-----------------------------------------------------------------------------
void CWeaponWrench::UpdateChargeEffects(CBasePlayer* pOwner)
{
	float flChargeTime = gpGlobals->curtime - m_flChargeStartTime;

	if (flChargeTime >= sk_wrench_charge_min_time.GetFloat())
	{
		float flPhase = flChargeTime * sk_wrench_shake_speed.GetFloat();
		float flRoll = sk_wrench_shake_amplitude.GetFloat() * sin(flPhase);

		pOwner->ViewPunch(QAngle(0, 0, flRoll));

		if (wrench_debug.GetBool())
		{
			Msg("[wrench] MAX CHARGE -- shake roll %.3f\n", flRoll);
		}
	}
	else if (wrench_debug.GetBool())
	{
		Msg("[wrench] charging: held %.2fs / %.2fs\n", flChargeTime, sk_wrench_charge_min_time.GetFloat());
	}
}