//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Prototype Gauss Pistol Rev. 2 -- self-recharging energy
// pistol. No reserve ammo; the internal charge (m_iClip1) regenerates on
// its own over time instead of being refilled by pickups/reloading.
//
// Primary (mouse1): fires a 3-shot burst in quick succession (one shot
// every sk_protogauss_primary_burst_shot_delay seconds), not simultaneously.
// Secondary (mouse2, hold): charges up, then automatically fires 5 bullets
// simultaneously in an evenly-fanned spread once fully charged. Releasing
// early cancels the charge with no shot fired.
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
#include "mathlib/mathlib.h"	// for DEG2RAD() used to build the secondary's spread cone vector

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Tuning ConVars
//-----------------------------------------------------------------------------
ConVar sk_protogauss_max_charge("sk_protogauss_max_charge", "60");				// max self-contained charge, acts as both clip size and total capacity
ConVar sk_protogauss_recharge_rate("sk_protogauss_recharge_rate", "5.0");		// charge units regained per second
ConVar sk_protogauss_recharge_delay("sk_protogauss_recharge_delay", "3.0");		// seconds after firing before recharge starts, resets on every shot

ConVar sk_protogauss_primary_burst_count("sk_protogauss_primary_burst_count", "3");
ConVar sk_protogauss_primary_burst_shot_delay("sk_protogauss_primary_burst_shot_delay", "0.08");	// gap between each of the 3 successive shots
ConVar sk_protogauss_primary_refire("sk_protogauss_primary_refire", "0.5");		// refire delay AFTER the full burst completes

ConVar sk_protogauss_secondary_burst_count("sk_protogauss_secondary_burst_count", "5");				// bullets actually fired, simultaneously
ConVar sk_protogauss_secondary_charge_cost("sk_protogauss_secondary_charge_cost", "10");				// charge consumed, independent of bullet count
ConVar sk_protogauss_secondary_spread_degrees("sk_protogauss_secondary_spread_degrees", "8.0");		// half-width of the fan, in degrees
ConVar sk_protogauss_secondary_charge_time("sk_protogauss_secondary_charge_time", "1.2");	// seconds held before it auto-fires
ConVar sk_protogauss_secondary_refire("sk_protogauss_secondary_refire", "0.75");

class CWeaponProtoGauss : public CBaseHLCombatWeapon
{
	DECLARE_CLASS(CWeaponProtoGauss, CBaseHLCombatWeapon);
public:

	CWeaponProtoGauss(void);

	void	PrimaryAttack(void);
	void	SecondaryAttack(void) { return; }	// handled entirely via ItemPostFrame() instead, same reasoning as the wrench's charge attack
	void	ItemPostFrame(void);

	bool	Reload(void) { return true; }		// no-op -- there's no reserve ammo to reload from, it self-charges instead
	bool	HasAnyAmmo(void) { return true; }	// being at 0 charge is temporary (it's recharging), not truly "out of ammo" the way a normal weapon is

	void	Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator);

	float	WeaponAutoAimScale() { return 0.6f; }

	int		CapabilitiesGet(void) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

private:
	void	Recharge(void);
	void	FireNextBurstShot(void);
	void	StartCharge(void);
	void	CancelCharge(void);
	void	FireChargedBurst(void);

	bool	m_bCharging;
	float	m_flChargeStartTime;
	float	m_flNextRechargeTick;
	float	m_flRechargeDelayEndsAt;	// recharge is gated until curtime passes this, reset on every shot fired

	int		m_nBurstShotsRemaining;	// >0 while a primary burst is playing out over successive frames
	float	m_flNextBurstShotTime;
};

LINK_ENTITY_TO_CLASS(weapon_protogauss, CWeaponProtoGauss);

PRECACHE_WEAPON_REGISTER(weapon_protogauss);

// Empty send table -- this weapon's state is entirely server-side logic
// (charge amount/recharge rate), same pattern already confirmed working
// for weapon_357/weapon_rpg. Adding SendProps here without a matching
// client-side class is what caused the earlier RecvProp/DT class errors
// on other weapons in this project.
IMPLEMENT_SERVERCLASS_ST(CWeaponProtoGauss, DT_WeaponProtoGauss)
END_SEND_TABLE()

BEGIN_DATADESC(CWeaponProtoGauss)
DEFINE_FIELD(m_bCharging, FIELD_BOOLEAN),
DEFINE_FIELD(m_flChargeStartTime, FIELD_TIME),
DEFINE_FIELD(m_flNextRechargeTick, FIELD_TIME),
DEFINE_FIELD(m_flRechargeDelayEndsAt, FIELD_TIME),
DEFINE_FIELD(m_nBurstShotsRemaining, FIELD_INTEGER),
DEFINE_FIELD(m_flNextBurstShotTime, FIELD_TIME),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: Constructor -- starts fully charged.
//-----------------------------------------------------------------------------
CWeaponProtoGauss::CWeaponProtoGauss(void)
{
	m_bReloadsSingly = false;
	m_bFiresUnderwater = false;

	m_bCharging = false;
	m_flChargeStartTime = 0.0f;
	m_flNextRechargeTick = 0.0f;
	m_flRechargeDelayEndsAt = 0.0f;

	m_nBurstShotsRemaining = 0;
	m_flNextBurstShotTime = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator)
{
	BaseClass::Operator_HandleAnimEvent(pEvent, pOperator);
}

