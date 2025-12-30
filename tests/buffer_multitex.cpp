#include <string.h>

#include "tests.h"

#include "../code/d3d_extension.hpp"
#include "../code/d3d_global.hpp"
#include "../code/d3d_state.hpp"
#include "../code/gl_headers/gl.h"
#include "../code/gl_headers/glext.h"

static void reset_texcoord_state()
{
    memset(&D3DState.CurrentState, 0, sizeof(D3DState.CurrentState));
    D3DState.TransformState.texcoordFixEnabled = FALSE;
    D3DGlobal.lastError = S_OK;
}

static void reset_buffer_state()
{
    D3DGlobal.lastError = S_OK;
}

static void assert_gl_error(GLenum expected)
{
    GLenum err = glGetError();
    assert(err == expected);
}

static void do_buffer_tests()
{
    unsigned char temp[4] = { 0 };

    reset_buffer_state();
    glGetBufferSubDataARB(GL_TEXTURE_2D, 0, 4, temp);
    assert_gl_error(GL_INVALID_ENUM);

    reset_buffer_state();
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 4, temp);
    assert_gl_error(GL_INVALID_OPERATION);

    GLuint buffer = 0;
    unsigned char source[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    glGenBuffersARB(1, &buffer);
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, buffer);
    glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(source), source, GL_STATIC_DRAW_ARB);
    assert_gl_error(GL_NO_ERROR);

    reset_buffer_state();
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 4, nullptr);
    assert_gl_error(GL_INVALID_OPERATION);

    reset_buffer_state();
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 6, 4, temp);
    assert_gl_error(GL_INVALID_OPERATION);

    reset_buffer_state();
    unsigned char sentinel = 0xAA;
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, 0, &sentinel);
    assert_gl_error(GL_NO_ERROR);
    assert(sentinel == 0xAA);

    reset_buffer_state();
    unsigned char output[4] = { 0 };
    glGetBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 2, 4, output);
    assert_gl_error(GL_NO_ERROR);
    assert(output[0] == source[2]);
    assert(output[1] == source[3]);
    assert(output[2] == source[4]);
    assert(output[3] == source[5]);

    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glDeleteBuffersARB(1, &buffer);
    assert_gl_error(GL_NO_ERROR);
}

static void do_multitex_tests()
{
    D3DGlobal.maxActiveTMU = 2;

    reset_texcoord_state();
    D3DState.CurrentState.currentTexCoord[0][0] = 0.25f;
    glMultiTexCoord1dEXT(GL_TEXTURE_2D, 2.5);
    assert_gl_error(GL_INVALID_ENUM);
    assert(D3DState.CurrentState.currentTexCoord[0][0] == 0.25f);

    reset_texcoord_state();
    glMultiTexCoord1dEXT(GL_TEXTURE0_ARB, 2.5);
    assert_gl_error(GL_NO_ERROR);
    assert(D3DState.CurrentState.currentTexCoord[0][0] == 2.5f);
    assert(D3DState.CurrentState.currentTexCoord[0][1] == 0.0f);
    assert(D3DState.CurrentState.currentTexCoord[0][2] == 0.0f);
    assert(D3DState.CurrentState.currentTexCoord[0][3] == 1.0f);

    reset_texcoord_state();
    glMultiTexCoord4sdARB(GL_TEXTURE2_ARB, 1, 2, 3, 4);
    assert_gl_error(GL_INVALID_ENUM);

    reset_texcoord_state();
    glMultiTexCoord4sdARB(GL_TEXTURE1_ARB, 1, -2, 3, -4);
    assert_gl_error(GL_NO_ERROR);
    assert(D3DState.CurrentState.currentTexCoord[1][0] == 1.0f);
    assert(D3DState.CurrentState.currentTexCoord[1][1] == -2.0f);
    assert(D3DState.CurrentState.currentTexCoord[1][2] == 3.0f);
    assert(D3DState.CurrentState.currentTexCoord[1][3] == -4.0f);
    assert((D3DState.CurrentState.isSet.bits.texcoord & (0x3 << 2)) == (0x3 << 2));
}

void do_buffer_multitex_tests()
{
    do_buffer_tests();
    do_multitex_tests();
}
