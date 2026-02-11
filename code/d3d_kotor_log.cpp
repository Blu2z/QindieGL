#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_kotor_log.hpp"
#include "d3d_helpers.hpp"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// KotOR diagnostic instrumentation (Phase 0)
// ---------------------------------------------------------------------------

static bool g_kotorLogEnabled = false;
static int  g_frameNumber = 0;

// Step 0.2: draw call counters
static int  g_drawCallsThisFrame = 0;
static int  g_drawCallsIM = 0;   // immediate mode
static int  g_drawCallsVA = 0;   // vertex array

// Frame dump state (Ctrl+D triggers a one-frame verbose dump)
static bool g_dumpNextFrame = false;
static bool g_dumpThisFrame = false;

// ---------------------------------------------------------------------------

void kotor_log_init()
{
	g_kotorLogEnabled = (D3DGlobal_ReadGameConf("kotor_debug_log") != 0);
	if (g_kotorLogEnabled)
		logPrintf("KotOR diagnostic logging enabled\n");
}

bool kotor_log_enabled()
{
	return g_kotorLogEnabled;
}

// ---------------------------------------------------------------------------
// Step 0.1 — Matrix operation logging
// ---------------------------------------------------------------------------

static const char* matrixModeName(GLenum mode)
{
	switch (mode) {
	case GL_MODELVIEW:  return "MODELVIEW";
	case GL_PROJECTION: return "PROJECTION";
	case GL_TEXTURE:    return "TEXTURE";
	default:            return "UNKNOWN";
	}
}

static bool isIdentityRotation(const float *m)
{
	// Check if the upper-left 3x3 is identity (tolerance 1e-4)
	const float id[9] = {1,0,0, 0,1,0, 0,0,1};
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			if (fabsf(m[c*4+r] - id[r*3+c]) > 1e-4f) return false;
	return true;
}

static bool isZeroTranslation(const float *m)
{
	// Column-major: translation is m[12], m[13], m[14]
	return (fabsf(m[12]) < 1e-4f && fabsf(m[13]) < 1e-4f && fabsf(m[14]) < 1e-4f);
}

void kotor_log_load_identity(GLenum matrixMode)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode == GL_MODELVIEW)
		logPrintf("[KotOR] F%d  MODELVIEW RESET (LoadIdentity)\n", g_frameNumber);
	else if (matrixMode == GL_PROJECTION)
		logPrintf("[KotOR] F%d  PROJECTION RESET (LoadIdentity)\n", g_frameNumber);
}

void kotor_log_mult_matrix(GLenum matrixMode, const float *m)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;

	bool pureRot = isZeroTranslation(m);
	bool identRot = isIdentityRotation(m);

	const char *tag = "TRANSFORM";
	if (pureRot && !identRot) tag = "ROTATION";
	else if (identRot && !isZeroTranslation(m)) tag = "TRANSLATION";
	else if (identRot && isZeroTranslation(m)) tag = "IDENTITY";

	logPrintf("[KotOR] F%d  MultMatrix(%s) t=(%.3f, %.3f, %.3f)\n",
		g_frameNumber, tag, m[12], m[13], m[14]);

	if (g_dumpThisFrame) {
		logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[0], m[4], m[8],  m[12]);
		logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[1], m[5], m[9],  m[13]);
		logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[2], m[6], m[10], m[14]);
		logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[3], m[7], m[11], m[15]);
	}
}

void kotor_log_translate(GLenum matrixMode, float x, float y, float z)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;
	logPrintf("[KotOR] F%d  Translate(%.3f, %.3f, %.3f)\n", g_frameNumber, x, y, z);
}

void kotor_log_rotate(GLenum matrixMode, float angle, float x, float y, float z)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;
	logPrintf("[KotOR] F%d  Rotate(%.1f, %.2f, %.2f, %.2f)\n", g_frameNumber, angle, x, y, z);
}

void kotor_log_scale(GLenum matrixMode, float x, float y, float z)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;
	logPrintf("[KotOR] F%d  Scale(%.3f, %.3f, %.3f)\n", g_frameNumber, x, y, z);
}

void kotor_log_push_matrix(GLenum matrixMode, int depth)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;
	logPrintf("[KotOR] F%d  PushMatrix (depth=%d)\n", g_frameNumber, depth);
}

void kotor_log_pop_matrix(GLenum matrixMode, int depth)
{
	if (!g_kotorLogEnabled && !g_dumpThisFrame) return;
	if (matrixMode != GL_MODELVIEW) return;
	logPrintf("[KotOR] F%d  PopMatrix  (depth=%d)\n", g_frameNumber, depth);
}

void kotor_log_set_transform(const char *label, const float *m)
{
	if (!g_dumpThisFrame) return;
	logPrintf("[KotOR] F%d  SetTransform(%s):\n", g_frameNumber, label);
	logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[0], m[4], m[8],  m[12]);
	logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[1], m[5], m[9],  m[13]);
	logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[2], m[6], m[10], m[14]);
	logPrintf("        [%.4f %.4f %.4f %.4f]\n", m[3], m[7], m[11], m[15]);
}

// ---------------------------------------------------------------------------
// Step 0.2 — Draw call tracking
// ---------------------------------------------------------------------------

void kotor_log_draw_call_im(GLenum primType, int vertexCount, DWORD fvf)
{
	g_drawCallsThisFrame++;
	g_drawCallsIM++;

	if (g_dumpThisFrame) {
		logPrintf("[KotOR] F%d  DRAW #%d IM prim=0x%x verts=%d fvf=0x%08x\n",
			g_frameNumber, g_drawCallsThisFrame, primType, vertexCount, fvf);
	}
}

void kotor_log_draw_call_va(GLenum primType, int indexCount, int vertexCount)
{
	g_drawCallsThisFrame++;
	g_drawCallsVA++;

	if (g_dumpThisFrame) {
		logPrintf("[KotOR] F%d  DRAW #%d VA prim=0x%x idx=%d verts=%d\n",
			g_frameNumber, g_drawCallsThisFrame, primType, indexCount, vertexCount);
	}
}

void kotor_log_frame_end()
{
	if (g_kotorLogEnabled || g_dumpThisFrame) {
		logPrintf("[KotOR] F%d  --- FRAME END --- draws=%d (IM=%d VA=%d)\n",
			g_frameNumber, g_drawCallsThisFrame, g_drawCallsIM, g_drawCallsVA);
	}

	g_frameNumber++;
	g_drawCallsThisFrame = 0;
	g_drawCallsIM = 0;
	g_drawCallsVA = 0;

	// Handle dump request for next frame
	g_dumpThisFrame = g_dumpNextFrame;
	g_dumpNextFrame = false;
}

bool kotor_log_dump_requested()
{
	return g_dumpThisFrame;
}

void kotor_log_request_dump()
{
	g_dumpNextFrame = true;
	logPrintf("[KotOR] Frame dump requested — will dump next frame\n");
}

// ---------------------------------------------------------------------------
// Step 0.3 — Texture hash logging
// ---------------------------------------------------------------------------

void kotor_log_set_texture(int stage, void *d3dTexPtr, int glTexName)
{
	if (!g_dumpThisFrame) return;
	logPrintf("[KotOR] F%d  SetTexture stage=%d d3dPtr=%p glName=%d\n",
		g_frameNumber, stage, d3dTexPtr, glTexName);
}
