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
#ifndef QINDIEGL_D3D_LISTS_H
#define QINDIEGL_D3D_LISTS_H

#include <vector>
#include <functional>
#include <unordered_map>
#include <array>
#include <cstring>

//==================================================================================
// Display List Recording Infrastructure
//==================================================================================

struct D3DDisplayList {
	std::vector<std::function<void()>> commands;
};

// Recording state - defined in d3d_lists.cpp
extern bool gDLRecording;                                   // true during glNewList..glEndList
extern bool gDLExecute;                                     // true for GL_COMPILE_AND_EXECUTE
extern D3DDisplayList* gDLCurrent;                          // list being recorded into
extern GLuint gDLListBase;                                  // base offset for glCallLists
extern std::unordered_map<GLuint, D3DDisplayList*> gDLMap;  // all display lists
extern GLuint gDLNextId;                                    // next ID for glGenLists

// Helper to record a command into the current display list
inline void DL_RecordCommand( std::function<void()>&& cmd )
{
	if ( gDLCurrent ) {
		gDLCurrent->commands.push_back( std::move( cmd ) );
	}
}

//--------------------------------------------------------------
// Macros for adding display list recording to GL entry points.
// Place at the very top of the function body.
// During recording: captures args by value and stores a lambda.
//   GL_COMPILE mode: returns after recording (skips execution).
//   GL_COMPILE_AND_EXECUTE: falls through to execute normally.
// During playback: gDLRecording is false so function runs normally.
//--------------------------------------------------------------

#define DL_RECORD_0(fn) \
	if (gDLRecording) { DL_RecordCommand([]() { fn(); }); if (!gDLExecute) return; }

#define DL_RECORD_1(fn, a1) \
	if (gDLRecording) { auto _dl1=(a1); DL_RecordCommand([_dl1]() { fn(_dl1); }); if (!gDLExecute) return; }

#define DL_RECORD_2(fn, a1, a2) \
	if (gDLRecording) { auto _dl1=(a1); auto _dl2=(a2); \
	DL_RecordCommand([_dl1,_dl2]() { fn(_dl1,_dl2); }); if (!gDLExecute) return; }

#define DL_RECORD_3(fn, a1, a2, a3) \
	if (gDLRecording) { auto _dl1=(a1); auto _dl2=(a2); auto _dl3=(a3); \
	DL_RecordCommand([_dl1,_dl2,_dl3]() { fn(_dl1,_dl2,_dl3); }); if (!gDLExecute) return; }

#define DL_RECORD_4(fn, a1, a2, a3, a4) \
	if (gDLRecording) { auto _dl1=(a1); auto _dl2=(a2); auto _dl3=(a3); auto _dl4=(a4); \
	DL_RecordCommand([_dl1,_dl2,_dl3,_dl4]() { fn(_dl1,_dl2,_dl3,_dl4); }); if (!gDLExecute) return; }

#define DL_RECORD_6(fn, a1, a2, a3, a4, a5, a6) \
	if (gDLRecording) { auto _dl1=(a1); auto _dl2=(a2); auto _dl3=(a3); auto _dl4=(a4); \
	auto _dl5=(a5); auto _dl6=(a6); \
	DL_RecordCommand([_dl1,_dl2,_dl3,_dl4,_dl5,_dl6]() { fn(_dl1,_dl2,_dl3,_dl4,_dl5,_dl6); }); \
	if (!gDLExecute) return; }

// For 16-element float matrix pointer parameters
#define DL_RECORD_MAT16F(fn, m) \
	if (gDLRecording) { std::array<GLfloat,16> _dlm; std::memcpy(_dlm.data(), (m), 16*sizeof(GLfloat)); \
	DL_RecordCommand([_dlm]() { fn(_dlm.data()); }); if (!gDLExecute) return; }

// For 16-element double matrix pointer parameters
#define DL_RECORD_MAT16D(fn, m) \
	if (gDLRecording) { std::array<GLdouble,16> _dlm; std::memcpy(_dlm.data(), (m), 16*sizeof(GLdouble)); \
	DL_RecordCommand([_dlm]() { fn(_dlm.data()); }); if (!gDLExecute) return; }

// Cleanup all display lists (called at shutdown)
extern void D3DDisplayList_Cleanup();

#endif //QINDIEGL_D3D_LISTS_H
