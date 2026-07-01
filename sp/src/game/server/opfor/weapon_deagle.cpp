//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Deagle - laser-sight toggle hand gun (Opposing Force style)
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
#include "weapon_rpg.h"	// for CreateLaserDot() / SetLaserDotTarget() / EnableLaserDot()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// CWeaponDeagle
//-----------------------------------------------------------------------------

#ifdef MAPBASE
extern acttable_t* GetPistolActtable();
extern int GetPistolActtableCount();
#endif

// Laser toggle tuning
ConVar sk_deagle_firerate_laser("sk_deagle_firerate_laser", "1.0");		// delay between shots, laser ON (slower)
ConVar sk_deagle_firerate_normal("sk_deagle_firerate_normal", "0.5");	// delay between shots, laser OFF (faster)
ConVar sk_deagle_laser_toggle_cooldown("sk_deagle_laser_toggle_cooldown", "0.3"); // debounce so holding mouse2 doesn't spam-toggle

class CWeaponDeagle : public CBaseHLCombatWeapon
{
	DECLARE_CLASS(CWeaponDeagle, CBaseHLCombatWeapon);
public:

	CWeaponDeagle(void);
	~CWeaponDeagle(void);

	void	PrimaryAttack(void);
	void	SecondaryAttack(void);
	void	ItemPostFrame(void);
	bool	Holster(CBaseCombatWeapon* pSwitchingTo = NULL);
	void	Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator);

	float	WeaponAutoAimScale() { return 0.6f; }

	// Public so HUD/crosshair code can query laser state to decide what to draw.
	bool	IsLaserOn(void) const { return m_bLaserOn; }

#ifdef MAPBASE
	int		CapabilitiesGet(void) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

	virtual int	GetMinBurst() { return 1; }
	virtual int	GetMaxBurst() { return 1; }
	virtual float	GetMinRestTime(void) { return 1.0f; }
	virtual float	GetMaxRestTime(void) { return 2.5f; }

	virtual float GetFireRate(void) { return m_bLaserOn ? sk_deagle_firerate_laser.GetFloat() : sk_deagle_firerate_normal.GetFloat(); }

	virtual const Vector& GetBulletSpread(void)
	{
		static Vector coneLaserOn = VECTOR_CONE_1DEGREES;
		static Vector coneLaserOff = VECTOR_CONE_6DEGREES;

		if (!GetOwner() || !GetOwner()->IsNPC())
			return m_bLaserOn ? coneLaserOn : coneLaserOff;

		static Vector AllyCone = VECTOR_CONE_2DEGREES;
		static Vector NPCCone = VECTOR_CONE_5DEGREES;

		if (GetOwner()->MyNPCPointer()->IsPlayerAlly())
		{
			// deagle allies should be cooler
			return AllyCone;
		}

		return NPCCone;
	}

	void	FireNPCPrimaryAttack(CBaseCombatCharacter* pOperator, Vector& vecShootOrigin, Vector& vecShootDir);
	void	Operator_ForceNPCFire(CBaseCombatCharacter* pOperator, bool bSecondary);

	virtual acttable_t* GetBackupActivityList() { return GetPistolActtable(); }
	virtual int				GetBackupActivityListCount() { return GetPistolActtableCount(); }
#endif

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
#ifdef MAPBASE
	DECLARE_ACTTABLE();
#endif

private:
	bool	m_bLaserOn;
	EHANDLE	m_hLaserDot;

	void	UpdateLaserDot(void);
	void	DestroyLaserDot(void);
};

LINK_ENTITY_TO_CLASS(weapon_deagle, CWeaponDeagle);

PRECACHE_WEAPON_REGISTER(weapon_deagle);

IMPLEMENT_SERVERCLASS_ST(CWeaponDeagle, DT_WeaponDeagle)
END_SEND_TABLE()

BEGIN_DATADESC(CWeaponDeagle)
DEFINE_FIELD(m_bLaserOn, FIELD_BOOLEAN),
END_DATADESC()

