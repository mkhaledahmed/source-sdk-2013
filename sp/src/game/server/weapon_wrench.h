//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//

#ifndef WEAPON_WRENCH_H
#define WEAPON_WRENCH_H

#include "basebludgeonweapon.h"

#if defined( _WIN32 )
#pragma once
#endif

#ifdef HL2MP
#error weapon_crowbar.h must not be included in hl2mp. The windows compiler will use the wrong class elsewhere if it is.
#endif

#define	WRENCH_RANGE	75.0f
#define	WRENCH_REFIRE	1.5f

//-----------------------------------------------------------------------------
// CWeaponWrench
//-----------------------------------------------------------------------------

class CWeaponWrench : public CBaseHLBludgeonWeapon
{
public:
	DECLARE_CLASS(CWeaponWrench, CBaseHLBludgeonWeapon);

	DECLARE_SERVERCLASS();
	DECLARE_ACTTABLE();

	CWeaponWrench();

	float		GetRange(void) { return	WRENCH_RANGE; }
	float		GetFireRate(void) { return	WRENCH_REFIRE; }

	void		AddViewKick(void);
	float		GetDamageForActivity(Activity hitActivity);

	virtual int WeaponMeleeAttack1Condition(float flDot, float flDist);
	void		SecondaryAttack(void) { return; }

	// mouse2 hold-charge / mouse3 fixup stand-in
	virtual void ItemPostFrame(void);

	// Animation event
	virtual void Operator_HandleAnimEvent(animevent_t* pEvent, CBaseCombatCharacter* pOperator);

#ifdef MAPBASE
	// Don't use backup activities
	acttable_t* GetBackupActivityList() { return NULL; }
	int				GetBackupActivityListCount() { return 0; }
#endif

private:
	float m_flCurrentRefire; //Khal change fire rate on hi

	// Animation event handlers
	void HandleAnimEventMeleeHit(animevent_t* pEvent, CBaseCombatCharacter* pOperator);

	// mouse2 charge attack
	void	StartChargeAttack(void);
	void	ReleaseChargeAttack(void);
	bool	m_bChargingAttack;
	float	m_flChargeStartTime;
	float	m_flAccumulatedChargeDamage;

	// mouse3 fixup stand-in
	void	FixupAttack(void);
};

#endif // WEAPON_WRENCH_H