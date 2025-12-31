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
#include "d3d_extension.hpp"
#include "d3d_global.hpp"

#include <cstring>
#include <vector>

#ifndef WGL_NUMBER_PIXEL_FORMATS_ARB
#define WGL_NUMBER_PIXEL_FORMATS_ARB 0x2000
#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_DRAW_TO_BITMAP_ARB 0x2002
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_NEED_PALETTE_ARB 0x2004
#define WGL_NEED_SYSTEM_PALETTE_ARB 0x2005
#define WGL_SWAP_LAYER_BUFFERS_ARB 0x2006
#define WGL_SWAP_METHOD_ARB 0x2007
#define WGL_NUMBER_OVERLAYS_ARB 0x2008
#define WGL_NUMBER_UNDERLAYS_ARB 0x2009
#define WGL_TRANSPARENT_ARB 0x200A
#define WGL_SHARE_DEPTH_ARB 0x200C
#define WGL_SHARE_STENCIL_ARB 0x200D
#define WGL_SHARE_ACCUM_ARB 0x200E
#define WGL_SUPPORT_GDI_ARB 0x200F
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_STEREO_ARB 0x2012
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_COLOR_BITS_ARB 0x2014
#define WGL_RED_BITS_ARB 0x2015
#define WGL_RED_SHIFT_ARB 0x2016
#define WGL_GREEN_BITS_ARB 0x2017
#define WGL_GREEN_SHIFT_ARB 0x2018
#define WGL_BLUE_BITS_ARB 0x2019
#define WGL_BLUE_SHIFT_ARB 0x201A
#define WGL_ALPHA_BITS_ARB 0x201B
#define WGL_ALPHA_SHIFT_ARB 0x201C
#define WGL_ACCUM_BITS_ARB 0x201D
#define WGL_ACCUM_RED_BITS_ARB 0x201E
#define WGL_ACCUM_GREEN_BITS_ARB 0x201F
#define WGL_ACCUM_BLUE_BITS_ARB 0x2020
#define WGL_ACCUM_ALPHA_BITS_ARB 0x2021
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_AUX_BUFFERS_ARB 0x2024
#define WGL_NO_ACCELERATION_ARB 0x2025
#define WGL_GENERIC_ACCELERATION_ARB 0x2026
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_SWAP_EXCHANGE_ARB 0x2028
#define WGL_SWAP_COPY_ARB 0x2029
#define WGL_SWAP_UNDEFINED_ARB 0x202A
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_TYPE_COLORINDEX_ARB 0x202C
#endif

static bool GetPixelFormatDescriptor(HDC hdc, int requestedFormat, PIXELFORMATDESCRIPTOR &outPfd, int &outFormat)
{
	if (!hdc) {
		SetLastError(ERROR_INVALID_PARAMETER);
		logPrintf("wgl*PixelFormatARB: invalid HDC\n");
		return false;
	}

	auto clampToByte = [](int value) -> BYTE {
		if (value < 0) {
			return 0;
		}
		if (value > 255) {
			return 255;
		}
		return static_cast<BYTE>(value);
	};

	auto buildFallbackPfd = [&]() -> bool {
		const int redBits = D3DGlobal.rgbaBits[0];
		const int greenBits = D3DGlobal.rgbaBits[1];
		const int blueBits = D3DGlobal.rgbaBits[2];
		const int alphaBits = D3DGlobal.rgbaBits[3];
		const int colorBits = redBits + greenBits + blueBits + alphaBits;

		if (colorBits <= 0 && D3DGlobal.depthBits <= 0) {
			logPrintf("wgl*PixelFormatARB: fallback PFD unavailable (no global bit data)\n");
			return false;
		}

		std::memset(&outPfd, 0, sizeof(outPfd));
		outPfd.nSize = sizeof(outPfd);
		outPfd.nVersion = 1;
		outPfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
		if (D3DGlobal.hPresentParams.BackBufferCount > 0) {
			outPfd.dwFlags |= PFD_DOUBLEBUFFER;
		}
		if (D3DGlobal.hPresentParams.SwapEffect == D3DSWAPEFFECT_COPY) {
			outPfd.dwFlags |= PFD_SWAP_COPY;
		}
		outPfd.iPixelType = PFD_TYPE_RGBA;
		outPfd.cColorBits = clampToByte(colorBits);
		outPfd.cRedBits = clampToByte(redBits);
		outPfd.cGreenBits = clampToByte(greenBits);
		outPfd.cBlueBits = clampToByte(blueBits);
		outPfd.cAlphaBits = clampToByte(alphaBits);
		outPfd.cDepthBits = clampToByte(D3DGlobal.depthBits);
		outPfd.cStencilBits = clampToByte(D3DGlobal.stencilBits);
		outPfd.iLayerType = PFD_MAIN_PLANE;
		return true;
	};

	int format = requestedFormat;
	if (format <= 0) {
		format = ::GetPixelFormat(hdc);
	}

	if (format > 0 && ::DescribePixelFormat(hdc, format, sizeof(outPfd), &outPfd)) {
		outFormat = format;
		return true;
	}

	if (buildFallbackPfd()) {
		outFormat = (format > 0) ? format : 1;
		logPrintf("wgl*PixelFormatARB: using fallback PFD for HDC %p\n", hdc);
		return true;
	}

	SetLastError(ERROR_INVALID_PIXEL_FORMAT);
	if (format <= 0) {
		logPrintf("wgl*PixelFormatARB: HDC %p has no pixel format\n", hdc);
	} else {
		logPrintf("wgl*PixelFormatARB: DescribePixelFormat failed for HDC %p format %d\n", hdc, format);
	}
	return false;
}

