#include "gl/GL.h"

#include "util/Log.h"

#define FAM_GL_DEFINE(ret, name, params) ret(FAM_GLAPI* fam_gl##name) params = nullptr;
FAM_GL_FUNCTIONS(FAM_GL_DEFINE)
#undef FAM_GL_DEFINE

namespace fam::gl {

bool LoadFunctions(void* (*getProcAddress)(const char*)) {
    int missing = 0;

#define FAM_GL_LOAD(ret, name, params)                                              \
    fam_gl##name = reinterpret_cast<ret(FAM_GLAPI*) params>(getProcAddress("gl" #name)); \
    if (!fam_gl##name) {                                                            \
        LogError("OpenGL: missing entry point gl" #name);                           \
        ++missing;                                                                  \
    }

    FAM_GL_FUNCTIONS(FAM_GL_LOAD)
#undef FAM_GL_LOAD

    return missing == 0;
}

}  // namespace fam::gl