//-----------------------------------------------------------------------------
// Purpose: Passive self-recharge, runs every frame regardless of firing.
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::Recharge(void)
{
	// Charging up the secondary diverts energy into that shot instead of
	// the reserve -- no recharge ticks while it's actively charging.
	if (m_bCharging)
		return;

	int nMaxCharge = sk_protogauss_max_charge.GetInt();

	if (m_iClip1 >= nMaxCharge)
	{
		m_iClip1 = nMaxCharge;	// clamp in case the convar was lowered mid-game
		return;
	}

	// Don't even start recharging until the post-fire delay has fully
	// elapsed -- this resets every time a shot is fired (see PrimaryAttack()
	// and FireChargedBurst()), so continuous firing keeps recharge stalled
	// the whole time.
	if (gpGlobals->curtime < m_flRechargeDelayEndsAt)
		return;

	if (gpGlobals->curtime < m_flNextRechargeTick)
		return;

	m_iClip1++;

	float flRate = sk_protogauss_recharge_rate.GetFloat();
	m_flNextRechargeTick = gpGlobals->curtime + (flRate > 0.0f ? (1.0f / flRate) : 1.0f);
}

//-----------------------------------------------------------------------------
// Purpose: mouse1 -- starts a 3-shot burst. The shots themselves fire one
// at a time from ItemPostFrame() via FireNextBurstShot(), spaced out by
// sk_protogauss_primary_burst_shot_delay, instead of all firing this frame.
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::PrimaryAttack(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());

	if (!pPlayer)
	{
		return;
	}

	// Already mid-burst -- ignore repeated trigger presses/held fire until
	// the current burst finishes.
	if (m_nBurstShotsRemaining > 0)
		return;

	if (m_iClip1 <= 0)
	{
		// Truly nothing left -- only refuse outright when there's
		// literally zero charge to fire with.
		WeaponSound(EMPTY);
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.2f;
		return;
	}

	int nBurstCount = sk_protogauss_primary_burst_count.GetInt();

	// Fire as many shots as we can afford -- if there's less than a full
	// burst left (e.g. 2 charge remaining instead of 3), fire that many
	// instead of refusing outright.
	m_nBurstShotsRemaining = MIN(nBurstCount, m_iClip1);
	m_flNextBurstShotTime = gpGlobals->curtime;	// fire the first shot on the very next ItemPostFrame() tick
}