static int GetSwapMethod(const PIXELFORMATDESCRIPTOR &pfd)
{
	if (pfd.dwFlags & PFD_SWAP_EXCHANGE) {
		return WGL_SWAP_EXCHANGE_ARB;
	}
	if (pfd.dwFlags & PFD_SWAP_COPY) {
		return WGL_SWAP_COPY_ARB;
	}
	return WGL_SWAP_UNDEFINED_ARB;
}

static int GetPixelType(const PIXELFORMATDESCRIPTOR &pfd)
{
	return (pfd.iPixelType == PFD_TYPE_RGBA) ? WGL_TYPE_RGBA_ARB : WGL_TYPE_COLORINDEX_ARB;
}

static int GetAccelType()
{
	return WGL_FULL_ACCELERATION_ARB;
}

static int GetPixelFormatCount(HDC hdc)
{
	int count = ::DescribePixelFormat(hdc, 1, 0, NULL);
	if (count <= 0) {
		PIXELFORMATDESCRIPTOR pfd = {};
		int format = 0;
		if (GetPixelFormatDescriptor(hdc, 0, pfd, format)) {
			return 1;
		}
	}
	return count;
}

static int GetGlobalColorBits()
{
	const int redBits = D3DGlobal.rgbaBits[0];
	const int greenBits = D3DGlobal.rgbaBits[1];
	const int blueBits = D3DGlobal.rgbaBits[2];
	const int alphaBits = D3DGlobal.rgbaBits[3];
	return redBits + greenBits + blueBits + alphaBits;
}

static int GetColorBits(const PIXELFORMATDESCRIPTOR &pfd)
{
	if (pfd.cColorBits > 0) {
		return pfd.cColorBits;
	}

	return GetGlobalColorBits();
}

static int GetDepthBits(const PIXELFORMATDESCRIPTOR &pfd)
{
	if (pfd.cDepthBits > 0) {
		return pfd.cDepthBits;
	}

	return D3DGlobal.depthBits;
}

static int GetStencilBits(const PIXELFORMATDESCRIPTOR &pfd)
{
	if (pfd.cStencilBits > 0) {
		return pfd.cStencilBits;
	}

	return D3DGlobal.stencilBits;
}

static int GetColorComponentBits(int pfdBits, int componentIndex)
{
	if (pfdBits > 0) {
		return pfdBits;
	}

	return D3DGlobal.rgbaBits[componentIndex];
}

