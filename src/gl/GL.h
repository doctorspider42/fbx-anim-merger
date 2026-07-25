// Minimal OpenGL 3.3 core loader.
//
// We deliberately avoid glad/glew: they either need a code generator at configure
// time or drag in a build system we don't want. Everything below is loaded through
// glfwGetProcAddress, which on every platform also resolves the legacy GL 1.x
// entry points.
//
// NOTE: never include <GL/gl.h> in this project. Define GLFW_INCLUDE_NONE before
// including GLFW.
#pragma once

#include <cstddef>

// ---------------------------------------------------------------- GL types
typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef signed char   GLbyte;
typedef short         GLshort;
typedef int           GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int  GLuint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef float         GLclampf;
typedef double        GLdouble;
typedef char          GLchar;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;
typedef void          GLvoid;

#if defined(_WIN32)
#define FAM_GLAPI __stdcall
#else
#define FAM_GLAPI
#endif

// ------------------------------------------------------------ GL constants
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408
#define GL_CW                             0x0900
#define GL_CCW                            0x0901
#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_TEXTURE_2D                     0x0DE1
#define GL_DONT_CARE                      0x1100
#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_DEPTH_COMPONENT                0x1902
#define GL_RED                            0x1903
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_FILL                           0x1B02
#define GL_LINE                           0x1B01
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_REPEAT                         0x2901
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_TEXTURE0                       0x84C0
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_SRGB8_ALPHA8                   0x8C43
#define GL_FRAMEBUFFER_SRGB               0x8DB9
#define GL_UNIFORM_BUFFER                 0x8A11
#define GL_MAX_UNIFORM_BLOCK_SIZE         0x8A30
#define GL_INVALID_INDEX                  0xFFFFFFFFu
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_MULTISAMPLE                    0x809D
#define GL_TEXTURE_2D_MULTISAMPLE         0x9100
#define GL_FRAMEBUFFER                    0x8D40
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_RENDERBUFFER                   0x8D41
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_RGBA8                          0x8058
#define GL_NO_ERROR                       0
#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B

