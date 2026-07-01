//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Knife -- fast melee weapon with a backstab insta-kill, and a
// secondary charge-and-throw attack.
//
//=============================================================================//

#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "ammodef.h"
#include "mathlib/mathlib.h"
#include "in_buttons.h"
#include "soundent.h"
#include "vstdlib/random.h"
#include "npcevent.h"
#include "weapon_knife.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Tuning ConVars
//-----------------------------------------------------------------------------
ConVar sk_knife_dmg_primary("sk_knife_dmg_primary", "20");
ConVar sk_knife_dmg_throw("sk_knife_dmg_throw", "100");			// base thrown damage before falloff, matching the crossbow's flat player damage
ConVar sk_knife_refire("sk_knife_refire", "0.3");				// faster swing rate / shorter pause than the wrench/crowbar
ConVar sk_knife_range("sk_knife_range", "48");

ConVar sk_knife_backstab_dot("sk_knife_backstab_dot", "0.5");	// how far into the target's "back cone" you need to be

ConVar sk_knife_charge_max_time("sk_knife_charge_max_time", "1.5");		// time held to reach max throw power
ConVar sk_knife_inaccurate_time("sk_knife_inaccurate_time", "1.0");		// holding past this starts adding random deviation
ConVar sk_knife_inaccuracy_per_sec("sk_knife_inaccuracy_per_sec", "15.0");	// degrees of deviation per second held past the above
ConVar sk_knife_throw_speed_min("sk_knife_throw_speed_min", "600");
ConVar sk_knife_throw_speed_max("sk_knife_throw_speed_max", "2200");
ConVar sk_knife_damage_falloff_scale("sk_knife_damage_falloff_scale", "250");	// distance scale for the 1/sqrt() falloff

ConVar sk_knife_shake_speed("sk_knife_shake_speed", "50.0");			// oscillation speed of the max-charge screen shake -- fast
ConVar sk_knife_shake_amplitude("sk_knife_shake_amplitude", "0.3");	// degrees -- tiny

ConVar sk_knife_recovery_delay("sk_knife_recovery_delay", "1.0");	// seconds after being thrown before it can be picked back up
ConVar sk_knife_recovery_radius("sk_knife_recovery_radius", "48");	// how close the owner needs to be to recover it

ConVar sk_knife_spin_rate("sk_knife_spin_rate", "720");	// degrees/sec while flying -- purely cosmetic, doesn't affect trajectory

ConVar knife_debug("knife_debug", "0", FCVAR_NONE, "1 = print knife charge/backstab/throw debug info to console");

//=============================================================================
// CThrownKnife
//=============================================================================

LINK_ENTITY_TO_CLASS(thrown_knife, CThrownKnife);

BEGIN_DATADESC(CThrownKnife)
DEFINE_FIELD(m_vecThrowOrigin, FIELD_POSITION_VECTOR),
DEFINE_FIELD(m_flThrowTime, FIELD_TIME),
DEFINE_FIELD(m_bStuck, FIELD_BOOLEAN),
DEFINE_FIELD(m_hOriginalOwner, FIELD_EHANDLE),
DEFINE_FIELD(m_hSourceWeapon, FIELD_EHANDLE),

DEFINE_ENTITYFUNC(KnifeTouch),
DEFINE_THINKFUNC(KnifeThink),
END_DATADESC()

// Networked so the client-side C_ThrownKnife (c_thrown_knife.cpp) can tell
// whether the local player owns this one, and only outline it for them.
// Same overall pattern grenade_hopwire.cpp uses for its explosion effect
// needing a client counterpart, and weapon_crossbow.cpp's CCrossbowBolt
// (DT_CrossbowBolt) for its own (empty) send table.
IMPLEMENT_SERVERCLASS_ST(CThrownKnife, DT_ThrownKnife)
SendPropEHandle(SENDINFO(m_hOriginalOwner)),
END_SEND_TABLE()

