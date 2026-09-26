// Projective texturing: GL divides S and T by Q per fragment. You Are Empty
// projects object shadows onto the ground with texgen (S, T, R, Q) and a
// projective texture matrix; without the division the whole receiver area
// sampled one clamped texel and showed as a dark rectangle.
//
// A full-viewport quad samples a 2x1 texture (left red, right green). The
// coordinates are chosen so that the centre is red with the division by
// q = 2 and green without it.

#include <stdio.h>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

const float kQuad[12] = { -1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0 };

bool IsRed( const RGBA8 &p ) { return p.r > 200 && p.g < 60; }
bool IsGreen( const RGBA8 &p ) { return p.g > 200 && p.r < 60; }

GLuint Begin()
{
	const unsigned char texels[8] = { 255, 0, 0, 255, 0, 255, 0, 255 };
	GLuint texture = 0;
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.Color4ub(255, 255, 255, 255);
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	gl.Enable(GL_TEXTURE_2D);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
	return texture;
}

// Object-linear texgen: s = x + 0.9, t = 0.5, and optionally q = 2.
void EnableTexGen( bool generateQ )
{
	const float sPlane[4] = { 1.0f, 0.0f, 0.0f, 0.9f };
	const float tPlane[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
	const float qPlane[4] = { 0.0f, 0.0f, 0.0f, 2.0f };
	gl.TexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
	gl.TexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
	gl.TexGenfv(GL_S, GL_OBJECT_PLANE, sPlane);
	gl.TexGenfv(GL_T, GL_OBJECT_PLANE, tPlane);
	gl.Enable(GL_TEXTURE_GEN_S);
	gl.Enable(GL_TEXTURE_GEN_T);
	if (generateQ) {
		gl.TexGeni(GL_Q, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
		gl.TexGenfv(GL_Q, GL_OBJECT_PLANE, qPlane);
		gl.Enable(GL_TEXTURE_GEN_Q);
	}
}

void LoadTextureMatrix( const float *matrix )
{
	gl.MatrixMode(GL_TEXTURE);
	if (matrix) gl.LoadMatrixf(matrix);
	else gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
}

// Draws, then checks the centre (s = 0.9 before division) and x = 0.5
// (s = 1.4, green with or without division).
void DrawAndCheck( const char *label, bool centreRed )
{
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	const RGBA8 centre = Harness_ReadCenter();
	const RGBA8 right = Harness_ReadPixel(Harness_Width() * 3 / 4, Harness_Height() / 2);
	Harness_Swap();
	CHECK(centreRed ? IsRed(centre) : IsGreen(centre), "%s: centre (%d,%d,%d), expected %s", label,
		centre.r, centre.g, centre.b, centreRed ? "red" : "green");
	CHECK(IsGreen(right), "%s: x=0.5 (%d,%d,%d), expected green", label, right.r, right.g, right.b);
}

void End( GLuint texture )
{
	LoadTextureMatrix(nullptr);
	gl.Disable(GL_TEXTURE_GEN_S);
	gl.Disable(GL_TEXTURE_GEN_T);
	gl.Disable(GL_TEXTURE_GEN_Q);
	gl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.Disable(GL_TEXTURE_2D);
	gl.DeleteTextures(1, &texture);
}

} // namespace

void do_texture_projection_tests()
{
	// GL column-major matrices.
	const float scaleQ[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2 };
	const float translateS[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5f, 0, 0, 1 };

	GLuint texture = Begin();
	EnableTexGen(false);
	DrawAndCheck("texgen S/T without projection", false);
	End(texture);

	texture = Begin();
	EnableTexGen(true);
	DrawAndCheck("texgen S/T/Q, q = 2", true);
	End(texture);

	texture = Begin();
	EnableTexGen(false);
	LoadTextureMatrix(scaleQ);
	DrawAndCheck("texgen S/T with a projective texture matrix", true);
	End(texture);

	texture = Begin();
	EnableTexGen(false);
	LoadTextureMatrix(translateS);
	DrawAndCheck("texgen S/T with an affine texture matrix (s - 0.5)", true);
	End(texture);

	// Texture coordinate array (the fast copy path would write only S and T).
	const float texcoords[8] = { -0.1f, 0.5f, 1.9f, 0.5f, -0.1f, 0.5f, 1.9f, 0.5f };
	texture = Begin();
	gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.TexCoordPointer(2, GL_FLOAT, 0, texcoords);
	DrawAndCheck("texcoord array without a texture matrix", false);
	End(texture);
	texture = Begin();
	gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.TexCoordPointer(2, GL_FLOAT, 0, texcoords);
	LoadTextureMatrix(scaleQ);
	DrawAndCheck("texcoord array with a projective texture matrix", true);
	End(texture);
}
