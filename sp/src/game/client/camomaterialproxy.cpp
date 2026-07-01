//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"
// identifier was truncated to '255' characters in the debug information
#pragma warning(disable: 4786)

#include "proxyentity.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "bitmap/tgaloader.h"
#include "view.h"
#include "datacache/idatacache.h"
#include "materialsystem/imaterial.h"
#include "vtf/vtf.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Set to 1 in-game to print the first few computed camo palette RGB values
// every time GenerateCamoTexture() runs. Leave at 0 normally -- OnBind() can
// fire every frame, so this will spam the console badly if left on.
ConVar camo_debug_colors("camo_debug_colors", "0");

class CCamoMaterialProxy;

class CCamoTextureRegen : public ITextureRegenerator
{
public:
	CCamoTextureRegen(CCamoMaterialProxy* pProxy) : m_pProxy(pProxy) {}
	virtual void RegenerateTextureBits(ITexture* pTexture, IVTFTexture* pVTFTexture, Rect_t* pSubRect);
	virtual void Release() {}

private:
	CCamoMaterialProxy* m_pProxy;
};

class CCamoMaterialProxy : public CEntityMaterialProxy
{
public:
	CCamoMaterialProxy();
	virtual ~CCamoMaterialProxy();
	virtual bool Init(IMaterial* pMaterial, KeyValues* pKeyValues);
	virtual void OnBind(C_BaseEntity* pC_BaseEntity);
	virtual IMaterial* GetMaterial();

	// Procedurally generates the camo texture...
	void GenerateCamoTexture(ITexture* pTexture, IVTFTexture* pVTFTexture);

protected:
#if 0
	virtual void SetInstanceDataSize(int size);
	virtual void* FindInstanceData(C_BaseEntity* pEntity);
	virtual void* AllocateInstanceData(C_BaseEntity* pEntity);
#endif

private:
	void LoadCamoPattern(void);
	void GenerateRandomPointsInNormalizedCube(void);
	void GetColors(Vector& lighting, Vector& base, int index,
		const Vector& boxMin, const Vector& boxExtents,
		const Vector& forward, const Vector& right, const Vector& up,
		const Vector& entityPosition);
	// this needs to go in a base class

private:
#if 0
	// stuff that needs to be in a base class.
	struct InstanceData_t
	{
		C_BaseEntity* pEntity;
		void* data;
		struct InstanceData_s* next;
	};

	struct CamoInstanceData_t
	{
		int dummy;
	};
#endif

	unsigned char* m_pCamoPatternImage;

#if 0
	int m_InstanceDataSize;
	InstanceData_t* m_InstanceDataListHead;
#endif

	IMaterial* m_pMaterial;
	IMaterialVar* m_pCamoTextureVar;
	// NOTE: the camo pattern texture path comes from the "Proxies" -> "Camo"
	// block in the VMT (e.g. "camopatterntexture" "models/foo/bar.tga"), which
	// is handed to Init() via pKeyValues -- it is NOT a $-prefixed material var.
	char m_szCamoPatternTexturePath[MAX_PATH];
	Vector* m_pointsInNormalizedBox; // [m_CamoPatternNumColors]

	int m_CamoPatternNumColors;
	int m_CamoPatternWidth;
	int m_CamoPatternHeight;
#if 0
	cache_user_t m_camoImageDataCache;
#endif
	unsigned char m_CamoPalette[256][3];
	// these represent that part of the entitiy's bounding box that we 
	// want to cast rays through to get colors for the camo
	Vector m_SubBoundingBoxMin; // normalized
	Vector m_SubBoundingBoxMax; // normalized

	CCamoTextureRegen m_TextureRegen;
	C_BaseEntity* m_pEnt;

	// Deferred until first bind, so the filesystem is guaranteed to be
	// ready before we try to load the camo pattern TGA off disk.
	bool m_bPatternLoaded;
};


void CCamoTextureRegen::RegenerateTextureBits(ITexture* pTexture, IVTFTexture* pVTFTexture, Rect_t* pSubRect)
{
	m_pProxy->GenerateCamoTexture(pTexture, pVTFTexture);
}


