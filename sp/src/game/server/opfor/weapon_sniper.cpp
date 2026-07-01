//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Sniper Rifle - fires a real projectile bullet affected by
// gravity, giving genuine drop-off over range/distance instead of the
// flat, instant-hit trajectory most HL2 weapons use.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "te_effect_dispatch.h"
#include "gamestats.h"
#include "smoke_trail.h"		// for a simple tracer/trail effect on the bullet, same header the RPG missile uses
#include "IEffects.h"		

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Tuning ConVars
//-----------------------------------------------------------------------------
ConVar sk_plr_dmg_sniper("sk_plr_dmg_sniper", "60");
ConVar sk_sniper_bullet_speed("sk_sniper_bullet_speed", "4000");		// units/sec, initial velocity
ConVar sk_sniper_refire_time("sk_sniper_refire_time", "1.5");			// bolt-action style, slow refire
ConVar sk_sniper_bullet_lifetime("sk_sniper_bullet_lifetime", "5.0");	// safety despawn if it never hits anything

//=============================================================================
// CBulletProjectile -- the actual flying, falling bullet
//=============================================================================
class CBulletProjectile : public CBaseAnimating
{
	DECLARE_CLASS(CBulletProjectile, CBaseAnimating);
public:

	CBulletProjectile(void);

	void	Spawn(void);
	void	Precache(void);
	void	BulletTouch(CBaseEntity* pOther);
	void	BulletDieThink(void);

	void	SetDamage(float flDamage) { m_flDamage = flDamage; }

	static CBulletProjectile* Create(const Vector& vecOrigin, const Vector& vecVelocity, CBaseEntity* pOwner, float flDamage);

	DECLARE_DATADESC();

private:
	float	m_flDamage;
};

LINK_ENTITY_TO_CLASS(sniper_bullet, CBulletProjectile);

BEGIN_DATADESC(CBulletProjectile)
DEFINE_FIELD(m_flDamage, FIELD_FLOAT),
DEFINE_FUNCTION(BulletTouch),
DEFINE_FUNCTION(BulletDieThink),
END_DATADESC()

CBulletProjectile::CBulletProjectile(void)
{
	m_flDamage = 0.0f;
}

void CBulletProjectile::Precache(void)
{
	// TODO: point this at whatever bullet/tracer model you actually have.
	// If you'd rather have an invisible bullet with just a particle trail,
	// swap this for a tiny/transparent placeholder model instead.
	PrecacheModel("models/weapons/w_bullet.mdl");
	PrecacheParticleSystem("weapon_sniper_bullet_trail");	// TODO: needs a real particle system by this name in your particles manifest
}

void CBulletProjectile::Spawn(void)
{
	Precache();

	SetModel("models/weapons/w_bullet.mdl");
	SetSolid(SOLID_BBOX);
	SetSolidFlags(FSOLID_NOT_STANDABLE);
	UTIL_SetSize(this, -Vector(1, 1, 1), Vector(1, 1, 1));

	// MOVETYPE_FLYGRAVITY is the key piece here -- unlike hitscan bullets
	// or HL2's crossbow bolt (which flies straight with no gravity), this
	// actually falls over its flight, giving real drop-off at range.
	SetMoveType(MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM);

	SetTouch(&CBulletProjectile::BulletTouch);
	SetThink(&CBulletProjectile::BulletDieThink);
	SetNextThink(gpGlobals->curtime + sk_sniper_bullet_lifetime.GetFloat());

	AddEffects(EF_NOSHADOW);

	// Orient the model along its current direction of travel, so it doesn't
	// just fly along facing whatever angle it spawned at while gravity bends
	// its actual path underneath it.
	Vector vecVelDir = GetAbsVelocity();
	VectorNormalize(vecVelDir);
	QAngle angFacing;
	VectorAngles(vecVelDir, angFacing);
	SetAbsAngles(angFacing);

	// Attaches and follows the entity automatically -- this is what makes
	// the gravity drop actually visible/readable in flight, rather than
	// just being an invisible calculation.
	DispatchParticleEffect("weapon_sniper_bullet_trail", PATTACH_ABSORIGIN_FOLLOW, this);
}

//-----------------------------------------------------------------------------
// Purpose: Safety net -- if the bullet somehow never touches anything
// (flew off into the skybox, etc.), don't let it exist forever.
//-----------------------------------------------------------------------------
void CBulletProjectile::BulletDieThink(void)
{
	UTIL_Remove(this);
}