// --------------------------------------------------------- entry point list
// X(return type, name, parameter list)
#define FAM_GL_FUNCTIONS(X)                                                                                            \
    X(void,      Enable,                     (GLenum cap))                                                             \
    X(void,      Disable,                    (GLenum cap))                                                             \
    X(void,      Clear,                      (GLbitfield mask))                                                        \
    X(void,      ClearColor,                 (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                             \
    X(void,      Viewport,                   (GLint x, GLint y, GLsizei w, GLsizei h))                                 \
    X(void,      Scissor,                    (GLint x, GLint y, GLsizei w, GLsizei h))                                 \
    X(void,      DepthFunc,                  (GLenum func))                                                            \
    X(void,      DepthMask,                  (GLboolean flag))                                                         \
    X(void,      BlendFunc,                  (GLenum sfactor, GLenum dfactor))                                         \
    X(void,      CullFace,                   (GLenum mode))                                                            \
    X(void,      FrontFace,                  (GLenum mode))                                                            \
    X(void,      LineWidth,                  (GLfloat width))                                                          \
    X(void,      PolygonMode,                (GLenum face, GLenum mode))                                               \
    X(void,      PixelStorei,                (GLenum pname, GLint param))                                              \
    X(GLenum,    GetError,                   (void))                                                                   \
    X(const GLubyte*, GetString,             (GLenum name))                                                            \
    X(void,      GetIntegerv,                (GLenum pname, GLint* data))                                              \
    X(void,      GetFloatv,                  (GLenum pname, GLfloat* data))                                            \
    X(void,      DrawArrays,                 (GLenum mode, GLint first, GLsizei count))                                \
    X(void,      DrawElements,               (GLenum mode, GLsizei count, GLenum type, const void* indices))           \
    X(void,      GenTextures,                (GLsizei n, GLuint* textures))                                            \
    X(void,      BindTexture,                (GLenum target, GLuint texture))                                          \
    X(void,      DeleteTextures,             (GLsizei n, const GLuint* textures))                                      \
    X(void,      TexImage2D,                 (GLenum target, GLint level, GLint internalFormat, GLsizei width,         \
                                              GLsizei height, GLint border, GLenum format, GLenum type,                \
                                              const void* pixels))                                                     \
    X(void,      TexParameteri,              (GLenum target, GLenum pname, GLint param))                               \
    X(void,      TexParameterf,              (GLenum target, GLenum pname, GLfloat param))                             \
    X(void,      ActiveTexture,              (GLenum texture))                                                         \
    X(void,      GenerateMipmap,             (GLenum target))                                                          \
    X(void,      TexImage2DMultisample,      (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,     \
                                              GLsizei height, GLboolean fixedsamplelocations))                         \
    X(void,      GenBuffers,                 (GLsizei n, GLuint* buffers))                                             \
    X(void,      BindBuffer,                 (GLenum target, GLuint buffer))                                           \
    X(void,      BufferData,                 (GLenum target, GLsizeiptr size, const void* data, GLenum usage))         \
    X(void,      BufferSubData,              (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))      \
    X(void,      DeleteBuffers,              (GLsizei n, const GLuint* buffers))                                       \
    X(void,      BindBufferBase,             (GLenum target, GLuint index, GLuint buffer))                             \
    X(void,      GenVertexArrays,            (GLsizei n, GLuint* arrays))                                              \
    X(void,      BindVertexArray,            (GLuint array))                                                           \
    X(void,      DeleteVertexArrays,         (GLsizei n, const GLuint* arrays))                                        \
    X(void,      EnableVertexAttribArray,    (GLuint index))                                                           \
    X(void,      VertexAttribPointer,        (GLuint index, GLint size, GLenum type, GLboolean normalized,             \
                                              GLsizei stride, const void* pointer))                                    \
    X(void,      VertexAttribIPointer,       (GLuint index, GLint size, GLenum type, GLsizei stride,                   \
                                              const void* pointer))                                                    \
    X(GLuint,    CreateShader,               (GLenum type))                                                            \
    X(void,      ShaderSource,               (GLuint shader, GLsizei count, const GLchar* const* string,               \
                                              const GLint* length))                                                    \
    X(void,      CompileShader,              (GLuint shader))                                                          \
    X(void,      GetShaderiv,                (GLuint shader, GLenum pname, GLint* params))                             \
    X(void,      GetShaderInfoLog,           (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))       \
    X(void,      DeleteShader,               (GLuint shader))                                                          \
    X(GLuint,    CreateProgram,              (void))                                                                   \
    X(void,      AttachShader,               (GLuint program, GLuint shader))                                          \
    X(void,      LinkProgram,                (GLuint program))                                                         \
    X(void,      GetProgramiv,               (GLuint program, GLenum pname, GLint* params))                            \
    X(void,      GetProgramInfoLog,          (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog))      \
    X(void,      DeleteProgram,              (GLuint program))                                                         \
    X(void,      UseProgram,                 (GLuint program))                                                         \
    X(GLint,     GetUniformLocation,         (GLuint program, const GLchar* name))                                     \
    X(void,      Uniform1i,                  (GLint location, GLint v0))                                               \
    X(void,      Uniform1f,                  (GLint location, GLfloat v0))                                             \
    X(void,      Uniform2fv,                 (GLint location, GLsizei count, const GLfloat* value))                    \
    X(void,      Uniform3fv,                 (GLint location, GLsizei count, const GLfloat* value))                    \
    X(void,      Uniform4fv,                 (GLint location, GLsizei count, const GLfloat* value))                    \
    X(void,      UniformMatrix4fv,           (GLint location, GLsizei count, GLboolean transpose,                      \
                                              const GLfloat* value))                                                   \
    X(GLuint,    GetUniformBlockIndex,       (GLuint program, const GLchar* uniformBlockName))                         \
    X(void,      UniformBlockBinding,        (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding))   \
    X(void,      GenFramebuffers,            (GLsizei n, GLuint* framebuffers))                                        \
    X(void,      BindFramebuffer,            (GLenum target, GLuint framebuffer))                                      \
    X(void,      DeleteFramebuffers,         (GLsizei n, const GLuint* framebuffers))                                  \
    X(void,      FramebufferTexture2D,       (GLenum target, GLenum attachment, GLenum textarget, GLuint texture,      \
                                              GLint level))                                                            \
    X(GLenum,    CheckFramebufferStatus,     (GLenum target))                                                          \
    X(void,      GenRenderbuffers,           (GLsizei n, GLuint* renderbuffers))                                       \
    X(void,      BindRenderbuffer,           (GLenum target, GLuint renderbuffer))                                     \
    X(void,      DeleteRenderbuffers,        (GLsizei n, const GLuint* renderbuffers))                                 \
    X(void,      RenderbufferStorage,        (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))    \
    X(void,      RenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat,               \
                                              GLsizei width, GLsizei height))                                          \
    X(void,      FramebufferRenderbuffer,    (GLenum target, GLenum attachment, GLenum renderbuffertarget,             \
                                              GLuint renderbuffer))                                                    \
    X(void,      BlitFramebuffer,            (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0,         \
                                              GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))

#define FAM_GL_DECLARE(ret, name, params) extern ret(FAM_GLAPI* fam_gl##name) params;
FAM_GL_FUNCTIONS(FAM_GL_DECLARE)
#undef FAM_GL_DECLARE

namespace fam::gl {
// Returns false if a required entry point is missing.
bool LoadFunctions(void* (*getProcAddress)(const char*));
}  // namespace fam::gl

// Call sites keep the familiar spelling.
#define glEnable                          fam_glEnable
#define glDisable                         fam_glDisable
#define glClear                           fam_glClear
#define glClearColor                      fam_glClearColor
#define glViewport                        fam_glViewport
#define glScissor                         fam_glScissor
#define glDepthFunc                       fam_glDepthFunc
#define glDepthMask                       fam_glDepthMask
#define glBlendFunc                       fam_glBlendFunc
#define glCullFace                        fam_glCullFace
#define glFrontFace                       fam_glFrontFace
#define glLineWidth                       fam_glLineWidth
#define glPolygonMode                     fam_glPolygonMode
#define glPixelStorei                     fam_glPixelStorei
#define glGetError                        fam_glGetError
#define glGetString                       fam_glGetString
#define glGetIntegerv                     fam_glGetIntegerv
#define glGetFloatv                       fam_glGetFloatv
#define glDrawArrays                      fam_glDrawArrays
#define glDrawElements                    fam_glDrawElements
#define glGenTextures                     fam_glGenTextures
#define glBindTexture                     fam_glBindTexture
#define glDeleteTextures                  fam_glDeleteTextures
#define glTexImage2D                      fam_glTexImage2D
#define glTexParameteri                   fam_glTexParameteri
#define glTexParameterf                   fam_glTexParameterf
#define glActiveTexture                   fam_glActiveTexture
#define glGenerateMipmap                  fam_glGenerateMipmap
#define glTexImage2DMultisample           fam_glTexImage2DMultisample
#define glGenBuffers                      fam_glGenBuffers
#define glBindBuffer                      fam_glBindBuffer
#define glBufferData                      fam_glBufferData
#define glBufferSubData                   fam_glBufferSubData
#define glDeleteBuffers                   fam_glDeleteBuffers
#define glBindBufferBase                  fam_glBindBufferBase
#define glGenVertexArrays                 fam_glGenVertexArrays
#define glBindVertexArray                 fam_glBindVertexArray
#define glDeleteVertexArrays              fam_glDeleteVertexArrays
#define glEnableVertexAttribArray         fam_glEnableVertexAttribArray
#define glVertexAttribPointer             fam_glVertexAttribPointer
#define glVertexAttribIPointer            fam_glVertexAttribIPointer
#define glCreateShader                    fam_glCreateShader
#define glShaderSource                    fam_glShaderSource
#define glCompileShader                   fam_glCompileShader
#define glGetShaderiv                     fam_glGetShaderiv
#define glGetShaderInfoLog                fam_glGetShaderInfoLog
#define glDeleteShader                    fam_glDeleteShader
#define glCreateProgram                   fam_glCreateProgram
#define glAttachShader                    fam_glAttachShader
#define glLinkProgram                     fam_glLinkProgram
#define glGetProgramiv                    fam_glGetProgramiv
#define glGetProgramInfoLog               fam_glGetProgramInfoLog
#define glDeleteProgram                   fam_glDeleteProgram
#define glUseProgram                      fam_glUseProgram
#define glGetUniformLocation              fam_glGetUniformLocation
#define glUniform1i                       fam_glUniform1i
#define glUniform1f                       fam_glUniform1f
#define glUniform2fv                      fam_glUniform2fv
#define glUniform3fv                      fam_glUniform3fv
#define glUniform4fv                      fam_glUniform4fv
#define glUniformMatrix4fv                fam_glUniformMatrix4fv
#define glGetUniformBlockIndex            fam_glGetUniformBlockIndex
#define glUniformBlockBinding             fam_glUniformBlockBinding
#define glGenFramebuffers                 fam_glGenFramebuffers
#define glBindFramebuffer                 fam_glBindFramebuffer
#define glDeleteFramebuffers              fam_glDeleteFramebuffers
#define glFramebufferTexture2D            fam_glFramebufferTexture2D
#define glCheckFramebufferStatus          fam_glCheckFramebufferStatus
#define glGenRenderbuffers                fam_glGenRenderbuffers
#define glBindRenderbuffer                fam_glBindRenderbuffer
#define glDeleteRenderbuffers             fam_glDeleteRenderbuffers
#define glRenderbufferStorage             fam_glRenderbufferStorage
#define glRenderbufferStorageMultisample  fam_glRenderbufferStorageMultisample
#define glFramebufferRenderbuffer         fam_glFramebufferRenderbuffer
#define glBlitFramebuffer                 fam_glBlitFramebuffer
