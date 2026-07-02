//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client-side counterpart for weapon_knife.cpp's CThrownKnife.
// Registers a real stencil-based outline (via CGlowObjectManager, the same
// system this project already uses elsewhere) for the throwing player
// only -- not a light/glow sprite.
//
//=============================================================================//

#include "cbase.h"
#include "c_baseanimating.h"
#include "glow_outline_effect.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class C_ThrownKnife : public C_BaseAnimating
{
	DECLARE_CLASS(C_ThrownKnife, C_BaseAnimating);
	DECLARE_CLIENTCLASS();

public:
	C_ThrownKnife(void);
	virtual ~C_ThrownKnife(void);

	virtual void OnDataChanged(DataUpdateType_t updateType);

private:
	void UpdateGlowState(void);

	EHANDLE	m_hOriginalOwner;

#ifdef GLOWS_ENABLE
	CGlowObject* m_pGlowEffect;
#endif
};

IMPLEMENT_CLIENTCLASS_DT(C_ThrownKnife, DT_ThrownKnife, CThrownKnife)
RecvPropEHandle(RECVINFO(m_hOriginalOwner)),
END_RECV_TABLE()

C_ThrownKnife::C_ThrownKnife(void)
{
#ifdef GLOWS_ENABLE
	m_pGlowEffect = NULL;
#endif
}

C_ThrownKnife::~C_ThrownKnife(void)
{
#ifdef GLOWS_ENABLE
	if (m_pGlowEffect)
	{
		delete m_pGlowEffect;
		m_pGlowEffect = NULL;
	}
#endif
}

void C_ThrownKnife::OnDataChanged(DataUpdateType_t updateType)
{
	BaseClass::OnDataChanged(updateType);
	UpdateGlowState();
}

//-----------------------------------------------------------------------------
// Purpose: Only the throwing player sees the outline -- everyone else's
// client just sees the plain model with no special effect.
//
// NOTE: this whole thing is a no-op if GLOWS_ENABLE isn't defined in your
// build -- glow_outline_effect.h wraps its entire contents in that guard,
// so CGlowObject/CGlowObjectManager don't exist at all otherwise.
//-----------------------------------------------------------------------------
void C_ThrownKnife::UpdateGlowState(void)
{
#ifdef GLOWS_ENABLE
	C_BasePlayer* pLocalPlayer = C_BasePlayer::GetLocalPlayer();

	bool bShouldGlow = (pLocalPlayer != NULL && m_hOriginalOwner.Get() == pLocalPlayer);

	if (bShouldGlow && !m_pGlowEffect)
	{
		// Soft white outline, same as the squad companions/friendly
		// scanner. bRenderWhenOccluded = true means it outlines through
		// walls too, which is the point -- makes a thrown knife findable
		// even out of direct line of sight.
		m_pGlowEffect = new CGlowObject(this, Vector(1.0f, 1.0f, 1.0f), 0.5f, true, true);
	}
	else if (!bShouldGlow && m_pGlowEffect)
	{
		delete m_pGlowEffect;
		m_pGlowEffect = NULL;
	}
#endif
}