static bool EvaluateAttribRequirement(const PIXELFORMATDESCRIPTOR &pfd, int attrib, int value)
{
	switch (attrib) {
	case WGL_DRAW_TO_WINDOW_ARB:
		return ((pfd.dwFlags & PFD_DRAW_TO_WINDOW) != 0) == (value != 0);
	case WGL_SUPPORT_OPENGL_ARB:
		return ((pfd.dwFlags & PFD_SUPPORT_OPENGL) != 0) == (value != 0);
	case WGL_DOUBLE_BUFFER_ARB:
		return ((pfd.dwFlags & PFD_DOUBLEBUFFER) != 0) == (value != 0);
	case WGL_STEREO_ARB:
		return ((pfd.dwFlags & PFD_STEREO) != 0) == (value != 0);
	case WGL_PIXEL_TYPE_ARB:
		return GetPixelType(pfd) == value;
	case WGL_COLOR_BITS_ARB:
		return GetColorBits(pfd) >= value;
	case WGL_ALPHA_BITS_ARB:
		return GetColorComponentBits(pfd.cAlphaBits, 3) >= value;
	case WGL_DEPTH_BITS_ARB:
		return GetDepthBits(pfd) >= value;
	case WGL_STENCIL_BITS_ARB:
		return GetStencilBits(pfd) >= value;
	case WGL_ACCELERATION_ARB:
		return GetAccelType() == value;
	case WGL_SWAP_METHOD_ARB:
		return GetSwapMethod(pfd) == value;
	default:
		logPrintf("wglChoosePixelFormatARB: unsupported attribute 0x%X\n", attrib);
		return false;
	}
}

OPENGL_API BOOL WINAPI wglChoosePixelFormatARB(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats)
{
	_CRT_UNUSED(pfAttribFList);

	PIXELFORMATDESCRIPTOR pfd = {};
	int format = 0;
	if (!GetPixelFormatDescriptor(hdc, 0, pfd, format)) {
		return FALSE;
	}

	if (!piFormats || !nNumFormats || nMaxFormats == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		logPrintf("wglChoosePixelFormatARB: invalid output pointers\n");
		return FALSE;
	}

	if (piAttribIList) {
		for (const int *attrib = piAttribIList; attrib[0] != 0; attrib += 2) {
			if (!EvaluateAttribRequirement(pfd, attrib[0], attrib[1])) {
				*nNumFormats = 0;
				SetLastError(ERROR_INVALID_PIXEL_FORMAT);
				logPrintf("wglChoosePixelFormatARB: no matching pixel format for attribute 0x%X\n", attrib[0]);
				return FALSE;
			}
		}
	}

	*piFormats = format;
	*nNumFormats = 1;
	return TRUE;
}