void CThrownKnife::Precache(void)
{
	// TODO: point this at a real knife world model.
	PrecacheModel("models/weapons/w_crowbar.mdl");

	// TODO: needs real soundscript entries.
	PrecacheScriptSound("Weapon_Knife.ThrowImpact");
	PrecacheScriptSound("Weapon_Knife.ThrowHitWorld");
	PrecacheScriptSound("Weapon_Knife.ThrowHitBody");
}

void CThrownKnife::Spawn(void)
{
	Precache();

	SetModel("models/weapons/w_crowbar.mdl");
	SetSolid(SOLID_BBOX);
	SetSolidFlags(FSOLID_NOT_STANDABLE);
	UTIL_SetSize(this, -Vector(2, 2, 2), Vector(2, 2, 2));

	// MOVECOLLIDE_FLY_CUSTOM (not FLY_BOUNCE) matches CCrossbowBolt --
	// we're handling all the collision response ourselves in KnifeTouch()
	// (reflect on a glancing hit, stick otherwise) rather than letting the
	// engine auto-bounce it.
	SetMoveType(MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM);
	SetTouch(&CThrownKnife::KnifeTouch);

	m_vecThrowOrigin = GetAbsOrigin();
	m_flThrowTime = gpGlobals->curtime;
	m_bStuck = false;

	AddEffects(EF_NOSHADOW);

	// Purely cosmetic tumble/spin -- angular velocity is handled entirely
	// separately from linear velocity by the engine, so this doesn't
	// affect the trajectory at all. Spinning around a single local axis
	// (pitch here) gives a clean tumbling-blade look rather than a wobble.
	SetLocalAngularVelocity(QAngle(sk_knife_spin_rate.GetFloat(), 0, 0));

	SetThink(&CThrownKnife::KnifeThink);
	SetNextThink(gpGlobals->curtime + 0.1f);
}

//-----------------------------------------------------------------------------
// Purpose: Same override CCrossbowBolt uses -- lets it hit hitboxes
// properly and pass through grates.
//-----------------------------------------------------------------------------
unsigned int CThrownKnife::PhysicsSolidMaskForEntity(void) const
{
	return (BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX) & ~CONTENTS_GRATE;
}

//-----------------------------------------------------------------------------
// Purpose: Keeps the model oriented along its flight path while airborne,
// and handles owner-proximity recovery.
//
// Recovery is done here via proximity rather than Touch() -- SetOwnerEntity()
// below makes the engine exclude the owner from colliding with their own
// thrown knife at all (standard behavior, same reason grenades/rockets
// don't collide with whoever fired them). Without that exclusion, the
// knife would spawn wedged directly inside the throwing player (since it
// spawns at their eye/weapon position) and physically block their movement
// until it was removed. With the exclusion in place, though, a physical
// Touch() from the owner will never fire, so recovery needs its own check.
//-----------------------------------------------------------------------------
void CThrownKnife::KnifeThink(void)
{
	CBasePlayer* pOwner = ToBasePlayer(m_hOriginalOwner.Get());
	if (pOwner)
	{
		float flTimeSinceThrown = gpGlobals->curtime - m_flThrowTime;

		if (flTimeSinceThrown >= sk_knife_recovery_delay.GetFloat())
		{
			float flDist = (pOwner->GetAbsOrigin() - GetAbsOrigin()).Length();

			if (flDist <= sk_knife_recovery_radius.GetFloat())
			{
				if (knife_debug.GetBool())
				{
					Msg("[knife] recovered by original owner (proximity, %.0f units away)\n", flDist);
				}

				if (m_hSourceWeapon)
				{
					m_hSourceWeapon->NotifyKnifeRecovered();
				}

				UTIL_Remove(this);
				return;
			}
		}
	}

	SetNextThink(gpGlobals->curtime + 0.1f);
}

