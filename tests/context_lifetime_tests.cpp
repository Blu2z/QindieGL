// GL calls without a context. DS2 deletes its textures from global destructors
// when the process exits, after wglDeleteContext destroyed QindieGL's object
// tables. The system opengl32 ignores GL calls made without a current
// context; QindieGL dereferenced its freed texture table. The GLIntercept
// loader hid the crash because it does not forward such calls.

#include <stdio.h>
#include <string>

#include "tests.h"
#include "gl_harness.h"

#define CHECK(condition, ...) do { \
		char checkMessage_[320]; \
		sprintf_s(checkMessage_, __VA_ARGS__); \
		xassert_str(!!(condition), checkMessage_, __func__, (unsigned)__LINE__, __FILE__); \
	} while (0)

namespace {
	GLuint gTexture = 0, gBuffer = 0;
}

// Runs while the context exists: objects the late calls refer to.
void prepare_calls_without_context()
{
	gl.GenTextures(1, &gTexture);
	gl.BindTexture(GL_TEXTURE_2D, gTexture);
	const unsigned char texel[4] = { 255, 255, 255, 255 };
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
	if (gl.GenBuffersARB)
		gl.GenBuffersARB(1, &gBuffer);
}

// Runs after Harness_Shutdown deleted the context, as DS2's destructors do.
void do_calls_without_context_tests()
{
	gl.DeleteTextures(1, &gTexture);
	gl.BindTexture(GL_TEXTURE_2D, gTexture);
	GLuint name = 0;
	gl.GenTextures(1, &name);
	if (gl.DeleteBuffersARB)
		gl.DeleteBuffersARB(1, &gBuffer);
	CHECK(name == 0, "glGenTextures without a context leaves the names alone (%u)", name);
}

// Runs in the parent: the ignored calls are listed in the session summary.
void check_calls_without_context_log( const std::string &logPath )
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
		"GL calls without a context (ignored):",
		"  glBindTexture: 1",
		"  glDeleteTextures: 1",
		"  glGenTextures: 1",
	};
	for (const char *pattern : patterns)
		CHECK(log.find(pattern) != std::string::npos, "%s: session summary contains \"%s\"", logPath.c_str(), pattern);
}
