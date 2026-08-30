#define GLI_INCLUDE_QINDIEGL_YAE

// GLIntercept definition overlay for QindieGL/YAE compatibility entry points.
// Keep the official corpus intact and add only non-standard names exposed by
// this wrapper.

#include "gliIncludes.h"

// Historical DS2/QindieGL spelling retained for YAE compatibility. The
// implementation accepts four GLshort coordinates despite the extra 'd'.
void glMultiTexCoord4sdARB (GLenum[Main] target, GLshort s, GLshort t, GLshort r, GLshort q );
