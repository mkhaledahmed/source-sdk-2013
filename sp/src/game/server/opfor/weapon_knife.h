//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Knife -- fast melee weapon with a backstab insta-kill, and a
// secondary charge-and-throw attack. Player only ever has one knife: while
// it's thrown and not yet recovered, both attacks are disabled.
//
//=============================================================================//

#ifndef WEAPON_KNIFE_H
#define WEAPON_KNIFE_H
#ifdef _WIN32
#pragma once
#endif

#include "basebludgeonweapon.h"

class CWeaponKnife;

//-----------------------------------------------------------------------------
// The knife itself once thrown -- movement/collision pattern matches
// weapon_crossbow.cpp's CCrossbowBolt (MOVECOLLIDE_FLY_CUSTOM, solid mask
// override, glancing-hit reflection), with recovery and damage falloff
// added on top.
//-----------------------------------------------------------------------------
class CThrownKnife : public CBaseAnimating
{
	DECLARE_CLASS( CThrownKnife, CBaseAnimating );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	void	Spawn( void );
	void	Precache( void );
	void	KnifeTouch( CBaseEntity *pOther );
	void	KnifeThink( void );
	void	KnifeStuckThink( void );	// NEW: think while stuck to parent
	unsigned int PhysicsSolidMaskForEntity( void ) const;

	static CThrownKnife *Create( const Vector &vecOrigin, const Vector &vecVelocity, CBasePlayer *pOwner, CWeaponKnife *pWeapon );

	// Networked so the client-side C_ThrownKnife (c_thrown_knife.cpp) can
	// tell whether the local player is the one who threw it, and only
	// show the outline to them. CNetworkHandle (not a plain EHANDLE) is
	// required here -- same root cause as an earlier networking error in
	// this project: SendPropEHandle needs the matching network-var
	// plumbing that only this macro generates.
	CNetworkHandle( CBaseEntity, m_hOriginalOwner );

private:
	Vector					m_vecThrowOrigin;	// for damage falloff-by-distance-traveled
	float					m_flThrowTime;		// for the recovery delay
	bool					m_bStuck;
	CHandle<CWeaponKnife>	m_hSourceWeapon;
	CHandle<CBaseEntity>	m_hStuckParent;		// NEW: track what we're stuck to
};

//-----------------------------------------------------------------------------
// CWeaponKnife
//-----------------------------------------------------------------------------
class CWeaponKnife : public CBaseHLBludgeonWeapon
{
public:
	DECLARE_CLASS( CWeaponKnife, CBaseHLBludgeonWeapon );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_ACTTABLE();

	CWeaponKnife( void );

	float		GetRange( void );
	float		GetFireRate( void );
	float		GetDamageForActivity( Activity hitActivity );

	void		PrimaryAttack( void );
	void		SecondaryAttack( void ) { return; }	// handled entirely via ItemPostFrame -- same pattern as the wrench's charge attack
	void		ItemPostFrame( void );
	bool		Deploy( void );
	bool		HasAnyAmmo( void );

	// Called by the thrown knife when the original owner touches it again.
	void		NotifyKnifeRecovered( void );
	void		NotifyKnifeRecovered( CBasePlayer *pOwner );	// NEW: Overload that takes owner

	// NEW: Store the owner reference for recovery
	void		SetOriginalOwner( CBasePlayer *pOwner ) { m_hOriginalOwner = pOwner; }

private:
	void		UpdateBodygroups( void );
	bool		IsBackstab( CBaseEntity *pTarget, CBasePlayer *pAttacker );

	void		StartCharge( void );
	void		UpdateChargeEffects( CBasePlayer *pOwner );
	void		ReleaseThrow( void );

	bool		m_bCharging;
	float		m_flChargeStartTime;
	bool		m_bThrown;	// true from the moment it's thrown until NotifyKnifeRecovered() fires
	CHandle<CBasePlayer>	m_hOriginalOwner;	// NEW: Track the owner for recovery
};

#endif // WEAPON_KNIFE_H