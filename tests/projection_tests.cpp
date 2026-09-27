// ProjectionFix for projections multiplied onto identity (Phase G). DS2 sets
// its perspective with glLoadIdentity + glMultMatrixf, which bypassed the
// OpenGL-to-Direct3D depth conversion: D3D clips at NDC z = 0 instead of -1,
// so the effective near plane was about twice the GL one (20 instead of 10).

#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

// GL column-major perspective with DS2's planes: 90 degrees, aspect 1, near 10, far 6000.
const float kNear = 10.0f, kFar = 6000.0f;
const float kPerspective[16] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, -(kFar + kNear) / (kFar - kNear), -1,
	0, 0, -2.0f * kFar * kNear / (kFar - kNear), 0 };

// GL glOrtho(-1, 1, -1, 1, -10, 10), the form of DS2's shadow silhouette
// projection (near -10000, far 10000): geometry behind the eye is visible.
const float kOrtho[16] = {
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, -0.1f, 0,
	0, 0, 0, 1 };

// Draws a green quad at eye-space z covering the centre of the viewport and
// reports whether the centre pixel is green, i.e. the quad was not clipped.
bool CentreQuadVisible( float eyeZ, float halfSize )
{
	const float quad[12] = {
		-halfSize, -halfSize, eyeZ,  halfSize, -halfSize, eyeZ,
		-halfSize,  halfSize, eyeZ,  halfSize,  halfSize, eyeZ };
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.Color4ub(0, 255, 0, 255);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, quad);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	gl.DisableClientState(GL_VERTEX_ARRAY);
	const RGBA8 centre = Harness_ReadCenter();
	Harness_Swap();
	return centre.g > 200 && centre.r < 40;
}

bool PerspectiveQuadVisible( float eyeDepth )
{
	return CentreQuadVisible(-eyeDepth, 0.5f * eyeDepth);
}

void MultiplyOntoIdentity( const float *matrix )
{
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MultMatrixf(matrix);
}

void ReadProjection( float *matrix )
{
	gl.GetFloatv(GL_PROJECTION_MATRIX, matrix);
}

} // namespace

void do_projection_tests( bool projectionFix )
{
	gl.Disable(GL_DEPTH_TEST);
	MultiplyOntoIdentity(kPerspective);

	if (projectionFix) {
		CHECK(PerspectiveQuadVisible(15.0f), "glMultMatrixf perspective: eye depth 15 with near 10 is visible");
		CHECK(!PerspectiveQuadVisible(9.0f), "glMultMatrixf perspective: eye depth 9 is clipped by near 10");
		CHECK(PerspectiveQuadVisible(5990.0f), "glMultMatrixf perspective: eye depth 5990 is visible");
		CHECK(!PerspectiveQuadVisible(6010.0f), "glMultMatrixf perspective: eye depth 6010 is clipped by far 6000");

		// A multiplication onto identity converts exactly as a load does.
		float multiplied[16], loaded[16];
		ReadProjection(multiplied);
		gl.MatrixMode(GL_PROJECTION);
		gl.LoadMatrixf(kPerspective);
		ReadProjection(loaded);
		CHECK(loaded[10] != kPerspective[10], "glLoadMatrixf converted the projection (C %g)", loaded[10]);
		CHECK(!memcmp(multiplied, loaded, sizeof(loaded)),
			"glLoadIdentity + glMultMatrixf and glLoadMatrixf give the same projection (C %g/%g, D %g/%g)",
			multiplied[10], loaded[10], multiplied[14], loaded[14]);

		// Multiplying onto a projection that is no longer identity must not
		// convert again.
		static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		MultiplyOntoIdentity(kPerspective);
		gl.MultMatrixf(identity);
		float twice[16];
		ReadProjection(twice);
		CHECK(!memcmp(twice, loaded, sizeof(loaded)),
			"a second glMultMatrixf leaves the converted projection alone (C %g, expected %g)", twice[10], loaded[10]);
		CHECK(PerspectiveQuadVisible(15.0f), "eye depth 15 still visible after a second glMultMatrixf");

		MultiplyOntoIdentity(kOrtho);
		CHECK(CentreQuadVisible(5.0f, 0.5f), "glMultMatrixf ortho with near -10: eye z +5 is visible");
		CHECK(CentreQuadVisible(-5.0f, 0.5f), "glMultMatrixf ortho with far 10: eye z -5 is visible");
		CHECK(!CentreQuadVisible(15.0f, 0.5f), "glMultMatrixf ortho with near -10: eye z +15 is clipped");
	} else {
		// Without ProjectionFix the GL matrix reaches D3D unchanged and D3D
		// clips at NDC z = 0: the effective near plane is 2*n*f/(n+f), about 20.
		CHECK(!PerspectiveQuadVisible(15.0f), "without ProjectionFix eye depth 15 is clipped (legacy behaviour)");
		CHECK(PerspectiveQuadVisible(25.0f), "without ProjectionFix eye depth 25 is visible");
	}

	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
}