#pragma warning (disable:4355)

CCamoMaterialProxy::CCamoMaterialProxy() : m_TextureRegen(this)
{
#if 0
	m_InstanceDataSize = 0;
#endif
#if 0
	memset(&m_camoImageDataCache, 0, sizeof(m_camoImageDataCache));
#endif
	m_pointsInNormalizedBox = NULL;
#if 0
	m_InstanceDataListHead = NULL;
#endif
	m_pCamoPatternImage = NULL;
	m_pMaterial = NULL;
	m_pCamoTextureVar = NULL;
	m_szCamoPatternTexturePath[0] = '\0';
	m_pointsInNormalizedBox = NULL;
	m_pEnt = NULL;
	m_bPatternLoaded = false;
}

#pragma warning (default:4355)

CCamoMaterialProxy::~CCamoMaterialProxy()
{
#if 0
	InstanceData_t* curr = m_InstanceDataListHead;
	while (curr)
	{
		InstanceData_t* next;
		next = curr->next;
		delete curr;
		curr = next;
	}
	m_InstanceDataListHead = NULL;
#endif

	// Disconnect the texture regenerator...
	if (m_pCamoTextureVar)
	{
		ITexture* pCamoTexture = m_pCamoTextureVar->GetTextureValue();
		if (pCamoTexture)
			pCamoTexture->SetTextureRegenerator(NULL);
	}

	delete m_pCamoPatternImage;
	delete m_pointsInNormalizedBox;
}


#if 0
void CCamoMaterialProxy::SetInstanceDataSize(int size)
{
	m_InstanceDataSize = size;
}
#endif

#if 0
void* CCamoMaterialProxy::FindInstanceData(C_BaseEntity* pEntity)
{
	InstanceData_t* curr = m_InstanceDataListHead;
	while (curr)
	{
		if (pEntity == curr->pEntity)
		{
			return curr->data;
		}
		curr = curr->next;
	}
	return NULL;
}
#endif

#if 0
void* CCamoMaterialProxy::AllocateInstanceData(C_BaseEntity* pEntity)
{
	InstanceData_t* newData = new InstanceData_t;
	newData->pEntity = pEntity;
	newData->next = m_InstanceDataListHead;
	m_InstanceDataListHead = newData;
	newData->data = new unsigned char[m_InstanceDataSize];
	return newData->data;
}
#endif

static bool ParseVMTVectorString(const char* pString, Vector* pOut)
{
	if (!pString || !pString[0])
		return false;

	float x, y, z;
	// Source VMT vectors look like "[ 0.00 1.00 0.50 ]" -- accept that form,
	// and a plain "x y z" form too, just in case.
	if (sscanf(pString, " [ %f %f %f ]", &x, &y, &z) == 3 ||
		sscanf(pString, " %f %f %f", &x, &y, &z) == 3)
	{
		pOut->Init(x, y, z);
		return true;
	}
	return false;
}

