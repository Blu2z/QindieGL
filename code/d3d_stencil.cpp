/***************************************************************************
* Copyright (C) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or (at your option) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_utils.hpp"

//==================================================================================
// Stencil buffer functions
//----------------------------------------------------------------------------------
// It also implements EXT_stencil_two_side extension for single-pass shadow volumes
// This is not actually correct, because in GL, ref and write mask are also separate
// TODO: test two-side stenciling!
//==================================================================================

OPENGL_API void WINAPI glStencilMask( GLuint mask )
{
	if (D3DState.StencilBufferState.stencilWriteMask != mask) {
		D3DState.StencilBufferState.stencilWriteMask = mask;
		D3DState_SetRenderState( D3DRS_STENCILWRITEMASK, mask );
	}
}

OPENGL_API void WINAPI glStencilFunc( GLenum func, GLint ref, GLuint mask )
{
	DWORD dfunc = UTIL_GLtoD3DCmpFunc(func);
	if (!D3DState.EnableState.twoSideStencilEnabled || D3DState.StencilBufferState.activeStencilFace == GL_CW) {
		if (dfunc != D3DState.StencilBufferState.stencilTestFunc) {
			D3DState.StencilBufferState.stencilTestFunc = dfunc;
			D3DState_SetRenderState( D3DRS_STENCILFUNC, dfunc );
		}
	} else {
		if (dfunc != D3DState.StencilBufferState.stencilTestFuncCCW) {
			D3DState.StencilBufferState.stencilTestFuncCCW = dfunc;
			D3DState_SetRenderState( D3DRS_CCW_STENCILFUNC, dfunc );
		}
	}

	if (mask != D3DState.StencilBufferState.stencilTestMask) {
		D3DState.StencilBufferState.stencilTestMask = mask;
		D3DState_SetRenderState( D3DRS_STENCILMASK, mask );
	}
	DWORD dref = (DWORD)ref;
	if (dref != D3DState.StencilBufferState.stencilTestReference) {
		D3DState.StencilBufferState.stencilTestReference = dref;
		D3DState_SetRenderState( D3DRS_STENCILREF, dref );
	}
}

OPENGL_API void WINAPI glStencilOp( GLenum fail, GLenum zfail, GLenum zpass )
{
	DWORD dfunc;
	if (!D3DState.EnableState.twoSideStencilEnabled || D3DState.StencilBufferState.activeStencilFace == GL_CW) {
		dfunc = UTIL_GLtoD3DStencilFunc(fail);
		if (dfunc != D3DState.StencilBufferState.stencilTestFailFunc) {
			D3DState.StencilBufferState.stencilTestFailFunc = dfunc;
			D3DState_SetRenderState( D3DRS_STENCILFAIL, dfunc );
		}
		dfunc = UTIL_GLtoD3DStencilFunc(zfail);
		if (dfunc != D3DState.StencilBufferState.stencilTestZFailFunc) {
			D3DState.StencilBufferState.stencilTestZFailFunc = dfunc;
			D3DState_SetRenderState( D3DRS_STENCILZFAIL, dfunc );
		}
		dfunc = UTIL_GLtoD3DStencilFunc(zpass);
		if (dfunc != D3DState.StencilBufferState.stencilTestPassFunc) {
			D3DState.StencilBufferState.stencilTestPassFunc = dfunc;
			D3DState_SetRenderState( D3DRS_STENCILPASS, dfunc );
		}
	} else {
		dfunc = UTIL_GLtoD3DStencilFunc(fail);
		if (dfunc != D3DState.StencilBufferState.stencilTestFuncCCW) {
			D3DState.StencilBufferState.stencilTestFuncCCW = dfunc;
			D3DState_SetRenderState( D3DRS_CCW_STENCILFAIL, dfunc );
		}
		dfunc = UTIL_GLtoD3DStencilFunc(zfail);
		if (dfunc != D3DState.StencilBufferState.stencilTestZFailFuncCCW) {
			D3DState.StencilBufferState.stencilTestZFailFuncCCW = dfunc;
			D3DState_SetRenderState( D3DRS_CCW_STENCILZFAIL, dfunc );
		}
		dfunc = UTIL_GLtoD3DStencilFunc(zpass);
		if (dfunc != D3DState.StencilBufferState.stencilTestPassFuncCCW) {
			D3DState.StencilBufferState.stencilTestPassFuncCCW = dfunc;
			D3DState_SetRenderState( D3DRS_CCW_STENCILPASS, dfunc );
		}
	}
}

OPENGL_API void WINAPI glActiveStencilFace( GLenum face )
{
	if (face == GL_FRONT)
		D3DState.StencilBufferState.activeStencilFace = (D3DState.PolygonState.frontFace == GL_CW) ? GL_CW : GL_CCW;
	else
		D3DState.StencilBufferState.activeStencilFace = (D3DState.PolygonState.frontFace == GL_CW) ? GL_CCW : GL_CW;
}

//==================================================================================
// GL 2.0 stencil separate functions
//----------------------------------------------------------------------------------
// Unlike EXT_stencil_two_side, these take a face parameter directly.
//==================================================================================

OPENGL_API void WINAPI glStencilFuncSeparate( GLenum face, GLenum func, GLint ref, GLuint mask )
{
	DWORD dfunc = UTIL_GLtoD3DCmpFunc(func);
	DWORD dref = (DWORD)ref;

	if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
		if (dfunc != D3DState.StencilBufferState.stencilTestFunc) {
			D3DState.StencilBufferState.stencilTestFunc = dfunc;
			D3DState_SetRenderState( D3DRS_STENCILFUNC, dfunc );
		}
		if (dref != D3DState.StencilBufferState.stencilTestReference) {
			D3DState.StencilBufferState.stencilTestReference = dref;
			D3DState_SetRenderState( D3DRS_STENCILREF, dref );
		}
		if (mask != D3DState.StencilBufferState.stencilTestMask) {
			D3DState.StencilBufferState.stencilTestMask = mask;
			D3DState_SetRenderState( D3DRS_STENCILMASK, mask );
		}
	}
	if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
		if (D3DGlobal.hD3DCaps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) {
			D3DState_SetRenderState( D3DRS_TWOSIDEDSTENCILMODE, TRUE );
			if (dfunc != D3DState.StencilBufferState.stencilTestFuncCCW) {
				D3DState.StencilBufferState.stencilTestFuncCCW = dfunc;
				D3DState_SetRenderState( D3DRS_CCW_STENCILFUNC, dfunc );
			}
		}
	}
}

OPENGL_API void WINAPI glStencilOpSeparate( GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass )
{
	DWORD dsfail = UTIL_GLtoD3DStencilFunc(sfail);
	DWORD ddpfail = UTIL_GLtoD3DStencilFunc(dpfail);
	DWORD ddppass = UTIL_GLtoD3DStencilFunc(dppass);

	if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
		if (dsfail != D3DState.StencilBufferState.stencilTestFailFunc) {
			D3DState.StencilBufferState.stencilTestFailFunc = dsfail;
			D3DState_SetRenderState( D3DRS_STENCILFAIL, dsfail );
		}
		if (ddpfail != D3DState.StencilBufferState.stencilTestZFailFunc) {
			D3DState.StencilBufferState.stencilTestZFailFunc = ddpfail;
			D3DState_SetRenderState( D3DRS_STENCILZFAIL, ddpfail );
		}
		if (ddppass != D3DState.StencilBufferState.stencilTestPassFunc) {
			D3DState.StencilBufferState.stencilTestPassFunc = ddppass;
			D3DState_SetRenderState( D3DRS_STENCILPASS, ddppass );
		}
	}
	if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
		if (D3DGlobal.hD3DCaps.StencilCaps & D3DSTENCILCAPS_TWOSIDED) {
			D3DState_SetRenderState( D3DRS_TWOSIDEDSTENCILMODE, TRUE );
			if (dsfail != D3DState.StencilBufferState.stencilTestFailFuncCCW) {
				D3DState.StencilBufferState.stencilTestFailFuncCCW = dsfail;
				D3DState_SetRenderState( D3DRS_CCW_STENCILFAIL, dsfail );
			}
			if (ddpfail != D3DState.StencilBufferState.stencilTestZFailFuncCCW) {
				D3DState.StencilBufferState.stencilTestZFailFuncCCW = ddpfail;
				D3DState_SetRenderState( D3DRS_CCW_STENCILZFAIL, ddpfail );
			}
			if (ddppass != D3DState.StencilBufferState.stencilTestPassFuncCCW) {
				D3DState.StencilBufferState.stencilTestPassFuncCCW = ddppass;
				D3DState_SetRenderState( D3DRS_CCW_STENCILPASS, ddppass );
			}
		}
	}
}

OPENGL_API void WINAPI glStencilMaskSeparate( GLenum face, GLuint mask )
{
	// D3D9 doesn't support separate front/back stencil write masks
	// Apply to both faces (or just front, which is D3D's single write mask)
	if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
		if (D3DState.StencilBufferState.stencilWriteMask != mask) {
			D3DState.StencilBufferState.stencilWriteMask = mask;
			D3DState_SetRenderState( D3DRS_STENCILWRITEMASK, mask );
		}
	}
	// Back face write mask is silently ignored (D3D9 limitation)
}