OPENGL_API BOOL WINAPI wglGetPixelFormatAttribivARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, int *piValues)
{
	if (!piAttributes || !piValues || nAttributes == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		logPrintf("wglGetPixelFormatAttribivARB: invalid attribute list\n");
		return FALSE;
	}
	if (iLayerPlane != 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		logPrintf("wglGetPixelFormatAttribivARB: unsupported layer plane %d\n", iLayerPlane);
		return FALSE;
	}

	PIXELFORMATDESCRIPTOR pfd = {};
	int format = 0;
	if (!GetPixelFormatDescriptor(hdc, iPixelFormat, pfd, format)) {
		return FALSE;
	}

	int totalFormats = GetPixelFormatCount(hdc);
	bool unsupported = false;
	for (UINT i = 0; i < nAttributes; ++i) {
		switch (piAttributes[i]) {
		case WGL_NUMBER_PIXEL_FORMATS_ARB:
			piValues[i] = (totalFormats > 0) ? totalFormats : 1;
			break;
		case WGL_DRAW_TO_WINDOW_ARB:
			piValues[i] = (pfd.dwFlags & PFD_DRAW_TO_WINDOW) ? TRUE : FALSE;
			break;
		case WGL_DRAW_TO_BITMAP_ARB:
			piValues[i] = (pfd.dwFlags & PFD_DRAW_TO_BITMAP) ? TRUE : FALSE;
			break;
		case WGL_SUPPORT_GDI_ARB:
			piValues[i] = (pfd.dwFlags & PFD_SUPPORT_GDI) ? TRUE : FALSE;
			break;
		case WGL_SUPPORT_OPENGL_ARB:
			piValues[i] = (pfd.dwFlags & PFD_SUPPORT_OPENGL) ? TRUE : FALSE;
			break;
		case WGL_DOUBLE_BUFFER_ARB:
			piValues[i] = (pfd.dwFlags & PFD_DOUBLEBUFFER) ? TRUE : FALSE;
			break;
		case WGL_STEREO_ARB:
			piValues[i] = (pfd.dwFlags & PFD_STEREO) ? TRUE : FALSE;
			break;
		case WGL_PIXEL_TYPE_ARB:
			piValues[i] = GetPixelType(pfd);
			break;
		case WGL_COLOR_BITS_ARB:
			piValues[i] = GetColorBits(pfd);
			break;
		case WGL_RED_BITS_ARB:
			piValues[i] = GetColorComponentBits(pfd.cRedBits, 0);
			break;
		case WGL_RED_SHIFT_ARB:
			piValues[i] = pfd.cRedShift;
			break;
		case WGL_GREEN_BITS_ARB:
			piValues[i] = GetColorComponentBits(pfd.cGreenBits, 1);
			break;
		case WGL_GREEN_SHIFT_ARB:
			piValues[i] = pfd.cGreenShift;
			break;
		case WGL_BLUE_BITS_ARB:
			piValues[i] = GetColorComponentBits(pfd.cBlueBits, 2);
			break;
		case WGL_BLUE_SHIFT_ARB:
			piValues[i] = pfd.cBlueShift;
			break;
		case WGL_ALPHA_BITS_ARB:
			piValues[i] = GetColorComponentBits(pfd.cAlphaBits, 3);
			break;
		case WGL_ALPHA_SHIFT_ARB:
			piValues[i] = pfd.cAlphaShift;
			break;
		case WGL_ACCUM_BITS_ARB:
			piValues[i] = pfd.cAccumBits;
			break;
		case WGL_ACCUM_RED_BITS_ARB:
			piValues[i] = pfd.cAccumRedBits;
			break;
		case WGL_ACCUM_GREEN_BITS_ARB:
			piValues[i] = pfd.cAccumGreenBits;
			break;
		case WGL_ACCUM_BLUE_BITS_ARB:
			piValues[i] = pfd.cAccumBlueBits;
			break;
		case WGL_ACCUM_ALPHA_BITS_ARB:
			piValues[i] = pfd.cAccumAlphaBits;
			break;
		case WGL_DEPTH_BITS_ARB:
			piValues[i] = GetDepthBits(pfd);
			break;
		case WGL_STENCIL_BITS_ARB:
			piValues[i] = GetStencilBits(pfd);
			break;
		case WGL_AUX_BUFFERS_ARB:
			piValues[i] = pfd.cAuxBuffers;
			break;
		case WGL_ACCELERATION_ARB:
			piValues[i] = GetAccelType();
			break;
		case WGL_SWAP_METHOD_ARB:
			piValues[i] = GetSwapMethod(pfd);
			break;
		case WGL_NUMBER_OVERLAYS_ARB:
		case WGL_NUMBER_UNDERLAYS_ARB:
		case WGL_NEED_PALETTE_ARB:
		case WGL_NEED_SYSTEM_PALETTE_ARB:
		case WGL_TRANSPARENT_ARB:
		case WGL_SHARE_DEPTH_ARB:
		case WGL_SHARE_STENCIL_ARB:
		case WGL_SHARE_ACCUM_ARB:
		case WGL_SWAP_LAYER_BUFFERS_ARB:
			piValues[i] = 0;
			break;
		default:
			piValues[i] = 0;
			unsupported = true;
			logPrintf("wglGetPixelFormatAttribivARB: unsupported attribute 0x%X\n", piAttributes[i]);
			break;
		}
	}

	if (unsupported) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	return TRUE;
}

OPENGL_API BOOL WINAPI wglGetPixelFormatAttribfvARB(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nAttributes, const int *piAttributes, FLOAT *pfValues)
{
	if (!piAttributes || !pfValues || nAttributes == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		logPrintf("wglGetPixelFormatAttribfvARB: invalid attribute list\n");
		return FALSE;
	}

	std::vector<int> values(nAttributes);
	if (!wglGetPixelFormatAttribivARB(hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, values.data())) {
		return FALSE;
	}

	for (UINT i = 0; i < nAttributes; ++i) {
		pfValues[i] = static_cast<FLOAT>(values[i]);
	}

	return TRUE;
}
