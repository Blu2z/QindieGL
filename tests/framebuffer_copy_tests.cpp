// glCopyTexSubImage2D into a 2D texture. A copy replacing a whole texture
// makes QindieGL keep it as a render-target texture and copy on the GPU (You
// Are Empty's shadow silhouettes); later partial copies stay on the GPU, and
// an upload turns it back into an ordinary texture without losing content.
//
// The framebuffer holds four coloured quadrants (split at half the window);
// the texture is then drawn over the whole viewport with texture coordinates
// 0..1, so screen point (s, t) shows texel (s, t).

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
const float kTexcoords[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
const int kTextureWidth = 64;
const int kTextureHeight = 32;

struct Colour { unsigned char r, g, b; const char *name; };
const Colour kRed = { 255, 0, 0, "red" };
const Colour kBlue = { 0, 0, 255, "blue" };
const Colour kGreen = { 0, 255, 0, "green" };
const Colour kWhite = { 255, 255, 255, "white" };
const Colour kMagenta = { 255, 0, 255, "magenta" };

bool Matches( const RGBA8 &p, const Colour &c )
{
	const auto close = []( int a, int b ) { return a > b - 40 && a < b + 40; };
	return close(p.r, c.r) && close(p.g, c.g) && close(p.b, c.b);
}

void ResetState()
{
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.Color4ub(255, 255, 255, 255);
	gl.EnableClientState(GL_VERTEX_ARRAY);
}

// Red bottom-left, blue bottom-right, green top-left, white top-right.
void DrawQuadrants()
{
	const Colour colours[4] = { kRed, kBlue, kGreen, kWhite };
	for (int i = 0; i < 4; ++i) {
		const float x0 = (i & 1) ? 0.0f : -1.0f;
		const float y0 = (i & 2) ? 0.0f : -1.0f;
		const float vertices[12] = { x0, y0, 0, x0 + 1, y0, 0, x0, y0 + 1, 0, x0 + 1, y0 + 1, 0 };
		gl.Color4ub(colours[i].r, colours[i].g, colours[i].b, 255);
		gl.VertexPointer(3, GL_FLOAT, 0, vertices);
		gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	gl.Color4ub(255, 255, 255, 255);
}

// Shows the texture over the viewport and checks texel positions (s, t).
struct Probe { float s, t; Colour expected; };
void CheckTexture( const char *label, GLuint texture, const Probe *probes, int count )
{
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.Enable(GL_TEXTURE_2D);
	gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.TexCoordPointer(2, GL_FLOAT, 0, kTexcoords);
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	gl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.Disable(GL_TEXTURE_2D);

	RGBA8 pixels[8];
	for (int i = 0; i < count; ++i)
		pixels[i] = Harness_ReadPixel(static_cast<int>(probes[i].s * Harness_Width()),
			static_cast<int>(probes[i].t * Harness_Height()));
	Harness_Swap();
	for (int i = 0; i < count; ++i) {
		CHECK(Matches(pixels[i], probes[i].expected), "%s: texel (%.2f, %.2f) is (%d,%d,%d), expected %s", label,
			probes[i].s, probes[i].t, pixels[i].r, pixels[i].g, pixels[i].b, probes[i].expected.name);
	}
}

} // namespace

void do_framebuffer_copy_tests()
{
	const int halfWidth = Harness_Width() / 2;
	const int halfHeight = Harness_Height() / 2;
	if (halfWidth < 40 || halfHeight < 24) {
		CHECK(false, "window too small for the framebuffer copy tests (%dx%d)", Harness_Width(), Harness_Height());
		return;
	}

	GLuint texture = 0;
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTextureWidth, kTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	// Whole texture from framebuffer (x0, y0): texel (s, t) shows framebuffer
	// pixel (x0 + 64 s, y0 + 32 t), straddling the quadrant split.
	const int x0 = halfWidth - kTextureWidth / 2;
	const int y0 = halfHeight - kTextureHeight / 2;
	ResetState();
	DrawQuadrants();
	// QindieGL ignores the first five framebuffer copies after the device is
	// created or reset.
	for (int copy = 0; copy < 6; ++copy)
		gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x0, y0, kTextureWidth, kTextureHeight);
	const Probe whole[4] = {
		{ 0.25f, 0.25f, kRed }, { 0.75f, 0.25f, kBlue }, { 0.25f, 0.75f, kGreen }, { 0.75f, 0.75f, kWhite },
	};
	CheckTexture("whole-texture copy", texture, whole, 4);

	// Partial copy of the top-right quadrant into texels [0,16) x [0,8).
	ResetState();
	DrawQuadrants();
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, halfWidth + 4, halfHeight + 4, 16, 8);
	const Probe partial[3] = {
		{ 0.1f, 0.1f, kWhite }, { 0.4f, 0.1f, kRed }, { 0.1f, 0.4f, kRed },
	};
	CheckTexture("partial copy", texture, partial, 3);

	// An upload into texels [48,64) x [24,32) keeps both copies' content.
	unsigned char patch[16 * 8 * 4];
	for (int i = 0; i < 16 * 8; ++i) {
		patch[i * 4 + 0] = 255;
		patch[i * 4 + 1] = 0;
		patch[i * 4 + 2] = 255;
		patch[i * 4 + 3] = 255;
	}
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 48, 24, 16, 8, GL_RGBA, GL_UNSIGNED_BYTE, patch);
	const Probe uploaded[4] = {
		{ 0.9f, 0.9f, kMagenta }, { 0.1f, 0.1f, kWhite }, { 0.75f, 0.25f, kBlue }, { 0.25f, 0.75f, kGreen },
	};
	CheckTexture("upload after copies", texture, uploaded, 4);

	// Copying the whole texture again works after the upload.
	ResetState();
	DrawQuadrants();
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x0, y0, kTextureWidth, kTextureHeight);
	CheckTexture("whole-texture copy after an upload", texture, whole, 4);

	CHECK(gl.GetError() == GL_NO_ERROR, "framebuffer copy tests raised a GL error");
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.DeleteTextures(1, &texture);
}
