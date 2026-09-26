// YAE camera split (yae_camera_split, Phase G). DS2 multiplies its camera into
// the modelview at stack depth 0 and draws objects on top of it; the split
// sends that camera as D3DTS_VIEW and the rest as D3DTS_WORLD, with lights and
// clip planes re-expressed for the view. This test renders a frame shaped like
// DS2's (orthographic silhouette pass, rotation-only sky, world camera with
// depth-0 and object draws, lights, a clip plane, a second camera, HUD) and
// checks it in configurations with and without the split. The parent then
// compares both framebuffers: the split must not change the output.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {

// Column-major 4x4 matrices, as OpenGL takes them.
struct Mat { float m[16]; };

Mat Identity()
{
	Mat r = {};
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
	return r;
}

Mat Multiply( const Mat &a, const Mat &b )
{
	Mat r = {};
	for (int column = 0; column < 4; ++column)
		for (int row = 0; row < 4; ++row)
			for (int k = 0; k < 4; ++k)
				r.m[column * 4 + row] += a.m[k * 4 + row] * b.m[column * 4 + k];
	return r;
}

Mat Translation( float x, float y, float z )
{
	Mat r = Identity();
	r.m[12] = x; r.m[13] = y; r.m[14] = z;
	return r;
}

Mat RotationX( float degrees )
{
	const float a = degrees * 3.14159265f / 180.0f;
	Mat r = Identity();
	r.m[5] = cosf(a); r.m[6] = sinf(a); r.m[9] = -sinf(a); r.m[10] = cosf(a);
	return r;
}

Mat RotationY( float degrees )
{
	const float a = degrees * 3.14159265f / 180.0f;
	Mat r = Identity();
	r.m[0] = cosf(a); r.m[2] = -sinf(a); r.m[8] = sinf(a); r.m[10] = cosf(a);
	return r;
}

// Inverse of a rotation followed by a translation.
Mat RigidInverse( const Mat &a )
{
	Mat r = Identity();
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			r.m[column * 4 + row] = a.m[row * 4 + column];
	for (int row = 0; row < 3; ++row)
		r.m[12 + row] = -(r.m[row] * a.m[12] + r.m[4 + row] * a.m[13] + r.m[8 + row] * a.m[14]);
	return r;
}

Mat RotationPart( const Mat &a )
{
	Mat r = a;
	r.m[12] = r.m[13] = r.m[14] = 0.0f;
	return r;
}

void TransformPoint( const Mat &a, const float *in, float *out )
{
	for (int row = 0; row < 3; ++row)
		out[row] = a.m[row] * in[0] + a.m[4 + row] * in[1] + a.m[8 + row] * in[2] + a.m[12 + row];
}

void TransformDirection( const Mat &a, const float *in, float *out )
{
	for (int row = 0; row < 3; ++row)
		out[row] = a.m[row] * in[0] + a.m[4 + row] * in[1] + a.m[8 + row] * in[2];
}

const float kNear = 2.0f, kFar = 300.0f;

// A quad in the eye-space plane z = eyeZ, drawn through toEye^-1 so that it
// lands at the given eye-space rectangle, facing the viewer.
struct Quad
{
	float positions[12];
	float normals[12];
};

Quad EyeQuad( const Mat &objectToEye, float x0, float y0, float x1, float y1, float eyeZ )
{
	const Mat eyeToObject = RigidInverse(objectToEye);
	const float corners[4][3] = { { x0, y0, eyeZ }, { x1, y0, eyeZ }, { x0, y1, eyeZ }, { x1, y1, eyeZ } };
	const float towardViewer[3] = { 0.0f, 0.0f, 1.0f };
	float normal[3];
	TransformDirection(eyeToObject, towardViewer, normal);
	Quad quad;
	for (int i = 0; i < 4; ++i) {
		TransformPoint(eyeToObject, corners[i], &quad.positions[i * 3]);
		memcpy(&quad.normals[i * 3], normal, sizeof(normal));
	}
	return quad;
}

void DrawQuad( const Quad &quad, unsigned char r, unsigned char g, unsigned char b )
{
	gl.Color4ub(r, g, b, 255);
	gl.EnableClientState(GL_VERTEX_ARRAY);
	gl.EnableClientState(GL_NORMAL_ARRAY);
	gl.VertexPointer(3, GL_FLOAT, 0, quad.positions);
	gl.NormalPointer(GL_FLOAT, 0, quad.normals);
	gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	gl.DisableClientState(GL_NORMAL_ARRAY);
	gl.DisableClientState(GL_VERTEX_ARRAY);
}