#ifdef MAPBASE
acttable_t	CWeaponDeagle::m_acttable[] =
{
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_IDLE,						ACT_IDLE_REVOLVER,					true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_REVOLVER,				true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_REVOLVER,			true },
	{ ACT_RELOAD,					ACT_RELOAD_REVOLVER,					true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_REVOLVER,				true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_REVOLVER,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_REVOLVER,	true },
	{ ACT_RELOAD_LOW,				ACT_RELOAD_REVOLVER_LOW,				false },
	{ ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_REVOLVER_LOW,		false },
	{ ACT_COVER_LOW,				ACT_COVER_REVOLVER_LOW,				false },
	{ ACT_RANGE_AIM_LOW,			ACT_RANGE_AIM_REVOLVER_LOW,			false },
	{ ACT_GESTURE_RELOAD,			ACT_GESTURE_RELOAD_REVOLVER,			false },
	{ ACT_WALK,						ACT_WALK_REVOLVER,					true },
	{ ACT_RUN,						ACT_RUN_REVOLVER,					true },
#else
	{ ACT_IDLE,						ACT_IDLE_PISTOL,				true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_PISTOL,			true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_PISTOL,		true },
	{ ACT_RELOAD,					ACT_RELOAD_PISTOL,				true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_PISTOL,			true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_PISTOL,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_PISTOL,true },
	{ ACT_RELOAD_LOW,				ACT_RELOAD_PISTOL_LOW,			false },
	{ ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_PISTOL_LOW,	false },
	{ ACT_COVER_LOW,				ACT_COVER_PISTOL_LOW,			false },
	{ ACT_RANGE_AIM_LOW,			ACT_RANGE_AIM_PISTOL_LOW,		false },
	{ ACT_GESTURE_RELOAD,			ACT_GESTURE_RELOAD_PISTOL,		false },
	{ ACT_WALK,						ACT_WALK_PISTOL,				false },
	{ ACT_RUN,						ACT_RUN_PISTOL,					false },
#endif

	// 
	// Activities ported from weapon_alyxgun below
	// 

	// Readiness activities (not aiming)
#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_IDLE_RELAXED,				ACT_IDLE_PISTOL_RELAXED,		false },//never aims
	{ ACT_IDLE_STIMULATED,			ACT_IDLE_PISTOL_STIMULATED,		false },
#else
	{ ACT_IDLE_RELAXED,				ACT_IDLE_PISTOL,				false },//never aims
	{ ACT_IDLE_STIMULATED,			ACT_IDLE_STIMULATED,			false },
#endif
	{ ACT_IDLE_AGITATED,			ACT_IDLE_ANGRY_PISTOL,			false },//always aims
	{ ACT_IDLE_STEALTH,				ACT_IDLE_STEALTH_PISTOL,		false },

#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_WALK_RELAXED,				ACT_WALK_PISTOL_RELAXED,		false },//never aims
	{ ACT_WALK_STIMULATED,			ACT_WALK_PISTOL_STIMULATED,		false },
#else
	{ ACT_WALK_RELAXED,				ACT_WALK,						false },//never aims
	{ ACT_WALK_STIMULATED,			ACT_WALK_STIMULATED,			false },
#endif
	{ ACT_WALK_AGITATED,			ACT_WALK_AIM_PISTOL,			false },//always aims
	{ ACT_WALK_STEALTH,				ACT_WALK_STEALTH_PISTOL,		false },

#if EXPANDED_HL2_WEAPON_ACTIVITIES
	{ ACT_RUN_RELAXED,				ACT_RUN_PISTOL_RELAXED,			false },//never aims
	{ ACT_RUN_STIMULATED,			ACT_RUN_PISTOL_STIMULATED,		false },
#else
	{ ACT_RUN_RELAXED,				ACT_RUN,						false },//never aims
	{ ACT_RUN_STIMULATED,			ACT_RUN_STIMULATED,				false },
