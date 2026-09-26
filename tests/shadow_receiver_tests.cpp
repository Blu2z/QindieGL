// You Are Empty's object shadows (use_shaders = 0). DS2 renders each caster's
// silhouette into a texture, then re-draws the receiving floor polygons over
// the already drawn floor:
//
//   blend DST_COLOR/ZERO, alpha test GREATER 0.5, depth LEQUAL without writes,
//   glPolygonOffset(-2, 3), receivers streamed through a VBO and an element
//   buffer, unit 0 = silhouette (eye-linear S/T/R/Q texgen, texture matrix,
//   GL_COMBINE primary + texture), unit 1 = 1D distance fade (texgen S,
//   texture matrix, GL_ADD), and for the nearest caster an ARB fragment program
//   blurring the silhouette with four TXP taps.
//
// A white floor is expected to darken to about 0.8 under the grey silhouette
// and to stay white elsewhere.

#include <stdio.h>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

// Floor plane in eye space: z = -300 + 1.5 y, seen from the origin with DS2's
// projection (near 10, far 6000). The screen centre sees (0, 0, -300).
const float kFloor[12] = {
	-400, -300, -750,
	 400, -300, -750,
	-400,  100, -150,
	 400,  100, -150,
};
const unsigned short kIndices[6] = { 0, 1, 2, 2, 1, 3 };

enum ReceiverPath { CLIENT_ARRAYS, STREAMED_BUFFERS };

struct ShadowCase
{
	const char *label;
	ReceiverPath path;
	bool polygonOffset;
	bool fragmentProgram;
};

// Like DS2's soft-shadow program: four projective taps around the silhouette
// coordinate, plus the fade texture and the primary colour; alpha from the
// silhouette only.
const char kSoftShadowProgram[] =
	"!!ARBfp1.0\n"
	"PARAM offsets[2] = { { -0.0078125, 0.0023437501, 0.00390625, 0.00078125001 },\n"
	"                     { -0.0015625, -0.0046875002, 0, 0.0054687499 } };\n"
	"TEMP coord, sum, tap, fade;\n"
	"MOV coord.zw, fragment.texcoord[0];\n"
	"MAD coord.xy, fragment.texcoord[0].w, offsets[0], fragment.texcoord[0];\n"
	"TXP sum, coord, texture[0], 2D;\n"
	"MAD coord.xy, fragment.texcoord[0].w, offsets[0].zwzw, fragment.texcoord[0];\n"
	"TXP tap, coord, texture[0], 2D;\n"
	"ADD sum, sum, tap;\n"
	"MAD coord.xy, fragment.texcoord[0].w, offsets[1], fragment.texcoord[0];\n"
	"TXP tap, coord, texture[0], 2D;\n"
	"ADD sum, sum, tap;\n"
	"MAD coord.xy, fragment.texcoord[0].w, offsets[1].zwzw, fragment.texcoord[0];\n"
	"TXP tap, coord, texture[0], 2D;\n"
	"ADD sum, sum, tap;\n"
	"MUL sum, sum, 0.25;\n"
	"TEX fade.xyz, fragment.texcoord[1], texture[1], 1D;\n"
	"ADD fade.xyz, fade, fragment.color.primary;\n"
	"ADD result.color.xyz, sum, fade;\n"
	"MOV result.color.w, sum;\n"
	"END\n";

GLuint CreateSilhouetteTexture()
{
	// 4x4, white with a grey (0.7) 2x2 centre, opaque everywhere.
	unsigned char texels[4 * 4 * 4];
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			const bool centre = x >= 1 && x <= 2 && y >= 1 && y <= 2;
			unsigned char *texel = texels + (y * 4 + x) * 4;
			texel[0] = texel[1] = texel[2] = centre ? 178 : 255;
			texel[3] = 255;
		}
	}
	GLuint texture = 0;
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	return texture;
}