bool CCamoMaterialProxy::Init(IMaterial* pMaterial, KeyValues* pKeyValues)
{
	// remember what material we belong to.
	m_pMaterial = pMaterial;

	// get pointer to the base texture material var (this one IS a normal
	// $-prefixed shader parameter, so FindVar is correct here).
	bool found;
	m_pCamoTextureVar = m_pMaterial->FindVar("$basetexture", &found);
	if (!found)
	{
		Warning("Camo proxy: material \"%s\" has no $basetexture -- proxy needs one to overwrite.\n",
			m_pMaterial->GetName());
		m_pCamoTextureVar = NULL;
		return false;
	}
	ITexture* pCamoTexture = m_pCamoTextureVar->GetTextureValue();
	if (pCamoTexture)
		pCamoTexture->SetTextureRegenerator(&m_TextureRegen);

	// Everything else comes from the proxy's own parameter block, e.g.:
	//   "Proxies" { "Camo" { "camopatterntexture" "..." "camoboundingboxmin" "[...]" } }
	// NOT from top-level $ material vars -- pKeyValues here IS that "Camo" block.
	const char* pPatternPath = pKeyValues->GetString("camopatterntexture", NULL);
	if (!pPatternPath || !pPatternPath[0])
	{
		Warning("Camo proxy: material \"%s\" has no \"camopatterntexture\" in its Proxies->Camo block.\n",
			m_pMaterial->GetName());
		m_pCamoTextureVar = NULL;
		return false;
	}
	Q_strncpy(m_szCamoPatternTexturePath, pPatternPath, sizeof(m_szCamoPatternTexturePath));

	if (!ParseVMTVectorString(pKeyValues->GetString("camoboundingboxmin", NULL), &m_SubBoundingBoxMin))
	{
		m_SubBoundingBoxMin = Vector(0.0f, 0.0f, 0.0f);
	}

	if (!ParseVMTVectorString(pKeyValues->GetString("camoboundingboxmax", NULL), &m_SubBoundingBoxMax))
	{
		m_SubBoundingBoxMax = Vector(1.0f, 1.0f, 1.0f);
	}

	// LoadCamoPattern() / GenerateRandomPointsInNormalizedCube() intentionally
	// NOT called here -- see OnBind(), which defers them until first bind so
	// the filesystem is guaranteed to be mounted.

	return true;
}

void CCamoMaterialProxy::GetColors(Vector& diffuseColor, Vector& baseColor, int index,
	const Vector& boxMin, const Vector& boxExtents,
	const Vector& forward, const Vector& right, const Vector& up,
	const Vector& entityPosition)
{
	Vector position, transformedPosition;

	// hack
//	m_pointsInNormalizedBox[index] = Vector( 0.5f, 0.5f, 1.0f );

	position[0] = m_pointsInNormalizedBox[index][0] * boxExtents[0] + boxMin[0];
	position[1] = m_pointsInNormalizedBox[index][1] * boxExtents[1] + boxMin[1];
	position[2] = m_pointsInNormalizedBox[index][2] * boxExtents[2] + boxMin[2];
	transformedPosition[0] = right[0] * position[0] + forward[0] * position[1] + up[0] * position[2];
	transformedPosition[1] = right[1] * position[0] + forward[1] * position[1] + up[1] * position[2];
	transformedPosition[2] = right[2] * position[0] + forward[2] * position[1] + up[2] * position[2];
	transformedPosition = transformedPosition + entityPosition;
	Vector direction = transformedPosition - CurrentViewOrigin();
	VectorNormalize(direction);
	direction = direction * (COORD_EXTENT * 1.74f);
	Vector endPoint = position + direction;

	// baseColor is already in gamma space
//	engine->TraceLineMaterialAndLighting( g_vecInstantaneousRenderOrigin, endPoint, diffuseColor, baseColor );
	engine->TraceLineMaterialAndLighting(transformedPosition, endPoint, diffuseColor, baseColor);

	// hack - optimize! - convert from linear to gamma space - this should be hidden
	diffuseColor[0] = pow(diffuseColor[0], 1.0f / 2.2f);
	diffuseColor[1] = pow(diffuseColor[1], 1.0f / 2.2f);
	diffuseColor[2] = pow(diffuseColor[2], 1.0f / 2.2f);

#if 0
	Msg("%f %f %f\n",
		diffuseColor[0],
		diffuseColor[1],
		diffuseColor[2]);
#endif

#if 0
	float max;
	max = diffuseColor[0];
	if (diffuseColor[1] > max)
	{
		max = diffuseColor[1];
	}
	if (diffuseColor[2] > max)
	{
		max = diffuseColor[2];
	}
	if (max > 1.0f)
	{
		max = 1.0f / max;
		diffuseColor = diffuseColor * max;
	}
#else
	if (diffuseColor[0] > 1.0f)
	{
		diffuseColor[0] = 1.0f;
	}
	if (diffuseColor[1] > 1.0f)
	{
		diffuseColor[1] = 1.0f;
	}
	if (diffuseColor[2] > 1.0f)
	{
		diffuseColor[2] = 1.0f;
	}
#endif
	// hack
	//baseColor = Vector( 1.0f, 1.0f, 1.0f );
	//diffuseColor = Vector( 1.0f, 1.0f, 1.0f );
}


