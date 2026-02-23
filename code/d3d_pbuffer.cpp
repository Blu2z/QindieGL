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
#include "d3d_pbuffer.hpp"
#include "d3d_extension.hpp"

#include <map>
#include <algorithm>

//==================================================================================
// Global PBuffer registry
//==================================================================================

// All active PBuffers, keyed by their handle (which is just the PBufferContext pointer)
static std::map<HPBUFFERARB, PBufferContext*> gPBuffers;

// Map from synthetic DC → PBuffer for fast lookup in wglMakeCurrent
static std::map<HDC, PBufferContext*> gPBufferDCMap;

// Counter for generating unique fake DC handles
static uintptr_t gNextFakeDC = 0x7B000001;

// Saved main render target / depth-stencil for context switching
static LPDIRECT3DSURFACE9 gSavedMainRT = nullptr;
static LPDIRECT3DSURFACE9 gSavedMainDS = nullptr;
static bool gMainRTSaved = false;

//==================================================================================
// Lookup helpers
//==================================================================================

PBufferContext* PBuffer_FindByDC( HDC hdc )
{
	auto it = gPBufferDCMap.find( hdc );
	return ( it != gPBufferDCMap.end() ) ? it->second : nullptr;
}

PBufferContext* PBuffer_FindByHandle( HPBUFFERARB handle )
{
	auto it = gPBuffers.find( handle );
	return ( it != gPBuffers.end() ) ? it->second : nullptr;
}

//==================================================================================
// Resource management
//==================================================================================

static bool PBuffer_CreateD3DResources( PBufferContext* pb )
{
	if ( !D3DGlobal.pDevice ) return false;

	// Determine render target format
	D3DFORMAT rtFormat = D3DFMT_A8R8G8B8;
	if ( pb->textureFormat == WGL_TEXTURE_RGB_ARB ) {
		rtFormat = D3DFMT_X8R8G8B8;
	}

	// Create render target texture
	DWORD usage = D3DUSAGE_RENDERTARGET;
	HRESULT hr = D3DGlobal.pDevice->CreateTexture(
		pb->width, pb->height, 1,
		usage,
		rtFormat,
		D3DPOOL_DEFAULT,
		&pb->colorTexture,
		nullptr
	);
	if ( FAILED( hr ) ) {
		logPrintf( "PBuffer: CreateTexture(%dx%d) failed: 0x%08X\n", pb->width, pb->height, hr );
		return false;
	}

	// Get surface level 0
	hr = pb->colorTexture->GetSurfaceLevel( 0, &pb->colorSurface );
	if ( FAILED( hr ) ) {
		logPrintf( "PBuffer: GetSurfaceLevel failed: 0x%08X\n", hr );
		pb->colorTexture->Release();
		pb->colorTexture = nullptr;
		return false;
	}

	// Create depth-stencil surface matching the PBuffer size
	D3DFORMAT dsFormat = D3DGlobal.hPresentParams.AutoDepthStencilFormat;
	if ( dsFormat == D3DFMT_UNKNOWN ) {
		dsFormat = D3DFMT_D24S8;
	}
	hr = D3DGlobal.pDevice->CreateDepthStencilSurface(
		pb->width, pb->height,
		dsFormat,
		D3DMULTISAMPLE_NONE,
		0,
		TRUE,		// discard
		&pb->depthStencil,
		nullptr
	);
	if ( FAILED( hr ) ) {
		logPrintf( "PBuffer: CreateDepthStencilSurface(%dx%d) failed: 0x%08X\n", pb->width, pb->height, hr );
		// Non-fatal: rendering without depth is possible for some use cases
		pb->depthStencil = nullptr;
	}

	return true;
}

static void PBuffer_ReleaseD3DResources( PBufferContext* pb )
{
	if ( pb->colorSurface ) {
		pb->colorSurface->Release();
		pb->colorSurface = nullptr;
	}
	if ( pb->colorTexture ) {
		pb->colorTexture->Release();
		pb->colorTexture = nullptr;
	}
	if ( pb->depthStencil ) {
		pb->depthStencil->Release();
		pb->depthStencil = nullptr;
	}
}