GLuint CreateFadeTexture()
{
	// DS2's 16-texel fade: transparent white, then opaque black, then a ramp.
	unsigned char texels[16 * 4];
	const unsigned char ramp[4] = { 79, 121, 200, 255 };
	for (int i = 0; i < 16; ++i) {
		const unsigned char value = i < 8 ? 255 : i < 12 ? 0 : ramp[i - 12];
		texels[i * 4 + 0] = texels[i * 4 + 1] = texels[i * 4 + 2] = value;
		texels[i * 4 + 3] = i < 8 ? 0 : 255;
	}
	GLuint texture = 0;
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_1D, texture);
	gl.TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	gl.TexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	return texture;
}

void DrawFloor()
{
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.Frustum(-10, 10, -5.625, 5.625, 10, 6000);
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.Enable(GL_DEPTH_TEST);
	gl.DepthFunc(GL_LEQUAL);
	gl.DepthMask(GL_TRUE);
	gl.Color4f(1, 1, 1, 1);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, kFloor);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kIndices);
}

void SetupReceiverState( GLuint silhouette, GLuint fade, bool polygonOffset )
{
	gl.Enable(GL_BLEND);
	gl.BlendFunc(GL_DST_COLOR, GL_ZERO);
	gl.Enable(GL_ALPHA_TEST);
	gl.AlphaFunc(GL_GREATER, 0.5f);
	gl.DepthMask(GL_FALSE);

	// Unit 0: silhouette, eye-linear S/T/R/Q = eye position, then
	// s = 0.5 + x / 400, t = 0.5 + y / 400.
	const float planes[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
	const GLenum coords[4] = { GL_S, GL_T, GL_R, GL_Q };
	const GLenum enables[4] = { GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_TEXTURE_GEN_R, GL_TEXTURE_GEN_Q };
	gl.ActiveTextureARB(GL_TEXTURE0_ARB);
	gl.Enable(GL_TEXTURE_2D);
	gl.BindTexture(GL_TEXTURE_2D, silhouette);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_ADD);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PRIMARY_COLOR_ARB);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);
	for (int i = 0; i < 4; ++i) {
		gl.TexGeni(coords[i], GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
		gl.TexGenfv(coords[i], GL_EYE_PLANE, planes[i]);
		gl.Enable(enables[i]);
	}
	gl.MatrixMode(GL_TEXTURE);
	gl.LoadIdentity();
	gl.Translatef(0.5f, 0.5f, 0.0f);
	gl.Scalef(1.0f / 400.0f, 1.0f / 400.0f, 1.0f);

	// Unit 1: fade, s = -z_eye mapped to 0.5 + 0.5 * k * 300 = 0.6 (opaque black).
	const float fadePlane[4] = { 0, 0, -1, 0 };
	gl.ActiveTextureARB(GL_TEXTURE1_ARB);
	gl.Enable(GL_TEXTURE_1D);
	gl.BindTexture(GL_TEXTURE_1D, fade);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
	gl.LoadIdentity();
	gl.Translatef(0.5f, 0.0f, 0.0f);
	gl.Scalef(0.5f, 1.0f, 1.0f);
	gl.Scalef(0.2f / 300.0f, 1.0f, 1.0f);
	gl.TexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
	gl.TexGenfv(GL_S, GL_EYE_PLANE, fadePlane);
	gl.Enable(GL_TEXTURE_GEN_S);
	gl.MatrixMode(GL_MODELVIEW);

	if (polygonOffset) {
		gl.Enable(GL_POLYGON_OFFSET_FILL);
		gl.PolygonOffset(-2.0f, 3.0f);
	}
	gl.Color4f(0.1f, 0.1f, 0.1f, 1.0f);
}

void ResetReceiverState()
{
	gl.Disable(GL_POLYGON_OFFSET_FILL);
	gl.ActiveTextureARB(GL_TEXTURE1_ARB);
	gl.MatrixMode(GL_TEXTURE);
	gl.LoadIdentity();
	gl.Disable(GL_TEXTURE_GEN_S);
	gl.Disable(GL_TEXTURE_1D);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	gl.ActiveTextureARB(GL_TEXTURE0_ARB);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.Disable(GL_TEXTURE_GEN_S);
	gl.Disable(GL_TEXTURE_GEN_T);
	gl.Disable(GL_TEXTURE_GEN_R);
	gl.Disable(GL_TEXTURE_GEN_Q);
	gl.Disable(GL_TEXTURE_2D);
	gl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	gl.Disable(GL_ALPHA_TEST);
	gl.Disable(GL_BLEND);
	gl.DepthMask(GL_TRUE);
	gl.Disable(GL_DEPTH_TEST);
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.Color4f(1, 1, 1, 1);
}