//-----------------------------------------------------------------------------
// Purpose: Damage-on-impact against anything (anyone) other than the
// owner -- the owner never triggers this at all, since SetOwnerEntity()
// (see Create()) excludes them from colliding with their own thrown
// knife entirely. Recovery is handled separately, via proximity, in
// KnifeThink(). Reflect-on-glancing-hit logic matches CCrossbowBolt.
//-----------------------------------------------------------------------------
void CThrownKnife::KnifeTouch(CBaseEntity* pOther)
{
	if (m_bStuck)
		return;	// already stuck in something -- ignore further touches

	bool bHitEntity = (pOther && pOther->m_takedamage != DAMAGE_NO);

	if (bHitEntity)
	{
		float flDistTraveled = (GetAbsOrigin() - m_vecThrowOrigin).Length();

		float flFalloff = 1.0f / sqrt(1.0f + (flDistTraveled / sk_knife_damage_falloff_scale.GetFloat()));
		float flDamage = sk_knife_dmg_throw.GetFloat() * flFalloff;

		Vector vecVelDir = GetAbsVelocity();
		VectorNormalize(vecVelDir);

		CTakeDamageInfo info(this, m_hOriginalOwner.Get(), flDamage, DMG_SLASH);
		info.SetDamagePosition(GetAbsOrigin());
		info.SetDamageForce(vecVelDir * flDamage * 10.0f);

		pOther->TakeDamage(info);

		EmitSound("Weapon_Knife.ThrowHitBody");


		// Stick into NPC/ragdoll
		m_bStuck = true;

		SetAbsVelocity(vec3_origin);
		SetLocalAngularVelocity(vec3_angle);
		SetMoveType(MOVETYPE_NONE);


		// Keep the knife attached to the victim
		SetParent(pOther);


		// Preserve the impact position/orientation
		Vector vecForward;
		AngleVectors(GetAbsAngles(), &vecForward);

		SetLocalOrigin(Vector(0, 0, 0));

		QAngle angKnife;
		VectorAngles(-vecVelDir, angKnife);
		SetLocalAngles(angKnife);


		if (knife_debug.GetBool())
		{
			Msg("[knife] embedded into %s\n", pOther->GetClassname());
		}

		return;
	}

	// Hit the world -- check for a glancing hit (reflect) vs. a solid hit
	// (stick), same logic CCrossbowBolt uses. Matching its exact pattern
	// here too: declare then assign, rather than copy-initializing in one
	// line -- trace_t's copy constructor is inaccessible in this branch.
	trace_t tr;
	tr = BaseClass::GetTouchTrace();

	if (pOther && pOther->GetMoveType() == MOVETYPE_NONE && !(tr.surface.flags & SURF_SKY))
	{
		Vector vecDir = GetAbsVelocity();
		float flSpeed = VectorNormalize(vecDir);

		float flHitDot = DotProduct(tr.plane.normal, -vecDir);

		if ((flHitDot < 0.5f) && (flSpeed > 100))
		{
			// Glancing hit -- reflect off the surface instead of sticking.
			Vector vecReflect = 2.0f * tr.plane.normal * flHitDot + vecDir;

			QAngle angReflect;
			VectorAngles(vecReflect, angReflect);
			SetAbsAngles(angReflect);

			SetAbsVelocity(vecReflect * flSpeed * 0.75f);

			if (knife_debug.GetBool())
			{
				Msg("[knife] glancing hit -- reflecting (dot %.2f)\n", flHitDot);
			}

			return;	// don't stick -- keep flying
		}
	}

	EmitSound("Weapon_Knife.ThrowHitWorld");

	m_bStuck = true;
	SetAbsVelocity(vec3_origin);
	SetLocalAngularVelocity(vec3_angle);
	SetMoveType(MOVETYPE_NONE);
}