//-----------------------------------------------------------------------------
// Procedurally generates the camo texture...
//-----------------------------------------------------------------------------
void CCamoMaterialProxy::GenerateCamoTexture(ITexture* pTexture, IVTFTexture* pVTFTexture)
{
	if (!m_pEnt)
		return;

#if 0
	CamoInstanceData_t* pInstanceData;
	pInstanceData = (CamoInstanceData_t*)FindInstanceData(pEnt);
	if (!pInstanceData)
	{
		pInstanceData = (CamoInstanceData_t*)AllocateInstanceData(pEnt);
		if (!pInstanceData)
		{
			return;
		}
		// init the instance data
	}
#endif

	Vector entityPosition;
	entityPosition = m_pEnt->GetAbsOrigin();

	QAngle entityAngles;
	entityAngles = m_pEnt->GetAbsAngles();

	// Get the bounding box for the entity
	Vector mins, maxs;
	mins = m_pEnt->WorldAlignMins();
	maxs = m_pEnt->WorldAlignMaxs();

	Vector traceDirection;
	Vector traceEnd;
	trace_t	traceResult;

	Vector forward, right, up;
	AngleVectors(entityAngles, &forward, &right, &up);

	Vector position, transformedPosition;
	Vector maxsMinusMins = maxs - mins;

	Vector diffuseColor[256];
	Vector baseColor;

	unsigned char camoPalette[256][3];
	// Calculate the camo palette
	//Msg( "start of loop\n" );
	int i;
	for (i = 0; i < m_CamoPatternNumColors; i++)
	{
		GetColors(diffuseColor[i], baseColor, i,
			mins, maxsMinusMins, forward, right, up, entityPosition);
#if 1
		camoPalette[i][0] = diffuseColor[i][0] * baseColor[0] * 255.0f;
		camoPalette[i][1] = diffuseColor[i][1] * baseColor[1] * 255.0f;
		camoPalette[i][2] = diffuseColor[i][2] * baseColor[2] * 255.0f;
#endif
#if 0
		camoPalette[i][0] = baseColor[0] * 255.0f;
		camoPalette[i][1] = baseColor[1] * 255.0f;
		camoPalette[i][2] = baseColor[2] * 255.0f;
#endif
#if 0
		camoPalette[i][0] = diffuseColor[i][0] * 255.0f;
		camoPalette[i][1] = diffuseColor[i][1] * 255.0f;
		camoPalette[i][2] = diffuseColor[i][2] * 255.0f;
#endif

		if (camo_debug_colors.GetBool() && i < 5)
		{
			Msg("Camo proxy: palette[%d] = diffuse(%.2f, %.2f, %.2f) x base(%.2f, %.2f, %.2f) -> RGB(%d, %d, %d)\n",
				i,
				diffuseColor[i][0], diffuseColor[i][1], diffuseColor[i][2],
				baseColor[0], baseColor[1], baseColor[2],
				camoPalette[i][0], camoPalette[i][1], camoPalette[i][2]);
		}
	}

	int width = pVTFTexture->Width();
	int height = pVTFTexture->Height();
	if (width != m_CamoPatternWidth || height != m_CamoPatternHeight)
	{
		Warning("Camo proxy: basetexture is %dx%d but pattern TGA (%s) is %dx%d -- they must match exactly.\n",
			width, height, m_szCamoPatternTexturePath, m_CamoPatternWidth, m_CamoPatternHeight);
		return;
	}

	unsigned char* imageData = pVTFTexture->ImageData(0, 0, 0);
	enum ImageFormat imageFormat = pVTFTexture->Format();

	// The basetexture must be some uncompressed format -- vtex won't reliably
	// give you IMAGE_FORMAT_RGB888 specifically (with $nocompress it usually
	// picks BGR888/BGRX8888/BGRA8888 instead), so support the common
	// uncompressed layouts here rather than fighting the texture compiler.
	int bytesPerPixel = 0;
	int rOff = 0, gOff = 0, bOff = 0;
	switch (imageFormat)
	{
	case IMAGE_FORMAT_RGB888:
		bytesPerPixel = 3; rOff = 0; gOff = 1; bOff = 2;
		break;
	case IMAGE_FORMAT_BGR888:
		bytesPerPixel = 3; bOff = 0; gOff = 1; rOff = 2;
		break;
	case IMAGE_FORMAT_RGBA8888:
		bytesPerPixel = 4; rOff = 0; gOff = 1; bOff = 2;
		break;
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_BGRX8888:
		bytesPerPixel = 4; bOff = 0; gOff = 1; rOff = 2;
		break;
	case IMAGE_FORMAT_ARGB8888:
		bytesPerPixel = 4; rOff = 1; gOff = 2; bOff = 3;
		break;
	case IMAGE_FORMAT_ABGR8888:
		bytesPerPixel = 4; bOff = 1; gOff = 2; rOff = 3;
		break;
	default:
		Warning("Camo proxy: basetexture format %d is not a supported uncompressed layout. "
			"Compile it with \"$nocompress\" \"1\" and no DXT compression.\n", (int)imageFormat);
		return;
	}
	int x, y;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			int offset = bytesPerPixel * (x + y * width);
			assert(offset < width * height * bytesPerPixel);
			int paletteID = m_pCamoPatternImage[x + y * width];
			assert(paletteID < 256);

			imageData[offset + rOff] = camoPalette[paletteID][0];
			imageData[offset + gOff] = camoPalette[paletteID][1];
			imageData[offset + bOff] = camoPalette[paletteID][2];
			// any 4th byte (alpha/padding) is left untouched deliberately --
			// for BGRX8888 it's unused, for *A8888 formats it preserves
			// whatever alpha the source basetexture was compiled with.
		}
	}
}


