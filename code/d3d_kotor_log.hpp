#ifndef QINDIEGL_D3D_KOTOR_LOG_H
#define QINDIEGL_D3D_KOTOR_LOG_H

// KotOR-specific diagnostic instrumentation (Phase 0)
// Controlled by INI setting: kotor_debug_log=1

void kotor_log_init();
bool kotor_log_enabled();

// Step 0.1: Matrix operation logging
void kotor_log_load_identity(GLenum matrixMode);
void kotor_log_mult_matrix(GLenum matrixMode, const float *m);
void kotor_log_translate(GLenum matrixMode, float x, float y, float z);
void kotor_log_rotate(GLenum matrixMode, float angle, float x, float y, float z);
void kotor_log_scale(GLenum matrixMode, float x, float y, float z);
void kotor_log_push_matrix(GLenum matrixMode, int depth);
void kotor_log_pop_matrix(GLenum matrixMode, int depth);
void kotor_log_set_transform(const char *label, const float *m);

// Step 0.2: Draw call tracking
void kotor_log_draw_call_im(GLenum primType, int vertexCount, DWORD fvf);
void kotor_log_draw_call_va(GLenum primType, int indexCount, int vertexCount);
void kotor_log_frame_end();
bool kotor_log_dump_requested();
void kotor_log_request_dump();

// Step 0.3: Texture hash logging
void kotor_log_set_texture(int stage, void *d3dTexPtr, int glTexName);

#endif