//-----------------------------------------------------------------------------
// Purpose: Factory.
//-----------------------------------------------------------------------------
CThrownKnife* CThrownKnife::Create(const Vector& vecOrigin, const Vector& vecVelocity, CBasePlayer* pOwner, CWeaponKnife* pWeapon)
{
	CThrownKnife* pKnife = (CThrownKnife*)CBaseEntity::Create("thrown_knife", vecOrigin, vec3_angle);
	if (!pKnife)
		return NULL;

	pKnife->SetAbsVelocity(vecVelocity);
	pKnife->m_hOriginalOwner = pOwner;
	pKnife->m_hSourceWeapon = pWeapon;

	// This IS needed, unlike what an earlier version of this file assumed --
	// without it, the knife spawns wedged directly inside the throwing
	// player's own collision hull (it spawns at their eye/weapon position)
	// and physically blocks their movement until removed. Recovery no
	// longer depends on a physical Touch() from the owner anyway -- see
	// KnifeThink()'s proximity check instead.
	pKnife->SetOwnerEntity(pOwner);

	return pKnife;
}

//=============================================================================
// CWeaponKnife
//=============================================================================

acttable_t CWeaponKnife::m_acttable[] =
{
	{ ACT_MELEE_ATTACK1,	ACT_MELEE_ATTACK_SWING, true },
	{ ACT_IDLE,				ACT_IDLE_ANGRY_MELEE,	false },
	{ ACT_IDLE_ANGRY,		ACT_IDLE_ANGRY_MELEE,	false },
};

IMPLEMENT_ACTTABLE(CWeaponKnife);

IMPLEMENT_SERVERCLASS_ST(CWeaponKnife, DT_WeaponKnife)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS(weapon_knife, CWeaponKnife);
PRECACHE_WEAPON_REGISTER(weapon_knife);

BEGIN_DATADESC(CWeaponKnife)
DEFINE_FIELD(m_bCharging, FIELD_BOOLEAN),
DEFINE_FIELD(m_flChargeStartTime, FIELD_TIME),
DEFINE_FIELD(m_bThrown, FIELD_BOOLEAN),
END_DATADESC()

CWeaponKnife::CWeaponKnife(void)
{
	m_bCharging = false;
	m_flChargeStartTime = 0.0f;
	m_bThrown = false;
}

