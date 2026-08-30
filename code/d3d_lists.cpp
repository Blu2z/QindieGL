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
#include "d3d_lists.hpp"

//==================================================================================
// Display Lists
//----------------------------------------------------------------------------------
// Lambda-based recording: each GL command captured as std::function<void()>.
// Recording: glNewList..glEndList captures commands into a D3DDisplayList.
// Playback:  glCallList iterates and invokes each lambda.
//==================================================================================

// Global recording state
bool gDLRecording = false;
bool gDLExecute = false;
D3DDisplayList* gDLCurrent = nullptr;
GLuint gDLListBase = 0;
std::unordered_map<GLuint, D3DDisplayList*> gDLMap;
GLuint gDLNextId = 1;

static int gDLCallDepth = 0;
static const int DL_MAX_CALL_DEPTH = 64;

void D3DDisplayList_Cleanup()
{
	for ( auto& pair : gDLMap ) {
		delete pair.second;
	}
	gDLMap.clear();
	gDLRecording = false;
	gDLExecute = false;
	gDLCurrent = nullptr;
	gDLListBase = 0;
	gDLNextId = 1;
	gDLCallDepth = 0;
}

//==================================================================================

OPENGL_API void WINAPI glListBase( GLuint base )
{
	gDLListBase = base;
}

OPENGL_API void WINAPI glNewList( GLuint list, GLenum mode )
{
	if ( gDLRecording ) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		logPrintf( "WARNING: glNewList called while already recording\n" );
		return;
	}
	if ( list == 0 ) {
		QGL_SET_ERROR(E_INVALID_ENUM);
		logPrintf( "WARNING: glNewList called with list 0\n" );
		return;
	}

	// Create or clear the list
	auto it = gDLMap.find( list );
	if ( it != gDLMap.end() ) {
		it->second->commands.clear();
		gDLCurrent = it->second;
	} else {
		gDLCurrent = new D3DDisplayList;
		gDLMap[list] = gDLCurrent;
	}

	gDLRecording = true;
	gDLExecute = ( mode == GL_COMPILE_AND_EXECUTE );

	logPrintf( "glNewList(%u, %s)\n", list, gDLExecute ? "GL_COMPILE_AND_EXECUTE" : "GL_COMPILE" );
}

OPENGL_API void WINAPI glEndList()
{
	if ( !gDLRecording ) {
		QGL_SET_ERROR(E_INVALID_OPERATION);
		logPrintf( "WARNING: glEndList called without glNewList\n" );
		return;
	}

	logPrintf( "glEndList: %d commands recorded\n", (int)gDLCurrent->commands.size() );

	gDLRecording = false;
	gDLExecute = false;
	gDLCurrent = nullptr;
}

OPENGL_API void WINAPI glCallList( GLuint list )
{
	// Record nested glCallList (resolved at playback time)
	if ( gDLRecording ) {
		DL_RecordCommand( [list]() { glCallList( list ); } );
		if ( !gDLExecute ) return;
	}

	auto it = gDLMap.find( list );
	if ( it == gDLMap.end() ) return;

	if ( gDLCallDepth >= DL_MAX_CALL_DEPTH ) {
		logPrintf( "WARNING: glCallList recursion depth exceeded\n" );
		return;
	}

	// Temporarily disable recording so child list commands don't
	// get re-recorded into the parent during COMPILE_AND_EXECUTE
	bool wasRecording = gDLRecording;
	D3DDisplayList* wasCurrent = gDLCurrent;
	gDLRecording = false;

	++gDLCallDepth;
	D3DDisplayList* dl = it->second;
	for ( auto& cmd : dl->commands ) {
		cmd();
	}
	--gDLCallDepth;

	gDLRecording = wasRecording;
	gDLCurrent = wasCurrent;
}

