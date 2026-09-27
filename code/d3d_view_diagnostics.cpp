/***************************************************************************
* QindieGL view diagnostics: projection classes and world-to-HUD boundaries
* (task brief sections 42 and 43). Observation only; rendering is unchanged.
*
* Every draw hashes the current projection. A changed projection is
* classified (perspective / orthographic, FOV, aspect, clip planes) and new
* classes are logged at DEBUG. At the end of each frame the sequence of
* classes and the boundary after the last world draw (perspective and depth
* tested) are compared with the previous frame; changes are logged at DEBUG
* and aggregated for the session summary. Clip planes are the D3D-effective
* distances (NDC z 0..1) of the matrix actually sent to the device.
*
* Camera census (Phase G, RTX Remix): DS2 sets each camera with glLoadIdentity
* and one transform at modelview stack depth 0. For every draw the census
* records how the modelview was built (d3d_matrix.cpp), the kind of the
* depth-0 matrix and its relation to the frame's main perspective camera, per
* label and as per-frame patterns: the evidence behind yae_camera_split.
***************************************************************************/
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_matrix_stack.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace {
	enum ProjectionType { PROJECTION_PERSPECTIVE, PROJECTION_ORTHO, PROJECTION_OTHER };

	struct ProjectionInfo
	{
		ProjectionType type;
		float fovY;			// degrees, perspective only
		float aspect;		// perspective only
		float left, right, bottom, top;	// orthographic only
		float nearPlane, farPlane;		// D3D-effective, eye-space distances
		bool infiniteFar;
	};

	struct ProjectionClass
	{
		ProjectionInfo info;
		uint32_t firstHash;
		uint64_t firstFrame;
		uint64_t lastCountedFrame;
		uint64_t frames;
		uint64_t draws;
	};

	// Per-draw flags recorded during a frame.
	enum { DRAW_ORTHO = 1, DRAW_PERSPECTIVE = 2, DRAW_DEPTH_TEST = 4, DRAW_DEPTH_WRITE = 8 };

	struct FrameSummary
	{
		std::string sequence;	// class ids in order of use
		bool world;
		bool hud;
		unsigned int boundary;	// BOUNDARY_* bits at the first draw after the last world draw
		bool orthoInsideWorld;
	};
	enum { BOUNDARY_TO_ORTHO = 1, BOUNDARY_DEPTH_TEST_OFF = 2, BOUNDARY_DEPTH_WRITE_OFF = 4 };

	const size_t kMaxClasses = 256;
	std::map<std::string, int> gClassByKey;
	std::vector<ProjectionClass> gClasses;
	uint32_t gLastHash = 0;
	int gLastClass = -1;
	std::vector<unsigned char> gDrawFlags;
	std::vector<std::pair<int, uint64_t>> gSegments;	// class, first draw
	FrameSummary gPreviousFrame = { std::string("<none>"), false, false, 0, false };

	uint64_t gFrames = 0;
	uint64_t gWorldFrames = 0;
	uint64_t gHudFrames = 0;
	uint64_t gBoundaryToOrtho = 0;
	uint64_t gBoundaryDepthTestOff = 0;
	uint64_t gBoundaryDepthWriteOff = 0;
	uint64_t gOrthoInsideWorldFrames = 0;

	uint32_t HashMatrix( const D3DXMATRIX &m )
	{
		const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&m);
		uint32_t hash = 2166136261u;
		for (size_t i = 0; i < sizeof(D3DXMATRIX); ++i) {
			hash ^= bytes[i];
			hash *= 16777619u;
		}
		return hash;
	}

	// The stack holds the transpose of the GL matrix: _34 is GL row 3, column 2.
	ProjectionInfo Classify( const D3DXMATRIX &m )
	{
		ProjectionInfo info = {};
		if (fabsf(m._44) < 1e-5f && fabsf(m._34) > 1e-5f) {
			info.type = PROJECTION_PERSPECTIVE;
			info.fovY = m._22 != 0.0f ? 2.0f * atanf(1.0f / fabsf(m._22)) * 57.2957795f : 0.0f;
			info.aspect = m._11 != 0.0f ? fabsf(m._22 / m._11) : 0.0f;
			// NDC z = (A*ze + B) / -ze with A = _33, B = _43 (right-handed).
			const float a = m._33, b = m._43;
			info.nearPlane = a != 0.0f ? b / a : 0.0f;
			info.infiniteFar = fabsf(a + 1.0f) < 1e-6f;
			info.farPlane = info.infiniteFar ? 0.0f : b / (a + 1.0f);
		} else if (fabsf(m._34) < 1e-6f && fabsf(m._44 - 1.0f) < 1e-5f) {
			info.type = PROJECTION_ORTHO;
			if (m._11 != 0.0f) {
				info.left = (-1.0f - m._41) / m._11;
				info.right = (1.0f - m._41) / m._11;
			}
			if (m._22 != 0.0f) {
				info.bottom = (-1.0f - m._42) / m._22;
				info.top = (1.0f - m._42) / m._22;
			}
			if (m._33 != 0.0f) {
				info.nearPlane = m._43 / m._33;
				info.farPlane = -(1.0f - m._43) / m._33;
			}
		} else {
			info.type = PROJECTION_OTHER;
		}
		return info;
	}

	long Quantize( float value, float scale )
	{
		return static_cast<long>(lroundf(value * scale));
	}

	long QuantizeLog( float value )
	{
		return value > 0.0f ? static_cast<long>(lroundf(log2f(value) * 16.0f)) : -100000;
	}

	std::string ClassKey( const ProjectionInfo &info, uint32_t hash )
	{
		char key[160];
		switch (info.type) {
		case PROJECTION_PERSPECTIVE:
			sprintf_s(key, "P %ld %ld %ld %ld", Quantize(info.fovY, 10.0f), Quantize(info.aspect, 100.0f),
				QuantizeLog(info.nearPlane), info.infiniteFar ? 100000 : QuantizeLog(info.farPlane));
			break;
		case PROJECTION_ORTHO:
			sprintf_s(key, "O %ld %ld %ld %ld %ld %ld", Quantize(info.left, 1.0f), Quantize(info.right, 1.0f),
				Quantize(info.bottom, 1.0f), Quantize(info.top, 1.0f),
				Quantize(info.nearPlane, 10.0f), Quantize(info.farPlane, 10.0f));
			break;
		default:
			sprintf_s(key, "X %08X", hash);
			break;
		}
		return key;
	}

	std::string DescribeClass( int id )
	{
		const ProjectionClass &c = gClasses[id];
		char text[224];
		switch (c.info.type) {
		case PROJECTION_PERSPECTIVE:
			if (c.info.infiniteFar)
				sprintf_s(text, "#%d PERSPECTIVE fovY=%.1f aspect=%.3f near=%.3f far=inf hash=%08X",
					id + 1, c.info.fovY, c.info.aspect, c.info.nearPlane, c.firstHash);
			else
				sprintf_s(text, "#%d PERSPECTIVE fovY=%.1f aspect=%.3f near=%.3f far=%.1f hash=%08X",
					id + 1, c.info.fovY, c.info.aspect, c.info.nearPlane, c.info.farPlane, c.firstHash);
			break;
		case PROJECTION_ORTHO:
			sprintf_s(text, "#%d ORTHO left=%.1f right=%.1f bottom=%.1f top=%.1f near=%.2f far=%.2f hash=%08X",
				id + 1, c.info.left, c.info.right, c.info.bottom, c.info.top,
				c.info.nearPlane, c.info.farPlane, c.firstHash);
			break;
		default:
			sprintf_s(text, "#%d OTHER hash=%08X", id + 1, c.firstHash);
			break;
		}
		return text;
	}

	std::string BoundaryText( unsigned int boundary )
	{
		std::string text;
		if (boundary & BOUNDARY_TO_ORTHO) text += " perspective->ortho";
		if (boundary & BOUNDARY_DEPTH_TEST_OFF) text += " depthTest on->off";
		if (boundary & BOUNDARY_DEPTH_WRITE_OFF) text += " depthWrite on->off";
		return text.empty() ? std::string(" no-state-change") : text;
	}

	//----------------------------------------------------------------------
	// Camera census
	//----------------------------------------------------------------------
	// Segments are runs of draws sharing the depth-0 matrix and projection
	// label. Relations compare a segment's depth-0 matrix with the main camera:
	// the perspective segment of the frame with the most draws.
	enum { RELATION_MAIN, RELATION_SAME_ROTATION, RELATION_OTHER, RELATION_NO_MAIN, RELATION_COUNT };
	const char kRelationMarker[RELATION_COUNT] = { '=', '~', '!', '?' };

	struct CameraSegment
	{
		unsigned int generation;
		int transforms;
		unsigned int projectionSerial;
		std::string label;
		D3DXMATRIX view;
		bool perspective;
		unsigned int draws, depth0Draws, objectDraws, eyeDraws;
		int relation;
	};

	struct CameraClass
	{
		uint64_t firstFrame, lastCountedFrame, frames, draws, depth0Draws, objectDraws, eyeDraws;
		uint64_t relation[RELATION_COUNT];
	};

	struct CameraPattern
	{
		uint64_t firstFrame, frames;
	};

	const size_t kMaxCameraClasses = 96, kMaxCameraPatterns = 256;
	std::string gProjectionLabel("?");
	unsigned int gProjectionSerial = 0;
	std::vector<CameraSegment> gCameraSegments;
	std::map<std::string, CameraClass> gCameraClasses;
	std::map<std::string, CameraPattern> gCameraPatterns;
	uint64_t gCameraClassesDropped = 0, gCameraPatternsDropped = 0;
	std::string gLastCameraDescription("<none>");

	std::string ProjectionLabel( const ProjectionInfo &info, const D3DXMATRIX &m )
	{
		if (D3DXMatrixIsIdentity(&m))
			return "I";
		char label[48];
		switch (info.type) {
		case PROJECTION_PERSPECTIVE:
			if (info.infiniteFar)
				sprintf_s(label, "P%.1f-inf", info.nearPlane);
			else
				sprintf_s(label, "P%.1f-%.0f", info.nearPlane,
					info.farPlane >= 1000.0f ? 100.0f * roundf(info.farPlane / 100.0f) : info.farPlane);
			return label;
		case PROJECTION_ORTHO:
			return "O";
		default:
			return "X";
		}
	}

	// The stack holds the transpose of the GL matrix: rows _1x.._3x are the
	// images of the axes and _41.._43 the translation.
	const char *ViewKind( const D3DXMATRIX &m )
	{
		if (fabsf(m._14) > 1e-5f || fabsf(m._24) > 1e-5f || fabsf(m._34) > 1e-5f || fabsf(m._44 - 1.0f) > 1e-5f)
			return "proj";
		const D3DXVECTOR3 x(m._11, m._12, m._13), y(m._21, m._22, m._23), z(m._31, m._32, m._33);
		const bool orthonormal =
			fabsf(D3DXVec3Length(&x) - 1.0f) < 1e-3f && fabsf(D3DXVec3Length(&y) - 1.0f) < 1e-3f &&
			fabsf(D3DXVec3Length(&z) - 1.0f) < 1e-3f && fabsf(D3DXVec3Dot(&x, &y)) < 1e-3f &&
			fabsf(D3DXVec3Dot(&x, &z)) < 1e-3f && fabsf(D3DXVec3Dot(&y, &z)) < 1e-3f;
		if (!orthonormal)
			return "affine";
		D3DXVECTOR3 xy;
		D3DXVec3Cross(&xy, &x, &y);
		const bool mirrored = D3DXVec3Dot(&xy, &z) < 0.0f;
		const bool translated = fabsf(m._41) > 1e-3f || fabsf(m._42) > 1e-3f || fabsf(m._43) > 1e-3f;
		if (mirrored)
			return translated ? "rigid-mirror" : "rot-mirror";
		if (!translated)
			return D3DXMatrixIsIdentity(&m) ? "ident" : "rot";
		return "rigid";
	}

	std::string ViewLabel( const D3DCameraTrackInfo &camera, const D3DXMATRIX &view )
	{
		if (camera.depth0Transforms == 0)
			return "none";
		std::string label = camera.depth0Loaded ? "load:" : "";
		label += ViewKind(view);
		if (camera.depth0Transforms > 1) {
			char extra[16];
			sprintf_s(extra, "+%d", camera.depth0Transforms - 1);
			label += extra;
		}
		return label;
	}

	// Eye position and viewing direction (GL -Z) of a depth-0 matrix in world space.
	void DescribeView( const D3DXMATRIX &view, char *text, size_t size )
	{
		D3DXMATRIX inverse;
		if (!D3DXMatrixInverse(&inverse, nullptr, &view)) {
			sprintf_s(text, size, "singular");
			return;
		}
		sprintf_s(text, size, "pos=(%.1f,%.1f,%.1f) fwd=(%.3f,%.3f,%.3f)",
			inverse._41, inverse._42, inverse._43, -inverse._31, -inverse._32, -inverse._33);
	}

	bool SameRotation( const D3DXMATRIX &a, const D3DXMATRIX &b )
	{
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 3; ++column)
				if (fabsf(a.m[row][column] - b.m[row][column]) > 1e-4f)
					return false;
		return true;
	}

	void CameraCensusOnDraw( uint64_t frame, uint64_t draw, bool perspective )
	{
		D3DCameraTrackInfo camera;
		D3DMatrix_GetCameraTrackInfo(&camera);
		const int depth = D3DGlobal.modelviewMatrixStack->stack_depth();

		if (gCameraSegments.empty() || gCameraSegments.back().generation != camera.generation ||
			gCameraSegments.back().transforms != camera.depth0Transforms ||
			gCameraSegments.back().projectionSerial != gProjectionSerial) {
			CameraSegment segment = {};
			segment.generation = camera.generation;
			segment.transforms = camera.depth0Transforms;
			segment.projectionSerial = gProjectionSerial;
			segment.view = *static_cast<const D3DXMATRIX *>(D3DGlobal.modelviewMatrixStack->level(0));
			segment.label = gProjectionLabel + " " + ViewLabel(camera, segment.view);
			segment.perspective = perspective;
			segment.relation = RELATION_NO_MAIN;
			gCameraSegments.push_back(segment);

			char view[96], text[224];
			DescribeView(segment.view, view, sizeof(view));
			sprintf_s(text, "\"%s\" g%u t%d depth0 %s", segment.label.c_str(), camera.generation,
				camera.depth0Transforms, view);
			gLastCameraDescription = text;

			auto it = gCameraClasses.find(segment.label);
			if (it == gCameraClasses.end()) {
				if (gCameraClasses.size() >= kMaxCameraClasses) {
					++gCameraClassesDropped;
				} else {
					CameraClass created = {};
					created.firstFrame = frame;
					created.lastCountedFrame = ~0ull;
					gCameraClasses.emplace(segment.label, created);
					logPrintfLevel(QGL_LOG_INFO, "CAMERA",
						"new class \"%s\" first at frame=%llu draw=%llu stackDepth=%d eyeSpace=%d depth0 %s",
						segment.label.c_str(), static_cast<unsigned long long>(frame),
						static_cast<unsigned long long>(draw), depth, camera.eyeSpace ? 1 : 0, view);
				}
			}
		}

		CameraSegment &segment = gCameraSegments.back();
		++segment.draws;
		if (camera.eyeSpace) ++segment.eyeDraws;
		else if (depth == 0) ++segment.depth0Draws;
		else ++segment.objectDraws;
	}

	void CameraCensusOnFrameEnd( uint64_t frame )
	{
		const CameraSegment *main = nullptr;
		for (const CameraSegment &segment : gCameraSegments)
			if (segment.perspective && (!main || segment.draws > main->draws))
				main = &segment;

		std::string key;
		const std::string *previous = nullptr;
		int previousRelation = -1;
		for (CameraSegment &segment : gCameraSegments) {
			if (main) {
				if (!memcmp(&segment.view, &main->view, sizeof(D3DXMATRIX)))
					segment.relation = RELATION_MAIN;
				else if (SameRotation(segment.view, main->view))
					segment.relation = RELATION_SAME_ROTATION;
				else
					segment.relation = RELATION_OTHER;
			}

			auto it = gCameraClasses.find(segment.label);
			if (it != gCameraClasses.end()) {
				CameraClass &stats = it->second;
				if (stats.lastCountedFrame != frame) {
					stats.lastCountedFrame = frame;
					++stats.frames;
				}
				stats.draws += segment.draws;
				stats.depth0Draws += segment.depth0Draws;
				stats.objectDraws += segment.objectDraws;
				stats.eyeDraws += segment.eyeDraws;
				++stats.relation[segment.relation];
			}

			// Consecutive segments with the same label and relation form one item.
			if (previous && *previous == segment.label && previousRelation == segment.relation)
				continue;
			previous = &segment.label;
			previousRelation = segment.relation;
			char item[96];
			sprintf_s(item, "%s%s%c", key.empty() ? "" : " | ", segment.label.c_str(),
				kRelationMarker[segment.relation]);
			key += item;
		}

		if (!key.empty()) {
			auto it = gCameraPatterns.find(key);
			if (it == gCameraPatterns.end()) {
				if (gCameraPatterns.size() >= kMaxCameraPatterns) {
					++gCameraPatternsDropped;
				} else {
					CameraPattern created = { frame, 0 };
					it = gCameraPatterns.emplace(key, created).first;
					std::string counts;
					for (const CameraSegment &segment : gCameraSegments) {
						char item[32];
						sprintf_s(item, "%s%u", counts.empty() ? "" : ",", segment.draws);
						counts += item;
					}
					logPrintfLevel(QGL_LOG_INFO, "CAMERA", "new frame pattern at frame=%llu: %s (draws per segment %s)",
						static_cast<unsigned long long>(frame), key.c_str(), counts.c_str());
				}
			}
			if (it != gCameraPatterns.end())
				++it->second.frames;
		}
		gCameraSegments.clear();
	}

	void CameraCensusDumpSummary()
	{
		logPrintf("  Camera census (depth-0 modelview vs the main camera: = same, ~ same rotation, ! other, ? no perspective camera):\n");
		std::vector<const std::pair<const std::string, CameraClass> *> classes;
		for (const auto &entry : gCameraClasses) classes.push_back(&entry);
		std::sort(classes.begin(), classes.end(), []( const auto *a, const auto *b ) {
			return a->second.draws > b->second.draws; });
		for (const auto *entry : classes) {
			const CameraClass &c = entry->second;
			logPrintf("    \"%s\" frames=%llu draws=%llu (depth0 %llu, objects %llu, eye-space %llu) "
				"segments: =%llu ~%llu !%llu ?%llu firstFrame=%llu\n",
				entry->first.c_str(), static_cast<unsigned long long>(c.frames),
				static_cast<unsigned long long>(c.draws), static_cast<unsigned long long>(c.depth0Draws),
				static_cast<unsigned long long>(c.objectDraws), static_cast<unsigned long long>(c.eyeDraws),
				static_cast<unsigned long long>(c.relation[RELATION_MAIN]),
				static_cast<unsigned long long>(c.relation[RELATION_SAME_ROTATION]),
				static_cast<unsigned long long>(c.relation[RELATION_OTHER]),
				static_cast<unsigned long long>(c.relation[RELATION_NO_MAIN]),
				static_cast<unsigned long long>(c.firstFrame));
		}
		if (gCameraClassesDropped)
			logPrintf("    ... %llu segments of classes beyond the first %u not counted\n",
				static_cast<unsigned long long>(gCameraClassesDropped), static_cast<unsigned int>(kMaxCameraClasses));

		std::vector<const std::pair<const std::string, CameraPattern> *> patterns;
		for (const auto &entry : gCameraPatterns) patterns.push_back(&entry);
		std::sort(patterns.begin(), patterns.end(), []( const auto *a, const auto *b ) {
			return a->second.frames > b->second.frames; });
		logPrintf("  Camera frame patterns: %u\n", static_cast<unsigned int>(patterns.size()));
		const size_t listed = std::min<size_t>(patterns.size(), 24);
		for (size_t i = 0; i < listed; ++i)
			logPrintf("    [%s] frames=%llu firstFrame=%llu\n", patterns[i]->first.c_str(),
				static_cast<unsigned long long>(patterns[i]->second.frames),
				static_cast<unsigned long long>(patterns[i]->second.firstFrame));
		if (patterns.size() > listed)
			logPrintf("    ... %u more\n", static_cast<unsigned int>(patterns.size() - listed));
		if (gCameraPatternsDropped)
			logPrintf("    ... %llu frames with patterns beyond the first %u not counted\n",
				static_cast<unsigned long long>(gCameraPatternsDropped), static_cast<unsigned int>(kMaxCameraPatterns));
	}
}