float CWeaponKnife::GetRange(void)
{
	return sk_knife_range.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: Re-syncs the hidden/shown bodygroup state whenever the weapon
// is drawn -- covers switching away and back, or a save/load, while
// m_bThrown is true.
//-----------------------------------------------------------------------------
bool CWeaponKnife::Deploy(void)
{
	bool bRet = BaseClass::Deploy();
	UpdateBodygroups();
	return bRet;
}

//-----------------------------------------------------------------------------
// Purpose: While thrown, this weapon is genuinely unusable -- reflecting
// that via HasAnyAmmo() means the game's normal "out of ammo" handling
// (HUD, auto-switch-away, not re-selecting it while cycling weapons)
// applies correctly until it's recovered.
//-----------------------------------------------------------------------------
bool CWeaponKnife::HasAnyAmmo(void)
{
	if (m_bThrown)
		return false;

	return BaseClass::HasAnyAmmo();
}

float CWeaponKnife::GetFireRate(void)
{
	return sk_knife_refire.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: Required by CBaseHLBludgeonWeapon -- not actually consulted by
// our own PrimaryAttack() below (which does its own manual trace/damage
// so it can check backstab conditions first), but the base class still
// needs a concrete implementation to be instantiable.
//-----------------------------------------------------------------------------
float CWeaponKnife::GetDamageForActivity(Activity hitActivity)
{
	return sk_knife_dmg_primary.GetFloat();
}

//-----------------------------------------------------------------------------
// Purpose: Determines whether pAttacker is hitting pTarget's exposed back.
//-----------------------------------------------------------------------------
bool CWeaponKnife::IsBackstab(CBaseEntity* pTarget, CBasePlayer* pAttacker)
{
	if (!pTarget)
		return false;

	CBaseCombatCharacter* pBCC = ToBaseCombatCharacter(pTarget);
	if (!pBCC)
		return false;

	Vector vecTargetForward = pBCC->BodyDirection2D();

	Vector vecToAttacker = pAttacker->GetAbsOrigin() - pTarget->GetAbsOrigin();
	vecToAttacker.z = 0.0f;
	VectorNormalize(vecToAttacker);

	// vecToAttacker points FROM the target TOWARD the attacker. If the
	// target's forward vector is roughly OPPOSITE that (dot near -1),
	// their front is facing away from the attacker -- i.e. their back is
	// turned to us. Dot near +1 means they're looking straight at us.
	float flDot = DotProduct(vecTargetForward, vecToAttacker);

	if (knife_debug.GetBool())
	{
		Msg("[knife] backstab check: dot = %.2f (threshold -%.2f)\n", flDot, sk_knife_backstab_dot.GetFloat());
	}

	return (flDot <= -sk_knife_backstab_dot.GetFloat());
}

//-----------------------------------------------------------------------------
// Purpose: Fast melee swing with a backstab insta-kill check. Does its own
// manual trace + damage instead of relying on the bludgeon base class's
// built-in swing, so we can evaluate the backstab condition before
// deciding how much damage to deal.
//-----------------------------------------------------------------------------
void CWeaponKnife::PrimaryAttack(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());
	if (!pPlayer)
		return;

	if (m_bThrown)
	{
		// The only knife you have is out there thrown somewhere.
		WeaponSound(EMPTY);
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
		return;
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + sk_knife_refire.GetFloat();

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming;
	pPlayer->EyeVectors(&vecAiming);

	Vector vecEnd;
	VectorMA(vecSrc, sk_knife_range.GetFloat(), vecAiming, vecEnd);

	trace_t tr;
	UTIL_TraceHull(vecSrc, vecEnd, Vector(-16, -16, -16), Vector(16, 16, 16), MASK_SHOT_HULL, pPlayer, COLLISION_GROUP_NONE, &tr);

	CBaseEntity* pHurt = tr.m_pEnt;

	if (pHurt && pHurt->m_takedamage != DAMAGE_NO)
	{
		bool bBackstab = IsBackstab(pHurt, pPlayer);

		float flDamage;
		int nDmgType = DMG_SLASH;

		if (bBackstab)
		{
			// Insta-kill -- deal comfortably more than their current health.
			flDamage = pHurt->GetHealth() + 100.0f;

			if (knife_debug.GetBool())
			{
				Msg("[knife] BACKSTAB on %s\n", pHurt->GetClassname());
			}
		}
		else
		{
			flDamage = sk_knife_dmg_primary.GetFloat();
		}

		CTakeDamageInfo info(pPlayer, pPlayer, flDamage, nDmgType);
		info.SetDamagePosition(tr.endpos);
		info.SetDamageForce(vecAiming * flDamage * 10.0f);
		pHurt->TakeDamage(info);

		WeaponSound(MELEE_HIT);
		SendWeaponAnim(ACT_VM_HITCENTER);

		ImpactEffect(tr);
	}
	else
	{
		WeaponSound(MELEE_MISS);
		SendWeaponAnim(ACT_VM_MISSCENTER);
	}

	pPlayer->SetAnimation(PLAYER_ATTACK1);
}

//-----------------------------------------------------------------------------
// Purpose: mouse2 hold-to-charge state machine, same pattern as the
// wrench's charge attack and the gauss pistol's secondary.
//-----------------------------------------------------------------------------
void CWeaponKnife::ItemPostFrame(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
	{
		BaseClass::ItemPostFrame();
		return;
	}

	bool bAttack2Held = (pOwner->m_nButtons & IN_ATTACK2) != 0;

	if (bAttack2Held && !m_bCharging && !m_bThrown && gpGlobals->curtime >= m_flNextSecondaryAttack)
	{
		StartCharge();
	}
	else if (!bAttack2Held && m_bCharging)
	{
		ReleaseThrow();
	}
	else if (bAttack2Held && m_bCharging)
	{
		UpdateChargeEffects(pOwner);
	}

	// While charging, lock out the primary swing -- same reasoning as the
	// wrench: holding mouse2 shouldn't let mouse1 interrupt the wind-up.
	if (m_bCharging)
	{
		WeaponIdle();
		return;
	}

	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose: Begins the mouse2 hold. Refuses if there's no knife to throw
// (already thrown and not yet recovered).
//-----------------------------------------------------------------------------
void CWeaponKnife::StartCharge(void)
{
	if (m_bThrown)
	{
		WeaponSound(EMPTY);
		m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;
		return;
	}

	m_bCharging = true;
	m_flChargeStartTime = gpGlobals->curtime;

	SendWeaponAnim(ACT_VM_PULLBACK);

	if (knife_debug.GetBool())
	{
		Msg("[knife] charge started\n");
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs every frame while charging. Handles the max-charge screen
// shake -- interpreted as a continuous sine-wave roll oscillation between
// +5 and -5 degrees, applied every frame via ViewPunch() until release.
//-----------------------------------------------------------------------------
void CWeaponKnife::UpdateChargeEffects(CBasePlayer* pOwner)
{
	float flChargeTime = gpGlobals->curtime - m_flChargeStartTime;

	if (flChargeTime >= sk_knife_charge_max_time.GetFloat())
	{
		float flPhase = flChargeTime * sk_knife_shake_speed.GetFloat();
		float flRoll = sk_knife_shake_amplitude.GetFloat() * sin(flPhase);

		pOwner->ViewPunch(QAngle(0, 0, flRoll));

		if (knife_debug.GetBool())
		{
			Msg("[knife] MAX CHARGE -- shake roll %.3f\n", flRoll);
		}
	}
	else if (knife_debug.GetBool())
	{
		Msg("[knife] charging: held %.2fs / %.2fs\n", flChargeTime, sk_knife_charge_max_time.GetFloat());
	}
}

//-----------------------------------------------------------------------------
// Purpose: mouse2 released -- throws the knife. Power (and thus range
// before the inverse-sqrt damage falloff really bites) scales with how
// long it was held, up to sk_knife_charge_max_time. Holding past
// sk_knife_inaccurate_time adds increasing random deviation to the throw.
//-----------------------------------------------------------------------------
void CWeaponKnife::ReleaseThrow(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());

	float flChargeTime = gpGlobals->curtime - m_flChargeStartTime;
	m_bCharging = false;

	if (!pOwner)
		return;

	float flMaxTime = sk_knife_charge_max_time.GetFloat();
	float flClampedCharge = clamp(flChargeTime, 0.0f, flMaxTime);
	float flPowerFrac = (flMaxTime > 0.0f) ? (flClampedCharge / flMaxTime) : 1.0f;

	float flThrowSpeed = Lerp(flPowerFrac, sk_knife_throw_speed_min.GetFloat(), sk_knife_throw_speed_max.GetFloat());

	Vector vecSrc = pOwner->Weapon_ShootPosition();
	Vector vecAiming;
	pOwner->EyeVectors(&vecAiming);

	// Overcharge inaccuracy -- holding past sk_knife_inaccurate_time
	// randomly deviates the throw direction, worse the longer it's held
	// beyond that point.
	float flOverchargeTime = flChargeTime - sk_knife_inaccurate_time.GetFloat();
	if (flOverchargeTime > 0.0f)
	{
		float flInaccuracyDeg = flOverchargeTime * sk_knife_inaccuracy_per_sec.GetFloat();

		QAngle angAiming;
		VectorAngles(vecAiming, angAiming);
		angAiming.y += random->RandomFloat(-flInaccuracyDeg, flInaccuracyDeg);
		angAiming.x += random->RandomFloat(-flInaccuracyDeg, flInaccuracyDeg);
		AngleVectors(angAiming, &vecAiming);

		if (knife_debug.GetBool())
		{
			Msg("[knife] overcharge inaccuracy: +/- %.1f degrees\n", flInaccuracyDeg);
		}
	}

	Vector vecVelocity = vecAiming * flThrowSpeed;

	CThrownKnife::Create(vecSrc, vecVelocity, pOwner, this);

	m_bThrown = true;

	UpdateBodygroups();

	SendWeaponAnim(ACT_VM_THROW);

	m_flNextSecondaryAttack = gpGlobals->curtime + 0.5f;

	// You have nothing left to fight with until it's recovered -- switch
	// away automatically, same as other weapons do when they run dry.
	pOwner->SwitchToNextBestWeapon(this);

	if (knife_debug.GetBool())
	{
		Msg("[knife] thrown: charge %.2fs, power %.2f, speed %.0f\n", flChargeTime, flPowerFrac, flThrowSpeed);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Hides/shows the knife model on both the viewmodel and this
// weapon's world model -- same approach the stock crossbow uses to hide
// its bolt once fired, so it visually looks like you're empty-handed
// while your only knife is out there thrown.
//
// TODO: this assumes a bodygroup literally named "knife" exists on both
// models. If it doesn't match your actual model's bodygroup name,
// FindBodygroupByName() returns -1 and SetBodygroup() becomes a no-op --
// nothing will break, it just won't visibly hide anything until the name
// matches your content.
//-----------------------------------------------------------------------------
void CWeaponKnife::UpdateBodygroups(void)
{
	int nBody = FindBodygroupByName("knife");
	if (nBody != -1)
	{
		SetBodygroup(nBody, m_bThrown ? 1 : 0);

		if (knife_debug.GetBool())
		{
			Msg("[knife] world model bodygroup 'knife' (index %d) set to %s\n", nBody, m_bThrown ? "HIDDEN" : "SHOWN");
		}
	}
	else
	{
		// Fallback for models without a dedicated bodygroup -- hides the
		// whole world model instead of just a blade sub-part. Works today
		// without needing a model change; swap back to the bodygroup once
		// one exists, since that'll look better (keeps the handle visible).
		if (m_bThrown)
		{
			AddEffects(EF_NODRAW);
		}
		else
		{
			RemoveEffects(EF_NODRAW);
		}

		if (knife_debug.GetBool())
		{
			Msg("[knife] world model has no bodygroup named 'knife' -- falling back to EF_NODRAW (%s)\n", m_bThrown ? "HIDDEN" : "SHOWN");
		}
	}

	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (pOwner)
	{
		CBaseViewModel* pViewModel = pOwner->GetViewModel();
		if (pViewModel)
		{
			int nViewBody = pViewModel->FindBodygroupByName("knife");
			if (nViewBody != -1)
			{
				pViewModel->SetBodygroup(nViewBody, m_bThrown ? 1 : 0);

				if (knife_debug.GetBool())
				{
					Msg("[knife] viewmodel bodygroup 'knife' (index %d) set to %s\n", nViewBody, m_bThrown ? "HIDDEN" : "SHOWN");
				}
			}
			else
			{
				// Same fallback as the world model above. NOTE: this hides
				// the ENTIRE viewmodel (the whole first-person weapon),
				// not just the knife -- there's no separate "empty hands"
				// asset to fall back to without a bodygroup, so this is
				// the closest achievable "you have nothing in hand" look
				// until a real bodygroup exists.
				if (m_bThrown)
				{
					pViewModel->AddEffects(EF_NODRAW);
				}
				else
				{
					pViewModel->RemoveEffects(EF_NODRAW);
				}

				if (knife_debug.GetBool())
				{
					Msg("[knife] viewmodel has no bodygroup named 'knife' -- falling back to EF_NODRAW (%s)\n", m_bThrown ? "HIDDEN" : "SHOWN");
				}
			}
		}
		else if (knife_debug.GetBool())
		{
			Msg("[knife] UpdateBodygroups: owner has no viewmodel right now\n");
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called by CThrownKnife when the original owner recovers it.
//-----------------------------------------------------------------------------
void CWeaponKnife::NotifyKnifeRecovered(void)
{
	m_bThrown = false;

	UpdateBodygroups();

	if (knife_debug.GetBool())
	{
		Msg("[knife] recovered -- ready to use again\n");
	}
}