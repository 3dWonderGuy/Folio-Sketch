#include "debug/gl_loader.hpp"

#if defined(_WIN32)
#include <SDL3/SDL_video.h>
#include <iostream>

namespace Folio::Debug {

GlFuncs g_gl{};

#define LOAD_GL(name, type) \
    g_gl.name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name)); \
    if (!g_gl.name) { \
        std::cerr << "[gl_loader] Failed to load GL procedure: " #name << std::endl; \
        success = false; \
    }

bool InitGlLoader() {
    static bool s_initialized = false;
    if (s_initialized) return true;

    bool success = true;
    LOAD_GL(glBindBuffer, PFNGLBINDBUFFERPROC);
    LOAD_GL(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
    LOAD_GL(glGenBuffers, PFNGLGENBUFFERSPROC);
    LOAD_GL(glBufferData, PFNGLBUFFERDATAPROC);
    LOAD_GL(glStencilOpSeparate, PFNGLSTENCILOPSEPARATEPROC);
    LOAD_GL(glAttachShader, PFNGLATTACHSHADERPROC);
    LOAD_GL(glCompileShader, PFNGLCOMPILESHADERPROC);
    LOAD_GL(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    LOAD_GL(glCreateShader, PFNGLCREATESHADERPROC);
    LOAD_GL(glDeleteProgram, PFNGLDELETEPROGRAMPROC);
    LOAD_GL(glDeleteShader, PFNGLDELETESHADERPROC);
    LOAD_GL(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    LOAD_GL(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    LOAD_GL(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    LOAD_GL(glGetShaderiv, PFNGLGETSHADERIVPROC);
    LOAD_GL(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    LOAD_GL(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);
    LOAD_GL(glLinkProgram, PFNGLLINKPROGRAMPROC);
    LOAD_GL(glShaderSource, PFNGLSHADERSOURCEPROC);
    LOAD_GL(glUseProgram, PFNGLUSEPROGRAMPROC);
    LOAD_GL(glUniform1f, PFNGLUNIFORM1FPROC);
    LOAD_GL(glUniform1i, PFNGLUNIFORM1IPROC);
    LOAD_GL(glUniform2f, PFNGLUNIFORM2FPROC);
    LOAD_GL(glUniform4f, PFNGLUNIFORM4FPROC);
    LOAD_GL(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    LOAD_GL(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    LOAD_GL(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC);
    LOAD_GL(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    LOAD_GL(glBlendEquation, PFNGLBLENDEQUATIONPROC);
    LOAD_GL(glActiveTexture, PFNGLACTIVETEXTUREPROC);
    LOAD_GL(glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC);
    LOAD_GL(glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC);
    LOAD_GL(glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC);
    LOAD_GL(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC);
    LOAD_GL(glDrawArraysInstanced, PFNGLDRAWARRAYSINSTANCEDPROC);
    LOAD_GL(glVertexAttribDivisor, PFNGLVERTEXATTRIBDIVISORPROC);

    s_initialized = success;
    return success;
}

} // namespace Folio::Debug

#endif
