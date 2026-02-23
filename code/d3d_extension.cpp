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
#include "d3d_extension.hpp"
#include "d3d_arb_program.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>

//This will enable export of our custom extensions
#define ALLOW_CHS_EXTENSIONS

//TODO:
//GL_ARB_depth_texture
//GL_ARB_vertex_buffer_object
//GL_ARB_point_sprite

OPENGL_API const char* WINAPI wglGetExtensionsStringARB( HDC )
{
	return D3DGlobal.szWExtensions;
}

//=========================================
// ARB_vertex_program / ARB_fragment_program
//-----------------------------------------
// Silently accept all calls; programs are
// not compiled to D3D shaders yet, so
// rendering falls back to fixed-function.
//=========================================

namespace {
	static GLuint gARBProgramNextID = 1;
	static std::set<GLuint> gARBProgramIDs;
	static GLuint gARBBoundVertexProgram = 0;
	static GLuint gARBBoundFragmentProgram = 0;
	static DWORD gARBVertexProgramEnabled = 0;
	static DWORD gARBFragmentProgramEnabled = 0;

	// Per-program stored source (for future compilation)
	struct ARBProgramData {
		GLenum target; // GL_VERTEX_PROGRAM_ARB or GL_FRAGMENT_PROGRAM_ARB
		std::string source;
	};
	static std::map<GLuint, ARBProgramData> gARBProgramStore;

	// Parameter storage
	static const int ARB_MAX_ENV_PARAMS = 256;
	static const int ARB_MAX_LOCAL_PARAMS = 256;
	static GLfloat gARBEnvParamsVP[ARB_MAX_ENV_PARAMS][4];
	static GLfloat gARBEnvParamsFP[ARB_MAX_ENV_PARAMS][4];
	static GLfloat gARBLocalParamsVP[ARB_MAX_LOCAL_PARAMS][4];
	static GLfloat gARBLocalParamsFP[ARB_MAX_LOCAL_PARAMS][4];


	GLfloat (*ARB_LocalParams_Internal( GLenum target ))[4] {
		return (target == GL_VERTEX_PROGRAM_ARB) ? gARBLocalParamsVP : gARBLocalParamsFP;
	}
	GLfloat (*ARB_EnvParams_Internal( GLenum target ))[4] {
		return (target == GL_VERTEX_PROGRAM_ARB) ? gARBEnvParamsVP : gARBEnvParamsFP;
	}
}

// Accessors for d3d_arb_program.cpp (cannot be in anonymous namespace)
GLfloat (*ARB_EnvParams( GLenum target ))[4] { return ARB_EnvParams_Internal( target ); }
GLfloat (*ARB_LocalParams( GLenum target ))[4] { return ARB_LocalParams_Internal( target ); }
GLuint ARB_GetBoundVertexProgram() { return gARBBoundVertexProgram; }
GLuint ARB_GetBoundFragmentProgram() { return gARBBoundFragmentProgram; }

OPENGL_API void WINAPI glProgramStringARB( GLenum target, GLenum format, GLsizei len, const GLvoid *string )
{
	GLuint bound = (target == GL_VERTEX_PROGRAM_ARB) ? gARBBoundVertexProgram : gARBBoundFragmentProgram;
	if (bound && string && len > 0 && format == GL_PROGRAM_FORMAT_ASCII_ARB) {
		ARBProgramData &pd = gARBProgramStore[bound];
		pd.target = target;
		pd.source.assign( static_cast<const char*>(string), len );
		logPrintf("glProgramStringARB: stored %s program %u (%d bytes)\n",
			target == GL_VERTEX_PROGRAM_ARB ? "VP" : "FP", bound, len);

		// Compile ARB program to D3D9 shader
		if ( D3DGlobal.pDevice && !D3DGlobal.settings.enableARBProgramsStub ) {
			std::string errorStr;
			if ( !ARB_CompileProgram( bound, target, static_cast<const char*>(string), len, errorStr ) ) {
				logPrintf("WARNING: ARB program %u compilation failed: %s\n", bound, errorStr.c_str());
			}
		}
	}
}

OPENGL_API void WINAPI glBindProgramARB( GLenum target, GLuint program )
{
	// ID 0 means unbind
	if (program != 0 && gARBProgramIDs.find(program) == gARBProgramIDs.end()) {
		// Auto-create if not existing (per spec)
		gARBProgramIDs.insert(program);
	}
	if (target == GL_VERTEX_PROGRAM_ARB)
		gARBBoundVertexProgram = program;
	else if (target == GL_FRAGMENT_PROGRAM_ARB)
		gARBBoundFragmentProgram = program;
}

OPENGL_API void WINAPI glDeleteProgramsARB( GLsizei n, const GLuint *programs )
{
	if (!programs) return;
	for (GLsizei i = 0; i < n; ++i) {
		gARBProgramIDs.erase(programs[i]);
		gARBProgramStore.erase(programs[i]);
		// Clean up compiled shader
		ARB_DeleteCompiledProgram( programs[i] );
		if (gARBBoundVertexProgram == programs[i]) gARBBoundVertexProgram = 0;
		if (gARBBoundFragmentProgram == programs[i]) gARBBoundFragmentProgram = 0;
	}
}

OPENGL_API void WINAPI glGenProgramsARB( GLsizei n, GLuint *programs )
{
	if (!programs || n <= 0) return;
	for (GLsizei i = 0; i < n; ++i) {
		GLuint id = gARBProgramNextID++;
		gARBProgramIDs.insert(id);
		programs[i] = id;
	}
}

OPENGL_API void WINAPI glProgramEnvParameter4dARB( GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w )
{
	if (index < ARB_MAX_ENV_PARAMS) {
		GLfloat (*p)[4] = ARB_EnvParams(target);
		p[index][0] = (GLfloat)x; p[index][1] = (GLfloat)y; p[index][2] = (GLfloat)z; p[index][3] = (GLfloat)w;
	}
}

OPENGL_API void WINAPI glProgramEnvParameter4dvARB( GLenum target, GLuint index, const GLdouble *v )
{
	if (v && index < ARB_MAX_ENV_PARAMS) {
		GLfloat (*p)[4] = ARB_EnvParams(target);
		p[index][0] = (GLfloat)v[0]; p[index][1] = (GLfloat)v[1]; p[index][2] = (GLfloat)v[2]; p[index][3] = (GLfloat)v[3];
	}
}

OPENGL_API void WINAPI glProgramEnvParameter4fARB( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	if (index < ARB_MAX_ENV_PARAMS) {
		GLfloat (*p)[4] = ARB_EnvParams(target);
		p[index][0] = x; p[index][1] = y; p[index][2] = z; p[index][3] = w;
	}
}

OPENGL_API void WINAPI glProgramEnvParameter4fvARB( GLenum target, GLuint index, const GLfloat *v )
{
	if (v && index < ARB_MAX_ENV_PARAMS) {
		GLfloat (*p)[4] = ARB_EnvParams(target);
		p[index][0] = v[0]; p[index][1] = v[1]; p[index][2] = v[2]; p[index][3] = v[3];
	}
}

