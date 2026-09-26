/***************************************************************************
* QindieGL diagnostic instrumentation.
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_texture.hpp"
#include "d3d_buffer.hpp"
#include "d3d_extension.hpp"
#include "d3d_arb_program.hpp"
#include "d3d_matrix_stack.hpp"
#include "d3d_utils.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

// Parameters of the currently bound ARB program object (d3d_extension.cpp).
extern GLfloat (*ARB_LocalParams( GLenum target ))[4];
extern bool ARB_GetLocalWriteStamp( GLenum target, GLuint index, uint64_t *frame, uint64_t *draw );

namespace {
	static const LONG kEventCapacity = 256;
	static const size_t kEventTextSize = 384;

	// GL object bindings captured before every draw. Kept as raw values: they
	// are only formatted when a crash report or a state-change event needs them.
	struct StateSnapshot
	{
		GLuint arrayBuffer;
		GLuint elementBuffer;
		GLuint vertexProgram;
		GLuint fragmentProgram;
		GLuint textures[MAX_D3D_TMU][D3D_TEXTARGET_MAX];
	};

	struct DrawEventPayload
	{
		const char *api;	// string literal
		unsigned int mode;
		int count;
		int first;
		unsigned int indexType;
		const void *indices;
	};

	// Hot per-draw events store raw payloads; text is produced when dumped.
	enum EventKind { EVENT_TEXT, EVENT_DRAW, EVENT_STATE };

	struct DiagnosticEvent
	{
		LONG sequence;
		bool d3dEvent;
		EventKind kind;
		uint64_t frameId;
		uint64_t drawId;
		char category[24];
		union {
			char text[kEventTextSize];
			DrawEventPayload draw;
			StateSnapshot state;
		};
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

	// Performance counters. Frame statistics only include frames with world
	// draws; the histogram has 0.1 ms buckets up to 200 ms plus an overflow.
	static const int kFrameHistogramBuckets = 2001;
	struct PerformanceState
	{
		int64_t frequency;
		int64_t lastFrameEnd;
		int64_t presentStart;
		int drawTimerDepth;
		int64_t frameDrawTicks;
		int64_t frameSectionTicks[QGL_PERF_SECTIONS];
		double sectionMsSum[QGL_PERF_SECTIONS];
		uint64_t fastPathLocks;
		uint64_t slowPathLocks;
		uint64_t frameVertices;
		uint64_t frameVertexBytes;
		uint64_t frameIndexBytes;
		uint64_t frames;
		double frameMsSum, frameMsMax;
		double drawMsSum, drawMsMax;
		double presentMsSum;
		uint64_t drawsSum, drawsMax;
		uint64_t verticesSum, vertexBytesSum, indexBytesSum;
		uint32_t histogram[kFrameHistogramBuckets];
	};
	static PerformanceState gPerformance = {};
	static std::map<int, uint64_t> gSlowPathReasons;

	int64_t PerformanceNow()
	{
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (!gPerformance.frequency) {
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
			gPerformance.frequency = frequency.QuadPart;
		}
		return now.QuadPart;
	}

	double TicksToMs( int64_t ticks )
	{
		return gPerformance.frequency ? ticks * 1000.0 / static_cast<double>(gPerformance.frequency) : 0.0;
	}

	void RecordFramePerformance( bool worldFrame, uint64_t draws )
	{
		const int64_t now = PerformanceNow();
		const double presentMs = gPerformance.presentStart ? TicksToMs(now - gPerformance.presentStart) : 0.0;
		if (worldFrame && gPerformance.lastFrameEnd) {
			const double frameMs = TicksToMs(now - gPerformance.lastFrameEnd);
			const double drawMs = TicksToMs(gPerformance.frameDrawTicks);
			++gPerformance.frames;
			gPerformance.frameMsSum += frameMs;
			gPerformance.frameMsMax = std::max(gPerformance.frameMsMax, frameMs);
			gPerformance.drawMsSum += drawMs;
			gPerformance.drawMsMax = std::max(gPerformance.drawMsMax, drawMs);
			gPerformance.presentMsSum += presentMs;
			for (int section = 0; section < QGL_PERF_SECTIONS; ++section)
				gPerformance.sectionMsSum[section] += TicksToMs(gPerformance.frameSectionTicks[section]);
			gPerformance.drawsSum += draws;
			gPerformance.drawsMax = std::max(gPerformance.drawsMax, draws);
			gPerformance.verticesSum += gPerformance.frameVertices;
			gPerformance.vertexBytesSum += gPerformance.frameVertexBytes;
			gPerformance.indexBytesSum += gPerformance.frameIndexBytes;
			++gPerformance.histogram[std::min(static_cast<int>(frameMs * 10.0), kFrameHistogramBuckets - 1)];

			static unsigned int slowFramesLogged = 0;
			if (frameMs > 100.0 && slowFramesLogged < 32 && logIsEnabled(QGL_LOG_INFO)) {
				++slowFramesLogged;
				const double uploadMs = TicksToMs(gPerformance.frameSectionTicks[QGL_PERF_TEXTURE_UPLOAD]);
				logPrintfLevel(QGL_LOG_INFO, "PERF",
					"slow frame %llu: %.1f ms = draw calls %.1f + Present %.1f + texture uploads %.1f + outside QindieGL %.1f; draws %llu, vertices %llu",
					static_cast<unsigned long long>(gDiagnostics.frameId), frameMs, drawMs, presentMs, uploadMs,
					std::max(0.0, frameMs - drawMs - presentMs - uploadMs), static_cast<unsigned long long>(draws),
					static_cast<unsigned long long>(gPerformance.frameVertices));
			}
		}
		gPerformance.lastFrameEnd = now;
		gPerformance.presentStart = 0;
		gPerformance.frameDrawTicks = 0;
		memset(gPerformance.frameSectionTicks, 0, sizeof(gPerformance.frameSectionTicks));
		gPerformance.frameVertices = 0;
		gPerformance.frameVertexBytes = 0;
		gPerformance.frameIndexBytes = 0;
	}

	double FramePercentileMs( double fraction )
	{
		const uint64_t target = static_cast<uint64_t>(gPerformance.frames * fraction);
		uint64_t seen = 0;
		for (int bucket = 0; bucket < kFrameHistogramBuckets; ++bucket) {
			seen += gPerformance.histogram[bucket];
			if (seen > target) return (bucket + 1) / 10.0;
		}
		return kFrameHistogramBuckets / 10.0;
	}

	void DumpPerformanceSummary()
	{
		const uint64_t frames = gPerformance.frames;
		logPrintf("Performance (%llu frames with world draws):\n", static_cast<unsigned long long>(frames));
		if (!frames) return;
		const double frameMs = gPerformance.frameMsSum / frames;
		const double drawMs = gPerformance.drawMsSum / frames;
		logPrintf("  Frame time: avg %.2f ms (%.1f fps), p50 %.1f, p95 %.1f, p99 %.1f, max %.1f ms\n",
			frameMs, frameMs > 0.0 ? 1000.0 / frameMs : 0.0, FramePercentileMs(0.50),
			FramePercentileMs(0.95), FramePercentileMs(0.99), gPerformance.frameMsMax);
		logPrintf("  Inside QindieGL draw calls: avg %.2f ms/frame (%.0f%% of frame time), max %.2f ms\n",
			drawMs, frameMs > 0.0 ? 100.0 * drawMs / frameMs : 0.0, gPerformance.drawMsMax);
		const double stateMs = gPerformance.sectionMsSum[QGL_PERF_STATE] / frames;
		const double verticesMs = gPerformance.sectionMsSum[QGL_PERF_VERTICES] / frames;
		const double submitMs = gPerformance.sectionMsSum[QGL_PERF_SUBMIT] / frames;
		const double diagnosticsMs = gPerformance.sectionMsSum[QGL_PERF_DIAGNOSTICS] / frames;
		logPrintf("    state application %.2f ms, vertex conversion/upload %.2f ms, DrawIndexedPrimitive %.2f ms, diagnostics %.2f ms, other %.2f ms\n",
			stateMs, verticesMs, submitMs, diagnosticsMs,
			std::max(0.0, drawMs - stateMs - verticesMs - submitMs - diagnosticsMs));
		const uint64_t locks = gPerformance.fastPathLocks + gPerformance.slowPathLocks;
		logPrintf("    vertex copy path (all frames): fast %llu, slow %llu (%.0f%% slow)\n",
			static_cast<unsigned long long>(gPerformance.fastPathLocks),
			static_cast<unsigned long long>(gPerformance.slowPathLocks),
			locks ? 100.0 * gPerformance.slowPathLocks / locks : 0.0);
		std::vector<std::pair<uint64_t, int>> reasons;
		for (const auto &reason : gSlowPathReasons) reasons.emplace_back(reason.second, reason.first);
		std::sort(reasons.rbegin(), reasons.rend());
		for (size_t i = 0; i < reasons.size() && i < 6; ++i)
			logPrintf("      slow because of d3d_array.cpp:%d: %llu\n", reasons[i].second,
				static_cast<unsigned long long>(reasons[i].first));
		logPrintf("  Present: avg %.2f ms/frame\n", gPerformance.presentMsSum / frames);
		logPrintf("  Texture uploads (outside draw calls): avg %.2f ms/frame\n",
			gPerformance.sectionMsSum[QGL_PERF_TEXTURE_UPLOAD] / frames);
		logPrintf("  Draw calls: avg %.0f/frame, max %llu\n", static_cast<double>(gPerformance.drawsSum) / frames,
			static_cast<unsigned long long>(gPerformance.drawsMax));
		logPrintf("  Streamed to D3D9 by vertex arrays: avg %.0f vertices, %.1f KB vertex data, %.1f KB index data per frame\n",
			static_cast<double>(gPerformance.verticesSum) / frames,
			static_cast<double>(gPerformance.vertexBytesSum) / frames / 1024.0,
			static_cast<double>(gPerformance.indexBytesSum) / frames / 1024.0);
	}
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
	static std::set<uint32_t> gYAEWorldDrawStates;
	static std::set<GLuint> gYAEDumpedTextures;
	static unsigned int gYAEPostEffectDraws = 0;
	static bool gYAEPostEffectAfterDumped = false;
	static StateSnapshot gSnapshot = {};
	static DrawEventPayload gLastDraw = {};
	static StateSnapshot gRecordedSnapshot = {};
	static bool gHaveRecordedSnapshot = false;

	DiagnosticEvent &BeginEvent( bool d3dEvent, const char *category, EventKind kind, LONG &sequence )
	{
		sequence = InterlockedIncrement(&gNextEvent);
		DiagnosticEvent &event = gEvents[(sequence - 1) % kEventCapacity];
		event.sequence = 0;
		event.d3dEvent = d3dEvent;
		event.kind = kind;
		event.frameId = gDiagnostics.frameId;
		event.drawId = gDiagnostics.drawId;
		strncpy_s(event.category, category ? category : "GENERAL", _TRUNCATE);
		return event;
	}

	void CommitEvent( DiagnosticEvent &event, LONG sequence )
	{
		MemoryBarrier();
		event.sequence = sequence;
	}

	void RecordDrawEvent( const char *api, unsigned int mode, int count, int first,
		unsigned int indexType, const void *indices )
	{
		gLastDraw.api = api;
		gLastDraw.mode = mode;
		gLastDraw.count = count;
		if (!gDiagnostics.initialized) return;
		LONG sequence;
		DiagnosticEvent &event = BeginEvent(false, "GL_DRAW", EVENT_DRAW, sequence);
		event.draw.api = api;
		event.draw.mode = mode;
		event.draw.count = count;
		event.draw.first = first;
		event.draw.indexType = indexType;
		event.draw.indices = indices;
		CommitEvent(event, sequence);
	}

	void RecordStateEvent( const StateSnapshot &snapshot )
	{
		if (!gDiagnostics.initialized) return;
		LONG sequence;
		DiagnosticEvent &event = BeginEvent(false, "STATE", EVENT_STATE, sequence);
		event.state = snapshot;
		CommitEvent(event, sequence);
	}

	void FormatTextures( const StateSnapshot &snapshot, char *out, size_t size )
	{
		size_t used = 0;
		out[0] = '\0';
		for (int unit = 0; unit < MAX_D3D_TMU; ++unit) {
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				if (!snapshot.textures[unit][target] || used + 32 >= size) continue;
				const int written = _snprintf_s(out + used, size - used, _TRUNCATE, "tmu%d:id%u ",
					unit, snapshot.textures[unit][target]);
				if (written > 0) used += static_cast<size_t>(written);
			}
		}
		if (!used) strcpy_s(out, size, "none");
	}

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

	void FormatEventText( const DiagnosticEvent &event, char *out, size_t size )
	{
		switch (event.kind) {
		case EVENT_DRAW:
			_snprintf_s(out, size, _TRUNCATE, "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
				event.draw.api ? event.draw.api : "<unknown>", GLModeName(event.draw.mode), event.draw.mode,
				event.draw.count, event.draw.first, event.draw.indexType, event.draw.indices);
			break;
		case EVENT_STATE:
			{
				char textures[384];
				FormatTextures(event.state, textures, sizeof(textures));
				_snprintf_s(out, size, _TRUNCATE, "buffers(array=%u element=%u) programs(vp=%u fp=%u) textures(%s)",
					event.state.arrayBuffer, event.state.elementBuffer, event.state.vertexProgram,
					event.state.fragmentProgram, textures);
			}
			break;
		default:
			strncpy_s(out, size, event.text, _TRUNCATE);
			break;
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

	void MixHash( uint32_t& hash, const void *data, size_t length )
	{
		const unsigned char *bytes = static_cast<const unsigned char *>(data);
		for (size_t i = 0; i < length; ++i) {
			hash ^= bytes[i];
			hash *= 16777619u;
		}
	}

	int ResolveSampleVertex( int first, unsigned int indexType, const void *indices )
	{
		if (!indexType) return first;
		size_t size = indexType == GL_UNSIGNED_BYTE ? 1 : indexType == GL_UNSIGNED_SHORT ? 2 :
			indexType == GL_UNSIGNED_INT ? 4 : 0;
		if (!size) return first;
		const GLubyte *resolved = D3DBuffer_ResolvePointer(
			D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB), indices, size);
		if (!resolved) return first;
		if (indexType == GL_UNSIGNED_BYTE) return *resolved;
		if (indexType == GL_UNSIGNED_SHORT) return *reinterpret_cast<const GLushort *>(resolved);
		return static_cast<int>(*reinterpret_cast<const GLuint *>(resolved));
	}

	void LogArraySample( const char *name, const D3DVAInfo& info, int vertex )
	{
		const size_t componentBytes = info.elementType == GL_FLOAT ? sizeof(GLfloat) : 0;
		const size_t packedBytes = componentBytes * static_cast<size_t>(info.elementCount);
		const size_t stride = info.stride > 0 ? static_cast<size_t>(info.stride) : packedBytes;
		const size_t required = componentBytes ? static_cast<size_t>(vertex) * stride + packedBytes : 0;
		const GLubyte *base = componentBytes ?
			D3DBuffer_ResolvePointer(info.bufferBinding, info.data, required) : nullptr;
		if (!base) {
			logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
				"%s buffer=%u offset=%p size=%d type=0x%X stride=%d sample=unavailable",
				name, info.bufferBinding, info.data, info.elementCount, info.elementType, info.stride);
			return;
		}
		const GLfloat *value = reinterpret_cast<const GLfloat *>(base + static_cast<size_t>(vertex) * stride);
		logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
			"%s buffer=%u offset=%p size=%d type=0x%X stride=%d v%d=(%.6f,%.6f,%.6f,%.6f)",
			name, info.bufferBinding, info.data, info.elementCount, info.elementType, info.stride, vertex,
			info.elementCount > 0 ? value[0] : 0.0f, info.elementCount > 1 ? value[1] : 0.0f,
			info.elementCount > 2 ? value[2] : 0.0f, info.elementCount > 3 ? value[3] : 1.0f);
	}

	void CensusYAEWorldDraw( const char *api, unsigned int mode, int count, int first,
		unsigned int indexType, const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility || gDiagnostics.frameId < 250 ||
			!D3DGlobal.projectionMatrixStack || D3DGlobal_IsOrthoProjection() ||
			!D3DState.EnableState.depthTestEnabled ||
			!(D3DState.ClientVertexArrayState.vertexArrayEnable & VA_ENABLE_VERTEX_BIT) ||
			gYAEWorldDrawStates.size() >= 64)
			return;

		uint32_t signature = 2166136261u;
		MixHash(signature, &D3DState.ClientVertexArrayState.vertexArrayEnable,
			sizeof(D3DState.ClientVertexArrayState.vertexArrayEnable));
		MixHash(signature, &D3DState.EnableState.textureEnabled,
			sizeof(D3DState.EnableState.textureEnabled));
		MixHash(signature, &D3DState.EnableState.textureTargetEnabled,
			sizeof(D3DState.EnableState.textureTargetEnabled));
		MixHash(signature, &D3DState.EnableState.vertexProgramEnabled,
			sizeof(D3DState.EnableState.vertexProgramEnabled));
		MixHash(signature, &D3DState.EnableState.fragmentProgramEnabled,
			sizeof(D3DState.EnableState.fragmentProgramEnabled));
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			MixHash(signature, &D3DState.TextureState.TextureCombineState[unit],
				sizeof(D3DState.TextureState.TextureCombineState[unit]));
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				GLuint id = texture ? texture->GetGLIndex() : 0;
				MixHash(signature, &id, sizeof(id));
			}
		}
		if (!gYAEWorldDrawStates.insert(signature).second) return;

		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);
		logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
			"state=%u/64 signature=%08X frame=%llu draw=%llu api=%s mode=0x%X count=%d sampleVertex=%d arrayMask=0x%08X programs=%u/%u enabled=%u/%u",
			(unsigned int)gYAEWorldDrawStates.size(), signature,
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, mode, count, sampleVertex,
			D3DState.ClientVertexArrayState.vertexArrayEnable,
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(),
			D3DState.EnableState.vertexProgramEnabled, D3DState.EnableState.fragmentProgramEnabled);
		LogArraySample("vertex", D3DState.ClientVertexArrayState.vertexInfo, sampleVertex);
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			const bool coordEnabled = VA_TEXTURE_BIT_IS_SET(D3DState.ClientVertexArrayState.vertexArrayEnable, unit);
			if (!D3DState.EnableState.textureEnabled[unit] && !coordEnabled) continue;
			GLuint textureId = 0;
			int chosenTarget = -1;
			unsigned int targetMask = 0;
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				if (D3DState.EnableState.textureTargetEnabled[unit][target]) {
					targetMask |= 1u << target;
					D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
					if (texture) { textureId = texture->GetGLIndex(); chosenTarget = target; }
				}
			}
			const auto& combiner = D3DState.TextureState.TextureCombineState[unit];
			DWORD textureTransformFlags = D3DTTFF_DISABLE;
			DWORD textureCoordinateIndex = 0;
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXTURETRANSFORMFLAGS,
				&textureTransformFlags);
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXCOORDINDEX,
				&textureCoordinateIndex);
			logPrintfLevel(QGL_LOG_INFO, "YAE_DRAW_CENSUS",
				"tmu=%d enabled=%u targetMask=0x%X texture=%u target=%d size=%ux%u coord=%s d3dCoord=%u transform=0x%X env=0x%X rgbOp=0x%X rgbArgs=0x%X/0x%X/0x%X scale=%u",
				unit, D3DState.EnableState.textureEnabled[unit], targetMask, textureId, chosenTarget,
				chosenTarget >= 0 && D3DState.TextureState.currentTexture[unit][chosenTarget] ?
					D3DState.TextureState.currentTexture[unit][chosenTarget]->GetWidth() : 0,
				chosenTarget >= 0 && D3DState.TextureState.currentTexture[unit][chosenTarget] ?
					D3DState.TextureState.currentTexture[unit][chosenTarget]->GetHeight() : 0,
				coordEnabled ? "YES" : "NO", textureCoordinateIndex, textureTransformFlags,
				combiner.envMode, combiner.colorOp,
				combiner.colorArg1, combiner.colorArg2, combiner.colorArg3, combiner.colorScale);

			// Capture the first static-world material inputs after the level has settled.
			// D3DX performs the DXT decompression, making the dump useful for checking
			// whether corruption happened during upload rather than during sampling.
			if (D3DState.EnableState.vertexProgramEnabled && ARB_GetBoundVertexProgram() == 6 &&
				chosenTarget >= 0) {
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][chosenTarget];
				if (texture && texture->GetTarget() != GL_TEXTURE_CUBE_MAP_ARB &&
					(texture->GetGLIndex() == 1 || texture->GetGLIndex() == 209) &&
					gYAEDumpedTextures.insert(texture->GetGLIndex()).second) {
					_mkdir("QindieGL-dump");
					_mkdir("QindieGL-dump\\textures");
					char filename[MAX_PATH];
					sprintf_s(filename, "QindieGL-dump\\textures\\yae_id_%u_%ux%u.png",
						texture->GetGLIndex(), texture->GetWidth(), texture->GetHeight());
					const HRESULT dumpResult = D3DXSaveTextureToFileA(filename, D3DXIFF_PNG,
						texture->GetD3DTexture(), nullptr);
					logPrintfLevel(QGL_LOG_INFO, "YAE_TEXTURE_DUMP",
						"texture=%u tmu=%d file=%s result=0x%08X",
						texture->GetGLIndex(), unit, filename, dumpResult);
				}
			}
			if (coordEnabled) {
				char name[24];
				sprintf_s(name, "texcoord%d", unit);
				LogArraySample(name, D3DState.ClientVertexArrayState.texCoordInfo[unit], sampleVertex);
			}
		}
	}

	void TraceYAEPostEffectDraw( const char *api, unsigned int mode, int count, int first,
		unsigned int indexType, const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility ||
			!D3DState.EnableState.fragmentProgramEnabled ||
			ARB_GetBoundFragmentProgram() != 8 || gYAEPostEffectDraws >= 96)
			return;

		++gYAEPostEffectDraws;
		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);
		logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
			"sample=%u frame=%llu draw=%llu api=%s mode=0x%X count=%d vertex=%d blend=%u glBlend=0x%X/0x%X d3dBlend=%u/%u op=%u color=0x%08X arrays=0x%08X programs=%u/%u enabled=%u/%u requiredTexcoords=%d",
			gYAEPostEffectDraws,
			static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, mode, count, sampleVertex,
			D3DState.EnableState.alphaBlendEnabled,
			D3DState.ColorBufferState.glBlendSrc, D3DState.ColorBufferState.glBlendDst,
			D3DState.ColorBufferState.alphaBlendSrcFunc,
			D3DState.ColorBufferState.alphaBlendDstFunc,
			D3DState.ColorBufferState.alphaBlendOp, D3DState.CurrentState.currentColor,
			D3DState.ClientVertexArrayState.vertexArrayEnable,
			ARB_GetBoundVertexProgram(), ARB_GetBoundFragmentProgram(),
			D3DState.EnableState.vertexProgramEnabled,
			D3DState.EnableState.fragmentProgramEnabled,
			ARB_GetRequiredVertexTexCoordCount());

		if (gYAEPostEffectDraws == 1) {
			_mkdir("QindieGL-dump");
			_mkdir("QindieGL-dump\\textures");
			D3DTextureObject *source = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
			const HRESULT sourceResult = source ? D3DXSaveTextureToFileA(
				"QindieGL-dump\\textures\\yae_post_source.png", D3DXIFF_PNG,
				source->GetD3DTexture(), nullptr) : E_FAIL;
			logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
				"source dump result=0x%08X file=QindieGL-dump\\textures\\yae_post_source.png",
				sourceResult);
		}

		for (int unit : { 0, 1, 2, 4 }) {
			D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][D3D_TEXTARGET_2D];
			DWORD transformFlags = D3DTTFF_DISABLE;
			D3DGlobal.pDevice->GetTextureStageState(unit, D3DTSS_TEXTURETRANSFORMFLAGS,
				&transformFlags);
			D3DStateMatrix& matrix = D3DGlobal.textureMatrixStack[unit]->top();
			const D3DXMATRIX& m = *static_cast<const D3DXMATRIX *>(matrix);
			logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
				"tmu=%d enabled=%u texture=%u size=%ux%u format=%d coord=(%.3f,%.3f,%.3f,%.3f) matrixIdentity=%u transform=0x%X matrix=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f]",
				unit, D3DState.EnableState.textureEnabled[unit], texture ? texture->GetGLIndex() : 0,
				texture ? texture->GetWidth() : 0, texture ? texture->GetHeight() : 0,
				texture ? texture->GetInternalFormat() : -1,
				D3DState.CurrentState.currentTexCoord[unit][0],
				D3DState.CurrentState.currentTexCoord[unit][1],
				D3DState.CurrentState.currentTexCoord[unit][2],
				D3DState.CurrentState.currentTexCoord[unit][3],
				matrix.is_identity(), transformFlags,
				m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24,
				m._31, m._32, m._33, m._34, m._41, m._42, m._43, m._44);
			if (VA_TEXTURE_BIT_IS_SET(D3DState.ClientVertexArrayState.vertexArrayEnable, unit)) {
				char name[24];
				sprintf_s(name, "postTexcoord%d", unit);
				LogArraySample(name, D3DState.ClientVertexArrayState.texCoordInfo[unit], sampleVertex);
			}
		}
	}

	// Fixed-function lighting census (You Are Empty profile). DS2 lights dynamic
	// geometry with GL lights; this records which light types, spot parameters,
	// materials and blend modes actually reach lit draws, and counts lit draws
	// using spot lights for the session summary.
	std::set<uint32_t> gYAELitDrawStates;
	uint64_t gLitDraws = 0;
	uint64_t gLitDrawsWithSpot = 0;

	void CensusLitDraw( const char *api, int count )
	{
		if (!D3DState.EnableState.lightingEnabled)
			return;
		const auto &lighting = D3DState.LightingState;
		uint32_t signature = 2166136261u;
		bool spot = false;
		for (int i = 0; i < IMPL_MAX_LIGHTS && i < D3DGlobal.maxActiveLights; ++i) {
			if (!D3DState.EnableState.lightEnabled[i]) continue;
			const bool lightSpot = lighting.lightType[i] != D3DLIGHT_DIRECTIONAL && lighting.lightSpotCutoff[i] < 180.0f;
			spot = spot || lightSpot;
			MixHash(signature, &i, sizeof(i));
			MixHash(signature, &lighting.lightType[i], sizeof(lighting.lightType[i]));
			MixHash(signature, &lightSpot, sizeof(lightSpot));
		}
		++gLitDraws;
		if (spot) ++gLitDrawsWithSpot;

		if (!D3DGlobal.settings.game.yaeFallbackCompatibility || gDiagnostics.frameId < 250 ||
			gYAELitDrawStates.size() >= 48)
			return;
		D3DTextureObject *texture = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
		const GLuint textureId = texture && D3DState.EnableState.textureEnabled[0] ? texture->GetGLIndex() : 0;
		MixHash(signature, &D3DState.EnableState.alphaBlendEnabled, sizeof(DWORD));
		MixHash(signature, &D3DState.ColorBufferState.glBlendSrc, sizeof(GLenum));
		MixHash(signature, &D3DState.ColorBufferState.glBlendDst, sizeof(GLenum));
		MixHash(signature, &D3DState.EnableState.colorMaterialEnabled, sizeof(DWORD));
		MixHash(signature, &lighting.colorMaterial, sizeof(GLenum));
		MixHash(signature, &textureId, sizeof(textureId));
		if (!gYAELitDrawStates.insert(signature).second)
			return;

		const D3DXMATRIX &mv = *static_cast<const D3DXMATRIX *>(D3DGlobal.modelviewMatrixStack->top());
		const auto &material = lighting.currentMaterial;
		const unsigned int record = static_cast<unsigned int>(gYAELitDrawStates.size());
		logPrintfLevel(QGL_LOG_INFO, "YAE_LIGHT_CENSUS",
			"state=%u/48 frame=%llu draw=%llu api=%s count=%d arrays=0x%08X tex0=%u blend=%u(0x%X,0x%X) colorMaterial=%u(0x%X) "
			"matDiffuse=(%.2f,%.2f,%.2f,%.2f) matAmbient=(%.2f,%.2f,%.2f) matEmissive=(%.2f,%.2f,%.2f) modelAmbient=0x%08X "
			"normalize=%u localViewer=%u mvScale=(%.3f,%.3f,%.3f)",
			record, static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, count,
			D3DState.ClientVertexArrayState.vertexArrayEnable, textureId,
			D3DState.EnableState.alphaBlendEnabled, D3DState.ColorBufferState.glBlendSrc,
			D3DState.ColorBufferState.glBlendDst, D3DState.EnableState.colorMaterialEnabled, lighting.colorMaterial,
			material.Diffuse.r, material.Diffuse.g, material.Diffuse.b, material.Diffuse.a,
			material.Ambient.r, material.Ambient.g, material.Ambient.b,
			material.Emissive.r, material.Emissive.g, material.Emissive.b, lighting.lightModelAmbient,
			D3DState.EnableState.normalizeEnabled, lighting.lightModelLocalViewer,
			sqrtf(mv._11 * mv._11 + mv._12 * mv._12 + mv._13 * mv._13),
			sqrtf(mv._21 * mv._21 + mv._22 * mv._22 + mv._23 * mv._23),
			sqrtf(mv._31 * mv._31 + mv._32 * mv._32 + mv._33 * mv._33));
		for (int i = 0; i < IMPL_MAX_LIGHTS && i < D3DGlobal.maxActiveLights; ++i) {
			if (!D3DState.EnableState.lightEnabled[i]) continue;
			const bool directional = lighting.lightType[i] == D3DLIGHT_DIRECTIONAL;
			logPrintfLevel(QGL_LOG_INFO, "YAE_LIGHT_CENSUS",
				"state=%u light=%d %s eyePos=(%.2f,%.2f,%.2f) diffuse=(%.2f,%.2f,%.2f) ambient=(%.2f,%.2f,%.2f) "
				"atten=(%.4f,%.6f,%.8f) spotCutoff=%.1f spotExponent=%.2f spotDir=(%.3f,%.3f,%.3f)",
				record, i, directional ? "DIRECTIONAL" : "POSITIONAL",
				directional ? -lighting.lightPosition[i].x : lighting.lightPosition[i].x,
				directional ? -lighting.lightPosition[i].y : lighting.lightPosition[i].y,
				directional ? -lighting.lightPosition[i].z : lighting.lightPosition[i].z,
				lighting.lightColorDiffuse[i].r, lighting.lightColorDiffuse[i].g, lighting.lightColorDiffuse[i].b,
				lighting.lightColorAmbient[i].r, lighting.lightColorAmbient[i].g, lighting.lightColorAmbient[i].b,
				lighting.lightAttenuation[i].x, lighting.lightAttenuation[i].y, lighting.lightAttenuation[i].z,
				lighting.lightSpotCutoff[i], lighting.lightSpotExponent[i],
				lighting.lightDirection[i].x, lighting.lightDirection[i].y, lighting.lightDirection[i].z);
		}
	}

	// Reads one array element without integer normalization, matching how the
	// draw path feeds texcoord arrays (YAE bone indices are GL_SHORT texcoords).
	// Unlike D3DBuffer_ResolvePointer this never records a GL error.
	bool ReadArrayElement( const D3DVAInfo& info, int vertex, float out[4] )
	{
		out[0] = out[1] = out[2] = 0.0f;
		out[3] = 1.0f;
		size_t componentBytes = 0;
		switch (info.elementType) {
		case GL_BYTE: case GL_UNSIGNED_BYTE: componentBytes = 1; break;
		case GL_SHORT: case GL_UNSIGNED_SHORT: componentBytes = 2; break;
		case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: componentBytes = 4; break;
		case GL_DOUBLE: componentBytes = 8; break;
		default: return false;
		}
		if (vertex < 0 || info.elementCount <= 0) return false;
		const size_t packedBytes = componentBytes * static_cast<size_t>(info.elementCount);
		const size_t stride = info.stride > 0 ? static_cast<size_t>(info.stride) : packedBytes;
		const size_t offset = static_cast<size_t>(vertex) * stride;
		const GLubyte *base = info.data;
		if (info.bufferBinding) {
			D3DBufferObject *buffer = D3DBuffer_GetObject(info.bufferBinding, false);
			const size_t bufferOffset = reinterpret_cast<size_t>(info.data);
			if (!buffer || !buffer->storage || buffer->size < 0 ||
				bufferOffset > static_cast<size_t>(buffer->size) ||
				offset + packedBytes > static_cast<size_t>(buffer->size) - bufferOffset)
				return false;
			base = static_cast<const GLubyte *>(buffer->storage) + bufferOffset;
		}
		if (!base) return false;
		const GLubyte *element = base + offset;
		for (int i = 0; i < info.elementCount && i < 4; ++i) {
			switch (info.elementType) {
			case GL_BYTE: out[i] = reinterpret_cast<const GLbyte *>(element)[i]; break;
			case GL_UNSIGNED_BYTE: out[i] = element[i]; break;
			case GL_SHORT: out[i] = reinterpret_cast<const GLshort *>(element)[i]; break;
			case GL_UNSIGNED_SHORT: out[i] = reinterpret_cast<const GLushort *>(element)[i]; break;
			case GL_INT: out[i] = static_cast<float>(reinterpret_cast<const GLint *>(element)[i]); break;
			case GL_UNSIGNED_INT: out[i] = static_cast<float>(reinterpret_cast<const GLuint *>(element)[i]); break;
			case GL_FLOAT: out[i] = reinterpret_cast<const GLfloat *>(element)[i]; break;
			case GL_DOUBLE: out[i] = static_cast<float>(reinterpret_cast<const GLdouble *>(element)[i]); break;
			}
		}
		return true;
	}

	float Dot4( const GLfloat row[4], const float v[4] )
	{
		return row[0] * v[0] + row[1] * v[1] + row[2] * v[2] + row[3] * v[3];
	}

	float Length3( const float v[3] )
	{
		return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	// Phase F probe for DS2's lit dynamic-material fragment programs, whose
	// linear fog reads the vertex program's TEXCOORD5 (camera_pos_ws minus the
	// inst_matrix-transformed position). The sample vertex is evaluated on the
	// CPU exactly as the rigid/skinned DS2 vertex programs do, and the camera
	// implied by the GL modelview is compared with the camera constants DS2
	// supplied. A mismatch identifies which input disagrees with the transform
	// that actually positions the geometry. Active only with LogLevel >= 3 (DEBUG);
	// the first saturated draws also dump the program/modelview call history.
	std::set<uint32_t> gYAEFogProbeStates;
	unsigned int gYAEFogProbeRecords = 0;

	struct ProgramHistoryEntry
	{
		uint32_t frame;
		uint32_t draw;
		char op;
		unsigned int target;
		unsigned int program;
		int index;
		float values[4];
	};
	static const size_t kProgramHistoryCapacity = 16384;
	static const unsigned int kProgramHistoryMaxDumps = 3;
	ProgramHistoryEntry gProgramHistory[kProgramHistoryCapacity];
	size_t gProgramHistoryNext = 0;
	unsigned int gProgramHistoryDumps = 0;

	bool ProgramHistoryActive()
	{
		return D3DGlobal.settings.game.yaeFallbackCompatibility && gDiagnostics.frameId >= 250 &&
			gProgramHistoryDumps < kProgramHistoryMaxDumps && logIsEnabled(QGL_LOG_DEBUG);
	}

	const char *ProgramTargetName( unsigned int target )
	{
		return target == GL_VERTEX_PROGRAM_ARB ? "VP" : target == GL_FRAGMENT_PROGRAM_ARB ? "FP" : "?";
	}

	// Writes the program binds, local writes and modelview operations issued
	// since four draws before the current one. Bone palette rows (VP local
	// 10..209) are counted rather than listed.
	void DumpProgramHistory( unsigned int record )
	{
		static const char *const modelviewOps[] = {
			"LoadIdentity", "LoadMatrix", "MultMatrix", "PushMatrix", "PopMatrix",
			"Translate", "Rotate", "Scale" };
		++gProgramHistoryDumps;
		const uint32_t frame = static_cast<uint32_t>(gDiagnostics.frameId);
		const uint32_t firstDraw = gDiagnostics.drawId > 4 ? static_cast<uint32_t>(gDiagnostics.drawId) - 4 : 0;
		const size_t available = gProgramHistoryNext < kProgramHistoryCapacity ?
			gProgramHistoryNext : kProgramHistoryCapacity;
		unsigned int boneWrites = 0, redundantBinds = 0, lines = 0;
		unsigned int boundVP = ~0u, boundFP = ~0u;
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u begin frame=%u fromDraw=%u",
			record, frame, firstDraw);
		for (size_t i = gProgramHistoryNext - available; i < gProgramHistoryNext; ++i) {
			const ProgramHistoryEntry& e = gProgramHistory[i % kProgramHistoryCapacity];
			if (e.frame != frame || e.draw < firstDraw) continue;
			if (e.op == 'L' && e.target == GL_VERTEX_PROGRAM_ARB && e.index >= 10 && e.index <= 209) {
				++boneWrites;
				continue;
			}
			// DS2 rebinds the same program around every parameter write.
			if (e.op == 'B') {
				unsigned int& bound = e.target == GL_VERTEX_PROGRAM_ARB ? boundVP : boundFP;
				if (bound == e.program) { ++redundantBinds; continue; }
				bound = e.program;
			}
			if (++lines > 400) break;
			switch (e.op) {
			case 'D':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u DRAW count=%.0f vp=%u(on=%.0f) fp=%d(on=%.0f)",
					record, e.draw, e.values[0], e.program, e.values[1], e.index, e.values[2]);
				break;
			case 'B':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u D:%u bind %s %u",
					record, e.draw, ProgramTargetName(e.target), e.program);
				break;
			case 'E': case 'e':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY", "record=%u D:%u %s %s",
					record, e.draw, e.op == 'E' ? "enable" : "disable", ProgramTargetName(e.target));
				break;
			case 'L':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u local %s program=%u [%d] = (%.4f, %.4f, %.4f, %.4f)",
					record, e.draw, ProgramTargetName(e.target), e.program, e.index,
					e.values[0], e.values[1], e.values[2], e.values[3]);
				break;
			case 'M':
				logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
					"record=%u D:%u modelview %s (%.4f, %.4f, %.4f, %.4f)",
					record, e.draw, e.index >= 0 && e.index < 8 ? modelviewOps[e.index] : "?",
					e.values[0], e.values[1], e.values[2], e.values[3]);
				break;
			}
		}
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_HISTORY",
			"record=%u end lines=%u boneWritesOmitted=%u redundantBindsOmitted=%u",
			record, lines, boneWrites, redundantBinds);
	}

	void LogLocalWriteStamps( unsigned int record, const char *name, GLenum target,
		const int *indices, int count )
	{
		char text[256] = "";
		size_t used = 0;
		for (int i = 0; i < count && used < sizeof(text) - 32; ++i) {
			uint64_t frame = 0, draw = 0;
			int written;
			if (indices[i] < 0)
				continue;
			if (ARB_GetLocalWriteStamp(target, static_cast<GLuint>(indices[i]), &frame, &draw))
				written = sprintf_s(text + used, sizeof(text) - used, " [%d]@F%llu:D%llu", indices[i],
					static_cast<unsigned long long>(frame), static_cast<unsigned long long>(draw));
			else
				written = sprintf_s(text + used, sizeof(text) - used, " [%d]@never", indices[i]);
			if (written > 0) used += static_cast<size_t>(written);
		}
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE", "record=%u %s lastWrites%s (now F%llu:D%llu)",
			record, name, text, static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId));
	}

	void ProbeYAEProgramFog( const char *api, int count, int first, unsigned int indexType,
		const void *indices )
	{
		if (!D3DGlobal.settings.game.yaeFallbackCompatibility || gDiagnostics.frameId < 250 ||
			!D3DState.EnableState.fragmentProgramEnabled || gYAEFogProbeRecords >= 160 ||
			!D3DGlobal.modelviewMatrixStack || !logIsEnabled(QGL_LOG_DEBUG))
			return;

		const GLuint fpId = ARB_GetBoundFragmentProgram();
		ARBCompiledProgram *fp = ARB_GetCompiledProgram(fpId);
		if (!fp || !fp->ps) return;
		const ARBParsedProgram& fpParsed = fp->parsed;
		if (!fpParsed.usedTexCoords.count(5) || !fpParsed.usedLocalParams.count(0) ||
			!fpParsed.usedLocalParams.count(1))
			return;

		GLfloat (*fpLocal)[4] = ARB_LocalParams(GL_FRAGMENT_PROGRAM_ARB);
		const auto& arrays = D3DState.ClientVertexArrayState;
		const int sampleVertex = ResolveSampleVertex(first, indexType, indices);

		float position[4];
		if (!(arrays.vertexArrayEnable & VA_ENABLE_VERTEX_BIT) ||
			!ReadArrayElement(arrays.vertexInfo, sampleVertex, position))
			return;

		const GLuint vpId = D3DState.EnableState.vertexProgramEnabled ? ARB_GetBoundVertexProgram() : 0;
		ARBCompiledProgram *vp = vpId ? ARB_GetCompiledProgram(vpId) : nullptr;
		const bool vpActive = vp && vp->vs;
		const bool skinned = vpActive && !vp->parsed.addressReg.empty();
		GLfloat (*vpLocal)[4] = ARB_LocalParams(GL_VERTEX_PROGRAM_ARB);

		// Model-space position consumed by MVP (DS2 VP register R2 / vertex.position).
		float model[4] = { position[0], position[1], position[2], 1.0f };
		float boneIds[4] = { 0, 0, 0, 1 }, boneWeights[4] = { 0, 0, 0, 1 };
		bool skinInputs = false;
		if (skinned &&
			VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 1) &&
			VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 2) &&
			ReadArrayElement(arrays.texCoordInfo[1], sampleVertex, boneIds) &&
			ReadArrayElement(arrays.texCoordInfo[2], sampleVertex, boneWeights)) {
			skinInputs = true;
			float skinnedPos[3] = { 0, 0, 0 };
			for (int influence = 0; influence < 2; ++influence) {
				const int a0 = static_cast<int>(floorf(boneIds[influence] * 3.0f));
				if (a0 + 11 < 0 || a0 + 11 >= 256) { skinInputs = false; break; }
				for (int axis = 0; axis < 3; ++axis)
					skinnedPos[axis] += boneWeights[influence] * Dot4(vpLocal[a0 + 9 + axis], position);
			}
			if (skinInputs) {
				model[0] = skinnedPos[0]; model[1] = skinnedPos[1]; model[2] = skinnedPos[2];
			}
		}

		// The matrix stack stores the transpose of the GL matrix, so D3DX row-vector
		// transforms reproduce GL's column-vector transforms.
		const D3DXMATRIX& modelview = *static_cast<const D3DXMATRIX *>(D3DGlobal.modelviewMatrixStack->top());
		D3DXVECTOR4 eye;
		D3DXVec4Transform(&eye, reinterpret_cast<const D3DXVECTOR4 *>(model), &modelview);
		const float eyeDistance = Length3(&eye.x);
		D3DXMATRIX inverseModelview;
		float modelCamera[4] = { 0, 0, 0, 1 };
		const bool invertible = D3DXMatrixInverse(&inverseModelview, nullptr, &modelview) != nullptr;
		if (invertible && inverseModelview._44 != 0.0f) {
			modelCamera[0] = inverseModelview._41 / inverseModelview._44;
			modelCamera[1] = inverseModelview._42 / inverseModelview._44;
			modelCamera[2] = inverseModelview._43 / inverseModelview._44;
		}
		float modelviewScale[3];
		for (int column = 0; column < 3; ++column) {
			const float axis[3] = { modelview.m[column][0], modelview.m[column][1], modelview.m[column][2] };
			modelviewScale[column] = Length3(axis);
		}

		float world[3] = { 0, 0, 0 }, camera[3] = { 0, 0, 0 }, impliedCamera[3] = { 0, 0, 0 };
		float toEye[3] = { 0, 0, 0 };
		int cameraIndex = -1;
		if (vpActive) {
			cameraIndex = vp->parsed.usedLocalParams.empty() ? -1 : *vp->parsed.usedLocalParams.rbegin();
			for (int axis = 0; axis < 3; ++axis) {
				world[axis] = Dot4(vpLocal[5 + axis], model);
				impliedCamera[axis] = Dot4(vpLocal[5 + axis], modelCamera);
				camera[axis] = cameraIndex >= 0 && cameraIndex < 256 ? vpLocal[cameraIndex][axis] : 0.0f;
				toEye[axis] = camera[axis] - world[axis];
			}
		} else {
			// Fixed-function vertex processing forwards texture coordinate set 5.
			float texcoord5[4];
			if (!VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, 5) ||
				!ReadArrayElement(arrays.texCoordInfo[5], sampleVertex, texcoord5))
				memcpy(texcoord5, D3DState.CurrentState.currentTexCoord[5], sizeof(texcoord5));
			memcpy(toEye, texcoord5, sizeof(toEye));
		}
		const float fogDistance = Length3(toEye);
		const float fogStart = fpLocal[0][0], fogEnd = fpLocal[1][0];
		float fog = fogEnd != fogStart ? (fogDistance - fogStart) / (fogEnd - fogStart) : 1.0f;
		fog = fog < 0.0f ? 0.0f : (fog > 1.0f ? 1.0f : fog);
		const float fpCameraDelta[3] = {
			fpLocal[11][0] - modelCamera[0], fpLocal[11][1] - modelCamera[1], fpLocal[11][2] - modelCamera[2] };
		const float vpCameraDelta[3] = {
			camera[0] - impliedCamera[0], camera[1] - impliedCamera[1], camera[2] - impliedCamera[2] };
		// fog is DS2's own value; renderedFog includes yae_eye_distance_fog.
		const bool saturated = fog > 0.95f;
		float renderedFog = fog;
		if (vpActive && vp->parsed.eyeDistanceTexCoord5 && fogEnd != fogStart) {
			renderedFog = (eyeDistance - fogStart) / (fogEnd - fogStart);
			renderedFog = renderedFog < 0.0f ? 0.0f : (renderedFog > 1.0f ? 1.0f : renderedFog);
		}

		D3DTextureObject *diffuse = D3DState.TextureState.currentTexture[0][D3D_TEXTARGET_2D];
		const GLuint diffuseId = diffuse ? diffuse->GetGLIndex() : 0;
		uint32_t signature = 2166136261u;
		MixHash(signature, &vpId, sizeof(vpId));
		MixHash(signature, &fpId, sizeof(fpId));
		MixHash(signature, &diffuseId, sizeof(diffuseId));
		MixHash(signature, &saturated, sizeof(saturated));
		if (!gYAEFogProbeStates.insert(signature).second) return;
		++gYAEFogProbeRecords;

		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u frame=%llu draw=%llu api=%s count=%d vertex=%d vp=%u(%s) fp=%u diffuse=%u %ux%u fog=%.3f%s renderedFog=%.3f fogDist=%.3f eyeDist=%.3f start=%.3f end=%.3f color=(%.3f,%.3f,%.3f) vpCamErr=%.3f fpCamErr=%.3f",
			gYAEFogProbeRecords, static_cast<unsigned long long>(gDiagnostics.frameId),
			static_cast<unsigned long long>(gDiagnostics.drawId), api, count, sampleVertex,
			vpId, vpActive ? (skinned ? (skinInputs ? "skinned" : "skinned-noinputs") : "rigid") :
				(D3DState.EnableState.vertexProgramEnabled ? "enabled-uncompiled" : "fixed-function"),
			fpId, diffuseId, diffuse ? diffuse->GetWidth() : 0, diffuse ? diffuse->GetHeight() : 0,
			fog, saturated ? " SATURATED" : "", renderedFog, fogDistance, eyeDistance, fogStart, fogEnd,
			fpLocal[2][0], fpLocal[2][1], fpLocal[2][2],
			vpActive ? Length3(vpCameraDelta) : -1.0f,
			fpParsed.usedLocalParams.count(11) ? Length3(fpCameraDelta) : -1.0f);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u pos=(%.3f,%.3f,%.3f) bones=(%.1f,%.1f) weights=(%.3f,%.3f) model=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) toEye=(%.3f,%.3f,%.3f)",
			gYAEFogProbeRecords, position[0], position[1], position[2],
			boneIds[0], boneIds[1], boneWeights[0], boneWeights[1],
			model[0], model[1], model[2], eye.x, eye.y, eye.z,
			world[0], world[1], world[2], toEye[0], toEye[1], toEye[2]);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u vpCam[local%d]=(%.3f,%.3f,%.3f) impliedWorldCam=(%.3f,%.3f,%.3f) fpCam[local11]=(%.3f,%.3f,%.3f) impliedModelCam=(%.3f,%.3f,%.3f) mvScale=(%.4f,%.4f,%.4f)",
			gYAEFogProbeRecords, cameraIndex, camera[0], camera[1], camera[2],
			impliedCamera[0], impliedCamera[1], impliedCamera[2],
			fpLocal[11][0], fpLocal[11][1], fpLocal[11][2],
			modelCamera[0], modelCamera[1], modelCamera[2],
			modelviewScale[0], modelviewScale[1], modelviewScale[2]);
		logPrintfLevel(QGL_LOG_DEBUG, "YAE_FOG_PROBE",
			"record=%u inst=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f] modelviewGL=[%.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f]",
			gYAEFogProbeRecords,
			vpLocal[5][0], vpLocal[5][1], vpLocal[5][2], vpLocal[5][3],
			vpLocal[6][0], vpLocal[6][1], vpLocal[6][2], vpLocal[6][3],
			vpLocal[7][0], vpLocal[7][1], vpLocal[7][2], vpLocal[7][3],
			modelview._11, modelview._21, modelview._31, modelview._41,
			modelview._12, modelview._22, modelview._32, modelview._42,
			modelview._13, modelview._23, modelview._33, modelview._43);

		const int vpStamps[] = { 5, 6, 7, cameraIndex };
		const int fpStamps[] = { 0, 1, 2, 11 };
		if (vpActive)
			LogLocalWriteStamps(gYAEFogProbeRecords, "vp", GL_VERTEX_PROGRAM_ARB, vpStamps, 4);
		LogLocalWriteStamps(gYAEFogProbeRecords, "fp", GL_FRAGMENT_PROGRAM_ARB, fpStamps, 4);
		if (saturated && gProgramHistoryDumps < kProgramHistoryMaxDumps)
			DumpProgramHistory(gYAEFogProbeRecords);
	}

	//----------------------------------------------------------------------
	// Frame capture
	//----------------------------------------------------------------------
	struct CaptureState
	{
		int configuredFrame;	// DebugCaptureFrame, -1 = none
		bool scrollLockDown;
		bool active;
		uint64_t frame;
		FILE *file;
		char directory[MAX_PATH];
		std::set<GLuint> copiedTextures;
		unsigned int copies;
		unsigned int shots;
		bool shotAfterDraw;
	};
	CaptureState gCapture = { -1, false, false, 0, nullptr, "", {}, 0, 0, false };
	const unsigned int kCaptureMaxShots = 48;
	std::set<GLuint> gCaptureDumpedTextures;	// textures written by the current capture

	HRESULT SaveRenderTargetPng( const char *path )
	{
		LPDIRECT3DSURFACE9 renderTarget = nullptr;
		LPDIRECT3DSURFACE9 systemCopy = nullptr;
		HRESULT result = D3DGlobal.pDevice->GetRenderTarget(0, &renderTarget);
		if (SUCCEEDED(result) && renderTarget) {
			D3DSURFACE_DESC desc = {};
			result = renderTarget->GetDesc(&desc);
			if (SUCCEEDED(result))
				result = D3DGlobal.pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height,
					desc.Format, D3DPOOL_SYSTEMMEM, &systemCopy, nullptr);
			if (SUCCEEDED(result))
				result = D3DGlobal.pDevice->GetRenderTargetData(renderTarget, systemCopy);
			if (SUCCEEDED(result))
				result = D3DXSaveSurfaceToFileA(path, D3DXIFF_PNG, systemCopy, nullptr, nullptr);
		}
		if (systemCopy) systemCopy->Release();
		if (renderTarget) renderTarget->Release();
		return result;
	}

	void CaptureShot( const char *name )
	{
		if (gCapture.shots >= kCaptureMaxShots) return;
		++gCapture.shots;
		char path[MAX_PATH];
		sprintf_s(path, "%s\\%s.png", gCapture.directory, name);
		const HRESULT result = SaveRenderTargetPng(path);
		fprintf(gCapture.file, "  -> %s.png (0x%08X)\n", name, static_cast<unsigned int>(result));
	}

	void StartCapture( uint64_t frame, const char *trigger )
	{
		_mkdir("QindieGL-capture");
		sprintf_s(gCapture.directory, "QindieGL-capture\\frame_%06llu", static_cast<unsigned long long>(frame));
		_mkdir(gCapture.directory);
		char path[MAX_PATH];
		sprintf_s(path, "%s\\draws.txt", gCapture.directory);
		if (fopen_s(&gCapture.file, path, "w") || !gCapture.file) {
			gCapture.file = nullptr;
			return;
		}
		gCapture.active = true;
		gCapture.frame = frame;
		gCapture.copiedTextures.clear();
		gCaptureDumpedTextures.clear();
		gCapture.copies = 0;
		gCapture.shots = 0;
		gCapture.shotAfterDraw = false;
		fprintf(gCapture.file, "QindieGL frame capture: frame %llu (%s)\n"
			"Columns: draw api mode count | projection/hash depth(test/write/func) blend(src,dst) alpha(func,ref) cull stencil colorMask "
			"lighting(mask) fog color arrays | per enabled texture unit: id target env texgen matrix\n\n",
			static_cast<unsigned long long>(frame), trigger);
		logPrintfLevel(QGL_LOG_INFO, "FRAME_CAPTURE", "capturing frame %llu (%s) into %s",
			static_cast<unsigned long long>(frame), trigger, gCapture.directory);
	}

	void FinishCapture()
	{
		CaptureShot("final");
		fprintf(gCapture.file, "\nEnd of frame %llu: %llu draws, %u framebuffer copies\n",
			static_cast<unsigned long long>(gCapture.frame), static_cast<unsigned long long>(gDiagnostics.drawId),
			gCapture.copies);
		fclose(gCapture.file);
		gCapture.file = nullptr;
		gCapture.active = false;
	}

	// Which build ARB_ActivateShaders uses for the bound fragment program.
	const char *FragmentProgramBuild()
	{
		ARBCompiledProgram *fp = ARB_GetCompiledProgram(ARB_GetBoundFragmentProgram());
		if (!fp || !fp->ps) return "none";
		ARBCompiledProgram *vp = D3DState.EnableState.vertexProgramEnabled ?
			ARB_GetCompiledProgram(ARB_GetBoundVertexProgram()) : nullptr;
		return (!vp || !vp->vs) && fp->psFixedFunction ? "ps_2_x" : "ps_3_0";
	}

	void CaptureDraw( const char *api, unsigned int mode, int count )
	{
		const auto &enable = D3DState.EnableState;
		const auto &color = D3DState.ColorBufferState;
		unsigned int lightMask = 0;
		for (int i = 0; i < IMPL_MAX_LIGHTS; ++i)
			if (enable.lightEnabled[i]) lightMask |= 1u << i;
		const bool ortho = D3DGlobal.projectionMatrixStack && D3DGlobal_IsOrthoProjection();
		const uint32_t projectionHash = D3DGlobal.projectionMatrixStack ?
			HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX)) : 0;
		fprintf(gCapture.file,
			"D%04llu %s mode=0x%X count=%d | %s/%08X depth=%u/%u/%u blend=%u(0x%X,0x%X) alpha=%u(%u,%u) cull=%u(%u) stencil=%u colorMask=0x%X "
			"lighting=%u(0x%X) fog=%u color=0x%08X arrays=0x%08X offset=%u(%g,%g) vp=%u(%u) fp=%u(%u,%s) |",
			static_cast<unsigned long long>(gDiagnostics.drawId), api, mode, count,
			ortho ? "ORTHO" : "PERSP", projectionHash, enable.depthTestEnabled,
			D3DState.DepthBufferState.depthWriteMask, D3DState.DepthBufferState.depthTestFunc,
			enable.alphaBlendEnabled, color.glBlendSrc, color.glBlendDst, enable.alphaTestEnabled,
			color.alphaTestFunc, color.alphaTestReference, enable.cullEnabled, D3DState.PolygonState.cullMode,
			enable.stencilTestEnabled, color.colorWriteMask, enable.lightingEnabled, lightMask, enable.fogEnabled,
			D3DState.CurrentState.currentColor, D3DState.ClientVertexArrayState.vertexArrayEnable,
			enable.depthBiasEnabled, D3DState.PolygonState.depthBiasFactor, D3DState.PolygonState.depthBiasUnits,
			enable.vertexProgramEnabled, ARB_GetBoundVertexProgram(), enable.fragmentProgramEnabled,
			ARB_GetBoundFragmentProgram(), FragmentProgramBuild());
		bool samplesCopy = false;
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			if (!enable.textureEnabled[unit]) continue;
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				if (!enable.textureTargetEnabled[unit][target]) continue;
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				const GLuint id = texture ? texture->GetGLIndex() : 0;
				const bool copied = gCapture.copiedTextures.count(id) != 0;
				samplesCopy = samplesCopy || copied;
				fprintf(gCapture.file, " t%d:%u%s tgt=%d env=0x%X texgen=%u matrix=%s", unit, id,
					copied ? "*" : "", target, D3DState.TextureState.TextureCombineState[unit].envMode,
					enable.texGenEnabled[unit],
					D3DGlobal.textureMatrixStack[unit]->top().is_identity() ? "I" : "M");
			}
		}
		fprintf(gCapture.file, "\n");
		gCapture.shotAfterDraw = samplesCopy;
	}

	// For draws with texgen or a texture matrix: modes, planes, the GL matrix,
	// the coordinates of one vertex as the vertex-array path computes them
	// (generated coordinates, the rest from (0,0,0,1)), each bound texture once,
	// and the framebuffer before and after the draw.

	void CaptureTexgenDetails( int first, unsigned int indexType, const void *indices )
	{
		const auto &arrays = D3DState.ClientVertexArrayState;
		bool involved = false;
		for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
			if (!D3DState.EnableState.textureEnabled[unit]) continue;
			D3DStateMatrix &matrixState = D3DGlobal.textureMatrixStack[unit]->top();
			const DWORD texgen = D3DState.EnableState.texGenEnabled[unit];
			if (!texgen && matrixState.is_identity()) continue;
			involved = true;
			const D3DXMATRIX &m = *static_cast<const D3DXMATRIX *>(matrixState);
			const auto *gen = D3DState.TextureState.TexGen[unit];
			fprintf(gCapture.file, "    t%d texgen=0x%X modes(0x%X,0x%X,0x%X,0x%X) projective=%u\n", unit, texgen,
				gen[0].mode, gen[1].mode, gen[2].mode, gen[3].mode, D3DState_IsProjectiveTextureStage(unit) ? 1u : 0u);
			static const char coordNames[4] = { 'S', 'T', 'R', 'Q' };
			for (int coord = 0; coord < 4; ++coord) {
				if (!(texgen & (1u << coord))) continue;
				fprintf(gCapture.file, "      %c object=(%g,%g,%g,%g) eye=(%g,%g,%g,%g)\n", coordNames[coord],
					gen[coord].objectPlane[0], gen[coord].objectPlane[1], gen[coord].objectPlane[2], gen[coord].objectPlane[3],
					gen[coord].eyePlane[0], gen[coord].eyePlane[1], gen[coord].eyePlane[2], gen[coord].eyePlane[3]);
			}
			fprintf(gCapture.file, "      matrix GL rows [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]\n",
				m._11, m._21, m._31, m._41, m._12, m._22, m._32, m._42,
				m._13, m._23, m._33, m._43, m._14, m._24, m._34, m._44);

			const int vertex = ResolveSampleVertex(first, indexType, indices);
			float position[4];
			if ((arrays.vertexArrayEnable & VA_ENABLE_VERTEX_BIT) &&
				ReadArrayElement(arrays.vertexInfo, vertex, position)) {
				float coords[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				if (VA_TEXTURE_BIT_IS_SET(arrays.vertexArrayEnable, unit))
					ReadArrayElement(arrays.texCoordInfo[unit], vertex, coords);
				for (int coord = 0; coord < 4; ++coord) {
					if (!(texgen & (1u << coord)) || !gen[coord].func) continue;
					float transformed[4] = { position[0], position[1], position[2], position[3] };
					float normal[3] = { D3DState.CurrentState.currentNormal[0],
						D3DState.CurrentState.currentNormal[1], D3DState.CurrentState.currentNormal[2] };
					float transformedNormal[3] = { normal[0], normal[1], normal[2] };
					if (gen[coord].trVertex) gen[coord].trVertex(position, transformed);
					if (gen[coord].trNormal) gen[coord].trNormal(normal, transformedNormal);
					gen[coord].func(unit, coord, transformed, transformedNormal, &coords[coord]);
				}
				D3DXVECTOR4 result;
				D3DXVec4Transform(&result, reinterpret_cast<const D3DXVECTOR4 *>(coords), &m);
				fprintf(gCapture.file, "      vertex %d pos=(%g,%g,%g) generated=(%g,%g,%g,%g) matrix=(%g,%g,%g,%g) s/q=%g t/q=%g\n",
					vertex, position[0], position[1], position[2], coords[0], coords[1], coords[2], coords[3],
					result.x, result.y, result.z, result.w,
					result.w != 0.0f ? result.x / result.w : 0.0f, result.w != 0.0f ? result.y / result.w : 0.0f);
			}

			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				if (!D3DState.EnableState.textureTargetEnabled[unit][target]) continue;
				D3DTextureObject *texture = D3DState.TextureState.currentTexture[unit][target];
				if (!texture || !texture->GetD3DTexture() || gCapture.shots >= kCaptureMaxShots ||
					!gCaptureDumpedTextures.insert(texture->GetGLIndex()).second)
					continue;
				++gCapture.shots;
				char path[MAX_PATH];
				sprintf_s(path, "%s\\texture_%u.png", gCapture.directory, texture->GetGLIndex());
				const HRESULT result = D3DXSaveTextureToFileA(path, D3DXIFF_PNG, texture->GetD3DTexture(), nullptr);
				fprintf(gCapture.file, "      -> texture_%u.png %ux%u (0x%08X)\n", texture->GetGLIndex(),
					texture->GetWidth(), texture->GetHeight(), static_cast<unsigned int>(result));
			}
		}
		if (involved) {
			// GL combiner state and what D3D9 actually received for this draw.
			for (int unit = 0; unit < D3DGlobal.maxActiveTMU; ++unit) {
				if (!D3DState.EnableState.textureEnabled[unit]) continue;
				const auto &c = D3DState.TextureState.TextureCombineState[unit];
				fprintf(gCapture.file,
					"    t%d GL env=0x%X rgb op=0x%X args=(0x%X,0x%X,0x%X) operands=(0x%X,0x%X,0x%X) scale=%u"
					" alpha op=0x%X args=(0x%X,0x%X,0x%X) operands=(0x%X,0x%X,0x%X) scale=%u\n",
					unit, c.envMode, c.colorOp, c.colorArg1, c.colorArg2, c.colorArg3,
					c.colorOperand1, c.colorOperand2, c.colorOperand3, c.colorScale,
					c.alphaOp, c.alphaArg1, c.alphaArg2, c.alphaArg3,
					c.alphaOperand1, c.alphaOperand2, c.alphaOperand3, c.alphaScale);
			}
			for (DWORD stage = 0; stage < 4; ++stage) {
				DWORD v[9] = {};
				const D3DTEXTURESTAGESTATETYPE types[9] = { D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
					D3DTSS_COLORARG0, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX,
					D3DTSS_TEXTURETRANSFORMFLAGS };
				for (int i = 0; i < 9; ++i)
					D3DGlobal.pDevice->GetTextureStageState(stage, types[i], &v[i]);
				fprintf(gCapture.file,
					"    D3D stage %lu colorop=%lu args=(0x%lX,0x%lX,0x%lX) alphaop=%lu args=(0x%lX,0x%lX) texcoordindex=%lu transform=0x%lX\n",
					stage, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
				if (v[0] == D3DTOP_DISABLE) break;
			}
			DWORD rs[13] = {};
			const D3DRENDERSTATETYPE renderStates[13] = { D3DRS_ALPHATESTENABLE, D3DRS_ALPHAFUNC, D3DRS_ALPHAREF,
				D3DRS_ZENABLE, D3DRS_ZFUNC, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND,
				D3DRS_DESTBLEND, D3DRS_COLORWRITEENABLE, D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_FOGENABLE };
			for (int i = 0; i < 13; ++i)
				D3DGlobal.pDevice->GetRenderState(renderStates[i], &rs[i]);
			fprintf(gCapture.file,
				"    D3D alphatest=%lu func=%lu ref=%lu z=%lu func=%lu write=%lu blend=%lu src=%lu dst=%lu colorwrite=0x%lX depthbias=%g slopebias=%g fog=%lu\n",
				rs[0], rs[1], rs[2], rs[3], rs[4], rs[5], rs[6], rs[7], rs[8], rs[9],
				*reinterpret_cast<const float *>(&rs[10]), *reinterpret_cast<const float *>(&rs[11]), rs[12]);

			char name[48];
			sprintf_s(name, "before_draw_%04llu", static_cast<unsigned long long>(gDiagnostics.drawId));
			CaptureShot(name);
			gCapture.shotAfterDraw = true;
		}
	}

	void SnapshotState()
	{
		if (!D3DGlobal.initialized)
			return;

		// Runs before every draw, so only raw values are captured here; a STATE
		// event is recorded when a binding changes and formatted when dumped.
		gSnapshot.arrayBuffer = D3DBuffer_GetBinding(GL_ARRAY_BUFFER_ARB);
		gSnapshot.elementBuffer = D3DBuffer_GetBinding(GL_ELEMENT_ARRAY_BUFFER_ARB);
		gSnapshot.vertexProgram = ARB_GetBoundVertexProgram();
		gSnapshot.fragmentProgram = ARB_GetBoundFragmentProgram();
		for (int unit = 0; unit < MAX_D3D_TMU; ++unit) {
			for (int target = 0; target < D3D_TEXTARGET_MAX; ++target) {
				D3DTextureObject *texture = unit < D3DGlobal.maxActiveTMU ?
					D3DState.TextureState.currentTexture[unit][target] : nullptr;
				gSnapshot.textures[unit][target] = texture ? texture->GetGLIndex() : 0;
			}
		}
		if (!gHaveRecordedSnapshot || memcmp(&gSnapshot, &gRecordedSnapshot, sizeof(gSnapshot))) {
			gRecordedSnapshot = gSnapshot;
			gHaveRecordedSnapshot = true;
			RecordStateEvent(gSnapshot);
		}
	}

	// Formats the current state for a crash report.
	void FormatSnapshotStrings()
	{
		sprintf_s(gActiveBuffers, "array=%u element=%u", gSnapshot.arrayBuffer, gSnapshot.elementBuffer);
		sprintf_s(gActivePrograms, "vp=%u fp=%u", gSnapshot.vertexProgram, gSnapshot.fragmentProgram);
		FormatTextures(gSnapshot, gActiveTextures, sizeof(gActiveTextures));
		if (D3DGlobal.projectionMatrixStack && D3DGlobal.modelviewMatrixStack) {
			sprintf_s(gProjectionState, "%s projHash=%08X modelviewHash=%08X",
				D3DGlobal_IsOrthoProjection() ? "ORTHOGRAPHIC" : "PERSPECTIVE",
				HashBytes(D3DGlobal.projectionMatrixStack->top(), sizeof(D3DXMATRIX)),
				HashBytes(D3DGlobal.modelviewMatrixStack->top(), sizeof(D3DXMATRIX)));
		}
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
			char text[kEventTextSize + 256];
			FormatEventText(event, text, sizeof(text));
			WriteCrashFormat(file, "[F:%08llu D:%06llu][%s] %s\r\n",
				static_cast<unsigned long long>(event.frameId),
				static_cast<unsigned long long>(event.drawId), event.category, text);
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
		FormatSnapshotStrings();
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
	gYAEWorldDrawStates.clear();
	gYAEDumpedTextures.clear();
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
	QGLSectionTimer diagnosticsTimer(QGL_PERF_DIAGNOSTICS);
	RecordDrawEvent(api, mode, count, first, indexType, indices);
	logPrintfLevel(QGL_LOG_TRACE, "GL_DRAW", "%s mode=%s(0x%X) count=%d first=%d type=0x%X indices=%p",
		api ? api : "<unknown>", GLModeName(mode), mode, count, first, indexType, indices);

	SnapshotState();
	QGL_ViewDiagnosticsOnDraw(gDiagnostics.frameId, gDiagnostics.drawId);
	CensusLitDraw(api ? api : "<unknown>", count);
	if (gCapture.active) {
		CaptureDraw(api ? api : "<unknown>", mode, count);
		CaptureTexgenDetails(first, indexType, indices);
	}
	CensusYAEWorldDraw(api ? api : "<unknown>", mode, count, first, indexType, indices);
	TraceYAEPostEffectDraw(api ? api : "<unknown>", mode, count, first, indexType, indices);
	if (ProgramHistoryActive()) {
		const float drawValues[4] = { static_cast<float>(count),
			static_cast<float>(D3DState.EnableState.vertexProgramEnabled),
			static_cast<float>(D3DState.EnableState.fragmentProgramEnabled), 0.0f };
		QGL_DiagnosticsRecordProgramOp('D', 0, ARB_GetBoundVertexProgram(),
			static_cast<int>(ARB_GetBoundFragmentProgram()), drawValues);
	}
	ProbeYAEProgramFog(api ? api : "<unknown>", count, first, indexType, indices);
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
	if (D3DState.EnableState.scissorEnabled && D3DState.ScissorState.empty) {
		++gDiagnostics.drawsSkipped;
		logPrintfLevel(QGL_LOG_TRACE, "GL_DRAW", "draw fully rejected by empty scissor box");
		return false;
	}
	return true;
}

void QGL_DiagnosticsBeginPresent()
{
	if (gCapture.active)
		FinishCapture();
	gPerformance.presentStart = PerformanceNow();
}

void QGL_DiagnosticsConfigureCapture( int frame )
{
	gCapture.configuredFrame = frame;
}

void QGL_DiagnosticsCaptureClear( unsigned int mask )
{
	if (!gCapture.active)
		return;
	const DWORD color = D3DState.ColorBufferState.clearColor;
	const RECT &scissor = D3DState.ScissorState.scissorRect;
	fprintf(gCapture.file,
		"CLEAR after D%04llu: mask=0x%X color ARGB=0x%08X depth=%g stencil=%lu colorMask=0x%lX scissor=%lu (%ld,%ld)-(%ld,%ld)\n",
		static_cast<unsigned long long>(gDiagnostics.drawId), mask, color, D3DState.DepthBufferState.clearDepth,
		D3DState.StencilBufferState.clearStencil, D3DState.ColorBufferState.colorWriteMask,
		D3DState.EnableState.scissorEnabled, scissor.left, scissor.top, scissor.right, scissor.bottom);
}

void QGL_DiagnosticsCaptureCopy( bool afterCopy, unsigned int target, int level, int xoffset, int yoffset,
	int x, int y, int width, int height )
{
	if (!gCapture.active)
		return;
	const int targetIndex = UTIL_GLTextureTargettoInternalIndex(target);
	const int unit = static_cast<int>(D3DState.TextureState.currentTMU);
	D3DTextureObject *texture = targetIndex >= 0 && targetIndex < D3D_TEXTARGET_MAX ?
		D3DState.TextureState.currentTexture[unit][targetIndex] : nullptr;
	const GLuint id = texture ? texture->GetGLIndex() : 0;
	char name[64];
	if (!afterCopy) {
		++gCapture.copies;
		fprintf(gCapture.file, "COPY #%u after D%04llu: target=0x%X level=%d dst=(%d,%d) src=(%d,%d) size=%dx%d into t%d:%u (%ux%u)\n",
			gCapture.copies, static_cast<unsigned long long>(gDiagnostics.drawId), target, level, xoffset, yoffset,
			x, y, width, height, unit, id, texture ? texture->GetWidth() : 0, texture ? texture->GetHeight() : 0);
		sprintf_s(name, "copy_%02u_framebuffer", gCapture.copies);
		CaptureShot(name);
		return;
	}
	if (!texture || !texture->GetD3DTexture())
		return;
	gCapture.copiedTextures.insert(id);
	if (gCapture.shots >= kCaptureMaxShots)
		return;
	++gCapture.shots;
	char path[MAX_PATH];
	sprintf_s(path, "%s\\copy_%02u_texture_%u.png", gCapture.directory, gCapture.copies, id);
	const HRESULT result = D3DXSaveTextureToFileA(path, D3DXIFF_PNG, texture->GetD3DTexture(), nullptr);
	fprintf(gCapture.file, "  -> copy_%02u_texture_%u.png (0x%08X)\n", gCapture.copies, id,
		static_cast<unsigned int>(result));
}

void QGL_DiagnosticsRecordVertexUpload( uint32_t vertices, uint32_t vertexBytes, uint32_t indexBytes )
{
	gPerformance.frameVertices += vertices;
	gPerformance.frameVertexBytes += vertexBytes;
	gPerformance.frameIndexBytes += indexBytes;
}

QGLDrawTimer::QGLDrawTimer() : m_start( 0 )
{
	if (gPerformance.drawTimerDepth++ == 0) {
		m_start = PerformanceNow();
		memcpy(m_sectionStart, gPerformance.frameSectionTicks, sizeof(m_sectionStart));
	}
}

QGLDrawTimer::~QGLDrawTimer()
{
	if (--gPerformance.drawTimerDepth != 0 || !m_start)
		return;
	const int64_t elapsed = PerformanceNow() - m_start;
	gPerformance.frameDrawTicks += elapsed;
	static unsigned int slowDrawsLogged = 0;
	if (TicksToMs(elapsed) > 20.0 && slowDrawsLogged < 32 && logIsEnabled(QGL_LOG_INFO)) {
		++slowDrawsLogged;
		char textures[384];
		FormatTextures(gSnapshot, textures, sizeof(textures));
		double section[QGL_PERF_SECTIONS];
		for (int i = 0; i < QGL_PERF_SECTIONS; ++i)
			section[i] = TicksToMs(gPerformance.frameSectionTicks[i] - m_sectionStart[i]);
		logPrintfLevel(QGL_LOG_INFO, "PERF",
			"slow draw %.1f ms (state %.1f, vertices %.1f, DrawIndexedPrimitive %.1f, diagnostics %.1f, texture upload %.1f): %s mode=0x%X count=%d textures(%s)",
			TicksToMs(elapsed), section[QGL_PERF_STATE], section[QGL_PERF_VERTICES], section[QGL_PERF_SUBMIT],
			section[QGL_PERF_DIAGNOSTICS], section[QGL_PERF_TEXTURE_UPLOAD],
			gLastDraw.api ? gLastDraw.api : "<unknown>", gLastDraw.mode, gLastDraw.count, textures);
	}
}

QGLSectionTimer::QGLSectionTimer( QGLPerfSection section ) : m_section( section ), m_start( PerformanceNow() )
{
}

QGLSectionTimer::~QGLSectionTimer()
{
	gPerformance.frameSectionTicks[m_section] += PerformanceNow() - m_start;
}

void QGL_DiagnosticsRecordVertexPath( bool fastPath, int abortLine )
{
	if (fastPath) {
		++gPerformance.fastPathLocks;
	} else {
		++gPerformance.slowPathLocks;
		++gSlowPathReasons[abortLine];
	}
}

void QGL_DiagnosticsRecordProgramOp( char op, unsigned int target, unsigned int program,
	int index, const float *values )
{
	if (!ProgramHistoryActive())
		return;
	ProgramHistoryEntry& entry = gProgramHistory[gProgramHistoryNext % kProgramHistoryCapacity];
	++gProgramHistoryNext;
	entry.frame = static_cast<uint32_t>(gDiagnostics.frameId);
	entry.draw = static_cast<uint32_t>(gDiagnostics.drawId);
	entry.op = op;
	entry.target = target;
	entry.program = program;
	entry.index = index;
	if (values)
		memcpy(entry.values, values, sizeof(entry.values));
	else
		memset(entry.values, 0, sizeof(entry.values));
}

void QGL_DiagnosticsAfterDraw()
{
	if (gCapture.active && gCapture.shotAfterDraw) {
		gCapture.shotAfterDraw = false;
		char name[48];
		sprintf_s(name, "after_draw_%04llu", static_cast<unsigned long long>(gDiagnostics.drawId));
		CaptureShot(name);
	}

	if (gYAEPostEffectAfterDumped || !D3DGlobal.settings.game.yaeFallbackCompatibility ||
		!D3DState.EnableState.fragmentProgramEnabled || ARB_GetBoundFragmentProgram() != 8)
		return;

	gYAEPostEffectAfterDumped = true;
	_mkdir("QindieGL-dump");
	_mkdir("QindieGL-dump\\textures");
	LPDIRECT3DSURFACE9 renderTarget = nullptr;
	LPDIRECT3DSURFACE9 systemCopy = nullptr;
	HRESULT result = D3DGlobal.pDevice->GetRenderTarget(0, &renderTarget);
	if (SUCCEEDED(result) && renderTarget) {
		D3DSURFACE_DESC desc = {};
		result = renderTarget->GetDesc(&desc);
		if (SUCCEEDED(result))
			result = D3DGlobal.pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height,
				desc.Format, D3DPOOL_SYSTEMMEM, &systemCopy, nullptr);
		if (SUCCEEDED(result))
			result = D3DGlobal.pDevice->GetRenderTargetData(renderTarget, systemCopy);
		if (SUCCEEDED(result))
			result = D3DXSaveSurfaceToFileA(
				"QindieGL-dump\\textures\\yae_post_result.png", D3DXIFF_PNG,
				systemCopy, nullptr, nullptr);
	}
	if (systemCopy) systemCopy->Release();
	if (renderTarget) renderTarget->Release();
	logPrintfLevel(QGL_LOG_INFO, "YAE_POST_EFFECT",
		"result dump result=0x%08X file=QindieGL-dump\\textures\\yae_post_result.png", result);
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
		const bool worldFrame = QGL_ViewDiagnosticsOnFrameEnd(gDiagnostics.frameId);
		RecordFramePerformance(worldFrame, gDiagnostics.drawId);
		++gDiagnostics.framesPresented;
		++gDiagnostics.frameId;
		gDiagnostics.drawId = 0;

		// Scroll Lock (edge-triggered) or DebugCaptureFrame captures the next frame.
		const bool scrollLockDown = (GetAsyncKeyState(VK_SCROLL) & 0x8000) != 0;
		const bool scrollLockPressed = scrollLockDown && !gCapture.scrollLockDown;
		gCapture.scrollLockDown = scrollLockDown;
		const bool configured = gCapture.configuredFrame >= 0 &&
			gDiagnostics.frameId == static_cast<uint64_t>(gCapture.configuredFrame);
		if (!gCapture.active && (scrollLockPressed || configured))
			StartCapture(gDiagnostics.frameId, scrollLockPressed ? "Scroll Lock" : "DebugCaptureFrame");
	}
}

void QGL_DiagnosticsRecordEvent( bool d3dEvent, const char *category, const char *fmt, ... )
{
	if (!gDiagnostics.initialized || !fmt)
		return;
	LONG sequence;
	DiagnosticEvent &event = BeginEvent(d3dEvent, category, EVENT_TEXT, sequence);
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(event.text, sizeof(event.text), _TRUNCATE, fmt, args);
	va_end(args);
	CommitEvent(event, sequence);
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
	logPrintf("  YAEFallbackCompatibility: %u\n", D3DGlobal.settings.game.yaeFallbackCompatibility);
	logPrintf("  YAECompileARBPrograms: %u\n", D3DGlobal.settings.game.yaeCompileARBPrograms);
	logPrintf("  YAEEyeDistanceFog: %u\n", D3DGlobal.settings.game.yaeEyeDistanceFog);
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
	logPrintf("Fixed-function lit draws: %llu, with a spot light enabled: %llu\n",
		static_cast<unsigned long long>(gLitDraws), static_cast<unsigned long long>(gLitDrawsWithSpot));
	QGL_ViewDiagnosticsDumpSummary();
	DumpPerformanceSummary();
	logPrintf("====================================\n");
}