#endif
	{ ACT_RUN_AGITATED,				ACT_RUN_AIM_PISTOL,				false },//always aims
	{ ACT_RUN_STEALTH,				ACT_RUN_STEALTH_PISTOL,			false },

	// Readiness activities (aiming)
	{ ACT_IDLE_AIM_RELAXED,			ACT_IDLE_PISTOL,				false },//never aims	
	{ ACT_IDLE_AIM_STIMULATED,		ACT_IDLE_ANGRY_PISTOL,			false },
	{ ACT_IDLE_AIM_AGITATED,		ACT_IDLE_ANGRY_PISTOL,			false },//always aims
	{ ACT_IDLE_AIM_STEALTH,			ACT_IDLE_STEALTH_PISTOL,		false },

	{ ACT_WALK_AIM_RELAXED,			ACT_WALK,						false },//never aims
	{ ACT_WALK_AIM_STIMULATED,		ACT_WALK_AIM_PISTOL,			false },
	{ ACT_WALK_AIM_AGITATED,		ACT_WALK_AIM_PISTOL,			false },//always aims
	{ ACT_WALK_AIM_STEALTH,			ACT_WALK_AIM_STEALTH_PISTOL,	false },//always aims

	{ ACT_RUN_AIM_RELAXED,			ACT_RUN,						false },//never aims
	{ ACT_RUN_AIM_STIMULATED,		ACT_RUN_AIM_PISTOL,				false },
	{ ACT_RUN_AIM_AGITATED,			ACT_RUN_AIM_PISTOL,				false },//always aims
	{ ACT_RUN_AIM_STEALTH,			ACT_RUN_AIM_STEALTH_PISTOL,		false },//always aims
	//End readiness activities

	// Crouch activities
	{ ACT_CROUCHIDLE_STIMULATED,	ACT_CROUCHIDLE_STIMULATED,		false },
	{ ACT_CROUCHIDLE_AIM_STIMULATED,ACT_RANGE_AIM_PISTOL_LOW,		false },//always aims
	{ ACT_CROUCHIDLE_AGITATED,		ACT_RANGE_AIM_PISTOL_LOW,		false },//always aims

	// Readiness translations
	{ ACT_READINESS_RELAXED_TO_STIMULATED, ACT_READINESS_PISTOL_RELAXED_TO_STIMULATED, false },
	{ ACT_READINESS_RELAXED_TO_STIMULATED_WALK, ACT_READINESS_PISTOL_RELAXED_TO_STIMULATED_WALK, false },
	{ ACT_READINESS_AGITATED_TO_STIMULATED, ACT_READINESS_PISTOL_AGITATED_TO_STIMULATED, false },
	{ ACT_READINESS_STIMULATED_TO_RELAXED, ACT_READINESS_PISTOL_STIMULATED_TO_RELAXED, false },

#if EXPANDED_HL2_COVER_ACTIVITIES
	{ ACT_RANGE_AIM_MED,			ACT_RANGE_AIM_REVOLVER_MED,			false },
	{ ACT_RANGE_ATTACK1_MED,		ACT_RANGE_ATTACK_REVOLVER_MED,		false },
#endif

#ifdef MAPBASE
	// HL2:DM activities (for third-person animations in SP)
#if EXPANDED_HL2DM_ACTIVITIES
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_REVOLVER,                    false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_REVOLVER,                    false },
	{ ACT_HL2MP_WALK,					ACT_HL2MP_WALK_REVOLVER,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_REVOLVER,            false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_REVOLVER,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_REVOLVER,    false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK2,	ACT_HL2MP_GESTURE_RANGE_ATTACK2_REVOLVER,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_REVOLVER,        false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_REVOLVER,                    false },
#else
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_PISTOL,                    false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_PISTOL,                    false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_PISTOL,            false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_PISTOL,            false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,    false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_PISTOL,        false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_PISTOL,                    false },
#endif
#endif
};


IMPLEMENT_ACTTABLE(CWeaponDeagle);

// Allows Weapon_BackupActivity() to access the deagle's activity table.
acttable_t* GetDeagleActtable()
{
	return CWeaponDeagle::m_acttable;
}

