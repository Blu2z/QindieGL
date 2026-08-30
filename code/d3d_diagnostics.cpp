/***************************************************************************
* QindieGL diagnostic instrumentation.
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_texture.hpp"
#include "d3d_buffer.hpp"
#include "d3d_extension.hpp"
#include "d3d_matrix_stack.hpp"

#include <algorithm>
#include <map>
#include <string>

namespace {
	static const LONG kEventCapacity = 256;
	static const size_t kEventTextSize = 384;

	struct DiagnosticEvent
	{
		LONG sequence;
		bool d3dEvent;
		uint64_t frameId;
		uint64_t drawId;
		char category[24];
		char text[kEventTextSize];
	};

	struct DiagnosticState
	{
		uint64_t frameId;
		uint64_t drawId;
		uint64_t framesPresented;
		uint64_t drawsSubmitted;
		uint64_t drawsSkipped;
		uint64_t failedD3DCalls;
		uint64_t deviceResets;
		uint64_t pBuffersCreated;
		uint64_t arbProgramsUploaded;
		uint64_t arbProgramsCompiled;
		uint64_t arbProgramFailures;
		uint64_t vbosCreated;
		int64_t currentVBOBytes;
		uint64_t peakVBOBytes;
		int debugMaxDrawCall;
		int debugDumpFrame;
		int debugDumpDraw;
		bool crashDiagnostics;
		bool initialized;
		bool summaryDumped;
	};

	static DiagnosticState gDiagnostics = {};
	static DiagnosticEvent gEvents[kEventCapacity] = {};
	static volatile LONG gNextEvent = 0;
	static LPTOP_LEVEL_EXCEPTION_FILTER gPreviousExceptionFilter = nullptr;
	static bool gExceptionFilterInstalled = false;
	static char gRenderTarget[64] = "MAIN";
	static char gLastErrorSource[96] = "<none>";
	static char gActiveBuffers[128] = "array=0 element=0";
	static char gActivePrograms[128] = "vp=0 fp=0";
	static char gActiveTextures[512] = "none";
	static char gProjectionState[160] = "unavailable";
	static std::map<std::string, uint64_t> gD3DFailures;
	static std::map<std::string, uint64_t> gUnsupportedEnums;

	const char *GLModeName( unsigned int mode )
	{
		switch (mode) {
		case GL_POINTS: return "POINTS";
		case GL_LINES: return "LINES";
		case GL_LINE_LOOP: return "LINE_LOOP";
		case GL_LINE_STRIP: return "LINE_STRIP";
		case GL_TRIANGLES: return "TRIANGLES";
		case GL_TRIANGLE_STRIP: return "TRIANGLE_STRIP";
		case GL_TRIANGLE_FAN: return "TRIANGLE_FAN";
		case GL_QUADS: return "QUADS";
		case GL_QUAD_STRIP: return "QUAD_STRIP";
		case GL_POLYGON: return "POLYGON";
		default: return "UNKNOWN";
		}
	}

	const char *GLErrorName( long error )
	{
		switch (error) {
		case E_INVALID_ENUM: return "GL_INVALID_ENUM";
		case E_INVALIDARG: return "GL_INVALID_VALUE";
		case E_INVALID_OPERATION: return "GL_INVALID_OPERATION";
		case E_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
		case E_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
		case E_OUTOFMEMORY:
		case D3DERR_OUTOFVIDEOMEMORY: return "GL_OUT_OF_MEMORY";
		default: return "GL_INVALID_OPERATION";
		}
	}

	bool HasExtension( const char *extensions, const char *name )
	{
		if (!extensions || !name || !*name)
			return false;
		const size_t nameLength = strlen(name);
		const char *current = extensions;
		while ((current = strstr(current, name)) != nullptr) {
			const bool startsToken = current == extensions || current[-1] == ' ';
			const char after = current[nameLength];
			if (startsToken && (after == '\0' || after == ' '))
				return true;
			current += nameLength;
		}
		return false;
	}

	uint32_t HashBytes( const void *data, size_t length )
	{
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < length; ++i) {
			hash ^= bytes[i];
			hash *= 16777619u;
		}
		return hash;
	}

	void SnapshotState()
	{
		if (!D3DGlobal.initialized)
			return;

		const GLuint arrayBuffer = D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB);
		const GLuint elementBuffer = D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB);
		const GLuint vertexProgram = ARB_GetBoundVertexProgram();
		const GLuint fragmentProgram = ARB_GetBoundFragmentProgram();
		const char *projection = "UNAVAILABLE";
		uint32_t projectionHash = 0;
		uint32_t modelviewHash = 0;
		if (D3DGlobal.projectionMatrixStack && D3DGlobal.modelviewMatrixStack) {
			projection = D3DGlobal_IsOrthoProjection() ? "ORTHOGRAPHIC" : "PERSPECTIVE";
			projectionHash = HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX));
			modelviewHash = HashBytes(D3DGlobal.modelviewMatrixStack->top(), sizeof(D3DXMATRIX));
		}
		sprintf_s(gActiveBuffers, "array=%u element=%u", arrayBuffer, elementBuffer);
		sprintf_s(gActivePrograms, "vp=%u fp=%u", vertexProgram, fragmentProgram);
		sprintf_s(gProjectionState, "%s projHash=%08X modelviewHash=%08X",
			projection, projectionHash, modelviewHash);
		gActiveTextures[0] = '\0';
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				if (!texture) continue;
				const size_t used = strlen(gActiveTextures);
				if (used + 32 >= sizeof(gActiveTextures)) continue;
				_snprintf_s(gActiveTextures + used, sizeof(gActiveTextures) - used, _TRUNCATE,
					"tmu%d:id%u ", unit, texture->GetGLIndex());
			}
		}
		if (!*gActiveTextures)
			strcpy_s(gActiveTextures, "none");

		QGL_DiagnosticsRecordEvent(false, "STATE",
			"buffers(%s) programs(%s) textures(%s) rt=%s projection=%s",
			gActiveBuffers, gActivePrograms, gActiveTextures, gRenderTarget, gProjectionState);
	}

	void DumpArray( const char *name, bool enabled, const D3DVAInfo& info )
	{
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"array=%s enabled=%s size=%d type=0x%X stride=%d pointer=%p arrayBuffer=%u",
			name, enabled ? "YES" : "NO", info.elementCount, info.elementType,
			info.stride, info.data, D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB));
	}

	void DumpSelectedDrawState( const char *api, unsigned int mode, int count,
		int first, unsigned int indexType, const void *indices )
	{
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "===== Selected draw state =====");
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"api=%s primitive=%s(0x%X) count=%d first=%d indexType=0x%X indices=%p",
			api, GLModeName(mode), mode, count, first, indexType, indices);

		const DWORD mask = D3DState.ClientVertexArrayState.vertexArrayEnable;
		DumpArray("vertex", (mask & VA_ENABLE_VERTEX_BIT) != 0, D3DState.ClientVertexArrayState.vertexInfo);
		DumpArray("normal", (mask & VA_ENABLE_NORMAL_BIT) != 0, D3DState.ClientVertexArrayState.normalInfo);
		DumpArray("color", (mask & VA_ENABLE_COLOR_BIT) != 0, D3DState.ClientVertexArrayState.colorInfo);
		DumpArray("secondaryColor", (mask & VA_ENABLE_COLOR2_BIT) != 0, D3DState.ClientVertexArrayState.color2Info);
		DumpArray("fog", (mask & VA_ENABLE_FOG_BIT) != 0, D3DState.ClientVertexArrayState.fogInfo);
		for (int i = 0; i < D3DGlobal.maxActiveTMU; ++i) {
			char name[24];
			sprintf_s(name, "texcoord%d", i);
			DumpArray(name, VA_TEXTURE_BIT_IS_SET(mask, i), D3DState.ClientVertexArrayState.texCoordInfo[i]);
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[i][target];
				if (!texture)
					continue;
				logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
					"texture tmu=%d id=%u target=0x%X size=%ux%ux%u internal=0x%X",
					i, texture->GetGLIndex(), texture->GetTarget(), texture->GetWidth(),
					texture->GetHeight(), texture->GetDepth(), texture->GetInternalFormat());
			}
		}

		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"blend=%u alphaTest=%u depthTest=%u depthWrite=%u cull=%u fog=%u stencil=%u",
			D3DState.EnableState.alphaBlendEnabled, D3DState.EnableState.alphaTestEnabled,
			D3DState.EnableState.depthTestEnabled, D3DState.DepthBufferState.depthWriteMask,
			D3DState.EnableState.cullEnabled, D3DState.EnableState.fogEnabled,
			D3DState.EnableState.stencilTestEnabled);
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE",
			"buffers array=%u element=%u programs vp=%u fp=%u renderTarget=%s projection=%s",
			D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB), D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB),
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(), gRenderTarget,
			(D3DGlobal.projectionMatrixStack && D3DGlobal_IsOrthoProjection()) ? "ORTHOGRAPHIC" : "PERSPECTIVE");
		if (D3DGlobal.projectionMatrixStack && D3DGlobal.modelviewMatrixStack) {
			logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "matrix projectionHash=%08X modelviewHash=%08X",
				HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX)),
				HashBytes(D3DGlobal.modelviewMatrixStack->top(), sizeof(D3DXMATRIX)));
		}
		logPrintfLevel(QGL_LOG_INFO, "DRAW_STATE", "===== End selected draw state =====");
	}

	void WriteCrashText( HANDLE file, const char *text )
	{
		if (file == INVALID_HANDLE_VALUE || !text)
			return;
		DWORD written = 0;
		WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
	}

	void WriteCrashFormat( HANDLE file, const char *fmt, ... )
	{
		char buffer[1024];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
		va_end(args);
		WriteCrashText(file, buffer);
	}

	void DumpCrashEvents( HANDLE file, bool d3dEvents, int maximum )
	{
		const LONG end = gNextEvent;
		int emitted = 0;
		for (LONG sequence = end - 1; sequence >= 0 && sequence >= end - kEventCapacity && emitted < maximum; --sequence) {
			const DiagnosticEvent& event = gEvents[sequence % kEventCapacity];
			if (event.sequence != sequence + 1 || event.d3dEvent != d3dEvents)
				continue;
			WriteCrashFormat(file, "[F:%08llu D:%06llu][%s] %s\r\n",
				static_cast<unsigned long long>(event.frameId),
				static_cast<unsigned long long>(event.drawId), event.category, event.text);
			++emitted;
		}
	}

	bool IsExecutableProtection( DWORD protection )
	{
		if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;
		switch (protection & 0xFF) {
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	void WriteCrashAddress( HANDLE file, const char *label, uintptr_t address )
	{
		MEMORY_BASIC_INFORMATION memory = {};
		char module[MAX_PATH] = "<unmapped>";
		uintptr_t moduleOffset = 0;
		if (address != 0 && VirtualQuery(reinterpret_cast<const void *>(address),
			&memory, sizeof(memory)) == sizeof(memory)) {
			moduleOffset = address - reinterpret_cast<uintptr_t>(memory.AllocationBase);
			if (!GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module, ARRAYSIZE(module)))
				strcpy_s(module, IsExecutableProtection(memory.Protect) ? "<mapped executable>" : "<mapped data>");
		}
		WriteCrashFormat(file, "%s: %p  %s+0x%IX\r\n", label,
			reinterpret_cast<const void *>(address), module, moduleOffset);
	}

	void DumpCrashContext( HANDLE file, EXCEPTION_POINTERS *exceptionInfo )
	{
		if (!exceptionInfo || !exceptionInfo->ContextRecord) {
			WriteCrashText(file, "CPU context unavailable.\r\n");
			return;
		}

		const CONTEXT *context = exceptionInfo->ContextRecord;
		WriteCrashText(file, "\r\nCPU context:\r\n");
#if defined(_M_IX86)
		WriteCrashFormat(file,
			"EAX=%08X EBX=%08X ECX=%08X EDX=%08X\r\n"
			"ESI=%08X EDI=%08X EBP=%08X ESP=%08X\r\n"
			"EIP=%08X EFlags=%08X\r\n",
			context->Eax, context->Ebx, context->Ecx, context->Edx,
			context->Esi, context->Edi, context->Ebp, context->Esp,
			context->Eip, context->EFlags);
		WriteCrashAddress(file, "Instruction", context->Eip);

		DWORD stackWords[64] = {};
		SIZE_T bytesRead = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(context->Esp),
			stackWords, sizeof(stackWords), &bytesRead) || bytesRead == 0) {
			WriteCrashText(file, "Stack memory unavailable.\r\n");
			return;
		}
		const size_t wordCount = bytesRead / sizeof(stackWords[0]);
		WriteCrashFormat(file, "\r\nRaw stack from ESP (%u DWORDs):\r\n", static_cast<unsigned int>(wordCount));
		for (size_t i = 0; i < wordCount; i += 4) {
			WriteCrashFormat(file, "  ESP+%03X: %08X %08X %08X %08X\r\n",
				static_cast<unsigned int>(i * sizeof(DWORD)), stackWords[i],
				i + 1 < wordCount ? stackWords[i + 1] : 0,
				i + 2 < wordCount ? stackWords[i + 2] : 0,
				i + 3 < wordCount ? stackWords[i + 3] : 0);
		}

		WriteCrashText(file, "\r\nExecutable addresses found on stack:\r\n");
		bool foundExecutable = false;
		for (size_t i = 0; i < wordCount; ++i) {
			MEMORY_BASIC_INFORMATION memory = {};
			const uintptr_t candidate = stackWords[i];
			if (candidate == 0 || VirtualQuery(reinterpret_cast<const void *>(candidate),
				&memory, sizeof(memory)) != sizeof(memory) || !IsExecutableProtection(memory.Protect))
				continue;
			char label[32];
			sprintf_s(label, "ESP+0x%03X", static_cast<unsigned int>(i * sizeof(DWORD)));
			WriteCrashAddress(file, label, candidate);
			foundExecutable = true;
		}
		if (!foundExecutable)
			WriteCrashText(file, "  <none>\r\n");
#elif defined(_M_X64)
		WriteCrashFormat(file,
			"RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\r\n"
			"RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\r\n"
			"RIP=%016llX EFlags=%08X\r\n",
			static_cast<unsigned long long>(context->Rax), static_cast<unsigned long long>(context->Rbx),
			static_cast<unsigned long long>(context->Rcx), static_cast<unsigned long long>(context->Rdx),
			static_cast<unsigned long long>(context->Rsi), static_cast<unsigned long long>(context->Rdi),
			static_cast<unsigned long long>(context->Rbp), static_cast<unsigned long long>(context->Rsp),
			static_cast<unsigned long long>(context->Rip), context->EFlags);
		WriteCrashAddress(file, "Instruction", static_cast<uintptr_t>(context->Rip));
#else
		WriteCrashText(file, "Register dump is unavailable for this architecture.\r\n");
#endif
	}

	LONG WINAPI QGL_UnhandledExceptionFilter( EXCEPTION_POINTERS *exceptionInfo )
	{
		SYSTEMTIME time = {};
		GetLocalTime(&time);
		char filename[MAX_PATH];
		sprintf_s(filename, "QindieGL-crash-%04u%02u%02u-%02u%02u%02u.txt",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		HANDLE file = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return EXCEPTION_CONTINUE_SEARCH;

		const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
			? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
		const void *address = exceptionInfo && exceptionInfo->ExceptionRecord
			? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;
		char module[MAX_PATH] = "<unknown>";
		MEMORY_BASIC_INFORMATION memory = {};
		if (address && VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory)) {
			GetModuleFileNameA(static_cast<HMODULE>(memory.AllocationBase), module, ARRAYSIZE(module));
		}

		WriteCrashText(file, "===== QindieGL Crash Diagnostic =====\r\n");
		WriteCrashFormat(file, "Exception code: 0x%08X\r\nFault address: %p\r\nModule: %s\r\nThread ID: %u\r\n",
			code, address, module, GetCurrentThreadId());
		if (exceptionInfo && exceptionInfo->ExceptionRecord) {
			const EXCEPTION_RECORD *record = exceptionInfo->ExceptionRecord;
			WriteCrashFormat(file, "Exception parameters: %u\r\n", record->NumberParameters);
			for (DWORD i = 0; i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
				WriteCrashFormat(file, "  [%u] 0x%IX\r\n", i, static_cast<uintptr_t>(record->ExceptionInformation[i]));
		}
		WriteCrashFormat(file, "Frame: %llu\r\nDraw: %llu\r\nLast GL error source: %s\r\nRender target: %s\r\n",
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), gLastErrorSource, gRenderTarget);
		WriteCrashFormat(file, "Active buffers: %s\r\nActive textures: %s\r\nActive ARB programs: %s\r\nProjection: %s\r\n",
			gActiveBuffers, gActiveTextures, gActivePrograms, gProjectionState);
		DumpCrashContext(file, exceptionInfo);
		WriteCrashText(file, "\r\nLast important GL/WGL events (newest first):\r\n");
		DumpCrashEvents(file, false, 100);
		WriteCrashText(file, "\r\nLast D3D calls/state transitions (newest first):\r\n");
		DumpCrashEvents(file, true, 50);
		WriteCrashText(file, "======================================\r\n");
		CloseHandle(file);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	void DumpCountMap( const char *emptyText, const std::map<std::string, uint64_t>& values )
	{
		if (values.empty()) {
			logPrintf("  %s\n", emptyText);
			return;
		}
		for (const auto& value : values)
			logPrintf("  %s: %llu\n", value.first.c_str(), static_cast<unsigned long long>(value.second));
	}
}

void QGL_DiagnosticsInitialize()
{
	gDiagnostics = {};
	gDiagnostics.debugMaxDrawCall = -1;
	gDiagnostics.debugDumpFrame = -1;
	gDiagnostics.debugDumpDraw = -1;
	gDiagnostics.crashDiagnostics = true;
	gDiagnostics.initialized = true;
	gNextEvent = 0;
	strcpy_s(gRenderTarget, "MAIN");
	strcpy_s(gLastErrorSource, "<none>");
	strcpy_s(gActiveBuffers, "array=0 element=0");
	strcpy_s(gActivePrograms, "vp=0 fp=0");
	strcpy_s(gActiveTextures, "none");
	strcpy_s(gProjectionState, "unavailable");
	gD3DFailures.clear();
	gUnsupportedEnums.clear();
	gPreviousExceptionFilter = SetUnhandledExceptionFilter(QGL_UnhandledExceptionFilter);
	gExceptionFilterInstalled = true;
	QGL_DiagnosticsRecordEvent(false, "LIFECYCLE", "diagnostics initialized");
}

void QGL_DiagnosticsShutdown()
{
	if (!gDiagnostics.initialized)
		return;
	if (gExceptionFilterInstalled)
		SetUnhandledExceptionFilter(gPreviousExceptionFilter);
	gPreviousExceptionFilter = nullptr;
	gExceptionFilterInstalled = false;
	gDiagnostics.initialized = false;
}

void QGL_DiagnosticsConfigure( int crashDiagnostics, int debugMaxDrawCall,
	int debugDumpFrame, int debugDumpDraw )
{
	gDiagnostics.crashDiagnostics = crashDiagnostics != 0;
	gDiagnostics.debugMaxDrawCall = debugMaxDrawCall;
	gDiagnostics.debugDumpFrame = debugDumpFrame;
	gDiagnostics.debugDumpDraw = debugDumpDraw;
	if (!gDiagnostics.crashDiagnostics && gExceptionFilterInstalled) {
		SetUnhandledExceptionFilter(gPreviousExceptionFilter);
		gExceptionFilterInstalled = false;
	} else if (gDiagnostics.crashDiagnostics && !gExceptionFilterInstalled) {
		gPreviousExceptionFilter = SetUnhandledExceptionFilter(QGL_UnhandledExceptionFilter);
		gExceptionFilterInstalled = true;
	}
	logPrintfLevel(QGL_LOG_INFO, "DIAGNOSTICS",
		"configured LogLevel=%d CrashDiagnostics=%d DebugMaxDrawCall=%d DebugDumpFrame=%d DebugDumpDraw=%d",
		logGetLevel(), crashDiagnostics, debugMaxDrawCall, debugDumpFrame, debugDumpDraw);
}

uint64_t QGL_DiagnosticsGetFrameId()
{
	return gDiagnostics.frameId;
}

uint64_t QGL_DiagnosticsGetDrawId()
{
	return gDiagnostics.drawId;
}

bool QGL_DiagnosticsBeginDraw( const char *api, unsigned int mode, int count,
	int first, unsigned int indexType, const void *indices )
{
	++gDiagnostics.drawId;
	++gDiagnostics.drawsSubmitted;
	QGL_DiagnosticsRecordEvent(false, "GL_DRAW", "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
		api ? api : "<unknown>", GLModeName(mode), mode, count, first, indexType, indices);
	logPrintfLevel(QGL_LOG_TRACE, "GL_DRAW", "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
		api ? api : "<unknown>", GLModeName(mode), mode, count, first, indexType, indices);

	SnapshotState();
	if (gDiagnostics.debugDumpDraw >= 0
		&& static_cast<int>(gDiagnostics.frameId) == gDiagnostics.debugDumpFrame
		&& static_cast<int>(gDiagnostics.drawId) == gDiagnostics.debugDumpDraw) {
		DumpSelectedDrawState(api ? api : "<unknown>", mode, count, first, indexType, indices);
	}

	if (gDiagnostics.debugMaxDrawCall >= 0
		&& static_cast<int>(gDiagnostics.drawId) > gDiagnostics.debugMaxDrawCall) {
		++gDiagnostics.drawsSkipped;
		logPrintfLevel(QGL_LOG_DEBUG, "GL_DRAW", "draw skipped by DebugMaxDrawCall=%d",
			gDiagnostics.debugMaxDrawCall);
		return false;
	}
	return true;
}

void QGL_DiagnosticsEndFrame( long presentResult )
{
	QGL_DiagnosticsRecordEvent(true, "PRESENT", "Present hr=0x%08X %s",
		static_cast<unsigned int>(presentResult), DXGetErrorString(presentResult));
	logPrintfLevel(QGL_LOG_TRACE, "PRESENT", "hr=0x%08X %s draws=%llu",
		static_cast<unsigned int>(presentResult), DXGetErrorString(presentResult),
		static_cast<unsigned long long>(gDiagnostics.drawId));
	// Only a successful Present is a real presentation boundary. In particular,
	// D3DERR_WASSTILLDRAWING from the DONOTWAIT path must not fabricate a frame.
	if (SUCCEEDED(presentResult)) {
		++gDiagnostics.framesPresented;
		++gDiagnostics.frameId;
		gDiagnostics.drawId = 0;
	}
}

void QGL_DiagnosticsRecordEvent( bool d3dEvent, const char *category, const char *fmt, ... )
{
	if (!gDiagnostics.initialized || !fmt)
		return;
	const LONG sequence = InterlockedIncrement(&gNextEvent);
	DiagnosticEvent& event = gEvents[(sequence - 1) % kEventCapacity];
	event.sequence = 0;
	event.d3dEvent = d3dEvent;
	event.frameId = gDiagnostics.frameId;
	event.drawId = gDiagnostics.drawId;
	strncpy_s(event.category, category ? category : "GENERAL", _TRUNCATE);
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(event.text, sizeof(event.text), _TRUNCATE, fmt, args);
	va_end(args);
	MemoryBarrier();
	event.sequence = sequence;
}

void QGL_DiagnosticsRecordD3DFailure( const char *call, long result )
{
	++gDiagnostics.failedD3DCalls;
	char key[192];
	sprintf_s(key, "%s hr=0x%08X %s", call ? call : "<unknown>",
		static_cast<unsigned int>(result), DXGetErrorString(result));
	++gD3DFailures[key];
	QGL_DiagnosticsRecordEvent(true, "D3D_ERROR", "%s", key);
	logPrintfLevel(QGL_LOG_ERROR, "D3D_ERROR", "%s", key);
}

void QGL_DiagnosticsRecordDeviceReset( long result )
{
	++gDiagnostics.deviceResets;
	QGL_DiagnosticsRecordEvent(true, "DEVICE_RESET", "Reset hr=0x%08X %s",
		static_cast<unsigned int>(result), DXGetErrorString(result));
	if (FAILED(result))
		QGL_DiagnosticsRecordD3DFailure("IDirect3DDevice9::Reset", result);
}

void QGL_DiagnosticsRecordPBufferCreated()
{
	++gDiagnostics.pBuffersCreated;
}

void QGL_DiagnosticsRecordARBProgramUpload( bool compiled, bool failed )
{
	++gDiagnostics.arbProgramsUploaded;
	if (compiled) ++gDiagnostics.arbProgramsCompiled;
	if (failed) ++gDiagnostics.arbProgramFailures;
}

void QGL_DiagnosticsRecordVBOCreated()
{
	++gDiagnostics.vbosCreated;
}

void QGL_DiagnosticsRecordVBOBytes( int64_t delta )
{
	gDiagnostics.currentVBOBytes += delta;
	if (gDiagnostics.currentVBOBytes < 0)
		gDiagnostics.currentVBOBytes = 0;
	if (static_cast<uint64_t>(gDiagnostics.currentVBOBytes) > gDiagnostics.peakVBOBytes)
		gDiagnostics.peakVBOBytes = static_cast<uint64_t>(gDiagnostics.currentVBOBytes);
}

void QGL_DiagnosticsSetRenderTarget( const char *name )
{
	strncpy_s(gRenderTarget, name ? name : "<unknown>", _TRUNCATE);
	QGL_DiagnosticsRecordEvent(true, "RENDER_TARGET", "active=%s", gRenderTarget);
}

void QGL_SetErrorImpl( long error, const char *source )
{
	D3DGlobal.lastError = error;
	if (SUCCEEDED(error)) {
		strcpy_s(gLastErrorSource, "<none>");
		return;
	}
	strncpy_s(gLastErrorSource, source ? source : "<unknown>", _TRUNCATE);
	if (error == E_INVALID_ENUM)
		++gUnsupportedEnums[gLastErrorSource];
	QGL_DiagnosticsRecordEvent(false, "GL_ERROR", "source=%s internal=0x%08X mapsTo=%s",
		gLastErrorSource, static_cast<unsigned int>(error), GLErrorName(error));
	logPrintfLevel(QGL_LOG_DEBUG, "GL_ERROR", "source=%s internal=0x%08X mapsTo=%s",
		gLastErrorSource, static_cast<unsigned int>(error), GLErrorName(error));
}

void QGL_DiagnosticsDumpCapabilityReport()
{
	if (!logIsEnabled(QGL_LOG_INFO))
		return;
	char executable[MAX_PATH] = "<unknown>";
	GetModuleFileNameA(nullptr, executable, ARRAYSIZE(executable));

	logPrintf("===== QindieGL Capability Report =====\n");
	logPrintf("Game executable: %s\n", executable);
#if defined(_M_IX86)
	logPrintf("Architecture: x86\n");
#elif defined(_M_AMD64)
	logPrintf("Architecture: x64\n");
#else
	logPrintf("Architecture: unknown\n");
#endif
	logPrintf("Reported OpenGL identity:\n");
	logPrintf("  GL_VENDOR: %s\n", WRAPPER_GL_VENDOR_STRING);
	logPrintf("  GL_RENDERER: %s\n", D3DGlobal.szRendererName ? D3DGlobal.szRendererName : "<unknown>");
	logPrintf("  GL_VERSION: %s\n", WRAPPER_GL_VERSION_STRING);
	logPrintf("D3D adapter: %s\n", D3DGlobal.szRendererName ? D3DGlobal.szRendererName : "<unknown>");
	logPrintf("D3D9 caps:\n");
	logPrintf("  VertexShaderVersion: 0x%08X\n", D3DGlobal.hD3DCaps.VertexShaderVersion);
	logPrintf("  PixelShaderVersion: 0x%08X\n", D3DGlobal.hD3DCaps.PixelShaderVersion);
	logPrintf("  MaxTextureWidth: %u\n", D3DGlobal.hD3DCaps.MaxTextureWidth);
	logPrintf("  MaxTextureHeight: %u\n", D3DGlobal.hD3DCaps.MaxTextureHeight);
	logPrintf("  MaxSimultaneousTextures: %u\n", D3DGlobal.hD3DCaps.MaxSimultaneousTextures);
	logPrintf("  MaxStreams: %u\n", D3DGlobal.hD3DCaps.MaxStreams);
	logPrintf("  MaxAnisotropy: %u\n", D3DGlobal.hD3DCaps.MaxAnisotropy);
	logPrintf("QindieGL settings:\n");
	logPrintf("  LogLevel: %d\n", logGetLevel());
	logPrintf("  ProjectionFix: %u\n", D3DGlobal.settings.projectionFix);
	logPrintf("  DrawCallFastPath: %u\n", D3DGlobal.settings.drawcallFastPath);
	logPrintf("  EnableARBProgramsStub: %u\n", D3DGlobal.settings.enableARBProgramsStub);
	logPrintf("  MultiSample: %u\n", D3DGlobal.settings.multisample);
	logPrintf("  CrashDiagnostics: %u\n", D3DGlobal.settings.crashDiagnostics);
	logPrintf("  DebugMaxDrawCall: %d\n", D3DGlobal.settings.debugMaxDrawCall);
	logPrintf("  DebugDumpFrame: %d\n", D3DGlobal.settings.debugDumpFrame);
	logPrintf("  DebugDumpDraw: %d\n", D3DGlobal.settings.debugDumpDraw);
	const char *glNames[] = {
		"GL_ARB_vertex_buffer_object", "GL_ARB_vertex_program", "GL_ARB_fragment_program",
		"GL_ARB_depth_texture", "GL_ARB_shadow", "GL_EXT_texture_rectangle",
		"GL_SGIS_generate_mipmap"
	};
	logPrintf("Advertised important GL extensions:\n");
	for (const char *name : glNames)
		logPrintf("  %s = %s\n", name, HasExtension(D3DGlobal.szExtensions, name) ? "YES" : "NO");
	const char *wglNames[] = { "WGL_ARB_pbuffer", "WGL_ARB_render_texture", "WGL_ARB_pixel_format" };
	logPrintf("Advertised important WGL extensions:\n");
	for (const char *name : wglNames)
		logPrintf("  %s = %s\n", name, HasExtension(D3DGlobal.szWExtensions, name) ? "YES" : "NO");
	logPrintf("======================================\n");
}

void QGL_DiagnosticsDumpSessionSummary()
{
	if (gDiagnostics.summaryDumped || !logIsEnabled(QGL_LOG_INFO))
		return;
	gDiagnostics.summaryDumped = true;
	logPrintf("===== QindieGL Session Summary =====\n");
	logPrintf("Frames: %llu\n", static_cast<unsigned long long>(gDiagnostics.framesPresented));
	logPrintf("Draw calls: %llu\n", static_cast<unsigned long long>(gDiagnostics.drawsSubmitted));
	logPrintf("Draw calls skipped: %llu\n", static_cast<unsigned long long>(gDiagnostics.drawsSkipped));
	D3DExtension_DumpProcSummary();
	logPrintf("Unsupported enums by originating function:\n");
	DumpCountMap("none", gUnsupportedEnums);
	logPrintf("Failed D3D calls:\n");
	DumpCountMap("none", gD3DFailures);
	logPrintf("Device resets: %llu\n", static_cast<unsigned long long>(gDiagnostics.deviceResets));
	logPrintf("PBuffers created: %llu\n", static_cast<unsigned long long>(gDiagnostics.pBuffersCreated));
	logPrintf("ARB programs uploaded: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramsUploaded));
	logPrintf("ARB programs compiled: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramsCompiled));
	logPrintf("ARB program compilation failures: %llu\n", static_cast<unsigned long long>(gDiagnostics.arbProgramFailures));
	logPrintf("VBOs created: %llu\n", static_cast<unsigned long long>(gDiagnostics.vbosCreated));
	logPrintf("Peak VBO bytes: %llu\n", static_cast<unsigned long long>(gDiagnostics.peakVBOBytes));
	logPrintf("====================================\n");
}