void DrawReceivers( ReceiverPath path, GLuint buffers[2] )
{
	if (path == CLIENT_ARRAYS) {
		gl.VertexPointer(3, GL_FLOAT, 0, kFloor);
		gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kIndices);
		return;
	}
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, buffers[0]);
	gl.BufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(kFloor), kFloor, GL_STREAM_DRAW_ARB);
	gl.VertexPointer(3, GL_FLOAT, 12, nullptr);
	gl.BindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, buffers[1]);
	gl.BufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof(kIndices), kIndices, GL_STREAM_DRAW_ARB);
	gl.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
	gl.BindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	gl.BindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
}

void RunCase( const ShadowCase &test, GLuint silhouette, GLuint fade, GLuint program, GLuint buffers[2] )
{
	DrawFloor();
	const RGBA8 floorCentre = Harness_ReadCenter();
	SetupReceiverState(silhouette, fade, test.polygonOffset);
	if (test.fragmentProgram) {
		gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, program);
		gl.Enable(GL_FRAGMENT_PROGRAM_ARB);
	}
	DrawReceivers(test.path, buffers);
	if (test.fragmentProgram)
		gl.Disable(GL_FRAGMENT_PROGRAM_ARB);
	const RGBA8 centre = Harness_ReadCenter();
	const RGBA8 outside = Harness_ReadPixel(Harness_Width() * 3 / 4, Harness_Height() / 2);
	ResetReceiverState();
	Harness_Swap();

	CHECK(floorCentre.r > 245, "%s: floor centre before the shadow is (%d,%d,%d), expected white",
		test.label, floorCentre.r, floorCentre.g, floorCentre.b);
	CHECK(centre.r > 185 && centre.r < 225, "%s: shadowed centre is (%d,%d,%d), expected about 204",
		test.label, centre.r, centre.g, centre.b);
	CHECK(outside.r > 245, "%s: unshadowed floor is (%d,%d,%d), expected white",
		test.label, outside.r, outside.g, outside.b);
}

} // namespace

void do_shadow_receiver_tests()
{
	if (!gl.ActiveTextureARB || !gl.BindBufferARB || !gl.GenBuffersARB || !gl.BufferDataARB) {
		CHECK(false, "multitexture or buffer object entry points unavailable");
		return;
	}
	const bool programs = gl.GenProgramsARB && gl.BindProgramARB && gl.ProgramStringARB && gl.DeleteProgramsARB;

	const ShadowCase cases[] = {
		{ "shadow receivers, client arrays, no offset", CLIENT_ARRAYS, false, false },
		{ "shadow receivers, client arrays", CLIENT_ARRAYS, true, false },
		{ "shadow receivers, streamed buffers", STREAMED_BUFFERS, true, false },
		{ "soft shadow receivers, fragment program", STREAMED_BUFFERS, true, true },
	};

	const GLuint silhouette = CreateSilhouetteTexture();
	const GLuint fade = CreateFadeTexture();
	GLuint buffers[2] = {};
	gl.GenBuffersARB(2, buffers);
	GLuint program = 0;
	if (programs) {
		gl.GenProgramsARB(1, &program);
		gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, program);
		gl.ProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
			static_cast<GLsizei>(sizeof(kSoftShadowProgram) - 1), kSoftShadowProgram);
	}

	for (const ShadowCase &test : cases) {
		if (test.fragmentProgram && !programs) continue;
		RunCase(test, silhouette, fade, program, buffers);
	}

	if (programs) {
		gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
		gl.DeleteProgramsARB(1, &program);
	}
	gl.DeleteBuffersARB(2, buffers);
	gl.DeleteTextures(1, &silhouette);
	gl.DeleteTextures(1, &fade);
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
}