void LoadCamera( const Mat &view )
{
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.MultMatrixf(view.m);
}

float gAspect = 1.0f;

RGBA8 ReadEye( float x, float y, float eyeZ )
{
	const float ndcX = x / gAspect / -eyeZ, ndcY = y / -eyeZ;
	return Harness_ReadPixel(static_cast<int>((ndcX * 0.5f + 0.5f) * Harness_Width()),
		static_cast<int>((ndcY * 0.5f + 0.5f) * Harness_Height()));
}

RGBA8 ReadNdc( float x, float y )
{
	return Harness_ReadPixel(static_cast<int>((x * 0.5f + 0.5f) * Harness_Width()),
		static_cast<int>((y * 0.5f + 0.5f) * Harness_Height()));
}

bool Near( int value, int expected, int tolerance )
{
	return value >= expected - tolerance && value <= expected + tolerance;
}

} // namespace

void do_camera_split_tests()
{
	const int width = Harness_Width(), height = Harness_Height();
	gAspect = static_cast<float>(width) / static_cast<float>(height);

	const Mat perspective = { {
		1.0f / gAspect, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, (kFar + kNear) / (kNear - kFar), -1,
		0, 0, 2.0f * kFar * kNear / (kNear - kFar), 0 } };
	// glOrtho(-2, 2, -2, 2, -10, 10): DS2's silhouette projections also see
	// casters behind the light view.
	const Mat ortho = { { 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, -0.1f, 0, 0, 0, 0, 1 } };

	const Mat cameraToWorld = Multiply(Translation(10.0f, 5.0f, 20.0f), Multiply(RotationY(30.0f), RotationX(-10.0f)));
	const Mat view = RigidInverse(cameraToWorld);
	const Mat skyView = RotationPart(view);
	const Mat lightView = RigidInverse(Multiply(Translation(3.0f, -2.0f, 1.0f), RotationX(40.0f)));
	const Mat secondView = RigidInverse(Multiply(Translation(-4.0f, 1.0f, 6.0f), RotationY(-50.0f)));

	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_LIGHTING);

	// 1. Shadow silhouette pass: its own orthographic light view, drawn
	//    behind that view (eye z +3) in the lower-left corner.
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MultMatrixf(ortho.m);
	LoadCamera(lightView);
	gl.PushMatrix();
	const Mat silhouetteObject = Multiply(RigidInverse(lightView), Translation(-1.5f, -1.5f, 3.0f));
	gl.MultMatrixf(silhouetteObject.m);
	DrawQuad(EyeQuad(Multiply(lightView, silhouetteObject), -1.8f, -1.8f, -1.2f, -1.2f, 3.0f), 255, 0, 255);
	gl.PopMatrix();

	// 2. Sky: rotation-only camera, an object at the origin, upper half.
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MultMatrixf(perspective.m);
	LoadCamera(skyView);
	gl.DepthMask(GL_FALSE);
	gl.PushMatrix();
	gl.MultMatrixf(Identity().m);
	DrawQuad(EyeQuad(skyView, -200.0f, 0.0f, 200.0f, 200.0f, -60.0f), 0, 0, 255);
	gl.PopMatrix();
	gl.DepthMask(GL_TRUE);

	// 3. World camera. Lights are specified in world space under the camera.
	LoadCamera(view);
	const float lightEye[3] = { 0.8f, 0.0f, 0.6f };
	float lightWorld[4] = { 0, 0, 0, 0 };
	TransformDirection(cameraToWorld, lightEye, lightWorld);
	gl.Lightfv(GL_LIGHT0, GL_POSITION, lightWorld);
	const float pointEye[3] = { 0.0f, -1.5f, -3.0f };
	float pointWorld[4] = { 0, 0, 0, 1 };
	TransformPoint(cameraToWorld, pointEye, pointWorld);
	const float white[4] = { 1, 1, 1, 1 };
	gl.Lightfv(GL_LIGHT1, GL_POSITION, pointWorld);
	gl.Lightfv(GL_LIGHT1, GL_DIFFUSE, white);

	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	// (a) static geometry at stack depth 0, in world coordinates.
	DrawQuad(EyeQuad(view, -3.0f, 0.5f, -1.0f, 2.5f, -5.0f), 255, 255, 255);

	float modelview[16] = {};
	gl.GetFloatv(GL_MODELVIEW_MATRIX, modelview);
	float largest = 0.0f;
	for (int i = 0; i < 16; ++i)
		largest = fmaxf(largest, fabsf(modelview[i] - view.m[i]));
	CHECK(largest < 1e-4f, "glGetFloatv(GL_MODELVIEW_MATRIX) returns the camera (largest difference %g)", largest);

	// (b) an object: glPushMatrix + glMultMatrixf(object).
	gl.PushMatrix();
	const Mat object = Multiply(cameraToWorld, Multiply(Translation(2.0f, 1.5f, -5.0f), RotationY(20.0f)));
	gl.MultMatrixf(object.m);
	DrawQuad(EyeQuad(Multiply(view, object), 1.0f, 0.5f, 3.0f, 2.5f, -5.0f), 255, 255, 255);
	gl.PopMatrix();

	// (c) positional light only.
	gl.Disable(GL_LIGHT0);
	gl.Enable(GL_LIGHT1);
	DrawQuad(EyeQuad(view, -1.0f, -2.5f, 1.0f, -0.5f, -5.0f), 255, 255, 255);
	gl.Disable(GL_LIGHT1);
	gl.Disable(GL_LIGHTING);

	// (d) a clip plane keeping eye-space x <= 0, given in world coordinates.
	double plane[4];
	for (int j = 0; j < 4; ++j)
		plane[j] = -view.m[j * 4];
	gl.ClipPlane(GL_CLIP_PLANE0, plane);
	gl.Enable(GL_CLIP_PLANE0);
	DrawQuad(EyeQuad(view, -4.0f, 3.0f, 4.0f, 4.5f, -5.0f), 255, 255, 0);
	gl.Disable(GL_CLIP_PLANE0);

	// 4. A second camera; LIGHT0 keeps its eye-space direction (GL semantics).
	LoadCamera(secondView);
	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	DrawQuad(EyeQuad(secondView, 3.5f, -2.5f, 4.5f, -0.5f, -5.0f), 255, 255, 255);
	gl.Disable(GL_LIGHT0);
	gl.Disable(GL_LIGHTING);

	// 5. HUD: identity projection and modelview, lower-right corner.
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	DrawQuad(EyeQuad(Identity(), 0.6f, -0.9f, 0.9f, -0.6f, 0.0f), 255, 255, 255);

	// GL lighting: 0.2 * 0.2 ambient plus 0.8 material diffuse times N.L. The
	// point light is 1 unit off each corner in x and y and 2 in front: N.L = 2/sqrt(6).
	const int directional = static_cast<int>((0.04f + 0.8f * 0.6f) * 255.0f + 0.5f);
	const int positional = static_cast<int>((0.04f + 0.8f * (2.0f / sqrtf(6.0f))) * 255.0f + 0.5f);
	const RGBA8 silhouette = ReadNdc(-0.75f, -0.75f);
	CHECK(silhouette.r > 200 && silhouette.g < 40 && silhouette.b > 200,
		"silhouette pass quad behind its orthographic view visible: (%d,%d,%d)", silhouette.r, silhouette.g, silhouette.b);
	const RGBA8 sky = ReadEye(0.0f, 4.8f, -5.0f);
	CHECK(sky.b > 200 && sky.r < 40, "rotation-only sky camera: (%d,%d,%d)", sky.r, sky.g, sky.b);
	const RGBA8 world = ReadEye(-2.0f, 1.5f, -5.0f);
	CHECK(Near(world.g, directional, 4), "directional light on depth-0 geometry: green %d, expected %d", world.g, directional);
	const RGBA8 objectPixel = ReadEye(2.0f, 1.5f, -5.0f);
	CHECK(Near(objectPixel.g, directional, 4), "directional light on an object: green %d, expected %d",
		objectPixel.g, directional);
	const RGBA8 point = ReadEye(0.0f, -1.5f, -5.0f);
	CHECK(Near(point.g, positional, 4), "positional light: green %d, expected %d", point.g, positional);
	const RGBA8 kept = ReadEye(-2.0f, 3.75f, -5.0f), clipped = ReadEye(2.0f, 3.75f, -5.0f);
	CHECK(kept.r > 200 && kept.g > 200 && kept.b < 40, "clip plane keeps eye x < 0: (%d,%d,%d)", kept.r, kept.g, kept.b);
	CHECK(clipped.b > 200 && clipped.r < 40, "clip plane removes eye x > 0: (%d,%d,%d)", clipped.r, clipped.g, clipped.b);
	const RGBA8 second = ReadEye(4.0f, -1.5f, -5.0f);
	CHECK(Near(second.g, directional, 4), "second camera keeps the eye-space light: green %d, expected %d",
		second.g, directional);
	const RGBA8 hud = ReadNdc(0.75f, -0.75f);
	CHECK(hud.r > 200 && hud.g > 200 && hud.b > 200, "HUD quad: (%d,%d,%d)", hud.r, hud.g, hud.b);

	// The whole frame, compared by the parent across configurations.
	std::string pixels(static_cast<size_t>(width) * height * 4, '\0');
	gl.Finish();
	gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
	FILE *file = nullptr;
	if (!fopen_s(&file, "camera_split_frame.bin", "wb") && file) {
		fwrite(&width, sizeof(width), 1, file);
		fwrite(&height, sizeof(height), 1, file);
		fwrite(pixels.data(), 1, pixels.size(), file);
		fclose(file);
	}
	Harness_Swap();

	// GL defaults for the tests that follow (the modelview is identity here).
	const float defaultPosition[4] = { 0, 0, 1, 0 }, black[4] = { 0, 0, 0, 1 };
	gl.Lightfv(GL_LIGHT0, GL_POSITION, defaultPosition);
	gl.Lightfv(GL_LIGHT1, GL_POSITION, defaultPosition);
	gl.Lightfv(GL_LIGHT1, GL_DIFFUSE, black);
}