void QGL_ViewDiagnosticsOnDraw( uint64_t frame, uint64_t draw )
{
	if (!D3DGlobal.projectionMatrixStack)
		return;

	const D3DXMATRIX &projection = *static_cast<const D3DXMATRIX *>(D3DGlobal.projectionMatrixStack->top());
	const uint32_t hash = HashMatrix(projection);
	if (gLastClass < 0 || hash != gLastHash) {
		gLastHash = hash;
		const ProjectionInfo info = Classify(projection);
		const std::string label = ProjectionLabel(info, projection);
		if (label != gProjectionLabel) {
			gProjectionLabel = label;
			++gProjectionSerial;
		}
		const std::string key = ClassKey(info, hash);
		auto it = gClassByKey.find(key);
		// Continuously animated projections (zoom) must not grow the table
		// without bound; later distinct classes share one OTHER entry.
		if (it == gClassByKey.end() && gClasses.size() >= kMaxClasses) {
			static const std::string overflowKey("X overflow");
			it = gClassByKey.find(overflowKey);
			if (it == gClassByKey.end()) {
				ProjectionClass overflow = {};
				overflow.info.type = PROJECTION_OTHER;
				overflow.firstHash = hash;
				overflow.firstFrame = frame;
				overflow.lastCountedFrame = ~0ull;
				gClasses.push_back(overflow);
				it = gClassByKey.emplace(overflowKey, static_cast<int>(gClasses.size()) - 1).first;
				logPrintfLevel(QGL_LOG_DEBUG, "PROJECTION", "class limit %u reached; further classes are merged into #%d",
					static_cast<unsigned int>(kMaxClasses), it->second + 1);
			}
		}
		if (it == gClassByKey.end()) {
			ProjectionClass created = {};
			created.info = info;
			created.firstHash = hash;
			created.firstFrame = frame;
			created.lastCountedFrame = ~0ull;
			gClasses.push_back(created);
			it = gClassByKey.emplace(key, static_cast<int>(gClasses.size()) - 1).first;
			uint32_t modelviewHash = 0;
			if (D3DGlobal.modelviewMatrixStack)
				modelviewHash = HashMatrix(*static_cast<const D3DXMATRIX *>(D3DGlobal.modelviewMatrixStack->top()));
			logPrintfLevel(QGL_LOG_DEBUG, "PROJECTION", "new class %s first at frame=%llu draw=%llu modelviewHash=%08X",
				DescribeClass(it->second).c_str(), static_cast<unsigned long long>(frame),
				static_cast<unsigned long long>(draw), modelviewHash);
		}
		gLastClass = it->second;
	}

	ProjectionClass &current = gClasses[gLastClass];
	++current.draws;
	if (current.lastCountedFrame != frame) {
		current.lastCountedFrame = frame;
		++current.frames;
	}
	if (gSegments.empty() || gSegments.back().first != gLastClass)
		gSegments.emplace_back(gLastClass, draw);

	unsigned char flags = 0;
	if (current.info.type == PROJECTION_ORTHO) flags |= DRAW_ORTHO;
	if (current.info.type == PROJECTION_PERSPECTIVE) flags |= DRAW_PERSPECTIVE;
	if (D3DState.EnableState.depthTestEnabled) flags |= DRAW_DEPTH_TEST;
	if (D3DState.DepthBufferState.depthWriteMask) flags |= DRAW_DEPTH_WRITE;
	gDrawFlags.push_back(flags);

	if (D3DGlobal.modelviewMatrixStack)
		CameraCensusOnDraw(frame, draw, gProjectionLabel[0] == 'P');
}