//-----------------------------------------------------------------------------
// Called when the texture is bound...
//-----------------------------------------------------------------------------
void CCamoMaterialProxy::OnBind(C_BaseEntity* pEntity)
{
	if (!m_pCamoTextureVar)
	{
		return;
	}

	// First bind: this is the earliest point we can safely guarantee the
	// filesystem is mounted, so do the deferred TGA load / point generation here
	// instead of in Init(). If either step fails, LoadCamoPattern() or
	// GenerateRandomPointsInNormalizedCube() will null out m_pCamoTextureVar,
	// and we'll just bail out on every future call via the check above.
	if (!m_bPatternLoaded)
	{
		m_bPatternLoaded = true; // only attempt this once, pass or fail

		LoadCamoPattern();

		if (m_pCamoTextureVar)
		{
			GenerateRandomPointsInNormalizedCube();
		}

		if (!m_pCamoTextureVar)
		{
			return;
		}
	}

	m_pEnt = pEntity;
	ITexture* pCamoTexture = m_pCamoTextureVar->GetTextureValue();
	if (pCamoTexture)
	{
		pCamoTexture->Download();
	}

	// Mark it so it doesn't get regenerated on task switch
	m_pEnt = NULL;
}

void CCamoMaterialProxy::LoadCamoPattern(void)
{
#if 0
	// hack - need to figure out a name to attach that isn't too long.
	m_pCamoPatternImage =
		(unsigned char*)datacache->FindByName(&m_camoImageDataCache, "camopattern");

	if (m_pCamoPatternImage)
	{
		// is already in the cache.
		return m_pCamoPatternImage;
	}
#endif

	enum ImageFormat indexImageFormat;
	int indexImageSize;
#ifndef _XBOX
	float dummyGamma;
	if (!TGALoader::GetInfo(m_szCamoPatternTexturePath,
		&m_CamoPatternWidth, &m_CamoPatternHeight, &indexImageFormat, &dummyGamma))
	{
		Warning("Camo proxy: can't get TGA info for pattern texture \"%s\" -- check the path is correct "
			"(relative to the game's materials search paths) and that the file exists.\n", m_szCamoPatternTexturePath);
		m_pCamoTextureVar = NULL;
		return;
	}
#else
	// xboxissue - no tga support, why implemented this way
	Assert(0);
	m_pCamoTextureVar = NULL;
	return;
#endif

	if (indexImageFormat != IMAGE_FORMAT_I8)
	{
		Warning("Camo proxy: pattern texture \"%s\" must be 8-bit indexed/greyscale (IMAGE_FORMAT_I8) -- "
			"got format %d instead. Re-export it as an 8-bit paletted/greyscale TGA.\n",
			m_szCamoPatternTexturePath, (int)indexImageFormat);
		m_pCamoTextureVar = NULL;
		return;
	}

	indexImageSize = ImageLoader::GetMemRequired(m_CamoPatternWidth, m_CamoPatternHeight, 1, indexImageFormat, false);
#if 0
	m_pCamoPatternImage = (unsigned char*)
		datacache->Alloc(&m_camoImageDataCache, indexImageSize, "camopattern");
#endif
	m_pCamoPatternImage = (unsigned char*)new unsigned char[indexImageSize];
	if (!m_pCamoPatternImage)
	{
		m_pCamoTextureVar = NULL;
		return;
	}

#ifndef _XBOX
	if (!TGALoader::Load(m_pCamoPatternImage, m_szCamoPatternTexturePath,
		m_CamoPatternWidth, m_CamoPatternHeight, IMAGE_FORMAT_I8, dummyGamma, false))
	{
		Warning("Camo proxy: TGALoader::Load failed loading pixel data for pattern texture \"%s\".\n",
			m_szCamoPatternTexturePath);
		m_pCamoTextureVar = NULL;
		return;
	}
#else
	// xboxissue - no tga support, why is the camo done this way?
	Assert(0);
#endif

	bool colorUsed[256];
	int colorRemap[256];
	// count the number of colors used in the image.
	int i;
	for (i = 0; i < 256; i++)
	{
		colorUsed[i] = false;
	}
	for (i = 0; i < indexImageSize; i++)
	{
		colorUsed[m_pCamoPatternImage[i]] = true;
	}
	m_CamoPatternNumColors = 0;
	for (i = 0; i < 256; i++)
	{
		if (colorUsed[i])
		{
			colorRemap[i] = m_CamoPatternNumColors;
			m_CamoPatternNumColors++;
		}
	}
	// remap the color to the beginning of the palette.
	for (i = 0; i < indexImageSize; i++)
	{
		m_pCamoPatternImage[i] = colorRemap[m_pCamoPatternImage[i]];
		// hack
//		m_pCamoPatternImage[i] = 0;
	}
}

void CCamoMaterialProxy::GenerateRandomPointsInNormalizedCube(void)
{
	m_pointsInNormalizedBox = new Vector[m_CamoPatternNumColors];
	if (!m_pointsInNormalizedBox)
	{
		m_pCamoTextureVar = NULL;
		return;
	}

	int i;
	for (i = 0; i < m_CamoPatternNumColors; i++)
	{
		m_pointsInNormalizedBox[i][0] = random->RandomFloat(m_SubBoundingBoxMin[0], m_SubBoundingBoxMax[0]);
		m_pointsInNormalizedBox[i][1] = random->RandomFloat(m_SubBoundingBoxMin[1], m_SubBoundingBoxMax[1]);
		m_pointsInNormalizedBox[i][2] = random->RandomFloat(m_SubBoundingBoxMin[2], m_SubBoundingBoxMax[2]);
	}
}

IMaterial* CCamoMaterialProxy::GetMaterial()
{
	if (!m_pCamoTextureVar)
	{
		return NULL;
	}
	return m_pCamoTextureVar->GetOwningMaterial();
}

EXPOSE_INTERFACE(CCamoMaterialProxy, IMaterialProxy, "Camo" IMATERIAL_PROXY_INTERFACE_VERSION);