int GetDeagleActtableCount()
{
	return ARRAYSIZE(CWeaponDeagle::m_acttable);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeaponDeagle::CWeaponDeagle(void)
{
	m_bReloadsSingly = false;
	m_bFiresUnderwater = false;
	m_bLaserOn = false;
	m_hLaserDot = NULL;

#ifdef MAPBASE
	m_fMinRange1 = 24;
	m_fMaxRange1 = 1000;
	m_fMinRange2 = 24;
	m_fMaxRange2 = 200;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Destructor -- make sure the laser dot entity doesn't outlive us.
//-----------------------------------------------------------------------------
CWeaponDeagle::~CWeaponDeagle(void)
{
	DestroyLaserDot();
}

//-----------------------------------------------------------------------------
// Purpose: Holstering should hide/clean up the laser dot, same as the RPG does.
//-----------------------------------------------------------------------------
bool CWeaponDeagle::Holster(CBaseCombatWeapon* pSwitchingTo)
{
	DestroyLaserDot();
	return BaseClass::Holster(pSwitchingTo);
}

//-----------------------------------------------------------------------------
// Purpose: Keeps the laser dot tracking the player's aim every frame while
// the laser is on. Ported from CWeaponRPG's UpdateLaserPosition(), but using
// the opaque CreateLaserDot()/SetLaserDotTarget()/EnableLaserDot() API from
// weapon_rpg.h instead of touching CLaserDot directly (it's private to
// weapon_rpg.cpp). SetAbsOrigin() is a plain CBaseEntity method, so moving
// the dot doesn't require the concrete type either.
//-----------------------------------------------------------------------------
void CWeaponDeagle::UpdateLaserDot(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
		return;

	if (m_hLaserDot == NULL)
	{
		m_hLaserDot = CreateLaserDot(pOwner->Weapon_ShootPosition(), pOwner, true);
		if (m_hLaserDot == NULL)
			return;
	}

	EnableLaserDot(m_hLaserDot, true);

	Vector vecMuzzle = pOwner->Weapon_ShootPosition();
	Vector vecAiming;
	pOwner->EyeVectors(&vecAiming);

	Vector vecEnd = vecMuzzle + (vecAiming * MAX_TRACE_LENGTH);

	trace_t tr;
	UTIL_TraceLine(vecMuzzle, vecEnd, (MASK_SHOT & ~CONTENTS_WINDOW), this, COLLISION_GROUP_NONE, &tr);

	m_hLaserDot->SetAbsOrigin(tr.endpos);

	if (tr.DidHitNonWorldEntity() && tr.m_pEnt && tr.m_pEnt->m_takedamage)
	{
		SetLaserDotTarget(m_hLaserDot, tr.m_pEnt);
	}
	else
	{
		SetLaserDotTarget(m_hLaserDot, NULL);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Fully removes the laser dot entity (holster/weapon destroy),
// as opposed to just hiding it on a toggle-off (see SecondaryAttack()).
//-----------------------------------------------------------------------------
void CWeaponDeagle::DestroyLaserDot(void)
{
	if (m_hLaserDot != NULL)
	{
		EnableLaserDot(m_hLaserDot, false);
		UTIL_Remove(m_hLaserDot);
		m_hLaserDot = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drives the laser dot's per-frame position update while it's on.
//-----------------------------------------------------------------------------
void CWeaponDeagle::ItemPostFrame(void)
{
	BaseClass::ItemPostFrame();

	if (m_bLaserOn)
	{
		UpdateLaserDot();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());

	switch (pEvent->event)
	{
	case EVENT_WEAPON_RELOAD:
	{
		CEffectData data;

		// Emit six spent shells
		for (int i = 0; i < 6; i++)
		{
			data.m_vOrigin = pOwner->WorldSpaceCenter() + RandomVector(-4, 4);
			data.m_vAngles = QAngle(90, random->RandomInt(0, 360), 0);
			data.m_nEntIndex = entindex();

			DispatchEffect("ShellEject", data);
		}

		break;
	}
#ifdef MAPBASE
	case EVENT_WEAPON_PISTOL_FIRE:
	{
		Vector vecShootOrigin, vecShootDir;
		vecShootOrigin = pOperator->Weapon_ShootPosition();

		CAI_BaseNPC* npc = pOperator->MyNPCPointer();
		ASSERT(npc != NULL);

		vecShootDir = npc->GetActualShootTrajectory(vecShootOrigin);

		FireNPCPrimaryAttack(pOperator, vecShootOrigin, vecShootDir);
	}
	break;
	default:
		BaseClass::Operator_HandleAnimEvent(pEvent, pOperator);
		break;
#endif
	}
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponDeagle::FireNPCPrimaryAttack(CBaseCombatCharacter* pOperator, Vector& vecShootOrigin, Vector& vecShootDir)
{
	CSoundEnt::InsertSound(SOUND_COMBAT | SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy());

	WeaponSound(SINGLE_NPC);
	pOperator->FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_PRECALCULATED, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 1);
	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

//-----------------------------------------------------------------------------
// Purpose: Some things need this. (e.g. the new Force(X)Fire inputs or blindfire actbusy)
//-----------------------------------------------------------------------------
void CWeaponDeagle::Operator_ForceNPCFire(CBaseCombatCharacter* pOperator, bool bSecondary)
{
	// Ensure we have enough rounds in the clip
	m_iClip1++;

	Vector vecShootOrigin, vecShootDir;
	QAngle	angShootDir;
	GetAttachment(LookupAttachment("muzzle"), vecShootOrigin, angShootDir);
	AngleVectors(angShootDir, &vecShootDir);
	FireNPCPrimaryAttack(pOperator, vecShootOrigin, vecShootDir);
}
#endif

//-----------------------------------------------------------------------------
// Purpose: mouse2 -- toggle the laser sight. Trades fire rate for accuracy:
// laser ON = slower fire rate, tight/near-perfect accuracy.
// laser OFF = faster fire rate, wide spread.
//-----------------------------------------------------------------------------
void CWeaponDeagle::SecondaryAttack(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());

	if (!pPlayer)
	{
		return;
	}

	m_bLaserOn = !m_bLaserOn;

	if (m_bLaserOn)
	{
		// TODO: soundscript entry "Weapon_Deagle.LaserOn" needs to exist in
		// this weapon's soundscript (scripts/game_sounds_weapons.txt or a
		// dedicated weapon_deagle.txt), pointing at a real wav.
		EmitSound("Weapon_Deagle.LaserOn");

		// Create (first time) or re-show the dot immediately -- don't wait
		// for the next ItemPostFrame() tick to avoid a one-frame flash of
		// the old position.
		UpdateLaserDot();
	}
	else
	{
		// TODO: same as above, "Weapon_Deagle.LaserOff".
		EmitSound("Weapon_Deagle.LaserOff");

		// Just hide it rather than destroying it -- avoids entity
		// creation/deletion churn if the player toggles rapidly. It gets
		// fully destroyed on Holster()/weapon destruction instead.
		if (m_hLaserDot != NULL)
		{
			EnableLaserDot(m_hLaserDot, false);
		}
	}

	// Debounce -- SecondaryAttack() gets called every frame IN_ATTACK2 is
	// held by most base ItemPostFrame() implementations, so without this
	// the laser would flicker on/off dozens of times per second instead of
	// toggling once per press.
	m_flNextSecondaryAttack = gpGlobals->curtime + sk_deagle_laser_toggle_cooldown.GetFloat();

	// m_bLaserOn itself isn't networked to the client -- it doesn't need to
	// be, since the real laser dot is its own networked entity (same as
	// how CWeaponRPG's DT_WeaponRPG send table is also empty, even though
	// it drives a laser dot too). Any HUD element that wants a laser-on
	// indicator can still call IsLaserOn() server-side, or infer it from
	// whether the dot entity exists/is visible.
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponDeagle::PrimaryAttack(void)
{
	// Only the player fires this way so we can cast
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

	// Laser on = slower fire rate (tradeoff for the accuracy bonus below).
	// Laser off = faster fire rate, at the cost of accuracy.
	float flRefireDelay = m_bLaserOn ? sk_deagle_firerate_laser.GetFloat() : sk_deagle_firerate_normal.GetFloat();

	m_flNextPrimaryAttack = gpGlobals->curtime + flRefireDelay;
	m_flNextSecondaryAttack = gpGlobals->curtime + flRefireDelay;

	m_iClip1--;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

	// Laser on = near-zero spread (big accuracy bonus).
	// Laser off = wide spread (the accuracy cost for faster fire rate).
	Vector vecSpread = m_bLaserOn ? vec3_origin : VECTOR_CONE_6DEGREES;

	pPlayer->FireBullets(1, vecSrc, vecAiming, vecSpread, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);

	pPlayer->SetMuzzleFlashTime(gpGlobals->curtime + 0.5);

	//Disorient the player
	QAngle angles = pPlayer->GetLocalAngles();

	angles.x += random->RandomInt(-1, 1);
	angles.y += random->RandomInt(-1, 1);
	angles.z = 0;

	pPlayer->SnapEyeAngles(angles);

	pPlayer->ViewPunch(QAngle(-8, random->RandomFloat(-2, 2), 0));

	CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), 600, 0.2, GetOwner());

	if (!m_iClip1 && pPlayer->GetAmmoCount(m_iPrimaryAmmoType) <= 0)
	{
		// HEV suit - indicate out of ammo condition
		pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
	}
}