void PBuffer_Cleanup()
{
	// Restore main RT if a PBuffer is still active
	if ( gMainRTSaved ) {
		PBuffer_RestoreMainRenderTarget();
	}

	for ( auto& pair : gPBuffers ) {
		PBuffer_ReleaseD3DResources( pair.second );
		if ( pair.second->fakeDC ) {
			gPBufferDCMap.erase( pair.second->fakeDC );
		}
		delete pair.second;
	}
	gPBuffers.clear();
	gPBufferDCMap.clear();

	if ( gSavedMainRT ) { gSavedMainRT->Release(); gSavedMainRT = nullptr; }
	if ( gSavedMainDS ) { gSavedMainDS->Release(); gSavedMainDS = nullptr; }
	gMainRTSaved = false;
	gNextFakeDC = 0x7B000001;
}

void PBuffer_ReleaseResources()
{
	// Called before device Reset — release all D3DPOOL_DEFAULT resources
	for ( auto& pair : gPBuffers ) {
		PBuffer_ReleaseD3DResources( pair.second );
	}
	if ( gSavedMainRT ) { gSavedMainRT->Release(); gSavedMainRT = nullptr; }
	if ( gSavedMainDS ) { gSavedMainDS->Release(); gSavedMainDS = nullptr; }
	gMainRTSaved = false;
}

void PBuffer_RecreateResources()
{
	// Called after device Reset — recreate all PBuffer surfaces
	for ( auto& pair : gPBuffers ) {
		PBuffer_CreateD3DResources( pair.second );
	}
}

//==================================================================================
// Render target switching
//==================================================================================

void PBuffer_SaveMainRenderTarget()
{
	if ( gMainRTSaved ) return;
	if ( !D3DGlobal.pDevice ) return;

	D3DGlobal.pDevice->GetRenderTarget( 0, &gSavedMainRT );
	D3DGlobal.pDevice->GetDepthStencilSurface( &gSavedMainDS );
	gMainRTSaved = true;
}

void PBuffer_RestoreMainRenderTarget()
{
	if ( !gMainRTSaved ) return;
	if ( !D3DGlobal.pDevice ) return;

	if ( gSavedMainRT ) {
		D3DGlobal.pDevice->SetRenderTarget( 0, gSavedMainRT );
		gSavedMainRT->Release();
		gSavedMainRT = nullptr;
	}
	if ( gSavedMainDS ) {
		D3DGlobal.pDevice->SetDepthStencilSurface( gSavedMainDS );
		gSavedMainDS->Release();
		gSavedMainDS = nullptr;
	}
	gMainRTSaved = false;
}

//==================================================================================
// WGL_ARB_pbuffer implementation
//==================================================================================

