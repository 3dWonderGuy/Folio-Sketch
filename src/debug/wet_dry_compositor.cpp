/**
 * =========================================================================================
 * @file src/debug/wet_dry_compositor.cpp
 * @brief Two-Phase Layer Compositor (Wet vs Dry) with Unified GPU Capsule SDF
 * =========================================================================================
 */

#include "debug/wet_dry_compositor.hpp"
#include "debug/gl_loader.hpp"
#include <iostream>
#include <string>
#include <algorithm>

namespace Folio::Debug {

unsigned int WetDryCompositor::CompileShader(unsigned int type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[WetDryCompositor] Shader compilation error:\n" << infoLog << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool WetDryCompositor::Init() {
#if defined(__ANDROID__)
    const char* glslHeader = "#version 300 es\nprecision highp float;\n";
#else
    const char* glslHeader = "#version 330 core\n";
#endif

    std::string vSrc = std::string(glslHeader) +
        "layout(location = 0) in vec2 aPos;\n"
        "out vec2 vUv;\n"
        "void main() {\n"
        "    vUv = aPos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    std::string fSrc = std::string(glslHeader) +
        "uniform sampler2D uTexture;\n"
        "uniform vec4 uTint;\n"
        "in vec2 vUv;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    vec4 c = texture(uTexture, vUv);\n"
        "    fragColor = c * uTint;\n"
        "}\n";

    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vSrc.c_str());
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fSrc.c_str());
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_blitProgram = glCreateProgram();
    glAttachShader(m_blitProgram, vs);
    glAttachShader(m_blitProgram, fs);
    glLinkProgram(m_blitProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int linked = 0;
    glGetProgramiv(m_blitProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char infoLog[512];
        glGetProgramInfoLog(m_blitProgram, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[WetDryCompositor] Link error:\n" << infoLog << "\n";
        glDeleteProgram(m_blitProgram);
        m_blitProgram = 0;
        return false;
    }

    m_locTexture = glGetUniformLocation(m_blitProgram, "uTexture");
    m_locTint    = glGetUniformLocation(m_blitProgram, "uTint");

    // Static fullscreen quad [-1.0, 1.0]
    static constexpr float kFullscreenQuad[12] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenQuad), kFullscreenQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void WetDryCompositor::Shutdown() {
    if (m_quadVbo)     { glDeleteBuffers(1, &m_quadVbo);       m_quadVbo = 0; }
    if (m_vao)         { glDeleteVertexArrays(1, &m_vao);       m_vao = 0; }
    if (m_blitProgram) { glDeleteProgram(m_blitProgram);       m_blitProgram = 0; }
    if (m_dryTex)      { glDeleteTextures(1, &m_dryTex);       m_dryTex = 0; }
    if (m_dryFbo)      { glDeleteFramebuffers(1, &m_dryFbo);   m_dryFbo = 0; }
    m_width = 0;
    m_height = 0;
}

void WetDryCompositor::EnsureSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_dryFbo && m_width == width && m_height == height) return;

    if (!m_dryFbo) glGenFramebuffers(1, &m_dryFbo);
    if (!m_dryTex) glGenTextures(1, &m_dryTex);

    glBindTexture(GL_TEXTURE_2D, m_dryTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_dryFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_dryTex, 0);

    m_width = width;
    m_height = height;

    // Initialize to clear transparent
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void WetDryCompositor::ClearDry(float r, float g, float b, float a) {
    if (!m_dryFbo) return;
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_dryFbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void WetDryCompositor::CommitStroke(GlCapsuleRenderer& renderer,
                                    std::span<const StrokePoint> points,
                                    uint32_t color,
                                    const CanvasTransform& transform,
                                    const CapsuleBrush& brush) {
    if (points.empty() || !m_dryFbo || m_width <= 0 || m_height <= 0) return;

    GLint prevFbo = 0;
    GLint prevVp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);

    glBindFramebuffer(GL_FRAMEBUFFER, m_dryFbo);
    glViewport(0, 0, m_width, m_height);

    // Draw directly into Dry FBO using the identical GPU Capsule SDF shader!
    renderer.RenderStroke(points, color, transform, m_width, m_height, brush);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void WetDryCompositor::PresentDry(int vpWidth, int vpHeight, bool debugTintSeams) {
    if (!m_blitProgram || !m_dryTex || vpWidth <= 0 || vpHeight <= 0) return;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_blitProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_dryTex);
    glUniform1i(m_locTexture, 0);

    // In seam debug mode, tint Dry layer with a slight cool cyan hue
    if (debugTintSeams || m_debugSeamHighlight) {
        glUniform4f(m_locTint, 0.70f, 0.88f, 1.0f, 1.0f);
    } else {
        glUniform4f(m_locTint, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void WetDryCompositor::RenderWet(GlCapsuleRenderer& renderer,
                                 std::span<const StrokePoint> livePoints,
                                 std::span<const StrokePoint> predictedPoints,
                                 uint32_t color,
                                 const CanvasTransform& transform,
                                 int vpWidth,
                                 int vpHeight,
                                 const CapsuleBrush& brush,
                                 bool debugTintSeams) {
    if (livePoints.empty()) return;

    uint32_t wetColor = color;
    if (debugTintSeams || m_debugSeamHighlight) {
        // Warm orange tint to visually distinguish active in-flight ink from committed dry ink
        uint32_t a = (color >> 24) & 0xFF;
        wetColor = (a << 24) | 0x0033B4FF; // Orange
    }

    renderer.RenderStroke(livePoints, wetColor, transform, vpWidth, vpHeight, brush);

    // Forward Kalman predicted tip
    if (!predictedPoints.empty()) {
        uint32_t predColor = wetColor;
        if (debugTintSeams || m_debugSeamHighlight) {
            uint32_t a = (color >> 24) & 0xFF;
            predColor = (a << 24) | 0x00FF3080; // Bright magenta/pink for predicted tip
        }
        renderer.RenderStroke(predictedPoints, predColor, transform, vpWidth, vpHeight, brush);
    }
}

} // namespace Folio::Debug
