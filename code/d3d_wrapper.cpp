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
#include "d3d_utils.hpp"
#include "hooking.h"

#include <string>
#include <tchar.h>
#include <cctype>
#include <cstring>

//==================================================================================
// Wrapper Log
//----------------------------------------------------------------------------------
// Log all internal errors into text file.
// This will help to debug the wrapper and monitor unimplemented functions.
//==================================================================================
static FILE *g_fpLog = nullptr;
static const char s_szLogFileName[] = WRAPPER_GL_SHORT_NAME_STRING ".log";
static char *log_string = nullptr;
static const size_t c_LogStringSize = 8192; //8 kbytes
static int g_logLevel = QGL_LOG_INFO;

static QGLLogLevel logInferLevel( const char *fmt )
{
	if (!fmt)
		return QGL_LOG_INFO;

	if (!_strnicmp(fmt, "ERROR", 5) || !_strnicmp(fmt, "Error", 5)
		|| !_strnicmp(fmt, "FATAL", 5))
		return QGL_LOG_ERROR;
	if (!_strnicmp(fmt, "WARNING", 7) || !_strnicmp(fmt, "Warn", 4))
		return QGL_LOG_WARN;
	return QGL_LOG_INFO;
}

static const char *logLevelName( QGLLogLevel level )
{
	switch (level) {
	case QGL_LOG_ERROR: return "ERROR";
	case QGL_LOG_WARN: return "WARN";
	case QGL_LOG_INFO: return "INFO";
	case QGL_LOG_DEBUG: return "DEBUG";
	case QGL_LOG_TRACE: return "TRACE";
	default: return "LOG";
	}
}

static void logInit()
{
	if (g_fpLog)
		return;

	if (!log_string)
	{
		log_string = reinterpret_cast<char*>( UTIL_Alloc(c_LogStringSize) );
		assert(log_string != NULL);
	}
	log_string[c_LogStringSize -1] = 0;

	if ( fopen_s( &g_fpLog, s_szLogFileName, "w" ) )
		return;

	char timeBuf[64];
	time_t t;
	memset(&t, 0, sizeof(t));
	time(&t);
	memset(timeBuf, 0, sizeof(timeBuf));
	ctime_s(timeBuf,sizeof(timeBuf),&t);

	fprintf(g_fpLog,"=======================================================================\n");
	fprintf(g_fpLog," " WRAPPER_GL_SHORT_NAME_STRING " wrapper initialized at %s",timeBuf);
	fprintf(g_fpLog,"=======================================================================\n");

	fprintf(g_fpLog, "\n");
	fflush(g_fpLog);
}

void logShutdown()
{
	if (g_fpLog) {
		time_t t;
		memset(&t, 0, sizeof(t));
		time(&t);
		char timeBuf[64];
		memset(timeBuf, 0, sizeof(timeBuf));
		ctime_s(timeBuf,sizeof(timeBuf),&t);

		fprintf(g_fpLog,"=======================================================================\n");
		fprintf(g_fpLog," " WRAPPER_GL_SHORT_NAME_STRING " wrapper shutdown at %s",timeBuf);
		fprintf(g_fpLog,"=======================================================================\n");

		fclose(g_fpLog);
		g_fpLog = NULL;
	}

	if (log_string)
	{
		UTIL_Free(log_string);
		log_string = NULL;
	}
}

void logPrintf( const char *fmt, ... )
{
	if (!g_fpLog || !logIsEnabled(logInferLevel(fmt)))
		return;

	va_list argptr;
	va_start(argptr,fmt);
	_vsnprintf_s(log_string,c_LogStringSize,c_LogStringSize-2,fmt,argptr);
	va_end(argptr);

	fprintf(g_fpLog, "%s", log_string);
	fflush(g_fpLog);
}

void logSetLevel( int level )
{
	if (level < QGL_LOG_ERROR) level = QGL_LOG_ERROR;
	if (level > QGL_LOG_TRACE) level = QGL_LOG_TRACE;
	g_logLevel = level;
}

int logGetLevel()
{
	return g_logLevel;
}

bool logIsEnabled( QGLLogLevel level )
{
	return static_cast<int>(level) <= g_logLevel;
}

void logPrintfLevel( QGLLogLevel level, const char *category, const char *fmt, ... )
{
	if (!g_fpLog || !fmt || !logIsEnabled(level))
		return;

	char message[c_LogStringSize];
	va_list argptr;
	va_start(argptr, fmt);
	_vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, argptr);
	va_end(argptr);

	fprintf(g_fpLog, "[%s][F:%08llu D:%06llu][%s] %s",
		logLevelName(level),
		static_cast<unsigned long long>(QGL_DiagnosticsGetFrameId()),
		static_cast<unsigned long long>(QGL_DiagnosticsGetDrawId()),
		category ? category : "GENERAL",
		message);
	if (!*message || message[strlen(message) - 1] != '\n')
		fputc('\n', g_fpLog);
	fflush(g_fpLog);
}

#define PATH_SZ 1024
static TCHAR exename[PATH_SZ] = { 0 };
static TCHAR dllname[PATH_SZ] = { 0 };

//#define QINDIEGLSRC_NO_REMIX
#ifdef QINDIEGLSRC_NO_REMIX
int hook_dll_on_load_check() { return false; }
void hook_on_process_attach() { }
void hook_do_init(const char*, const char*, const char*) { }
void hook_do_deinit() { }
#else
#pragma comment(lib, "idtech3_mixup.lib")
#endif

//=========================================
// DLL Entry Point
//-----------------------------------------
// Init and shutdown global DLL data
//=========================================

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID )
{
	std::string game_cfg(GLOBAL_GAMENAME);

	if (hook_dll_on_load_check())
	{
		return TRUE;
	}

    switch ( ul_reason_for_call )
	{
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hModule);
			hook_on_process_attach();
			logInit();
			QGL_DiagnosticsInitialize();
			//logPrintf("DllMain( DLL_PROCESS_ATTACH )\n");
			//detect executable name
			{
				DWORD ercd = GetModuleFileName(NULL, exename, PATH_SZ);
				if (ercd > 0)
				{
					TCHAR* name = _tcsrchr(exename, _T('\\'));
					if (name) {
						name++;
						size_t count = _tclen( name );
						TCHAR* tmp = _tcsrchr(exename, _T('.'));
						if (tmp) {
							count = tmp - name;
						}
						game_cfg.assign("game.");
						game_cfg.append(name, count);
					}
				}
				ercd = GetModuleFileName(hModule, dllname, PATH_SZ);
			}
			D3DGlobal_StoreGameName(game_cfg.c_str());
			D3DGlobal_Init( true );
			D3DGlobal.hModule = hModule;
			hook_do_init(exename, dllname, game_cfg.c_str());
			break;
		case DLL_PROCESS_DETACH:
			//logPrintf("DllMain( DLL_PROCESS_DETACH )\n");
			D3DGlobal_Cleanup( true );
			hook_do_deinit();
			QGL_DiagnosticsShutdown();
			logShutdown();
			break;
		default:
			break;
    }
    return TRUE;
}