OPENGL_API HPBUFFERARB WINAPI wglCreatePbufferARB( HDC hDC, int iPixelFormat, int iWidth, int iHeight, const int *piAttribList )
{
	_CRT_UNUSED( hDC );
	_CRT_UNUSED( iPixelFormat );

	if ( !D3DGlobal.pDevice ) {
		logPrintf( "wglCreatePbufferARB: no D3D device\n" );
		return NULL;
	}

	if ( iWidth <= 0 || iHeight <= 0 ) {
		logPrintf( "wglCreatePbufferARB: invalid size %dx%d\n", iWidth, iHeight );
		return NULL;
	}

	// Cap size to device texture limits
	DWORD maxSize = D3DGlobal.hD3DCaps.MaxTextureWidth;
	if ( (DWORD)iWidth > maxSize ) iWidth = (int)maxSize;
	if ( (DWORD)iHeight > maxSize ) iHeight = (int)maxSize;

	PBufferContext* pb = new PBufferContext;
	pb->width = iWidth;
	pb->height = iHeight;
	pb->pixelFormat = iPixelFormat;

	// Parse attribute list
	if ( piAttribList ) {
		for ( const int* attrib = piAttribList; attrib[0] != 0; attrib += 2 ) {
			switch ( attrib[0] ) {
			case WGL_TEXTURE_FORMAT_ARB:
				pb->textureFormat = attrib[1];
				break;
			case WGL_TEXTURE_TARGET_ARB:
				pb->textureTarget = attrib[1];
				break;
			case WGL_MIPMAP_TEXTURE_ARB:
				pb->mipmapTexture = ( attrib[1] != 0 );
				break;
			case WGL_PBUFFER_LARGEST_ARB:
				// Already capped to device limits above
				break;
			default:
				logPrintf( "wglCreatePbufferARB: unknown attrib 0x%X = %d\n", attrib[0], attrib[1] );
				break;
			}
		}
	}

	// Create D3D9 resources
	if ( !PBuffer_CreateD3DResources( pb ) ) {
		delete pb;
		return NULL;
	}

	// Generate a unique fake DC
	pb->fakeDC = (HDC)(uintptr_t)gNextFakeDC++;

	HPBUFFERARB handle = (HPBUFFERARB)pb;
	gPBuffers[handle] = pb;

	logPrintf( "wglCreatePbufferARB: created %dx%d PBuffer (handle=0x%p, texFmt=0x%X)\n",
		iWidth, iHeight, handle, pb->textureFormat );

	return handle;
}

OPENGL_API HDC WINAPI wglGetPbufferDCARB( HPBUFFERARB hPbuffer )
{
	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb ) {
		logPrintf( "wglGetPbufferDCARB: invalid handle 0x%p\n", hPbuffer );
		return NULL;
	}

	// Register the DC → PBuffer mapping
	gPBufferDCMap[pb->fakeDC] = pb;

	logPrintf( "wglGetPbufferDCARB: returning DC 0x%p for PBuffer 0x%p\n", pb->fakeDC, hPbuffer );
	return pb->fakeDC;
}

OPENGL_API int WINAPI wglReleasePbufferDCARB( HPBUFFERARB hPbuffer, HDC hDC )
{
	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb ) {
		logPrintf( "wglReleasePbufferDCARB: invalid handle 0x%p\n", hPbuffer );
		return FALSE;
	}

	// If this PBuffer is currently the render target, switch back
	if ( pb->isActive ) {
		PBuffer_RestoreMainRenderTarget();
		pb->isActive = false;
	}

	_CRT_UNUSED( hDC );
	return TRUE;
}

OPENGL_API BOOL WINAPI wglDestroyPbufferARB( HPBUFFERARB hPbuffer )
{
	auto it = gPBuffers.find( hPbuffer );
	if ( it == gPBuffers.end() ) {
		logPrintf( "wglDestroyPbufferARB: invalid handle 0x%p\n", hPbuffer );
		return FALSE;
	}

	PBufferContext* pb = it->second;

	// If currently active, restore main RT first
	if ( pb->isActive ) {
		PBuffer_RestoreMainRenderTarget();
		pb->isActive = false;
	}

	// Remove DC mapping
	if ( pb->fakeDC ) {
		gPBufferDCMap.erase( pb->fakeDC );
	}

	logPrintf( "wglDestroyPbufferARB: destroying PBuffer 0x%p (%dx%d)\n",
		hPbuffer, pb->width, pb->height );

	PBuffer_ReleaseD3DResources( pb );
	delete pb;
	gPBuffers.erase( it );

	return TRUE;
}

