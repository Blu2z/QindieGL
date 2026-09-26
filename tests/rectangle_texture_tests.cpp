// GL_TEXTURE_RECTANGLE orientation. Rectangle coordinates are in texels with
// the origin at the first uploaded row, and a framebuffer copy puts the
// framebuffer's bottom row there. You Are Empty's pickup effect copies the
// screen into a rectangle texture and draws it back through an ARB fragment
// program; a flipped lookup turned the whole picture upside down.
//
// Every case draws a full-viewport quad and expects the texture's four
// quadrants in place: red bottom-left, blue bottom-right, green top-left and
// white top-right.

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

struct Quadrant { const char *name; int column, row; unsigned char r, g, b; };
const Quadrant kQuadrants[4] = {
	{ "bottom-left red", 0, 0, 255, 0, 0 },
	{ "bottom-right blue", 1, 0, 0, 0, 255 },
	{ "top-left green", 0, 1, 0, 255, 0 },
	{ "top-right white", 1, 1, 255, 255, 255 },
};

bool Near( unsigned char value, unsigned char expected ) { return expected ? value > 200 : value < 60; }

void CheckQuadrants( const char *label )
{
	RGBA8 pixels[4];
	for (int i = 0; i < 4; ++i)
		pixels[i] = Harness_ReadPixel(Harness_Width() * (1 + 2 * kQuadrants[i].column) / 4,
			Harness_Height() * (1 + 2 * kQuadrants[i].row) / 4);
	Harness_Swap();
	for (int i = 0; i < 4; ++i) {
		const Quadrant &q = kQuadrants[i];
		const RGBA8 &p = pixels[i];
		CHECK(Near(p.r, q.r) && Near(p.g, q.g) && Near(p.b, q.b), "%s: %s is (%d,%d,%d)",
			label, q.name, p.r, p.g, p.b);
	}
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
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
}

GLuint CreateRectangleTexture( int width, int height, const unsigned char *texels )
{
	GLuint texture = 0;
	gl.GenTextures(1, &texture);
	gl.BindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);
	gl.TexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.TexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl.TexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	return texture;
}

// Draws the full-viewport quad with texture coordinates spanning
// width x height texels.
void DrawTexturedQuad( float width, float height )
{
	const float texcoords[8] = { 0, 0, width, 0, 0, height, width, height };
	gl.EnableClientState(GL_TEXTURE_COORD_ARRAY);
	gl.TexCoordPointer(2, GL_FLOAT, 0, texcoords);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	gl.DisableClientState(GL_TEXTURE_COORD_ARRAY);
}

// Fills the framebuffer with the four quadrants, untextured.
void DrawQuadrants()
{
	for (const Quadrant &q : kQuadrants) {
		const float x0 = q.column ? 0.0f : -1.0f;
		const float y0 = q.row ? 0.0f : -1.0f;
		const float vertices[12] = { x0, y0, 0, x0 + 1, y0, 0, x0, y0 + 1, 0, x0 + 1, y0 + 1, 0 };
		gl.Color4ub(q.r, q.g, q.b, 255);
		gl.VertexPointer(3, GL_FLOAT, 0, vertices);
		gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	gl.Color4ub(255, 255, 255, 255);
	gl.VertexPointer(3, GL_FLOAT, 0, kQuad);
}

} // namespace

void do_rectangle_texture_tests()
{
	// Rectangle textures are only accepted by the You Are Empty profile, which
	// also exposes ARB programs.
	if (!gl.GenProgramsARB || !gl.BindProgramARB || !gl.ProgramStringARB || !gl.DeleteProgramsARB)
		return;

	// Row 0 is the bottom row.
	const unsigned char texels[16] = {
		255, 0, 0, 255, 0, 0, 255, 255,
		0, 255, 0, 255, 255, 255, 255, 255,
	};

	ResetState();
	for (int i = 0; i < 8 && gl.GetError() != GL_NO_ERROR; ++i) {}
	GLuint texture = CreateRectangleTexture(2, 2, texels);
	GLenum error = gl.GetError();
	CHECK(error == GL_NO_ERROR, "rectangle texture upload failed (0x%X)", error);
	gl.Enable(GL_TEXTURE_RECTANGLE_ARB);
	DrawTexturedQuad(2, 2);
	gl.Disable(GL_TEXTURE_RECTANGLE_ARB);
	CheckQuadrants("fixed-function RECT texture");

	static const char source[] =
		"!!ARBfp1.0\n"
		"TEX result.color, fragment.texcoord[0], texture[0], RECT;\n"
		"END\n";
	GLuint program = 0;
	gl.GenProgramsARB(1, &program);
	gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, program);
	gl.ProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
		static_cast<GLsizei>(sizeof(source) - 1), source);

	ResetState();
	gl.Enable(GL_FRAGMENT_PROGRAM_ARB);
	DrawTexturedQuad(2, 2);
	gl.Disable(GL_FRAGMENT_PROGRAM_ARB);
	CheckQuadrants("fragment program TEX RECT");
	gl.DeleteTextures(1, &texture);

	// Copy the framebuffer into a rectangle texture and draw it back.
	const int width = Harness_Width();
	const int height = Harness_Height();
	ResetState();
	texture = CreateRectangleTexture(width, height, nullptr);
	DrawQuadrants();
	// QindieGL ignores the first five framebuffer copies after the device is
	// created or reset.
	for (int copy = 0; copy < 6; ++copy)
		gl.CopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 0, 0, width, height);
	error = gl.GetError();
	CHECK(error == GL_NO_ERROR, "framebuffer copy into a rectangle texture failed (0x%X)", error);

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.Enable(GL_TEXTURE_RECTANGLE_ARB);
	DrawTexturedQuad(static_cast<float>(width), static_cast<float>(height));
	gl.Disable(GL_TEXTURE_RECTANGLE_ARB);
	CheckQuadrants("fixed-function RECT texture of a framebuffer copy");

	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.Enable(GL_FRAGMENT_PROGRAM_ARB);
	DrawTexturedQuad(static_cast<float>(width), static_cast<float>(height));
	gl.Disable(GL_FRAGMENT_PROGRAM_ARB);
	CheckQuadrants("fragment program TEX RECT of a framebuffer copy");
	gl.DeleteTextures(1, &texture);

	gl.BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
	gl.DeleteProgramsARB(1, &program);
	gl.DisableClientState(GL_VERTEX_ARRAY);
}
