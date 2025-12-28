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
#include <unordered_map>

#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_buffer.hpp"

static std::unordered_map<GLuint, D3DBufferObject> g_bufferObjects;
static GLuint g_arrayBufferBinding = 0;
static GLuint g_elementArrayBufferBinding = 0;

void D3DBuffer_Bind( GLenum target, GLuint buffer )
{
	switch (target) {
		case GL_ARRAY_BUFFER_ARB:
			g_arrayBufferBinding = buffer;
			break;
		case GL_ELEMENT_ARRAY_BUFFER_ARB:
			g_elementArrayBufferBinding = buffer;
			break;
		default:
			D3DGlobal.lastError = E_INVALID_ENUM;
			return;
	}

	if (buffer != 0 && !D3DBuffer_GetObject(buffer, true)) {
		D3DGlobal.lastError = E_OUTOFMEMORY;
		return;
	}

	D3DGlobal.lastError = S_OK;
}

GLuint D3DBuffer_GetBinding( GLenum target )
{
	switch (target) {
		case GL_ARRAY_BUFFER_ARB:
			return g_arrayBufferBinding;
		case GL_ELEMENT_ARRAY_BUFFER_ARB:
			return g_elementArrayBufferBinding;
		default:
			D3DGlobal.lastError = E_INVALID_ENUM;
			return 0;
	}
}

D3DBufferObject *D3DBuffer_GetObject( GLuint buffer, bool create )
{
	if (!buffer) return nullptr;

	auto it = g_bufferObjects.find(buffer);
	if (it != g_bufferObjects.end()) return &it->second;
	if (!create) return nullptr;

	D3DBufferObject object = {};
	object.name = buffer;
	object.size = 0;
	object.usage = GL_STATIC_DRAW_ARB;
	object.data = nullptr;

	auto result = g_bufferObjects.emplace(buffer, object);
	if (!result.second) return nullptr;

	return &result.first->second;
}

OPENGL_API void WINAPI glBindBuffer( GLenum target, GLuint buffer )
{
	D3DBuffer_Bind( target, buffer );
}

OPENGL_API void WINAPI glBindBufferARB( GLenum target, GLuint buffer )
{
	glBindBuffer( target, buffer );
}

OPENGL_API GLboolean WINAPI glIsBufferARB( GLuint buffer )
{
	if (!buffer) {
		return GL_FALSE;
	}

	return D3DBuffer_GetObject( buffer, false ) ? GL_TRUE : GL_FALSE;
}
