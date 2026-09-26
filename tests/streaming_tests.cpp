// Vertex and index streaming. QindieGL copies each draw's referenced vertices
// and its indices into dynamic D3D9 rings, appending without discarding until a
// ring is full. The draws below reference a large vertex range, so one frame
// wraps the rings several times and one draw needs a larger ring; every band
// must still show the colour of its own draw.

#include <stdio.h>
#include <vector>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

const int kBands = 8;

struct Colour { unsigned char r, g, b; };
const Colour kColours[kBands] = {
	{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 },
	{ 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 }, { 255, 128, 0 },
};

bool Matches( const RGBA8 &p, const Colour &c )
{
	const auto close = []( int a, int b ) { return a > b - 40 && a < b + 40; };
	return close(p.r, c.r) && close(p.g, c.g) && close(p.b, c.b);
}

// Horizontal band b as two triangles whose corners sit at both ends of a
// vertexCount-long array, so the draw references (and QindieGL copies) all of
// it. The two triangles are repeated so the draw has indexCount indices.
void DrawBand( int band, std::vector<float> &vertices, int vertexCount, int indexCount, GLenum indexType )
{
	const float y0 = -1.0f + 2.0f * band / kBands;
	const float y1 = y0 + 2.0f / kBands;
	const int last = vertexCount - 1;
	const float corners[4][2] = { { -1, y0 }, { 1, y0 }, { -1, y1 }, { 1, y1 } };
	const int slots[4] = { 0, 1, last - 1, last };
	for (int i = 0; i < 4; ++i) {
		vertices[slots[i] * 3 + 0] = corners[i][0];
		vertices[slots[i] * 3 + 1] = corners[i][1];
		vertices[slots[i] * 3 + 2] = 0.0f;
	}
	const GLuint quad[6] = { 0, 1, GLuint(last - 1), GLuint(last - 1), 1, GLuint(last) };
	gl.Color4ub(kColours[band].r, kColours[band].g, kColours[band].b, 255);
	gl.VertexPointer(3, GL_FLOAT, 0, vertices.data());
	if (indexType == GL_UNSIGNED_INT) {
		std::vector<GLuint> indices(indexCount);
		for (int i = 0; i < indexCount; ++i) indices[i] = quad[i % 6];
		gl.DrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, indices.data());
	} else {
		std::vector<GLushort> indices(indexCount);
		for (int i = 0; i < indexCount; ++i) indices[i] = static_cast<GLushort>(quad[i % 6]);
		gl.DrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, indices.data());
	}
}

void RunFrame( const char *label, const int *vertexCounts, int indexCount, GLenum indexType )
{
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
	gl.EnableClientState(GL_VERTEX_ARRAY);

	std::vector<float> vertices;
	for (int band = 0; band < kBands; ++band) {
		vertices.assign(static_cast<size_t>(vertexCounts[band]) * 3, 0.0f);
		DrawBand(band, vertices, vertexCounts[band], indexCount, indexType);
	}

	RGBA8 pixels[kBands];
	for (int band = 0; band < kBands; ++band)
		pixels[band] = Harness_ReadPixel(Harness_Width() / 2, Harness_Height() * (2 * band + 1) / (2 * kBands));
	Harness_Swap();
	gl.DisableClientState(GL_VERTEX_ARRAY);
	gl.Color4ub(255, 255, 255, 255);

	for (int band = 0; band < kBands; ++band) {
		const Colour &c = kColours[band];
		CHECK(Matches(pixels[band], c), "%s: band %d is (%d,%d,%d), expected (%d,%d,%d)", label, band,
			pixels[band].r, pixels[band].g, pixels[band].b, c.r, c.g, c.b);
	}
}

} // namespace

void do_streaming_tests()
{
	// 16-bit indices: 65536 vertices of 16 bytes (position and colour) are 1 MB
	// per draw and 180000 indices 350 KB, so the 8 MB vertex ring and the 2 MB
	// index ring both wrap within a frame.
	int counts16[kBands];
	for (int band = 0; band < kBands; ++band) counts16[band] = 65536;
	RunFrame("streaming, 16-bit indices", counts16, 180000, GL_UNSIGNED_SHORT);
	RunFrame("streaming, 16-bit indices, second frame", counts16, 180000, GL_UNSIGNED_SHORT);

	// 32-bit indices, with one draw larger than the initial vertex ring.
	int counts32[kBands];
	for (int band = 0; band < kBands; ++band) counts32[band] = 200000;
	counts32[3] = 700000;
	RunFrame("streaming, 32-bit indices, growing ring", counts32, 6, GL_UNSIGNED_INT);
}