void QGL_ViewDiagnosticsDescribeCamera( char *text, size_t size )
{
	sprintf_s(text, size, "%s", gLastCameraDescription.c_str());
}

bool QGL_ViewDiagnosticsOnFrameEnd( uint64_t frame )
{
	++gFrames;
	FrameSummary summary = { std::string(), false, false, 0, false };
	for (const auto &segment : gSegments) {
		char item[32];
		sprintf_s(item, "%s#%d", summary.sequence.empty() ? "" : " ", segment.first + 1);
		summary.sequence += item;
	}

	// The last world draw: perspective with depth testing.
	size_t lastWorld = gDrawFlags.size();
	for (size_t i = gDrawFlags.size(); i-- > 0; ) {
		if ((gDrawFlags[i] & DRAW_PERSPECTIVE) && (gDrawFlags[i] & DRAW_DEPTH_TEST)) {
			lastWorld = i;
			break;
		}
	}
	size_t hudDraws = 0, hudOrtho = 0;
	if (lastWorld < gDrawFlags.size()) {
		summary.world = true;
		++gWorldFrames;
		for (size_t i = 0; i < lastWorld; ++i)
			if (gDrawFlags[i] & DRAW_ORTHO) summary.orthoInsideWorld = true;
		if (summary.orthoInsideWorld) ++gOrthoInsideWorldFrames;
		if (lastWorld + 1 < gDrawFlags.size()) {
			summary.hud = true;
			++gHudFrames;
			const unsigned char before = gDrawFlags[lastWorld], after = gDrawFlags[lastWorld + 1];
			if ((before & DRAW_PERSPECTIVE) && (after & DRAW_ORTHO)) summary.boundary |= BOUNDARY_TO_ORTHO;
			if ((before & DRAW_DEPTH_TEST) && !(after & DRAW_DEPTH_TEST)) summary.boundary |= BOUNDARY_DEPTH_TEST_OFF;
			if ((before & DRAW_DEPTH_WRITE) && !(after & DRAW_DEPTH_WRITE)) summary.boundary |= BOUNDARY_DEPTH_WRITE_OFF;
			if (summary.boundary & BOUNDARY_TO_ORTHO) ++gBoundaryToOrtho;
			if (summary.boundary & BOUNDARY_DEPTH_TEST_OFF) ++gBoundaryDepthTestOff;
			if (summary.boundary & BOUNDARY_DEPTH_WRITE_OFF) ++gBoundaryDepthWriteOff;
			hudDraws = gDrawFlags.size() - lastWorld - 1;
			for (size_t i = lastWorld + 1; i < gDrawFlags.size(); ++i)
				if (gDrawFlags[i] & DRAW_ORTHO) ++hudOrtho;
		}
	}

	if (summary.sequence != gPreviousFrame.sequence || summary.world != gPreviousFrame.world ||
		summary.hud != gPreviousFrame.hud || summary.boundary != gPreviousFrame.boundary ||
		summary.orthoInsideWorld != gPreviousFrame.orthoInsideWorld) {
		if (summary.hud)
			logPrintfLevel(QGL_LOG_DEBUG, "VIEW_FRAME",
				"frame=%llu draws=%u projections=[%s] lastWorldDraw=%u hudDraws=%u hudOrtho=%u boundary:%s%s",
				static_cast<unsigned long long>(frame), static_cast<unsigned int>(gDrawFlags.size()),
				summary.sequence.c_str(), static_cast<unsigned int>(lastWorld + 1),
				static_cast<unsigned int>(hudDraws), static_cast<unsigned int>(hudOrtho),
				BoundaryText(summary.boundary).c_str(),
				summary.orthoInsideWorld ? " (ortho draws inside the world phase)" : "");
		else
			logPrintfLevel(QGL_LOG_DEBUG, "VIEW_FRAME", "frame=%llu draws=%u projections=[%s] %s",
				static_cast<unsigned long long>(frame), static_cast<unsigned int>(gDrawFlags.size()),
				summary.sequence.c_str(), summary.world ? "world only, no HUD phase" : "no world draws");
		gPreviousFrame = summary;
	}

	gDrawFlags.clear();
	gSegments.clear();
	CameraCensusOnFrameEnd(frame);
	return summary.world;
}

