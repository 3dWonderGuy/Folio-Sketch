#pragma once
/**
 * =========================================================================================
 * @file src/debug/gl_loader.hpp
 * @brief Dynamic OpenGL Extension Function Loader for Windows and Desktop Platforms
 * =========================================================================================
 *
 * Windows opengl32.dll only statically exports OpenGL 1.1 symbols. Modern OpenGL 2.0+
 * and 3.0+ core functions (VBOs, VAOs, Shaders, Framebuffers, Instancing, etc.) must be
 * resolved dynamically at runtime via SDL_GL_GetProcAddress. On Android/GLES3, symbols
 * are exported directly by libGLESv3.so.
 */

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#endif

#if defined(_WIN32)

namespace Folio::Debug {

struct GlFuncs {
    PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
    PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
    PFNGLBUFFERDATAPROC glBufferData = nullptr;
    PFNGLSTENCILOPSEPARATEPROC glStencilOpSeparate = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
    PFNGLUNIFORM1FPROC glUniform1f = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i = nullptr;
    PFNGLUNIFORM2FPROC glUniform2f = nullptr;
    PFNGLUNIFORM4FPROC glUniform4f = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
    PFNGLBLENDEQUATIONPROC glBlendEquation = nullptr;
    PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
    PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced = nullptr;
    PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor = nullptr;
};

extern GlFuncs g_gl;

bool InitGlLoader();

} // namespace Folio::Debug

// Macro redirections for transparent drop-in OpenGL extension calls on Windows
#define glBindBuffer                  (::Folio::Debug::g_gl.glBindBuffer)
#define glDeleteBuffers               (::Folio::Debug::g_gl.glDeleteBuffers)
#define glGenBuffers                  (::Folio::Debug::g_gl.glGenBuffers)
#define glBufferData                  (::Folio::Debug::g_gl.glBufferData)
#define glStencilOpSeparate           (::Folio::Debug::g_gl.glStencilOpSeparate)
#define glAttachShader                (::Folio::Debug::g_gl.glAttachShader)
#define glCompileShader               (::Folio::Debug::g_gl.glCompileShader)
#define glCreateProgram               (::Folio::Debug::g_gl.glCreateProgram)
#define glCreateShader                (::Folio::Debug::g_gl.glCreateShader)
#define glDeleteProgram               (::Folio::Debug::g_gl.glDeleteProgram)
#define glDeleteShader                (::Folio::Debug::g_gl.glDeleteShader)
#define glEnableVertexAttribArray     (::Folio::Debug::g_gl.glEnableVertexAttribArray)
#define glGetProgramiv                (::Folio::Debug::g_gl.glGetProgramiv)
#define glGetProgramInfoLog           (::Folio::Debug::g_gl.glGetProgramInfoLog)
#define glGetShaderiv                 (::Folio::Debug::g_gl.glGetShaderiv)
#define glGetShaderInfoLog            (::Folio::Debug::g_gl.glGetShaderInfoLog)
#define glGetUniformLocation          (::Folio::Debug::g_gl.glGetUniformLocation)
#define glLinkProgram                 (::Folio::Debug::g_gl.glLinkProgram)
#define glShaderSource                (::Folio::Debug::g_gl.glShaderSource)
#define glUseProgram                  (::Folio::Debug::g_gl.glUseProgram)
#define glUniform1f                   (::Folio::Debug::g_gl.glUniform1f)
#define glUniform1i                   (::Folio::Debug::g_gl.glUniform1i)
#define glUniform2f                   (::Folio::Debug::g_gl.glUniform2f)
#define glUniform4f                   (::Folio::Debug::g_gl.glUniform4f)
#define glVertexAttribPointer         (::Folio::Debug::g_gl.glVertexAttribPointer)
#define glBindVertexArray             (::Folio::Debug::g_gl.glBindVertexArray)
#define glDeleteVertexArrays          (::Folio::Debug::g_gl.glDeleteVertexArrays)
#define glGenVertexArrays             (::Folio::Debug::g_gl.glGenVertexArrays)
#define glBlendEquation               (::Folio::Debug::g_gl.glBlendEquation)
#define glActiveTexture               (::Folio::Debug::g_gl.glActiveTexture)
#define glBindFramebuffer             (::Folio::Debug::g_gl.glBindFramebuffer)
#define glDeleteFramebuffers          (::Folio::Debug::g_gl.glDeleteFramebuffers)
#define glGenFramebuffers             (::Folio::Debug::g_gl.glGenFramebuffers)
#define glFramebufferTexture2D        (::Folio::Debug::g_gl.glFramebufferTexture2D)
#define glDrawArraysInstanced         (::Folio::Debug::g_gl.glDrawArraysInstanced)
#define glVertexAttribDivisor         (::Folio::Debug::g_gl.glVertexAttribDivisor)

#else

namespace Folio::Debug {
inline bool InitGlLoader() { return true; }
} // namespace Folio::Debug

#endif