//==================================================================================
// Decode a list index from the raw type-encoded data
//==================================================================================
static GLuint DL_DecodeListIndex( GLenum type, const GLubyte* data, int index )
{
	switch ( type ) {
	case GL_BYTE:
		return (GLuint)( (const GLbyte*)data )[index];
	case GL_UNSIGNED_BYTE:
		return (GLuint)data[index];
	case GL_SHORT:
		return (GLuint)( (const GLshort*)data )[index];
	case GL_UNSIGNED_SHORT:
		return (GLuint)( (const GLushort*)data )[index];
	case GL_INT:
		return (GLuint)( (const GLint*)data )[index];
	case GL_UNSIGNED_INT:
		return ( (const GLuint*)data )[index];
	case GL_FLOAT:
		return (GLuint)( (const GLfloat*)data )[index];
	case GL_2_BYTES: {
		int off = index * 2;
		return ( (GLuint)data[off] << 8 ) | data[off + 1];
	}
	case GL_3_BYTES: {
		int off = index * 3;
		return ( (GLuint)data[off] << 16 ) | ( (GLuint)data[off + 1] << 8 ) | data[off + 2];
	}
	case GL_4_BYTES: {
		int off = index * 4;
		return ( (GLuint)data[off] << 24 ) | ( (GLuint)data[off + 1] << 16 ) |
		       ( (GLuint)data[off + 2] << 8 ) | data[off + 3];
	}
	default:
		return 0;
	}
}

OPENGL_API void WINAPI glCallLists( GLsizei n, GLenum type, const GLvoid* lists )
{
	if ( n <= 0 || !lists ) return;

	if ( gDLRecording ) {
		// Deep-copy the list data for deferred playback
		size_t elemSize = 1;
		switch ( type ) {
		case GL_BYTE: case GL_UNSIGNED_BYTE:    elemSize = 1; break;
		case GL_SHORT: case GL_UNSIGNED_SHORT:  elemSize = 2; break;
		case GL_INT: case GL_UNSIGNED_INT:
		case GL_FLOAT:                          elemSize = 4; break;
		case GL_2_BYTES:                        elemSize = 2; break;
		case GL_3_BYTES:                        elemSize = 3; break;
		case GL_4_BYTES:                        elemSize = 4; break;
		}
		std::vector<GLubyte> dataCopy( (const GLubyte*)lists, (const GLubyte*)lists + n * elemSize );
		DL_RecordCommand( [n, type, dataCopy]() {
			glCallLists( n, type, dataCopy.data() );
		} );
		if ( !gDLExecute ) return;
	}

	if ( gDLCallDepth >= DL_MAX_CALL_DEPTH ) {
		logPrintf( "WARNING: glCallLists recursion depth exceeded\n" );
		return;
	}

	// Temporarily disable recording so child commands don't
	// get re-recorded during COMPILE_AND_EXECUTE
	bool wasRecording = gDLRecording;
	D3DDisplayList* wasCurrent = gDLCurrent;
	gDLRecording = false;

	const GLubyte* data = (const GLubyte*)lists;
	++gDLCallDepth;
	for ( GLsizei i = 0; i < n; i++ ) {
		GLuint idx = DL_DecodeListIndex( type, data, i ) + gDLListBase;
		auto it = gDLMap.find( idx );
		if ( it != gDLMap.end() ) {
			D3DDisplayList* dl = it->second;
			for ( auto& cmd : dl->commands ) {
				cmd();
			}
		}
	}
	--gDLCallDepth;

	gDLRecording = wasRecording;
	gDLCurrent = wasCurrent;
}

OPENGL_API void WINAPI glDeleteLists( GLuint list, GLsizei range )
{
	for ( GLsizei i = 0; i < range; i++ ) {
		auto it = gDLMap.find( list + i );
		if ( it != gDLMap.end() ) {
			delete it->second;
			gDLMap.erase( it );
		}
	}
	logPrintf( "glDeleteLists(%u, %d)\n", list, range );
}

OPENGL_API GLuint WINAPI glGenLists( GLsizei range )
{
	if ( range <= 0 ) return 0;

	// Find a contiguous range of unused IDs starting from gDLNextId
	GLuint base = gDLNextId;
	bool conflict = true;
	while ( conflict ) {
		conflict = false;
		for ( GLsizei i = 0; i < range; i++ ) {
			if ( gDLMap.find( base + i ) != gDLMap.end() ) {
				base = base + i + 1;
				conflict = true;
				break;
			}
		}
	}

	// Reserve the IDs with empty lists
	for ( GLsizei i = 0; i < range; i++ ) {
		gDLMap[base + i] = new D3DDisplayList;
	}

	gDLNextId = base + range;
	logPrintf( "glGenLists(%d) = %u\n", range, base );
	return base;
}

OPENGL_API GLboolean WINAPI glIsList( GLuint list )
{
	return ( gDLMap.find( list ) != gDLMap.end() ) ? GL_TRUE : GL_FALSE;
}
