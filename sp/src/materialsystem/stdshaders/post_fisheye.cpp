#include "BaseVSShader.h"
#include "PassThrough_vs30.inc"
#include "post_fisheye_ps30.inc"

ConVar mat_fisheye_strength("mat_fisheye_strength", "0.6");
ConVar mat_fisheye_zoom("mat_fisheye_zoom", "1.0");

BEGIN_VS_SHADER(Post_Fisheye, "Help for Post_Fisheye")
BEGIN_SHADER_PARAMS
SHADER_PARAM(FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "")
END_SHADER_PARAMS
SHADER_FALLBACK
{
	return 0;
}
SHADER_INIT_PARAMS()
{
	if (!params[FBTEXTURE]->IsDefined())
	{
		params[FBTEXTURE]->SetStringValue("_rt_FullFrameFB");
	}
}
SHADER_INIT
{
	if (params[FBTEXTURE]->IsDefined())
	{
		LoadTexture(FBTEXTURE);
	}
}
SHADER_DRAW
{
	SHADOW_STATE
	{
		pShaderShadow->EnableDepthWrites(false);
		pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);

		int fmt = VERTEX_POSITION;

		pShaderShadow->VertexShaderVertexFormat(fmt, 1, 0, 0);
		pShaderShadow->SetVertexShader("PassThrough_vs30", 0);
		pShaderShadow->SetPixelShader("post_fisheye_ps30");

		DefaultFog();
	}
	DYNAMIC_STATE
	{
		BindTexture(SHADER_SAMPLER0, FBTEXTURE, -1);

		float fisheyeParams[4];
		fisheyeParams[0] = mat_fisheye_strength.GetFloat();
		fisheyeParams[1] = mat_fisheye_zoom.GetFloat();
		fisheyeParams[2] = 0.0f;
		fisheyeParams[3] = 0.0f;

		pShaderAPI->SetPixelShaderConstant(0, fisheyeParams);
	}
	Draw();
}
END_SHADER