OPENGL_API void WINAPI glProgramLocalParameter4dARB( GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w )
{
	if (index < ARB_MAX_LOCAL_PARAMS) {
		GLfloat (*p)[4] = ARB_LocalParams(target);
		p[index][0] = (GLfloat)x; p[index][1] = (GLfloat)y; p[index][2] = (GLfloat)z; p[index][3] = (GLfloat)w;
	}
}

OPENGL_API void WINAPI glProgramLocalParameter4dvARB( GLenum target, GLuint index, const GLdouble *v )
{
	if (v && index < ARB_MAX_LOCAL_PARAMS) {
		GLfloat (*p)[4] = ARB_LocalParams(target);
		p[index][0] = (GLfloat)v[0]; p[index][1] = (GLfloat)v[1]; p[index][2] = (GLfloat)v[2]; p[index][3] = (GLfloat)v[3];
	}
}

OPENGL_API void WINAPI glProgramLocalParameter4fARB( GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	if (index < ARB_MAX_LOCAL_PARAMS) {
		GLfloat (*p)[4] = ARB_LocalParams(target);
		p[index][0] = x; p[index][1] = y; p[index][2] = z; p[index][3] = w;
	}
}

OPENGL_API void WINAPI glProgramLocalParameter4fvARB( GLenum target, GLuint index, const GLfloat *v )
{
	if (v && index < ARB_MAX_LOCAL_PARAMS) {
		GLfloat (*p)[4] = ARB_LocalParams(target);
		p[index][0] = v[0]; p[index][1] = v[1]; p[index][2] = v[2]; p[index][3] = v[3];
	}
}

OPENGL_API void WINAPI glGetProgramEnvParameterdvARB( GLenum target, GLuint index, GLdouble *params )
{
	if (params) {
		if (index < ARB_MAX_ENV_PARAMS) {
			GLfloat (*p)[4] = ARB_EnvParams(target);
			params[0] = p[index][0]; params[1] = p[index][1]; params[2] = p[index][2]; params[3] = p[index][3];
		} else {
			params[0] = params[1] = params[2] = params[3] = 0.0;
		}
	}
}

OPENGL_API void WINAPI glGetProgramEnvParameterfvARB( GLenum target, GLuint index, GLfloat *params )
{
	if (params) {
		if (index < ARB_MAX_ENV_PARAMS) {
			GLfloat (*p)[4] = ARB_EnvParams(target);
			params[0] = p[index][0]; params[1] = p[index][1]; params[2] = p[index][2]; params[3] = p[index][3];
		} else {
			params[0] = params[1] = params[2] = params[3] = 0.0f;
		}
	}
}

OPENGL_API void WINAPI glGetProgramLocalParameterdvARB( GLenum target, GLuint index, GLdouble *params )
{
	if (params) {
		if (index < ARB_MAX_LOCAL_PARAMS) {
			GLfloat (*p)[4] = ARB_LocalParams(target);
			params[0] = p[index][0]; params[1] = p[index][1]; params[2] = p[index][2]; params[3] = p[index][3];
		} else {
			params[0] = params[1] = params[2] = params[3] = 0.0;
		}
	}
}

OPENGL_API void WINAPI glGetProgramLocalParameterfvARB( GLenum target, GLuint index, GLfloat *params )
{
	if (params) {
		if (index < ARB_MAX_LOCAL_PARAMS) {
			GLfloat (*p)[4] = ARB_LocalParams(target);
			params[0] = p[index][0]; params[1] = p[index][1]; params[2] = p[index][2]; params[3] = p[index][3];
		} else {
			params[0] = params[1] = params[2] = params[3] = 0.0f;
		}
	}
}

OPENGL_API void WINAPI glGetProgramivARB( GLenum target, GLenum pname, GLint *params )
{
	if (!params) return;
	switch (pname) {
	case GL_PROGRAM_LENGTH_ARB: {
		GLuint bound = (target == GL_VERTEX_PROGRAM_ARB) ? gARBBoundVertexProgram : gARBBoundFragmentProgram;
		auto it = gARBProgramStore.find(bound);
		params[0] = (it != gARBProgramStore.end()) ? (GLint)it->second.source.size() : 0;
		break;
	}
	case GL_PROGRAM_FORMAT_ARB:
		params[0] = GL_PROGRAM_FORMAT_ASCII_ARB;
		break;
	case GL_PROGRAM_BINDING_ARB:
		params[0] = (target == GL_VERTEX_PROGRAM_ARB) ? gARBBoundVertexProgram : gARBBoundFragmentProgram;
		break;
	case GL_PROGRAM_ERROR_POSITION_ARB:
		params[0] = -1; // no error
		break;
	case GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB:
		params[0] = GL_TRUE;
		break;
	// Resource limits - report generous values
	case GL_MAX_PROGRAM_INSTRUCTIONS_ARB:
	case GL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB:
		params[0] = 4096;
		break;
	case GL_MAX_PROGRAM_TEMPORARIES_ARB:
	case GL_MAX_PROGRAM_NATIVE_TEMPORARIES_ARB:
		params[0] = 256;
		break;
	case GL_MAX_PROGRAM_PARAMETERS_ARB:
	case GL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB:
		params[0] = ARB_MAX_ENV_PARAMS;
		break;
	case GL_MAX_PROGRAM_ATTRIBS_ARB:
	case GL_MAX_PROGRAM_NATIVE_ATTRIBS_ARB:
		params[0] = 16;
		break;
	case GL_MAX_PROGRAM_ADDRESS_REGISTERS_ARB:
	case GL_MAX_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB:
		params[0] = (target == GL_VERTEX_PROGRAM_ARB) ? 1 : 0;
		break;
	case GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB:
		params[0] = ARB_MAX_LOCAL_PARAMS;
		break;
	case GL_MAX_PROGRAM_ENV_PARAMETERS_ARB:
		params[0] = ARB_MAX_ENV_PARAMS;
		break;
	case GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB:
	case GL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB:
		params[0] = (target == GL_FRAGMENT_PROGRAM_ARB) ? 4096 : 0;
		break;
	case GL_MAX_PROGRAM_TEX_INSTRUCTIONS_ARB:
	case GL_MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB:
		params[0] = (target == GL_FRAGMENT_PROGRAM_ARB) ? 4096 : 0;
		break;
	case GL_MAX_PROGRAM_TEX_INDIRECTIONS_ARB:
	case GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB:
		params[0] = (target == GL_FRAGMENT_PROGRAM_ARB) ? 4096 : 0;
		break;
	case GL_MAX_PROGRAM_MATRIX_STACK_DEPTH_ARB:
		params[0] = 1;
		break;
	case GL_MAX_PROGRAM_MATRICES_ARB:
		params[0] = 8;
		break;
	default:
		params[0] = 0;
		break;
	}
}

OPENGL_API void WINAPI glGetProgramStringARB( GLenum target, GLenum pname, GLvoid *string )
{
	if (!string) return;
	if (pname == GL_PROGRAM_STRING_ARB) {
		GLuint bound = (target == GL_VERTEX_PROGRAM_ARB) ? gARBBoundVertexProgram : gARBBoundFragmentProgram;
		auto it = gARBProgramStore.find(bound);
		if (it != gARBProgramStore.end() && !it->second.source.empty()) {
			memcpy(string, it->second.source.c_str(), it->second.source.size());
		}
	} else if (pname == GL_PROGRAM_ERROR_STRING_ARB) {
		static_cast<char*>(string)[0] = '\0';
	}
}

