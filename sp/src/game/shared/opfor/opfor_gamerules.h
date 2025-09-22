//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game rules for Half-Life 2.
//
//=============================================================================//

#ifndef OPFOR_GAMERULES_H
#define OPFOR_GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "gamerules.h"
#include "singleplay_gamerules.h"
#include "hl2_gamerules.h"

#ifndef OPFOR_DLL
#include "opfor_shareddefs.h"
#endif

#ifdef CLIENT_DLL
	#define COpposingForce C_OpposingForce
	#define COpposingForceProxy C_OpposingForceProxy
#endif

#if MAPBASE && GAME_DLL
#define FRIENDLY_FIRE_GLOBALNAME "friendly_fire_override"
#endif


class COpposingForceProxy : public CGameRulesProxy
{
public:
	DECLARE_CLASS( COpposingForceProxy, CGameRulesProxy );
	DECLARE_NETWORKCLASS();

#if defined(MAPBASE) && defined(GAME_DLL)
	bool KeyValue( const char *szKeyName, const char *szValue );
	bool GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen );

	virtual int	Save( ISave &save );
	virtual int	Restore( IRestore &restore );
	virtual void UpdateOnRemove();

	// Inputs
	void InputEpisodicOn( inputdata_t &inputdata );
	void InputEpisodicOff( inputdata_t &inputdata );
	void InputSetFriendlyFire( inputdata_t &inputdata );
	void InputSetDefaultCitizenType( inputdata_t &inputdata );
	void InputSetLegacyFlashlight( inputdata_t &inputdata );
	void InputSetPlayerSquadAutosummon( inputdata_t &inputdata );
	void InputSetStunstickPickupBehavior( inputdata_t &inputdata );
	void InputSetAllowSPRespawn( inputdata_t &inputdata );

	// Gamerules classes don't seem to support datadescs, so the OPFOR_gamerules entity takes the current values
	// from the actual gamerules and saves them in the entity itself, where they're saved via the entity's own datadesc.
	// When the save is loaded, the entity sets the main gamerules values according to what was saved.
	int m_save_DefaultCitizenType;
	char m_save_LegacyFlashlight;
	bool m_save_PlayerSquadAutosummonDisabled;
	int m_save_StunstickPickupBehavior;
	bool m_save_AllowSPRespawn;

	DECLARE_DATADESC();
#endif
};


class COpposingForce : public CSingleplayRules
{
public:
	DECLARE_CLASS( COpposingForce, CSingleplayRules );

	// Damage Query Overrides.
	virtual bool			Damage_IsTimeBased( int iDmgType );
	// TEMP:
	virtual int				Damage_GetTimeBased( void );
	
	virtual bool			ShouldCollide( int collisionGroup0, int collisionGroup1 );
	virtual bool			ShouldUseRobustRadiusDamage(CBaseEntity *pEntity);
#ifndef CLIENT_DLL
	virtual bool			ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target );
	virtual float			GetAutoAimScale( CBasePlayer *pPlayer );
	virtual float			GetAmmoQuantityScale( int iAmmoIndex );
	virtual void			LevelInitPreEntity();
#endif

#ifdef MAPBASE_VSCRIPT
	virtual void			RegisterScriptFunctions( void );
#endif

private:
	// Rules change for the mega physgun
	CNetworkVar( bool, m_bMegaPhysgun );

#ifdef CLIENT_DLL

	DECLARE_CLIENTCLASS_NOBASE(); // This makes datatables able to access our private vars.

#else

	DECLARE_SERVERCLASS_NOBASE(); // This makes datatables able to access our private vars.

	COpposingForce();
	virtual ~COpposingForce() {}

	virtual void			Think( void );

	virtual bool			ClientCommand( CBaseEntity *pEdict, const CCommand &args );
	virtual void			PlayerSpawn( CBasePlayer *pPlayer );

	virtual void			InitDefaultAIRelationships( void );
	virtual const char*		AIClassText(int classType);
	virtual const char *GetGameDescription( void ) { return "Half-Life 2"; }

	// Ammo
	virtual void			PlayerThink( CBasePlayer *pPlayer );
	virtual float			GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType );

	virtual bool			ShouldBurningPropsEmitLight();
public:

	bool AllowDamage( CBaseEntity *pVictim, const CTakeDamageInfo &info );

	bool	NPC_ShouldDropGrenade( CBasePlayer *pRecipient );
	bool	NPC_ShouldDropHealth( CBasePlayer *pRecipient );
	void	NPC_DroppedHealth( void );
	void	NPC_DroppedGrenade( void );
	bool	MegaPhyscannonActive( void ) { return m_bMegaPhysgun;	}
	
	virtual bool IsAlyxInDarknessMode();

#ifdef MAPBASE
	int				GetDefaultCitizenType();
	void			SetDefaultCitizenType(int val);

	ThreeState_t	GlobalFriendlyFire();
	void			SetGlobalFriendlyFire(ThreeState_t val);

	bool			AutosummonDisabled();
	void			SetAutosummonDisabled(bool toggle);

	int				GetStunstickPickupBehavior();
	void			SetStunstickPickupBehavior(int val);

	virtual bool	AllowSPRespawn();
	void			SetAllowSPRespawn( bool toggle );
#endif

private:

	float	m_flLastHealthDropTime;
	float	m_flLastGrenadeDropTime;

#ifdef MAPBASE
	int		m_DefaultCitizenType;
	bool	m_bPlayerSquadAutosummonDisabled;
	int		m_StunstickPickupBehavior;
	bool	m_bAllowSPRespawn;
#endif

	void AdjustPlayerDamageTaken( CTakeDamageInfo *pInfo );
	float AdjustPlayerDamageInflicted( float damage );

	int						DefaultFOV( void ) { return 75; }
#endif
};


//-----------------------------------------------------------------------------
// Gets us at the Half-Life 2 game rules
//-----------------------------------------------------------------------------
inline COpposingForce* OPFORGameRules()
{
#if ( !defined( OPFOR_DLL ) && !defined( OPFOR_CLIENT_DLL ) ) || defined( OPFORMP )
	Assert( 0 );	// g_pGameRules is NOT an instance of COpposingForce and bad things happen
#endif

	return static_cast<COpposingForce*>(g_pGameRules);
}



#endif // OPFOR_GAMERULES_H
