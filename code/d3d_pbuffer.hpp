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
#ifndef QINDIEGL_D3D_PBUFFER_H
#define QINDIEGL_D3D_PBUFFER_H

#ifndef HPBUFFERARB
typedef void* HPBUFFERARB;
#endif

//==================================================================================
// WGL_ARB_pbuffer / WGL_ARB_render_texture → D3D9 render target textures
//==================================================================================
// PBuffers are off-screen rendering surfaces.  In D3D9 we implement them
// as D3DUSAGE_RENDERTARGET textures with an associated depth-stencil surface.
// wglMakeCurrent with a PBuffer DC redirects rendering to the PBuffer's
// render target surface.  wglBindTexImageARB allows using the PBuffer
// contents as a GL texture (zero-copy via the RT texture itself).
//==================================================================================

//----------------------------------------------------------
// WGL_ARB_pbuffer constants
//----------------------------------------------------------
#ifndef WGL_DRAW_TO_PBUFFER_ARB
#define WGL_DRAW_TO_PBUFFER_ARB        0x202D
#define WGL_MAX_PBUFFER_PIXELS_ARB     0x202E
#define WGL_MAX_PBUFFER_WIDTH_ARB      0x202F
#define WGL_MAX_PBUFFER_HEIGHT_ARB     0x2030
#define WGL_PBUFFER_LARGEST_ARB        0x2033
#define WGL_PBUFFER_WIDTH_ARB          0x2034
#define WGL_PBUFFER_HEIGHT_ARB         0x2035
#define WGL_PBUFFER_LOST_ARB           0x2036
#endif

//----------------------------------------------------------
// WGL_ARB_render_texture constants
//----------------------------------------------------------
#ifndef WGL_BIND_TO_TEXTURE_RGB_ARB
#define WGL_BIND_TO_TEXTURE_RGB_ARB    0x2070
#define WGL_BIND_TO_TEXTURE_RGBA_ARB   0x2071
#define WGL_TEXTURE_FORMAT_ARB         0x2072
#define WGL_TEXTURE_TARGET_ARB         0x2073
#define WGL_MIPMAP_TEXTURE_ARB         0x2074
#define WGL_TEXTURE_RGB_ARB            0x2075
#define WGL_TEXTURE_RGBA_ARB           0x2076
#define WGL_NO_TEXTURE_ARB             0x2077
#define WGL_TEXTURE_CUBE_MAP_ARB       0x2078
#define WGL_TEXTURE_1D_ARB             0x2079
#define WGL_TEXTURE_2D_ARB             0x207A
#define WGL_MIPMAP_LEVEL_ARB           0x207B
#define WGL_CUBE_MAP_FACE_ARB          0x207C
#define WGL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB 0x207D
#define WGL_TEXTURE_CUBE_MAP_NEGATIVE_X_ARB 0x207E
#define WGL_TEXTURE_CUBE_MAP_POSITIVE_Y_ARB 0x207F
#define WGL_TEXTURE_CUBE_MAP_NEGATIVE_Y_ARB 0x2080
#define WGL_TEXTURE_CUBE_MAP_POSITIVE_Z_ARB 0x2081
#define WGL_TEXTURE_CUBE_MAP_NEGATIVE_Z_ARB 0x2082
#define WGL_FRONT_LEFT_ARB             0x2083
#define WGL_FRONT_RIGHT_ARB            0x2084
#define WGL_BACK_LEFT_ARB              0x2085
#define WGL_BACK_RIGHT_ARB             0x2086
#define WGL_AUX0_ARB                   0x2087
#endif

//----------------------------------------------------------
// PBuffer context structure
//----------------------------------------------------------
struct PBufferContext {
	LPDIRECT3DTEXTURE9		colorTexture;		// render target texture
	LPDIRECT3DSURFACE9		colorSurface;		// level-0 surface of colorTexture
	LPDIRECT3DSURFACE9		depthStencil;		// depth-stencil surface
	int						width;
	int						height;
	int						pixelFormat;
	int						textureFormat;		// WGL_TEXTURE_RGB/RGBA/NO_TEXTURE
	int						textureTarget;		// WGL_TEXTURE_2D_ARB etc.
	bool					mipmapTexture;
	HDC						fakeDC;				// synthetic DC handle
	bool					isBound;			// currently bound to a GL texture?
	bool					isActive;			// currently the render target?

	PBufferContext() : colorTexture( nullptr ), colorSurface( nullptr ), depthStencil( nullptr ),
		width( 0 ), height( 0 ), pixelFormat( 0 ),
		textureFormat( WGL_NO_TEXTURE_ARB ), textureTarget( WGL_TEXTURE_2D_ARB ),
		mipmapTexture( false ), fakeDC( nullptr ), isBound( false ), isActive( false )
	{}
};

//----------------------------------------------------------
// Public API
//----------------------------------------------------------

// Check if an HDC belongs to a PBuffer
PBufferContext* PBuffer_FindByDC( HDC hdc );

// Check if an HPBUFFERARB is valid
PBufferContext* PBuffer_FindByHandle( HPBUFFERARB handle );

// Clean up all PBuffers (called from D3DGlobal_Cleanup)
void PBuffer_Cleanup();

// Release all D3D9 resources (called before device Reset)
void PBuffer_ReleaseResources();

// Recreate D3D9 resources after device Reset
void PBuffer_RecreateResources();

// Save/restore main render target when switching to/from PBuffer
void PBuffer_SaveMainRenderTarget();
void PBuffer_RestoreMainRenderTarget();

#endif //QINDIEGL_D3D_PBUFFER_H