// Runs in the parent. The census does not depend on the split: the sky is a
// rotation-only camera with the main camera's rotation, the world camera
// covers depth-0 and object draws, and the second camera is another view.
// With the split, the first camera D3D receives is the silhouette light view.
void check_camera_split_log( const std::string &logPath, bool split )
{
	std::string log;
	FILE *file = nullptr;
	if (!fopen_s(&file, logPath.c_str(), "rb") && file) {
		char buffer[4096];
		size_t read;
		while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) log.append(buffer, read);
		fclose(file);
	}
	const char *patterns[] = {
		"\"P2.0-300 rot\" frames=1 draws=1 (depth0 0, objects 1, eye-space 0) segments: =0 ~1 !0 ?0",
		"\"P2.0-300 rigid\" frames=1 draws=5 (depth0 4, objects 1, eye-space 0) segments: =1 ~0 !1 ?0",
	};
	for (const char *pattern : patterns)
		CHECK(log.find(pattern) != std::string::npos, "%s: camera census contains %s", logPath.c_str(), pattern);
	const char *view = "[CAMERA_SPLIT] first camera sent as D3DTS_VIEW: pos=(3.0,-2.0,1.0)";
	if (split)
		CHECK(log.find(view) != std::string::npos, "%s: contains %s", logPath.c_str(), view);
	else
		CHECK(log.find("[CAMERA_SPLIT]") == std::string::npos, "%s: no camera split without the setting", logPath.c_str());
}