//-----------------------------------------------------------------------------
// Purpose: Applies damage on impact and cleans up. Doesn't distinguish
// between world and entity hits beyond that -- add pOther->IsWorld()
// checks here if you want different behavior (e.g. no damage/decal-only
// vs. an actual hit) later.
//-----------------------------------------------------------------------------
void CBulletProjectile::BulletTouch(CBaseEntity* pOther)
{
	if (pOther == GetOwnerEntity())
		return;

	if (pOther->m_takedamage != DAMAGE_NO)
	{
		Vector vecVelDir = GetAbsVelocity();
		VectorNormalize(vecVelDir);

		CTakeDamageInfo info(this, GetOwnerEntity(), m_flDamage, DMG_BULLET);
		info.SetDamagePosition(GetAbsOrigin());
		// Manual force scale instead of CalculateBulletDamageForce(), which
		// needs a registered ammo type (e.g. "SniperRound") to look up mass
		// data -- this keeps the projectile self-contained without requiring
		// you to add a new ammo definition just for physics force. Tune the
		// 25.0f multiplier to taste.
		info.SetDamageForce(vecVelDir * m_flDamage * 25.0f);
		pOther->TakeDamage(info);
	}

	// Simple impact spark -- swap for a proper decal/particle if you want
	// something more visually substantial on hit.
	g_pEffects->Sparks(GetAbsOrigin());

	UTIL_Remove(this);
}

//-----------------------------------------------------------------------------
// Purpose: Factory -- spawns the bullet at a position with a given velocity.
//-----------------------------------------------------------------------------
CBulletProjectile* CBulletProjectile::Create(const Vector& vecOrigin, const Vector& vecVelocity, CBaseEntity* pOwner, float flDamage)
{
	CBulletProjectile* pBullet = (CBulletProjectile*)CBaseEntity::Create("sniper_bullet", vecOrigin, vec3_angle);

	if (!pBullet)
		return NULL;

	pBullet->SetOwnerEntity(pOwner);
	pBullet->SetAbsVelocity(vecVelocity);
	pBullet->SetDamage(flDamage);

	return pBullet;
}

//=============================================================================
// CWeaponSniperRifle
//=============================================================================
class CWeaponSniperRifle : public CBaseHLCombatWeapon
{
	DECLARE_CLASS(CWeaponSniperRifle, CBaseHLCombatWeapon);
public:

	CWeaponSniperRifle(void);

	void	Precache(void);
	void	PrimaryAttack(void);

	virtual float GetFireRate(void) { return sk_sniper_refire_time.GetFloat(); }

	float	WeaponAutoAimScale() { return 0.4f; }	// less autoaim assistance -- this is a precision weapon

	int		CapabilitiesGet(void) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
};

LINK_ENTITY_TO_CLASS(weapon_sniper, CWeaponSniperRifle);

PRECACHE_WEAPON_REGISTER(weapon_sniper);

IMPLEMENT_SERVERCLASS_ST(CWeaponSniperRifle, DT_WeaponSniperRifle)
END_SEND_TABLE()

BEGIN_DATADESC(CWeaponSniperRifle)
END_DATADESC()

CWeaponSniperRifle::CWeaponSniperRifle(void)
{
	m_bReloadsSingly = false;
	m_bFiresUnderwater = false;
}

void CWeaponSniperRifle::Precache(void)
{
	BaseClass::Precache();
	UTIL_PrecacheOther("sniper_bullet");
}

//-----------------------------------------------------------------------------
// Purpose: Fires the actual projectile instead of a hitscan bullet.
//-----------------------------------------------------------------------------
void CWeaponSniperRifle::PrimaryAttack(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());

	if (!pPlayer)
	{
		return;
	}

	if (m_iClip1 <= 0)
	{
		if (!m_bFireOnEmpty)
		{
			Reload();
		}
		else
		{
			WeaponSound(EMPTY);
			m_flNextPrimaryAttack = 0.15;
		}

		return;
	}

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired(pPlayer, true, GetClassname());

	WeaponSound(SINGLE);
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim(ACT_VM_PRIMARYATTACK);
	pPlayer->SetAnimation(PLAYER_ATTACK1);

	m_flNextPrimaryAttack = gpGlobals->curtime + sk_sniper_refire_time.GetFloat();
	m_flNextSecondaryAttack = gpGlobals->curtime + sk_sniper_refire_time.GetFloat();

	m_iClip1--;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

	Vector vecVelocity = vecAiming * sk_sniper_bullet_speed.GetFloat();

	CBulletProjectile::Create(vecSrc, vecVelocity, pPlayer, sk_plr_dmg_sniper.GetFloat());

	pPlayer->SetMuzzleFlashTime(gpGlobals->curtime + 0.5);

	pPlayer->ViewPunch(QAngle(-2, random->RandomFloat(-1, 1), 0));

	CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), 600, 0.2, GetOwner());

	if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
	{
		pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
	}
}