OPENGL_API GLboolean WINAPI glIsProgramARB( GLuint program )
{
	return (gARBProgramIDs.find(program) != gARBProgramIDs.end()) ? GL_TRUE : GL_FALSE;
}

//=========================================
// ARB_vertex_program vertex attributes
//-----------------------------------------
// Stubs - accept and store per-vertex
// generic attributes for future use.
//=========================================

namespace {
	static GLfloat gVertexAttribs[16][4];
}

OPENGL_API void WINAPI glVertexAttrib1fARB( GLuint index, GLfloat x )
{
	if (index < 16) { gVertexAttribs[index][0] = x; gVertexAttribs[index][1] = 0; gVertexAttribs[index][2] = 0; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib2fARB( GLuint index, GLfloat x, GLfloat y )
{
	if (index < 16) { gVertexAttribs[index][0] = x; gVertexAttribs[index][1] = y; gVertexAttribs[index][2] = 0; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib3fARB( GLuint index, GLfloat x, GLfloat y, GLfloat z )
{
	if (index < 16) { gVertexAttribs[index][0] = x; gVertexAttribs[index][1] = y; gVertexAttribs[index][2] = z; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib4fARB( GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w )
{
	if (index < 16) { gVertexAttribs[index][0] = x; gVertexAttribs[index][1] = y; gVertexAttribs[index][2] = z; gVertexAttribs[index][3] = w; }
}
OPENGL_API void WINAPI glVertexAttrib1fvARB( GLuint index, const GLfloat *v )
{
	if (index < 16 && v) { gVertexAttribs[index][0] = v[0]; gVertexAttribs[index][1] = 0; gVertexAttribs[index][2] = 0; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib2fvARB( GLuint index, const GLfloat *v )
{
	if (index < 16 && v) { gVertexAttribs[index][0] = v[0]; gVertexAttribs[index][1] = v[1]; gVertexAttribs[index][2] = 0; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib3fvARB( GLuint index, const GLfloat *v )
{
	if (index < 16 && v) { gVertexAttribs[index][0] = v[0]; gVertexAttribs[index][1] = v[1]; gVertexAttribs[index][2] = v[2]; gVertexAttribs[index][3] = 1; }
}
OPENGL_API void WINAPI glVertexAttrib4fvARB( GLuint index, const GLfloat *v )
{
	if (index < 16 && v) { gVertexAttribs[index][0] = v[0]; gVertexAttribs[index][1] = v[1]; gVertexAttribs[index][2] = v[2]; gVertexAttribs[index][3] = v[3]; }
}
OPENGL_API void WINAPI glVertexAttrib4NubvARB( GLuint index, const GLubyte *v )
{
	if (index < 16 && v) { gVertexAttribs[index][0] = v[0]/255.0f; gVertexAttribs[index][1] = v[1]/255.0f; gVertexAttribs[index][2] = v[2]/255.0f; gVertexAttribs[index][3] = v[3]/255.0f; }
}
OPENGL_API void WINAPI glVertexAttribPointerARB( GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid * )
{
	// stub - vertex attrib arrays not yet routed to D3D
}
OPENGL_API void WINAPI glEnableVertexAttribArrayARB( GLuint )
{
	// stub
}
OPENGL_API void WINAPI glDisableVertexAttribArrayARB( GLuint )
{
	// stub
}
OPENGL_API void WINAPI glGetVertexAttribfvARB( GLuint index, GLenum pname, GLfloat *params )
{
	if (!params) return;
	if (pname == GL_CURRENT_VERTEX_ATTRIB_ARB && index < 16) {
		params[0] = gVertexAttribs[index][0]; params[1] = gVertexAttribs[index][1];
		params[2] = gVertexAttribs[index][2]; params[3] = gVertexAttribs[index][3];
	} else {
		params[0] = 0;
	}
}
OPENGL_API void WINAPI glGetVertexAttribivARB( GLuint index, GLenum pname, GLint *params )
{
	if (!params) return;
	switch (pname) {
	case GL_VERTEX_ATTRIB_ARRAY_ENABLED_ARB: params[0] = 0; break;
	case GL_VERTEX_ATTRIB_ARRAY_SIZE_ARB: params[0] = 4; break;
	case GL_VERTEX_ATTRIB_ARRAY_STRIDE_ARB: params[0] = 0; break;
	case GL_VERTEX_ATTRIB_ARRAY_TYPE_ARB: params[0] = GL_FLOAT; break;
	case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED_ARB: params[0] = 0; break;
	default: params[0] = 0; break;
	}
}
OPENGL_API void WINAPI glGetVertexAttribPointervARB( GLuint, GLenum, GLvoid **pointer )
{
	if (pointer) *pointer = NULL;
}

//=========================================
// GL_ARB_occlusion_query stubs
//-----------------------------------------
// Accept calls silently; queries always
// report "result available" with 1 sample.
//=========================================

namespace {
	static GLuint gOccQueryNextID = 1;
	static std::set<GLuint> gOccQueryIDs;
	static GLuint gOccQueryActive = 0;
}

OPENGL_API void WINAPI glGenQueriesARB( GLsizei n, GLuint *ids )
{
	if (!ids || n <= 0) return;
	for (GLsizei i = 0; i < n; ++i) {
		GLuint id = gOccQueryNextID++;
		gOccQueryIDs.insert(id);
		ids[i] = id;
	}
}
OPENGL_API void WINAPI glDeleteQueriesARB( GLsizei n, const GLuint *ids )
{
	if (!ids) return;
	for (GLsizei i = 0; i < n; ++i) {
		gOccQueryIDs.erase(ids[i]);
		if (gOccQueryActive == ids[i]) gOccQueryActive = 0;
	}
}
OPENGL_API GLboolean WINAPI glIsQueryARB( GLuint id )
{
	return (gOccQueryIDs.find(id) != gOccQueryIDs.end()) ? GL_TRUE : GL_FALSE;
}
OPENGL_API void WINAPI glBeginQueryARB( GLenum, GLuint id )
{
	gOccQueryActive = id;
}
OPENGL_API void WINAPI glEndQueryARB( GLenum )
{
	gOccQueryActive = 0;
}
OPENGL_API void WINAPI glGetQueryivARB( GLenum, GLenum pname, GLint *params )
{
	if (!params) return;
	switch (pname) {
	case GL_QUERY_COUNTER_BITS_ARB: params[0] = 32; break;
	case 0x8865 /*GL_CURRENT_QUERY_ARB*/: params[0] = gOccQueryActive; break;
	default: params[0] = 0; break;
	}
}
OPENGL_API void WINAPI glGetQueryObjectivARB( GLuint, GLenum pname, GLint *params )
{
	if (!params) return;
	switch (pname) {
	case GL_QUERY_RESULT_ARB: params[0] = 1; break; // always 1 sample passed
	case GL_QUERY_RESULT_AVAILABLE_ARB: params[0] = GL_TRUE; break;
	default: params[0] = 0; break;
	}
}
OPENGL_API void WINAPI glGetQueryObjectuivARB( GLuint, GLenum pname, GLuint *params )
{
	if (!params) return;
	switch (pname) {
	case GL_QUERY_RESULT_ARB: params[0] = 1; break;
	case GL_QUERY_RESULT_AVAILABLE_ARB: params[0] = GL_TRUE; break;
	default: params[0] = 0; break;
	}
}

//=========================================
// GL_ARB_point_parameters stubs
//=========================================
OPENGL_API void WINAPI glPointParameterfARB( GLenum, GLfloat )
{
	// stub - point parameters not implemented
}
OPENGL_API void WINAPI glPointParameterfvARB( GLenum, const GLfloat * )
{
	// stub
}

typedef struct glext_entry_point_s
{
	const char *name;
	const char *extname;
	int  enabled;
	PROC func;
} glext_entry_point_t;

#define GL_EXT_ENTRY_POINT( postfix, extname, func, defaultEnable )		{ #func, "GL_" ## postfix ## "_" ## extname, defaultEnable, (PROC)func }, \
																		{ #func ## postfix, "GL_" ## postfix ## "_" ## extname, defaultEnable, (PROC)func }
#define WGL_EXT_ENTRY_POINT( postfix, extname, func, defaultEnable )	{ #func, "WGL_" ## postfix ## "_" ## extname, defaultEnable, (PROC)func }, \
																		{ #func ## postfix, "WGL_" ## postfix ## "_" ## extname, defaultEnable, (PROC)func }

static glext_entry_point_t glext_EntryPoints[] =
{
	//GL_EXT_texture_object (not and extension in GL 1.1)
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glDeleteTextures, -2 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glGenTextures, -2 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glIsTexture, -2 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glBindTexture, -2 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glAreTexturesResident, -2 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture_object", glPrioritizeTextures, -2 ),

	//GL_ARB_texture_compression
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexImage1D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexImage2D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexImage3D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexSubImage1D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexSubImage2D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glCompressedTexSubImage3D, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "texture_compression", glGetCompressedTexImage, -1 ),

	//GL_ARB_vertex_buffer_object
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glBindBuffer, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glDeleteBuffersARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glGenBuffersARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glIsBufferARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glBufferDataARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glGetBufferSubDataARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glMapBufferARB, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "vertex_buffer_object", glUnmapBufferARB, -1 ),

	//GL_ARB_multitexture
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glActiveTexture, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glClientActiveTexture, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1s, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1i, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1f, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1d, -1 ),
	{ "glMultiTexCoord1dEXT", "GL_EXT_multitexture", -1, (PROC)glMultiTexCoord1dEXT },
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2s, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2i, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2f, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2d, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3s, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3i, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3f, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3d, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4s, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4i, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4f, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4d, -1 ),
	{ "glMultiTexCoord4sdARB", "GL_ARB_multitexture", -1, (PROC)glMultiTexCoord4sdARB },
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1sv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1iv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1fv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord1dv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2sv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2iv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2fv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord2dv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3sv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3iv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3fv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord3dv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4sv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4iv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4fv, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "multitexture", glMultiTexCoord4dv, -1 ),

	//GL_ARB_transpose_matrix
	GL_EXT_ENTRY_POINT( "ARB", "transpose_matrix", glLoadTransposeMatrixf, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "transpose_matrix", glLoadTransposeMatrixd, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "transpose_matrix", glMultTransposeMatrixf, -1 ),
	GL_EXT_ENTRY_POINT( "ARB", "transpose_matrix", glMultTransposeMatrixd, -1 ),

	//GL_ARB_program (stub)
	{ "glProgramStringARB", "GL_ARB_program", -1, (PROC)glProgramStringARB },
	{ "glBindProgramARB", "GL_ARB_program", -1, (PROC)glBindProgramARB },
	{ "glDeleteProgramsARB", "GL_ARB_program", -1, (PROC)glDeleteProgramsARB },
	{ "glGenProgramsARB", "GL_ARB_program", -1, (PROC)glGenProgramsARB },
	{ "glProgramEnvParameter4dARB", "GL_ARB_program", -1, (PROC)glProgramEnvParameter4dARB },
	{ "glProgramEnvParameter4dvARB", "GL_ARB_program", -1, (PROC)glProgramEnvParameter4dvARB },
	{ "glProgramEnvParameter4fARB", "GL_ARB_program", -1, (PROC)glProgramEnvParameter4fARB },
	{ "glProgramEnvParameter4fvARB", "GL_ARB_program", -1, (PROC)glProgramEnvParameter4fvARB },
	{ "glProgramLocalParameter4dARB", "GL_ARB_program", -1, (PROC)glProgramLocalParameter4dARB },
	{ "glProgramLocalParameter4dvARB", "GL_ARB_program", -1, (PROC)glProgramLocalParameter4dvARB },
	{ "glProgramLocalParameter4fARB", "GL_ARB_program", -1, (PROC)glProgramLocalParameter4fARB },
	{ "glProgramLocalParameter4fvARB", "GL_ARB_program", -1, (PROC)glProgramLocalParameter4fvARB },
	{ "glGetProgramEnvParameterdvARB", "GL_ARB_program", -1, (PROC)glGetProgramEnvParameterdvARB },
	{ "glGetProgramEnvParameterfvARB", "GL_ARB_program", -1, (PROC)glGetProgramEnvParameterfvARB },
	{ "glGetProgramLocalParameterdvARB", "GL_ARB_program", -1, (PROC)glGetProgramLocalParameterdvARB },
	{ "glGetProgramLocalParameterfvARB", "GL_ARB_program", -1, (PROC)glGetProgramLocalParameterfvARB },
	{ "glGetProgramivARB", "GL_ARB_program", -1, (PROC)glGetProgramivARB },
	{ "glGetProgramStringARB", "GL_ARB_program", -1, (PROC)glGetProgramStringARB },
	{ "glIsProgramARB", "GL_ARB_program", -1, (PROC)glIsProgramARB },

	//GL_EXT_blend_color
	GL_EXT_ENTRY_POINT( "EXT", "blend_color", glBlendColor, -1 ),

	//GL_EXT_blend_minmax and GL_EXT_blend_subtract
	GL_EXT_ENTRY_POINT( "EXT", "blend_minmax", glBlendEquation, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "blend_subtract", glBlendEquation, -1 ),

	//GL_EXT_compiled_vertex_array
	GL_EXT_ENTRY_POINT( "EXT", "compiled_vertex_array", glLockArrays, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "compiled_vertex_array", glUnlockArrays, -1 ),

	//GL_EXT_draw_range_elements
	GL_EXT_ENTRY_POINT( "EXT", "draw_range_elements", glDrawRangeElements, -1 ),

	//GL_EXT_multi_draw_arrays
	GL_EXT_ENTRY_POINT( "EXT", "multi_draw_arrays", glMultiDrawArrays, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "multi_draw_arrays", glMultiDrawElements, -1 ),
	GL_EXT_ENTRY_POINT( "SUN", "multi_draw_arrays", glMultiDrawArrays, -1 ),
	GL_EXT_ENTRY_POINT( "SUN", "multi_draw_arrays", glMultiDrawElements, -1 ),

	//GL_EXT_secondary_color
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3b, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3bv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3d, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3dv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3f, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3fv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3i, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3iv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3s, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3sv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3ub, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3ubv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3ui, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3uiv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3us, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColor3usv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "secondary_color", glSecondaryColorPointer, -1 ),

	//GL_EXT_fog_coord
	GL_EXT_ENTRY_POINT( "EXT", "fog_coord", glFogCoordd, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "fog_coord", glFogCoordf, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "fog_coord", glFogCoorddv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "fog_coord", glFogCoordfv, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "fog_coord", glFogCoordPointer, -1 ),

	//GL_SGIS_multitexture
	GL_EXT_ENTRY_POINT( "SGIS", "multitexture", glSelectTexture, -1 ),
	GL_EXT_ENTRY_POINT( "SGIS", "multitexture", glMTexCoord2f, -1 ),
	GL_EXT_ENTRY_POINT( "SGIS", "multitexture", glMTexCoord2fv, -1 ),

	//GL_EXT_texture3D
	GL_EXT_ENTRY_POINT( "EXT", "texture3D", glTexImage3D, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture3D", glTexSubImage3D, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture3D", glCopyTexImage3D, -1 ),
	GL_EXT_ENTRY_POINT( "EXT", "texture3D", glCopyTexSubImage3D, -1 ),

	//GL_EXT_stencil_two_side
	GL_EXT_ENTRY_POINT( "EXT", "stencil_two_side", glActiveStencilFace, -1 ),

	//GL_ATI_pn_triangles
	{ "glPNTrianglesiATI", "GL_ATI_pn_triangles", -1, (PROC)glPNTrianglesiATI },
	{ "glPNTrianglesfATI", "GL_ATI_pn_triangles", -1, (PROC)glPNTrianglesfATI },

	//WGL_EXT_swap_control
	WGL_EXT_ENTRY_POINT( "EXT", "swap_control", wglSwapInterval, -2 ),
	WGL_EXT_ENTRY_POINT( "EXT", "swap_control", wglGetSwapInterval, -2 ),

	//WGL_ARB_extensions_string
	WGL_EXT_ENTRY_POINT( "ARB", "extensions_string", wglGetExtensionsStringARB, -2 ),

	//WGL_ARB_pbuffer
	WGL_EXT_ENTRY_POINT( "ARB", "pbuffer", wglCreatePbufferARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pbuffer", wglGetPbufferDCARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pbuffer", wglReleasePbufferDCARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pbuffer", wglDestroyPbufferARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pbuffer", wglQueryPbufferARB, -2 ),

	//WGL_ARB_render_texture
	WGL_EXT_ENTRY_POINT( "ARB", "render_texture", wglBindTexImageARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "render_texture", wglReleaseTexImageARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "render_texture", wglSetPbufferAttribARB, -2 ),
	//WGL_ARB_pixel_format
	WGL_EXT_ENTRY_POINT( "ARB", "pixel_format", wglChoosePixelFormatARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pixel_format", wglGetPixelFormatAttribivARB, -2 ),
	WGL_EXT_ENTRY_POINT( "ARB", "pixel_format", wglGetPixelFormatAttribfvARB, -2 ),

	//GL_ARB_vertex_program (vertex attrib functions)
	{ "glVertexAttrib1fARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib1fARB },
	{ "glVertexAttrib2fARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib2fARB },
	{ "glVertexAttrib3fARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib3fARB },
	{ "glVertexAttrib4fARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib4fARB },
	{ "glVertexAttrib1fvARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib1fvARB },
	{ "glVertexAttrib2fvARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib2fvARB },
	{ "glVertexAttrib3fvARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib3fvARB },
	{ "glVertexAttrib4fvARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib4fvARB },
	{ "glVertexAttrib4NubvARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttrib4NubvARB },
	{ "glVertexAttribPointerARB", "GL_ARB_vertex_program", -1, (PROC)glVertexAttribPointerARB },
	{ "glEnableVertexAttribArrayARB", "GL_ARB_vertex_program", -1, (PROC)glEnableVertexAttribArrayARB },
	{ "glDisableVertexAttribArrayARB", "GL_ARB_vertex_program", -1, (PROC)glDisableVertexAttribArrayARB },
	{ "glGetVertexAttribfvARB", "GL_ARB_vertex_program", -1, (PROC)glGetVertexAttribfvARB },
	{ "glGetVertexAttribivARB", "GL_ARB_vertex_program", -1, (PROC)glGetVertexAttribivARB },
	{ "glGetVertexAttribPointervARB", "GL_ARB_vertex_program", -1, (PROC)glGetVertexAttribPointervARB },

	//GL_ARB_occlusion_query
	{ "glGenQueriesARB", "GL_ARB_occlusion_query", -1, (PROC)glGenQueriesARB },
	{ "glDeleteQueriesARB", "GL_ARB_occlusion_query", -1, (PROC)glDeleteQueriesARB },
	{ "glIsQueryARB", "GL_ARB_occlusion_query", -1, (PROC)glIsQueryARB },
	{ "glBeginQueryARB", "GL_ARB_occlusion_query", -1, (PROC)glBeginQueryARB },
	{ "glEndQueryARB", "GL_ARB_occlusion_query", -1, (PROC)glEndQueryARB },
	{ "glGetQueryivARB", "GL_ARB_occlusion_query", -1, (PROC)glGetQueryivARB },
	{ "glGetQueryObjectivARB", "GL_ARB_occlusion_query", -1, (PROC)glGetQueryObjectivARB },
	{ "glGetQueryObjectuivARB", "GL_ARB_occlusion_query", -1, (PROC)glGetQueryObjectuivARB },

	//GL_EXT_blend_func_separate
	{ "glBlendFuncSeparateEXT", "GL_EXT_blend_func_separate", -1, (PROC)glBlendFuncSeparateEXT },
	{ "glBlendFuncSeparate", "GL_EXT_blend_func_separate", -1, (PROC)glBlendFuncSeparateEXT },

	//GL_EXT_blend_equation_separate
	{ "glBlendEquationSeparateEXT", "GL_EXT_blend_equation_separate", -1, (PROC)glBlendEquationSeparateEXT },
	{ "glBlendEquationSeparate", "GL_EXT_blend_equation_separate", -1, (PROC)glBlendEquationSeparateEXT },

	//GL 2.0 stencil separate (always available)
	{ "glStencilFuncSeparate", "GL_EXT_stencil_two_side", -2, (PROC)glStencilFuncSeparate },
	{ "glStencilOpSeparate", "GL_EXT_stencil_two_side", -2, (PROC)glStencilOpSeparate },
	{ "glStencilMaskSeparate", "GL_EXT_stencil_two_side", -2, (PROC)glStencilMaskSeparate },

	//GL_ARB_point_parameters
	{ "glPointParameterfARB", "GL_ARB_point_parameters", -1, (PROC)glPointParameterfARB },
	{ "glPointParameterfvARB", "GL_ARB_point_parameters", -1, (PROC)glPointParameterfvARB },
	{ "glPointParameterf", "GL_ARB_point_parameters", -1, (PROC)glPointParameterfARB },
	{ "glPointParameterfv", "GL_ARB_point_parameters", -1, (PROC)glPointParameterfvARB },
	{ "glPointParameterfEXT", "GL_EXT_point_parameters", -1, (PROC)glPointParameterfARB },
	{ "glPointParameterfvEXT", "GL_EXT_point_parameters", -1, (PROC)glPointParameterfvARB },

	{ NULL, NULL }
};

static bool D3DExtension_CheckDepthTextureSupport()
{
	const D3DFORMAT depthFormats[] = { D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D16, D3DFMT_D32, D3DFMT_UNKNOWN };
	D3DFORMAT adapterFormat = D3DGlobal.hCurrentMode.Format;

	for (int i = 0; depthFormats[i] != D3DFMT_UNKNOWN; ++i) {
		HRESULT hr = D3DGlobal.pD3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, adapterFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, depthFormats[i]);
		if (SUCCEEDED(hr)) {
			return true;
		}
	}

	return false;
}

class CExtensionBuf
{
public:
	CExtensionBuf() : m_size( 256 ), m_cur( 0 ), m_buf( reinterpret_cast<char*>( UTIL_Alloc( m_size ) ) ) {}
	~CExtensionBuf()
	{
		UTIL_Free(m_buf);
	}
	char *CopyBuffer() const
	{
		return UTIL_AllocString( !m_cur ? "" : m_buf );
	}
	void AddExtensionUnchecked( const char *ext )
	{
		if (!ext) return;
		size_t len = strlen(ext);
		CheckSpace( len + 2 );
		strcpy_s( m_buf + m_cur, len + 1, ext );
		*(m_buf + m_cur + len) = ' ';
		*(m_buf + m_cur + len + 1) = '\0';
		m_cur += len + 1;
	}
	void AddExtension( const char *ext )
	{
		if (!ext) return;
		if (!D3DGlobal_GetRegistryValue(ext, "Extensions", 0)) return;
		size_t len = strlen(ext);
		CheckSpace( len + 2 );
		strcpy_s( m_buf + m_cur, len + 1, ext );
		*(m_buf + m_cur + len) = ' ';
		*(m_buf + m_cur + len + 1) = '\0';
		m_cur += len + 1;
	}
	void CheckSpace( int len )
	{
		if (m_size > m_cur + len) return;
		m_size += 256;
		m_buf = reinterpret_cast<char*>( UTIL_Realloc( m_buf, m_size ) );
	}
private:
	int m_size;
	int m_cur;
	char *m_buf; 
};

namespace {
	void LogExtensionsString(const char* label, const char* extensions)
	{
		if (!extensions || !*extensions) {
			logPrintf("%s extensions: <empty>\n", label);
			return;
		}

		const size_t length = std::strlen(extensions);
		const size_t chunkSize = 1024;
		for (size_t offset = 0; offset < length; offset += chunkSize) {
			const size_t remaining = length - offset;
			const size_t count = remaining < chunkSize ? remaining : chunkSize;
			logPrintf("%s extensions [%u/%u]: %.*s\n",
				label,
				static_cast<unsigned int>(offset + count),
				static_cast<unsigned int>(length),
				static_cast<int>(count),
				extensions + offset);
		}
	}
}

namespace {
	bool gEnableARBProgramsStub = false;
}

void D3DExtension_BuildExtensionsString()
{
	assert( D3DGlobal.pD3D != NULL );
	assert( D3DGlobal.pDevice != NULL );

	CExtensionBuf ExtensionBuf;
	CExtensionBuf WExtensionBuf;
	GLuint checkCaps;
	bool bCombineSupportEXT( true );
	bool bCombineSupportARB( true );

	if (D3DGlobal.maxActiveTMU > 1) ExtensionBuf.AddExtension( "GL_ARB_multitexture" );
	ExtensionBuf.AddExtension( "GL_ARB_vertex_buffer_object" );

	gEnableARBProgramsStub = D3DGlobal.settings.enableARBProgramsStub != 0
		|| D3DGlobal_GetRegistryValue( "GL_ARB_program", "Extensions", 0 )
		|| D3DGlobal_GetRegistryValue( "GL_ARB_vertex_program", "Extensions", 0 )
		|| D3DGlobal_GetRegistryValue( "GL_ARB_fragment_program", "Extensions", 0 );

	if (gEnableARBProgramsStub) {
		ExtensionBuf.AddExtensionUnchecked( "GL_ARB_program" );
		ExtensionBuf.AddExtensionUnchecked( "GL_ARB_vertex_program" );
		ExtensionBuf.AddExtensionUnchecked( "GL_ARB_fragment_program" );
	}
	if (D3DExtension_CheckDepthTextureSupport()) {
		ExtensionBuf.AddExtension( "GL_ARB_depth_texture" );
		ExtensionBuf.AddExtension( "GL_ARB_shadow" );
		ExtensionBuf.AddExtension( "GL_SGIX_depth_texture" );
	}
	
	checkCaps = (D3DPTADDRESSCAPS_BORDER);
	if ((D3DGlobal.hD3DCaps.TextureAddressCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ARB_texture_border_clamp" );
	checkCaps = (D3DPTEXTURECAPS_CUBEMAP);
	if ((D3DGlobal.hD3DCaps.TextureCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ARB_texture_cube_map" );
	checkCaps = (D3DTEXOPCAPS_ADD);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ARB_texture_env_add" );

	checkCaps = (D3DTEXOPCAPS_SELECTARG1);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	checkCaps = (D3DTEXOPCAPS_MODULATE);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	checkCaps = (D3DTEXOPCAPS_ADD);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	checkCaps = (D3DTEXOPCAPS_ADDSIGNED);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	checkCaps = (D3DTEXOPCAPS_LERP);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	bCombineSupportEXT = bCombineSupportARB;
	checkCaps = (D3DTEXOPCAPS_SUBTRACT);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) != checkCaps) bCombineSupportARB = false;
	if (bCombineSupportARB) ExtensionBuf.AddExtension( "GL_ARB_texture_env_combine" );

	checkCaps = (D3DTEXOPCAPS_DOTPRODUCT3);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ARB_texture_env_dot3" );

	checkCaps = (D3DPTADDRESSCAPS_MIRROR);
	if ((D3DGlobal.hD3DCaps.TextureAddressCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ARB_texture_mirrored_repeat" );

	if (!(D3DGlobal.hD3DCaps.TextureCaps & D3DPTEXTURECAPS_POW2) &&
		(!(D3DGlobal.hD3DCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) || !(D3DGlobal.hD3DCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP_POW2)) &&
		(!(D3DGlobal.hD3DCaps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) || !(D3DGlobal.hD3DCaps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP_POW2))) {
			ExtensionBuf.AddExtension( "GL_ARB_texture_non_power_of_two" );
	}

	if ( D3DGlobal.supportsS3TC ) ExtensionBuf.AddExtension( "GL_ARB_texture_compression" );

	//we implement them at driver level
	ExtensionBuf.AddExtension( "GL_ARB_transpose_matrix" );
	ExtensionBuf.AddExtension( "GL_ARB_vertex_buffer_object" );

	checkCaps = (D3DPTADDRESSCAPS_MIRRORONCE);
	if ((D3DGlobal.hD3DCaps.TextureAddressCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_ATI_texture_mirror_once" );

#ifdef ALLOW_CHS_EXTENSIONS
	//our own specific extensions 
	//use CHS prefix (CHain Studios)
	checkCaps = (D3DPTEXTURECAPS_MIPVOLUMEMAP);
	if ((D3DGlobal.hD3DCaps.TextureCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_CHS_mipmap_texture3D" );
#endif

	//we implement them at driver level
	ExtensionBuf.AddExtension( "GL_EXT_abgr" );
	ExtensionBuf.AddExtension( "GL_EXT_bgra" );

	checkCaps = (D3DPBLENDCAPS_BLENDFACTOR);
	if ((D3DGlobal.hD3DCaps.SrcBlendCaps & checkCaps) == checkCaps) {
		if ((D3DGlobal.hD3DCaps.DestBlendCaps & checkCaps) == checkCaps) {
			ExtensionBuf.AddExtension( "GL_EXT_blend_color" );
		}
	}
	checkCaps = (D3DPMISCCAPS_BLENDOP);
	if ((D3DGlobal.hD3DCaps.PrimitiveMiscCaps & checkCaps) == checkCaps) {
		ExtensionBuf.AddExtension( "GL_EXT_blend_minmax" );
		ExtensionBuf.AddExtension( "GL_EXT_blend_subtract" );
	}

	// Separate blend func/equation - requires D3D9 separate alpha blend support
	if (D3DGlobal.hD3DCaps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) {
		ExtensionBuf.AddExtension( "GL_EXT_blend_func_separate" );
		ExtensionBuf.AddExtension( "GL_EXT_blend_equation_separate" );
	}
	
	//we implement them at driver level
	ExtensionBuf.AddExtension( "GL_EXT_compiled_vertex_array" );
	ExtensionBuf.AddExtension( "GL_EXT_draw_range_elements" );
	ExtensionBuf.AddExtension( "GL_EXT_multi_draw_arrays" );
	ExtensionBuf.AddExtension( "GL_EXT_fog_coord" );
	ExtensionBuf.AddExtension( "GL_EXT_packed_pixels" );
	ExtensionBuf.AddExtension( "GL_EXT_secondary_color" );

	checkCaps = (D3DPTEXTURECAPS_VOLUMEMAP);
	if ((D3DGlobal.hD3DCaps.TextureCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_texture3D" );
	if ( D3DGlobal.supportsS3TC ) ExtensionBuf.AddExtension( "GL_EXT_texture_compression_s3tc" );
	checkCaps = (D3DPTEXTURECAPS_CUBEMAP);
	if ((D3DGlobal.hD3DCaps.TextureCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_texture_cube_map" );
	ExtensionBuf.AddExtension( "GL_EXT_texture_rectangle" );
	
	checkCaps = (D3DTEXOPCAPS_ADD);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_texture_env_add" );
	if (bCombineSupportEXT) ExtensionBuf.AddExtension( "GL_EXT_texture_env_combine" );
	checkCaps = (D3DTEXOPCAPS_DOTPRODUCT3);
	if ((D3DGlobal.hD3DCaps.TextureOpCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_texture_env_dot3" );

	if (D3DGlobal.hD3DCaps.MaxAnisotropy > 1) ExtensionBuf.AddExtension( "GL_EXT_texture_filter_anisotropic" );

	checkCaps = (D3DPRASTERCAPS_MIPMAPLODBIAS);
	if ((D3DGlobal.hD3DCaps.RasterCaps & checkCaps) == checkCaps) {
		ExtensionBuf.AddExtension( "GL_EXT_texture_lod" );		//assume per-object bias
		ExtensionBuf.AddExtension( "GL_EXT_texture_lod_bias" );	//assume per-stage bias
	}

	ExtensionBuf.AddExtension( "GL_EXT_texture_object" );	//GL 1.0 legacy, but exists in modern drivers, therefore we add it too

	checkCaps = (D3DSTENCILCAPS_TWOSIDED);
	if ((D3DGlobal.hD3DCaps.StencilCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_stencil_two_side" );
	checkCaps = (D3DSTENCILCAPS_INCRSAT|D3DSTENCILCAPS_DECRSAT);
	if ((D3DGlobal.hD3DCaps.StencilCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_EXT_stencil_wrap" );

	checkCaps = (D3DPTADDRESSCAPS_MIRROR);
	if ((D3DGlobal.hD3DCaps.TextureAddressCaps & checkCaps) == checkCaps) ExtensionBuf.AddExtension( "GL_IBM_texture_mirrored_repeat" );


	checkCaps = (D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR);
	if ((D3DGlobal.hD3DCaps.SrcBlendCaps & checkCaps) == checkCaps) {
		checkCaps = (D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR);
		if ((D3DGlobal.hD3DCaps.DestBlendCaps & checkCaps) == checkCaps) {
			ExtensionBuf.AddExtension( "GL_NV_blend_square" );
		}
	}

	//we implement it at driver level
	ExtensionBuf.AddExtension( "GL_NV_texgen_reflection" );

	ExtensionBuf.AddExtension( "GL_SGIS_generate_mipmap" );

	//For Quake2 that won't use ARB extension
	if (D3DGlobal.maxActiveTMU > 1) ExtensionBuf.AddExtension( "GL_SGIS_multitexture" );

	//an alias to GL_EXT_multi_draw_arrays
	ExtensionBuf.AddExtension( "GL_SUN_multi_draw_arrays" );

	//for idtech3 games that pass normal pointer when this is present
	ExtensionBuf.AddExtension( "GL_ATI_pn_triangles" );

	// Occlusion query - we stub it (always report visible)
	ExtensionBuf.AddExtension( "GL_ARB_occlusion_query" );

	// Point parameters
	ExtensionBuf.AddExtension( "GL_ARB_point_parameters" );
	ExtensionBuf.AddExtension( "GL_EXT_point_parameters" );

	// Texture env crossbar (allows any texture unit source in combiners)
	ExtensionBuf.AddExtension( "GL_ARB_texture_env_crossbar" );

	// GL 2.0 stencil separate (always advertise if two-sided stencil available)
	// Note: we don't add GL_VERSION_2_0 to extension string since it's not a real extension.
	// GL 2.0 functions are found via wglGetProcAddress.

	//we implement it at driver level
	ExtensionBuf.AddExtension( "WGL_ARB_extensions_string" );
	ExtensionBuf.AddExtension( "WGL_ARB_pbuffer" );
	ExtensionBuf.AddExtension( "WGL_ARB_pixel_format" );
	ExtensionBuf.AddExtension( "WGL_ARB_render_texture" );
	ExtensionBuf.AddExtension( "WGL_EXT_swap_control" );

	//add WGL extensions
	WExtensionBuf.AddExtension( "WGL_ARB_extensions_string" );
	WExtensionBuf.AddExtension( "WGL_ARB_pbuffer" );
	WExtensionBuf.AddExtension( "WGL_ARB_pixel_format" );
	WExtensionBuf.AddExtension( "WGL_ARB_render_texture" );
	WExtensionBuf.AddExtension( "WGL_EXT_swap_control" );

	D3DGlobal.szExtensions = ExtensionBuf.CopyBuffer();
	D3DGlobal.szWExtensions = WExtensionBuf.CopyBuffer();

	LogExtensionsString("GL", D3DGlobal.szExtensions);
	LogExtensionsString("WGL", D3DGlobal.szWExtensions);
}

//=========================================
// wglGetProcAddress
//-----------------------------------------
// Return a requested extension proc address
//=========================================
namespace {
	std::vector<std::string> gMissingProcs;
	std::vector<std::string> gDisabledProcs;

	bool IsARBProgramExtension( const char *extname )
	{
		return extname
			&& (!strcmp( extname, "GL_ARB_program" )
				|| !strcmp( extname, "GL_ARB_vertex_program" )
				|| !strcmp( extname, "GL_ARB_fragment_program" ));
	}

	void AddUniqueProc(std::vector<std::string>& list, const std::string& name)
	{
		if (name.empty()) {
			return;
		}

		if (std::find(list.begin(), list.end(), name) == list.end()) {
			list.emplace_back(name);
		}
	}
}

static const char *gMissingProcName = NULL;
static const char *gMissingProcExt = NULL;
static void WINAPI MissingProcStub()
{
	if (gMissingProcName)
	{
		if (gMissingProcExt)
			logPrintf("ERROR: Missing GL/WGL proc '%s' (extension '%s') invoked; returning GL_INVALID_OPERATION.\n", gMissingProcName, gMissingProcExt);
		else
			logPrintf("ERROR: Missing GL/WGL proc '%s' invoked; returning GL_INVALID_OPERATION.\n", gMissingProcName);
	}
	else
	{
		logPrintf("ERROR: Missing GL/WGL proc invoked; returning GL_INVALID_OPERATION.\n");
	}

	D3DGlobal.lastError = E_INVALID_OPERATION;
}

OPENGL_API PROC WINAPI wrap_wglGetProcAddress( LPCSTR s )
{
	// WG: some games check for this being NULL, and crash
	static PROC stubAddress = (PROC)MissingProcStub;
	const char *pszDisabledExt = NULL;

	// Block NV-specific extensions to force ARB fallback path in engines
	// that support both (e.g. "You Are Empty" / DS2 engine)
	if (s && (
		strstr(s, "NV_register_combiners") ||
		strstr(s, "CombinerParameterfvNV") || strstr(s, "CombinerParameterfNV") ||
		strstr(s, "CombinerParameterivNV") || strstr(s, "CombinerParameteriNV") ||
		strstr(s, "CombinerInputNV") || strstr(s, "CombinerOutputNV") ||
		strstr(s, "FinalCombinerInputNV") || strstr(s, "GetCombinerInputParameterfvNV") ||
		strstr(s, "GetCombinerOutputParameterfvNV") || strstr(s, "GetFinalCombinerInputParameterfvNV") ||
		// Block NV_vertex_program / NV_fragment_program entry points
		(strncmp(s, "glLoad", 6) == 0 && strstr(s, "ProgramNV")) ||
		(strncmp(s, "glBind", 6) == 0 && strstr(s, "ProgramNV")) ||
		(strncmp(s, "glGen", 5) == 0 && strstr(s, "ProgramsNV")) ||
		(strncmp(s, "glDelete", 8) == 0 && strstr(s, "ProgramsNV")) ||
		(strncmp(s, "glExecute", 9) == 0 && strstr(s, "ProgramNV")) ||
		(strncmp(s, "glGet", 5) == 0 && strstr(s, "ProgramNV")) ||
		(strncmp(s, "glAre", 5) == 0 && strstr(s, "ProgramsResidentNV")) ||
		(strncmp(s, "glRequest", 9) == 0 && strstr(s, "ResidentProgramsNV")) ||
		(strncmp(s, "glTrack", 7) == 0 && strstr(s, "MatrixNV")) ||
		(strncmp(s, "glVertex", 8) == 0 && strstr(s, "NV") && !strstr(s, "ARB")) ||
		(strncmp(s, "glProgram", 9) == 0 && strstr(s, "NV") && !strstr(s, "ARB")) ||
		(strncmp(s, "glProgramNamedParameter", 23) == 0 && strstr(s, "NV"))
	))
	{
		logPrintf("wglGetProcAddress: BLOCKED NV proc '%s' (forcing ARB path)\n", s);
		return NULL;
	}

	for (int i = 0; ; ++i) {
		// no more entrypoints
		if (!glext_EntryPoints[i].name) 
			break;

		if (glext_EntryPoints[i].enabled < 0)
		{
			if (IsARBProgramExtension(glext_EntryPoints[i].extname)) {
				glext_EntryPoints[i].enabled = gEnableARBProgramsStub ? 1 : 0;
			} else {
				glext_EntryPoints[i].enabled = D3DGlobal_GetRegistryValue(glext_EntryPoints[i].extname, "Extensions", glext_EntryPoints[i].enabled==-1 ? 0 : 1);
			}
		}

		if (!strcmp(s, glext_EntryPoints[i].name)) {
			if (!glext_EntryPoints[i].enabled) {
				pszDisabledExt = glext_EntryPoints[i].extname;
				break;
			} else {
				logPrintf("wglGetProcAddress: queried proc '%s'\n", s);
				return glext_EntryPoints[i].func;
			}
		}
	}

	//++stubAddress;

	if (pszDisabledExt)
	{
		gMissingProcName = s;
		gMissingProcExt = pszDisabledExt;
		logPrintf("WARNING: wglGetProcAddress: returning stub for disabled proc '%s' (extension '%s')\n", s, pszDisabledExt);
		AddUniqueProc(gDisabledProcs, std::string(s).append(" (").append(pszDisabledExt).append(")"));
	}
	else
	{
		FARPROC fp = GetProcAddress(D3DGlobal.hModule, s);
		if (fp)
		{
			logPrintf("wglGetProcAddress: queried proc '%s'\n", s);
			return fp;
		}
		else
		{
			gMissingProcName = s;
			gMissingProcExt = NULL;
			logPrintf("WARNING: wglGetProcAddress: returning stub for unknown proc '%s'\n", s);
			AddUniqueProc(gMissingProcs, s);
		}
	}

	return (PROC)stubAddress;
}

OPENGL_API PROC WINAPI wrap_wglGetDefaultProcAddress( LPCSTR s )
{
	return wrap_wglGetProcAddress(s);
}

void D3DExtension_DumpMissingProcs()
{
	if (gMissingProcs.empty() && gDisabledProcs.empty()) {
		return;
	}

	logPrintf("---- Missing/disabled OpenGL entry points summary ----\n");
	if (!gDisabledProcs.empty()) {
		logPrintf("Disabled extension entry points (requested anyway):\n");
		for (const auto& name : gDisabledProcs) {
			logPrintf("  %s\n", name.c_str());
		}
	}
	if (!gMissingProcs.empty()) {
		logPrintf("Unknown entry points (not implemented/exposed):\n");
		for (const auto& name : gMissingProcs) {
			logPrintf("  %s\n", name.c_str());
		}
	}
	logPrintf("---- End missing/disabled OpenGL entry points summary ----\n");
}