// Runs in the parent: the frame must be identical with and without the split.
void compare_camera_split_frames( const std::string &withoutSplit, const std::string &withSplit )
{
	std::string frames[2];
	const std::string *paths[2] = { &withoutSplit, &withSplit };
	for (int i = 0; i < 2; ++i) {
		FILE *file = nullptr;
		if (!fopen_s(&file, paths[i]->c_str(), "rb") && file) {
			char buffer[4096];
			size_t read;
			while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) frames[i].append(buffer, read);
			fclose(file);
		}
	}
	CHECK(!frames[0].empty() && frames[0].size() == frames[1].size(),
		"camera split frames readable and equally sized (%u, %u bytes)",
		static_cast<unsigned int>(frames[0].size()), static_cast<unsigned int>(frames[1].size()));
	if (frames[0].empty() || frames[0].size() != frames[1].size())
		return;
	unsigned int differing = 0, largest = 0;
	for (size_t i = 8; i + 3 < frames[0].size(); i += 4) {
		unsigned int delta = 0;
		for (int c = 0; c < 3; ++c) {
			const int d = abs(static_cast<unsigned char>(frames[0][i + c]) - static_cast<unsigned char>(frames[1][i + c]));
			delta = d > static_cast<int>(delta) ? static_cast<unsigned int>(d) : delta;
		}
		if (delta) ++differing;
		largest = delta > largest ? delta : largest;
	}
	CHECK(differing == 0, "the camera split leaves the frame unchanged: %u pixels differ, largest channel difference %u",
		differing, largest);
}