void QGL_ViewDiagnosticsDumpSummary()
{
	logPrintf("View diagnostics:\n");
	logPrintf("  Frames with world draws (perspective, depth tested): %llu of %llu\n",
		static_cast<unsigned long long>(gWorldFrames), static_cast<unsigned long long>(gFrames));
	logPrintf("  Frames with draws after the last world draw (HUD/post phase): %llu\n",
		static_cast<unsigned long long>(gHudFrames));
	logPrintf("    boundary perspective->ortho: %llu, depthTest on->off: %llu, depthWrite on->off: %llu\n",
		static_cast<unsigned long long>(gBoundaryToOrtho), static_cast<unsigned long long>(gBoundaryDepthTestOff),
		static_cast<unsigned long long>(gBoundaryDepthWriteOff));
	logPrintf("  Frames with orthographic draws before the last world draw: %llu\n",
		static_cast<unsigned long long>(gOrthoInsideWorldFrames));
	logPrintf("  Projection classes: %u\n", static_cast<unsigned int>(gClasses.size()));

	std::vector<int> order(gClasses.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
	std::sort(order.begin(), order.end(), []( int a, int b ) { return gClasses[a].draws > gClasses[b].draws; });
	const size_t listed = std::min<size_t>(order.size(), 24);
	for (size_t i = 0; i < listed; ++i) {
		const ProjectionClass &c = gClasses[order[i]];
		logPrintf("    %s frames=%llu draws=%llu firstFrame=%llu\n", DescribeClass(order[i]).c_str(),
			static_cast<unsigned long long>(c.frames), static_cast<unsigned long long>(c.draws),
			static_cast<unsigned long long>(c.firstFrame));
	}
	if (order.size() > listed)
		logPrintf("    ... %u more\n", static_cast<unsigned int>(order.size() - listed));
	CameraCensusDumpSummary();
}
