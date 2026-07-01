//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef GRENADE_HOPWIRE_H
#define GRENADE_HOPWIRE_H
#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"
#include "Sprite.h"

extern ConVar hopwire_trap;

class CGravityVortexController;
class CSpriteTrail;

class CGrenadeHopwire : public CBaseGrenade
{
	DECLARE_CLASS(CGrenadeHopwire, CBaseGrenade);
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	void	Spawn(void);
	void	Precache(void);
	bool	CreateVPhysics(void);
	void	SetTimer(float timer);
	void	SetVelocity(const Vector& velocity, const AngularImpulse& angVelocity);
	void	Detonate(void);
	virtual void OnRestore(void);

	void	EndThink(void);		// Last think before going away
	void	CombatThink(void);	// Makes the main explosion go off
	void	BeepThink(void);		// Runs independently of the above via a named context, beeping on an accelerating schedule until Detonate() fires

protected:

	void	KillStriders(void);
	void	CreateEffects(void);	// cyan glow + light trail

	CHandle<CGravityVortexController>	m_hVortexController;
	CHandle<CSprite>		m_pMainGlow;
	CHandle<CSpriteTrail>	m_pGlowTrail;

	float	m_flDetonateTime;	// when Detonate() will fire, used by BeepThink() to know how close it is
};

extern CBaseGrenade* HopWire_Create(const Vector& position, const QAngle& angles, const Vector& velocity, const AngularImpulse& angVelocity, CBaseEntity* pOwner, float timer);

#endif // GRENADE_HOPWIRE_H