//-----------------------------------------------------------------------------
// Purpose: Fires a single shot of the in-progress primary burst, and
// schedules the next one (or finalizes refire timing if this was the last).
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::FireNextBurstShot(void)
{
	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());
	if (!pPlayer)
	{
		m_nBurstShotsRemaining = 0;
		return;
	}

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired(pPlayer, true, GetClassname());

	WeaponSound(SINGLE);
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim(ACT_VM_PRIMARYATTACK);
	pPlayer->SetAnimation(PLAYER_ATTACK1);

	m_iClip1--;

	// Reset the recharge delay on every individual shot, same as before.
	m_flRechargeDelayEndsAt = gpGlobals->curtime + sk_protogauss_recharge_delay.GetFloat();

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

	pPlayer->FireBullets(1, vecSrc, vecAiming, VECTOR_CONE_3DEGREES, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);

	pPlayer->SetMuzzleFlashTime(gpGlobals->curtime + 0.5);
	pPlayer->ViewPunch(QAngle(-1, random->RandomFloat(-1, 1), 0));

	CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), 600, 0.2, GetOwner());

	m_nBurstShotsRemaining--;

	if (m_nBurstShotsRemaining > 0)
	{
		m_flNextBurstShotTime = gpGlobals->curtime + sk_protogauss_primary_burst_shot_delay.GetFloat();
	}
	else
	{
		// Burst complete -- now the real post-burst refire delay applies.
		m_flNextPrimaryAttack = gpGlobals->curtime + sk_protogauss_primary_refire.GetFloat();
		m_flNextSecondaryAttack = gpGlobals->curtime + sk_protogauss_primary_refire.GetFloat();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Begin the mouse2 hold-to-charge. Refuses (with a debounce so it
// doesn't spam every frame) if there isn't even enough charge stored to
// attempt the eventual 10-round burst.
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::StartCharge(void)
{
	if (m_iClip1 <= 0)
	{
		// Truly nothing left -- only refuse outright when there's
		// literally zero charge available to start charging with.
		WeaponSound(EMPTY);
		m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;
		return;
	}

	m_bCharging = true;
	m_flChargeStartTime = gpGlobals->curtime;

	// TODO: soundscript entry "Weapon_ProtoGauss.ChargeUp" needs to exist
	// (scripts/game_sounds_weapons.txt or a dedicated weapon_protogauss.txt).
	EmitSound("Weapon_ProtoGauss.ChargeUp");

	SendWeaponAnim(ACT_VM_PULLBACK);	// reused as a charge-up pose
}

//-----------------------------------------------------------------------------
// Purpose: mouse2 released before fully charged -- cancel, no shot fired.
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::CancelCharge(void)
{
	if (!m_bCharging)
		return;

	m_bCharging = false;

	// TODO: soundscript entry "Weapon_ProtoGauss.ChargeCancel".
	EmitSound("Weapon_ProtoGauss.ChargeCancel");

	m_flNextSecondaryAttack = gpGlobals->curtime + 0.2f;
}

//-----------------------------------------------------------------------------
// Purpose: Charge completed -- fires the 10-round burst. Re-checks charge
// in case primary attacks drained it while the charge was building.
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::FireChargedBurst(void)
{
	m_bCharging = false;

	CBasePlayer* pPlayer = ToBasePlayer(GetOwner());
	if (!pPlayer)
		return;

	int nBurstCount = sk_protogauss_secondary_burst_count.GetInt();
	int nChargeCost = sk_protogauss_secondary_charge_cost.GetInt();

	if (m_iClip1 <= 0)
	{
		// Drained to literally nothing by primary fire while charging --
		// this is the only case that actually cancels the shot.
		WeaponSound(EMPTY);
		m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;
		return;
	}

	// Spend whatever charge cost we can actually afford, and fire however
	// many bullets that leftover charge can actually back -- both computed
	// from what was available BEFORE spending, so a low-charge shot fires
	// fewer bullets instead of always firing the full designed count.
	int nAvailable = m_iClip1;
	int nActualCost = MIN(nChargeCost, nAvailable);
	int nBulletsToFire = MIN(nBurstCount, nAvailable);

	m_iClip1 -= nActualCost;

	// Same recharge-delay reset as the primary burst -- either attack
	// resets the 3-second (default) window before regen resumes.
	m_flRechargeDelayEndsAt = gpGlobals->curtime + sk_protogauss_recharge_delay.GetFloat();

	// TODO: needs a real "special1" sound entry in the weapon soundscript
	// for the big burst -- currently falls back to whatever WeaponSound()
	// does if SPECIAL1 isn't defined for this weapon.
	WeaponSound(SPECIAL1);
	pPlayer->DoMuzzleFlash();

	SendWeaponAnim(ACT_VM_SECONDARYATTACK);
	pPlayer->SetAnimation(PLAYER_ATTACK1);

	m_flNextPrimaryAttack = gpGlobals->curtime + sk_protogauss_secondary_refire.GetFloat();
	m_flNextSecondaryAttack = gpGlobals->curtime + sk_protogauss_secondary_refire.GetFloat();

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector(AUTOAIM_SCALE_DEFAULT);

	// Single native multi-pellet call, same pattern the stock shotgun uses
	// (FireBullets( pelletCount, ..., coneVector, ... )) -- the engine
	// handles per-pellet randomization internally, so there's no need to
	// manually compute per-bullet angles ourselves.
	float flSpreadRad = DEG2RAD(sk_protogauss_secondary_spread_degrees.GetFloat());
	float flConeVal = tanf(flSpreadRad);
	Vector vecCone(flConeVal, flConeVal, flConeVal);

	pPlayer->FireBullets(nBulletsToFire, vecSrc, vecAiming, vecCone, MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 0);

	pPlayer->ViewPunch(QAngle(-4, random->RandomFloat(-3, 3), 0));
	CSoundEnt::InsertSound(SOUND_COMBAT, GetAbsOrigin(), 800, 0.3, GetOwner());
}

//-----------------------------------------------------------------------------
// Purpose: Drives recharge every frame, and the mouse2 hold/charge/fire
// state machine (same pattern as the wrench's mouse2 charge attack).
//-----------------------------------------------------------------------------
void CWeaponProtoGauss::ItemPostFrame(void)
{
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
	{
		BaseClass::ItemPostFrame();
		return;
	}

	Recharge();

	// A primary burst is playing out over successive frames -- fire the
	// next scheduled shot and lock out everything else until it finishes.
	if (m_nBurstShotsRemaining > 0)
	{
		if (gpGlobals->curtime >= m_flNextBurstShotTime)
		{
			FireNextBurstShot();
		}

		WeaponIdle();
		return;
	}

	bool bAttack2Held = (pOwner->m_nButtons & IN_ATTACK2) != 0;

	if (bAttack2Held && !m_bCharging && gpGlobals->curtime >= m_flNextSecondaryAttack)
	{
		StartCharge();
	}
	else if (!bAttack2Held && m_bCharging)
	{
		CancelCharge();
	}
	else if (bAttack2Held && m_bCharging)
	{
		if (gpGlobals->curtime - m_flChargeStartTime >= sk_protogauss_secondary_charge_time.GetFloat())
		{
			FireChargedBurst();
		}
	}

	// While charging, lock out primary attack -- same reasoning as the
	// wrench: holding mouse2 shouldn't let mouse1 interrupt the charge pose.
	if (m_bCharging)
	{
		WeaponIdle();
		return;
	}

	BaseClass::ItemPostFrame();
}