OPENGL_API BOOL WINAPI wglQueryPbufferARB( HPBUFFERARB hPbuffer, int iAttribute, int *piValue )
{
	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb || !piValue ) {
		logPrintf( "wglQueryPbufferARB: invalid params (handle=0x%p)\n", hPbuffer );
		return FALSE;
	}

	switch ( iAttribute ) {
	case WGL_PBUFFER_WIDTH_ARB:
		*piValue = pb->width;
		break;
	case WGL_PBUFFER_HEIGHT_ARB:
		*piValue = pb->height;
		break;
	case WGL_PBUFFER_LOST_ARB:
		*piValue = ( pb->colorTexture == nullptr ) ? TRUE : FALSE;
		break;
	case WGL_TEXTURE_FORMAT_ARB:
		*piValue = pb->textureFormat;
		break;
	case WGL_TEXTURE_TARGET_ARB:
		*piValue = pb->textureTarget;
		break;
	case WGL_MIPMAP_TEXTURE_ARB:
		*piValue = pb->mipmapTexture ? TRUE : FALSE;
		break;
	default:
		logPrintf( "wglQueryPbufferARB: unsupported attribute 0x%X\n", iAttribute );
		*piValue = 0;
		break;
	}

	return TRUE;
}

//==================================================================================
// WGL_ARB_render_texture implementation
//==================================================================================

OPENGL_API BOOL WINAPI wglBindTexImageARB( HPBUFFERARB hPbuffer, int iBuffer )
{
	_CRT_UNUSED( iBuffer );

	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb ) {
		logPrintf( "wglBindTexImageARB: invalid handle 0x%p\n", hPbuffer );
		return FALSE;
	}

	if ( !pb->colorTexture ) {
		logPrintf( "wglBindTexImageARB: PBuffer has no color texture\n" );
		return FALSE;
	}

	if ( pb->isActive ) {
		// Must not bind while PBuffer is the active render target
		// The spec says this is an error, but some games do it anyway.
		// We'll just flush (end scene) first.
		if ( D3DGlobal.sceneBegan ) {
			D3DGlobal.pDevice->EndScene();
			D3DGlobal.sceneBegan = false;
		}
	}

	// Get the currently bound 2D texture and replace its D3D texture
	// with the PBuffer's render target texture.
	// We do this by directly setting the texture on the current TMU stage.
	int currentTMU = D3DState.TextureState.currentTMU;

	// Set the PBuffer's RT texture directly on this texture stage
	if ( D3DGlobal.pDevice ) {
		D3DGlobal.pDevice->SetTexture( currentTMU, pb->colorTexture );
		D3DState.TextureState.textureSamplerStateChanged = TRUE;
	}

	pb->isBound = true;
	logPrintf( "wglBindTexImageARB: bound PBuffer 0x%p to TMU %d\n", hPbuffer, currentTMU );
	return TRUE;
}

OPENGL_API BOOL WINAPI wglReleaseTexImageARB( HPBUFFERARB hPbuffer, int iBuffer )
{
	_CRT_UNUSED( iBuffer );

	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb ) {
		logPrintf( "wglReleaseTexImageARB: invalid handle 0x%p\n", hPbuffer );
		return FALSE;
	}

	pb->isBound = false;
	// The texture will be overwritten next time the normal texture is set
	// via D3DState_Check → D3D_SetupTextureStages, so no explicit unbind needed.
	logPrintf( "wglReleaseTexImageARB: released PBuffer 0x%p\n", hPbuffer );
	return TRUE;
}

OPENGL_API BOOL WINAPI wglSetPbufferAttribARB( HPBUFFERARB hPbuffer, const int *piAttribList )
{
	PBufferContext* pb = PBuffer_FindByHandle( hPbuffer );
	if ( !pb ) {
		logPrintf( "wglSetPbufferAttribARB: invalid handle 0x%p\n", hPbuffer );
		return FALSE;
	}

	// Parse attribute list — mainly used for cube map face selection and mipmap level
	if ( piAttribList ) {
		for ( const int* attrib = piAttribList; attrib[0] != 0; attrib += 2 ) {
			switch ( attrib[0] ) {
			case WGL_MIPMAP_LEVEL_ARB:
				// Mipmap level selection — not supported in our single-level RT scheme
				break;
			case WGL_CUBE_MAP_FACE_ARB:
				// Cube map face selection — not supported in simple 2D RT scheme
				break;
			default:
				logPrintf( "wglSetPbufferAttribARB: unknown attrib 0x%X = %d\n", attrib[0], attrib[1] );
				break;
			}
		}
	}

	return TRUE;
}
