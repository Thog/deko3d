#include "../dk_device.h"
#include "../cmdbuf_writer.h"

#include "mme_macros.h"
#include "engine_3d.h"

using namespace maxwell;
using namespace dk::detail;

using E = Engine3D;
using SRC = E::MmeShadowRamControl;

void dkCmdBufBindRasterizerState(DkCmdBuf obj, DkRasterizerState const* state)
{
#ifdef DEBUG
	if (!state)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
#endif

	CmdBufWriter w{obj};
	w.reserve(17);

	w << CmdInline(3D, ViewVolumeClipControl{}, state->depthClampEnable ? 0x101A : 0x181D); // todo: figure out what this actually does
	w << CmdInline(3D, RasterizerEnable{}, !state->rasterizerDiscardEnable);

	static const uint16_t polygonModes[] = { E::SetPolygonModeFront::Point, E::SetPolygonModeFront::Line, E::SetPolygonModeFront::Fill, 0 };
	w << CmdInline(3D, SetPolygonModeFront{}, polygonModes[state->polygonMode]);
	w << CmdInline(3D, SetPolygonModeBack{},  polygonModes[state->polygonMode]);

	w << CmdInline(3D, CullFaceEnable{}, state->cullMode != DkFace_None);
	if (state->cullMode != DkFace_None)
	{
		static const uint16_t cullModes[] = { E::SetCullFace::Front, E::SetCullFace::Back, E::SetCullFace::FrontAndBack };
		w << CmdInline(3D, SetCullFace{}, cullModes[state->cullMode-1]);
	}

	static const uint16_t frontFaces[] = { E::SetFrontFace::CW, E::SetFrontFace::CCW };
	w << CmdInline(3D, SetFrontFace{}, frontFaces[state->frontFace]);

	w << CmdInline(3D, PolygonOffsetPointEnable{} + state->polygonMode, state->depthBiasEnable);
	if (state->depthBiasEnable)
	{
		w << Cmd(3D, PolygonOffsetUnits{}, state->depthBiasConstantFactor*2.0f);
		w << Cmd(3D, PolygonOffsetClamp{}, state->depthBiasClamp);
		w << Cmd(3D, PolygonOffsetFactor{}, state->depthBiasSlopeFactor);
	}

	w << Cmd(3D, LineWidthSmooth{}, state->lineWidth, state->lineWidth);
}

void dkCmdBufBindColorState(DkCmdBuf obj, DkColorState const* state)
{
#ifdef DEBUG
	if (!state)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
#endif

	CmdBufWriter w{obj};
	w.reserve(5);

	if (state->logicOp == DkLogicOp_Copy)
		w << MacroInline(BindColorBlendEnableState, state->blendEnableMask);
	else
		w << Cmd(3D, ColorLogicOpEnable{}, 1, 0x1500 | state->logicOp);

	if (state->alphaCompareOp == DkCompareOp_Always)
		w << CmdInline(3D, AlphaTestEnable{}, 0);
	else
	{
		w << CmdInline(3D, AlphaTestEnable{}, 1);
		w << CmdInline(3D, AlphaTestFunc{}, state->alphaCompareOp);
	}
}

void dkCmdBufBindColorWriteState(DkCmdBuf obj, DkColorWriteState const* state)
{
#ifdef DEBUG
	if (!state)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
#endif

	CmdBufWriter w{obj};
	w.reserve(2);

	w << Macro(BindColorWriteMasks, state->masks);
}

void dkCmdBufBindDepthStencilState(DkCmdBuf obj, DkDepthStencilState const* state)
{
#ifdef DEBUG
	if (!state)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
#endif

	CmdBufWriter w{obj};
	w.reserve(3);

	static_assert(sizeof(DkDepthStencilState) == 8, "Bad definition for DkDepthStencilState");

	w << CmdList<1>{ MakeCmdHeader(IncreaseOnce, 2, Subchannel3D, MmeMacroBindDepthStencilState) };
	w.addRawData(state, 8);
}

void dkCmdBufSetDepthBounds(DkCmdBuf obj, bool enable, float near, float far)
{
	CmdBufWriter w{obj};
	w.reserve(4);

	w << CmdInline(3D, DepthBoundsEnable{}, enable);
	if (enable)
		w << Cmd(3D, DepthBoundsNear{}, near, far);
}

void dkCmdBufSetAlphaRef(DkCmdBuf obj, float ref)
{
	CmdBufWriter w{obj};
	w.reserve(2);

	w << Cmd(3D, AlphaTestRef{}, ref);
}

void dkCmdBufSetStencil(DkCmdBuf obj, DkFace face, uint8_t mask, uint8_t funcRef, uint8_t funcMask)
{
	CmdBufWriter w{obj};
	w.reserve(6);

	if (unsigned(face) & DkFace_Front)
	{
		w << CmdInline(3D, StencilFrontMask{}, mask);
		w << CmdInline(3D, StencilFrontFuncRef{}, funcRef);
		w << CmdInline(3D, StencilFrontFuncMask{}, funcMask);
	}

	if (unsigned(face) & DkFace_Back)
	{
		w << CmdInline(3D, StencilBackMask{}, mask);
		w << CmdInline(3D, StencilBackFuncRef{}, funcRef);
		w << CmdInline(3D, StencilBackFuncMask{}, funcMask);
	}
}

void dkCmdBufSetTileSize(DkCmdBuf obj, uint32_t width, uint32_t height)
{
#ifdef DEBUG
	if (width < 16 || width > 16384)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
	if (width & (width - 1))
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
	if (height < 16 || height > 16384)
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
	if (height & (height - 1))
		obj->raiseError(DK_FUNC_ERROR_CONTEXT, DkResult_BadInput);
#endif

	CmdBufWriter w{obj};
	w.reserve(2);

	w << Cmd(3D, TiledCacheTileSize{}, E::TiledCacheTileSize::Width{width} | E::TiledCacheTileSize::Height{height});
}

void dkCmdBufTiledCacheOp(DkCmdBuf obj, DkTiledCacheOp op)
{
	CmdBufWriter w{obj};
	w.reserve(1);

	switch (op)
	{
		default:
		case DkTiledCacheOp_Disable:
			w << CmdInline(3D, TiledCacheEnable{}, 0);
			break;
		case DkTiledCacheOp_Enable:
			w << CmdInline(3D, TiledCacheEnable{}, 1);
			break;
		case DkTiledCacheOp_Flush:
			w << CmdInline(3D, TiledCacheFlush{}, 0);
			break;
		case DkTiledCacheOp_FlushAlt:
			w << CmdInline(3D, TiledCacheFlush{}, 1);
			break;
		case DkTiledCacheOp_UnkDisable:
			w << CmdInline(3D, TiledCacheUnkFeatureEnable{}, 0);
			break;
		case DkTiledCacheOp_UnkEnable:
			w << CmdInline(3D, TiledCacheUnkFeatureEnable{}, 1);
			break;
	}
}
