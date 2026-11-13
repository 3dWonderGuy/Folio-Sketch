/**
 * =========================================================================================
 * @file folio_backends/common/gl_capsule_renderer.cpp
 * @brief Standalone OpenGL ES 3.0 / Core 3.3 Analytic Capsule SDF Instanced Renderer
 * =========================================================================================
 */

#include "folio_backends/common/gl_capsule_renderer.hpp"
#include "folio_backends/common/display_surface_transform.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#if __has_include("gl_loader.hpp")
#include "gl_loader.hpp"
#elif __has_include("inspectorV2.0/gl_loader.hpp")
#include "inspectorV2.0/gl_loader.hpp"
#else
#include <GL/gl.h>
#endif
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <algorithm>

namespace Folio::Backends {

GlCapsuleRenderer::GlCapsuleRenderer() = default;
GlCapsuleRenderer::~GlCapsuleRenderer() {
    Shutdown();
}

bool GlCapsuleRenderer::Init() {
#if defined(__ANDROID__)
    const char* glslHeader = "#version 300 es\nprecision highp float;\n";
#else
    const char* glslHeader = "#version 330 core\n";
#endif

    // Vertex Shader:
    // Reconstructs local segment frame (u, n) from (aPointA, aPointB), expands the
    // static unit quad [-1, 1]^2 by max(rA, rB) + AA padding along +/-u and +/-n,
    // and projects world coordinates to clip space using canvas pan & zoom.
    std::string vSrc = std::string(glslHeader) +
        GetGlslNdcSnippet() +
        "layout(location = 0) in vec2 aQuadPos;\n"
        "layout(location = 1) in vec2 aPointA;\n"
        "layout(location = 2) in vec2 aPointB;\n"
        "layout(location = 3) in vec2 aRadii;\n"
        "layout(location = 4) in vec4 aColor;\n"
        "layout(location = 5) in vec4 aColorEnd;\n"
        "uniform vec4 uTransform; // (panX, panY, zoom, unused)\n"
        "uniform vec2 uViewport;  // (width, height)\n"
        "uniform mat4 uSurfaceTransform;\n"
        "out vec2 vWorldPos;\n"
        "flat out vec2 vPointA;\n"
        "flat out vec2 vPointB;\n"
        "flat out vec2 vRadii;\n"
        "flat out vec4 vColor;\n"
        "flat out vec4 vColorEnd;\n"
        "void main() {\n"
        "    vec2 delta = aPointB - aPointA;\n"
        "    float segLen = length(delta);\n"
        "    vec2 u = (segLen > 1e-5) ? (delta / segLen) : vec2(1.0, 0.0);\n"
        "    vec2 n = vec2(-u.y, u.x);\n"
        "    float zoom = max(uTransform.z, 1e-4);\n"
        "    float padWorld = 2.0 / zoom;\n"
        "    float maxR = max(aRadii.x, aRadii.y) + padWorld;\n"
        "    vec2 mid = 0.5 * (aPointA + aPointB);\n"
        "    float halfSpanU = 0.5 * segLen + maxR;\n"
        "    vec2 worldPos = mid + u * (aQuadPos.x * halfSpanU) + n * (aQuadPos.y * maxR);\n"
        "    vec2 screenPos = worldPos * zoom + uTransform.xy;\n"
        "    gl_Position = ComputeNdc(screenPos, uViewport, uSurfaceTransform);\n"
        "    vWorldPos   = worldPos;\n"
        "    vPointA     = aPointA;\n"
        "    vPointB     = aPointB;\n"
        "    vRadii      = aRadii;\n"
        "    vColor      = aColor;\n"
        "    vColorEnd   = aColorEnd;\n"
        "}\n";

    // Fragment Shader (Analytic Capsule SDF):
    // Evaluates Euclidean distance to segment [vPointA, vPointB],
    // linearly interpolates radius r(t) = mix(rA, rB, t) and color mix(vColor, vColorEnd, t),
    // and applies smoothstep falloff + high-frequency micro-fiber paper graphite grain.
    std::string fSrc = std::string(glslHeader) +
        "in vec2 vWorldPos;\n"
        "flat in vec2 vPointA;\n"
        "flat in vec2 vPointB;\n"
        "flat in vec2 vRadii;\n"
        "flat in vec4 vColor;\n"
        "flat in vec4 vColorEnd;\n"
        "uniform vec4 uTransform; // (panX, panY, zoom, unused)\n"
        "uniform vec2 uGrain;     // (grain strength 0..1, grain cells per world unit)\n"
        "out vec4 fragColor;\n"
        "float hash21(vec2 p) {\n"
        "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
        "    p3 += dot(p3, p3.yzx + 33.33);\n"
        "    return fract((p3.x + p3.y) * p3.z);\n"
        "}\n"
        "float vnoise(vec2 p) {\n"
        "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),\n"
        "               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "void main() {\n"
        "    vec2 ab = vPointB - vPointA;\n"
        "    float lenSq = dot(ab, ab);\n"
        "    float t = (lenSq > 1e-8) ? clamp(dot(vWorldPos - vPointA, ab) / lenSq, 0.0, 1.0) : 0.0;\n"
        "    vec2 closestPt = vPointA + t * ab;\n"
        "    float dist = length(vWorldPos - closestPt);\n"
        "    float r = mix(vRadii.x, vRadii.y, t);\n"
        "    float sdfWorld = r - dist;\n"
        "    float zoom = max(uTransform.z, 1e-4);\n"
        "    float fw = fwidth(dist - r);\n"
        "    float aaWidthWorld = (fw > 1e-6 && fw < 4.0 / zoom) ? fw : (1.0 / zoom);\n"
        "    float edgeAlpha = smoothstep(-0.5 * aaWidthWorld, 0.5 * aaWidthWorld, sdfWorld);\n"
        "    if (edgeAlpha <= 0.0) {\n"
        "        discard;\n"
        "    }\n"
        "    float tooth = 1.0;\n"
        "    if (uGrain.x > 0.0) {\n"
        "        vec2 gp = vWorldPos * uGrain.y;\n"
        "        float n1 = vnoise(gp * 1.1);\n"
        "        float n2 = vnoise(gp * 3.1 + vec2(17.3, 9.2));\n"
        "        float n3 = hash21(floor(gp * 6.5));\n"
        "        float fiber = 0.45 * n1 + 0.35 * n2 + 0.20 * n3;\n"
        "        float edgeRatio = clamp(dist / max(r, 1e-4), 0.0, 1.0);\n"
        "        float edgeRough = fiber - 0.28 * (edgeRatio * edgeRatio);\n"
        "        float grainMask = smoothstep(0.22, 0.68, edgeRough);\n"
        "        tooth = mix(1.0, grainMask, uGrain.x);\n"
        "    }\n"
        "    vec4 segCol = mix(vColor, vColorEnd, t);\n"
        "    fragColor = segCol * (edgeAlpha * tooth);\n"
        "}\n";

    // Composite shader: fullscreen quad sampling offscreen layer with brush opacity
    std::string cvSrc = std::string(glslHeader) +
        "layout(location = 0) in vec2 aPos;\n"
        "out vec2 vUv;\n"
        "void main() {\n"
        "    vUv = aPos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    std::string cfSrc = std::string(glslHeader) +
        "in vec2 vUv;\n"
        "uniform sampler2D uLayer;\n"
        "uniform float uOpacity;\n"
        "out vec4 fragColor;\n"
        "void main() { fragColor = texture(uLayer, vUv) * uOpacity; }\n";

    {
        GLuint cvs = CompileShader(GL_VERTEX_SHADER, cvSrc.c_str());
        GLuint cfs = CompileShader(GL_FRAGMENT_SHADER, cfSrc.c_str());
        if (!cvs || !cfs) return false;
        m_compositeProgram = glCreateProgram();
        glAttachShader(m_compositeProgram, cvs);
        glAttachShader(m_compositeProgram, cfs);
        glLinkProgram(m_compositeProgram);
        glDeleteShader(cvs);
        glDeleteShader(cfs);
        m_locCompLayer   = glGetUniformLocation(m_compositeProgram, "uLayer");
        m_locCompOpacity = glGetUniformLocation(m_compositeProgram, "uOpacity");

        static constexpr float kCompQuad[12] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f
        };
        glGenVertexArrays(1, &m_compositeVao);
        glGenBuffers(1, &m_compositeQuadVbo);
        glBindVertexArray(m_compositeVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_compositeQuadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kCompQuad), kCompQuad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vSrc.c_str());
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fSrc.c_str());
    if (!vs || !fs) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        std::cerr << "[GlCapsuleRenderer] Program link failed: " << log << std::endl;
        return false;
    }

    m_locTransform = glGetUniformLocation(m_program, "uTransform");
    m_locViewport  = glGetUniformLocation(m_program, "uViewport");
    m_locGrain     = glGetUniformLocation(m_program, "uGrain");
    m_locSurfaceTransform = glGetUniformLocation(m_program, "uSurfaceTransform");

    // Static unit quad [-1.0, 1.0]
    static constexpr float kUnitQuadVertices[12] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVbo);
    glGenBuffers(1, &m_instanceVbo);

    glBindVertexArray(m_vao);

    // 1. Static Quad VBO (divisor 0)
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuadVertices), kUnitQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    // 2. Persistent Dynamic Instance VBO (divisor 1)
    // Pre-allocate initial capacity for 4096 segments (128 KB)
    m_vboCapacity = 4096;
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vboCapacity * sizeof(CapsuleInstance)),
                 nullptr, GL_DYNAMIC_DRAW);

    // Attribute 1: vec2 pointA
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                          reinterpret_cast<const void*>(offsetof(CapsuleInstance, pointA)));
    glVertexAttribDivisor(1, 1);

    // Attribute 2: vec2 pointB
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                          reinterpret_cast<const void*>(offsetof(CapsuleInstance, pointB)));
    glVertexAttribDivisor(2, 1);

    // Attribute 3: vec2 radii
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                          reinterpret_cast<const void*>(offsetof(CapsuleInstance, radii)));
    glVertexAttribDivisor(3, 1);

    // Attribute 4: uint32 colorRgba
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                          reinterpret_cast<const void*>(offsetof(CapsuleInstance, colorRgba)));
    glVertexAttribDivisor(4, 1);

    // Attribute 5: uint32 colorRgbaEnd
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                          reinterpret_cast<const void*>(offsetof(CapsuleInstance, colorRgbaEnd)));
    glVertexAttribDivisor(5, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void GlCapsuleRenderer::Shutdown() {
    if (m_layerTex)         { glDeleteTextures(1, &m_layerTex);         m_layerTex = 0; }
    if (m_layerFbo)         { glDeleteFramebuffers(1, &m_layerFbo);     m_layerFbo = 0; }
    if (m_compositeQuadVbo) { glDeleteBuffers(1, &m_compositeQuadVbo);  m_compositeQuadVbo = 0; }
    if (m_compositeVao)     { glDeleteVertexArrays(1, &m_compositeVao); m_compositeVao = 0; }
    if (m_compositeProgram) { glDeleteProgram(m_compositeProgram);      m_compositeProgram = 0; }
    m_layerW = m_layerH = 0;
    if (m_instanceVbo) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_quadVbo)     { glDeleteBuffers(1, &m_quadVbo);     m_quadVbo = 0; }
    if (m_vao)         { glDeleteVertexArrays(1, &m_vao);    m_vao = 0; }
    if (m_program)     { glDeleteProgram(m_program);         m_program = 0; }
    m_vboCapacity = 0;
    m_instanceCount = 0;
}

void GlCapsuleRenderer::EnsureCapacity(size_t requiredCount) {
    if (requiredCount <= m_vboCapacity) return;
    m_vboCapacity = std::max(m_vboCapacity * 2, requiredCount + 1024);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vboCapacity * sizeof(CapsuleInstance)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlCapsuleRenderer::UploadStroke(std::span<const StrokePoint> points, uint32_t color) {
    auto t0 = std::chrono::steady_clock::now();
    CapsuleBuilder::BuildCapsuleInstances(points, color, m_instances);
    auto t1 = std::chrono::steady_clock::now();
    m_lastCpuIngestionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    UploadInstances(m_instances);
}

void GlCapsuleRenderer::UploadInstances(std::span<const CapsuleInstance> instances) {
    m_instanceCount = instances.size();
    if (m_instanceCount == 0 || !m_instanceVbo) return;

    EnsureCapacity(m_instanceCount);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(m_instanceCount * sizeof(CapsuleInstance)),
                    instances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlCapsuleRenderer::AppendInstances(std::span<const CapsuleInstance> newInstances, size_t fromIndex) {
    if (newInstances.empty() || !m_instanceVbo) return;
    const size_t total = fromIndex + newInstances.size();
    EnsureCapacity(total);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(fromIndex * sizeof(CapsuleInstance)),
                    static_cast<GLsizeiptr>(newInstances.size() * sizeof(CapsuleInstance)),
                    newInstances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_instanceCount = total;
}

void GlCapsuleRenderer::EnsureLayer(int w, int h) {
    if (m_layerFbo && m_layerW == w && m_layerH == h) return;
    if (!m_layerFbo) glGenFramebuffers(1, &m_layerFbo);
    if (!m_layerTex) glGenTextures(1, &m_layerTex);
    glBindTexture(GL_TEXTURE_2D, m_layerTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_layerFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_layerTex, 0);
    m_layerW = w;
    m_layerH = h;
}

void GlCapsuleRenderer::DrawInstances(size_t firstInstance, size_t instanceCount,
                                      const CanvasTransform& transform, int vpWidth, int vpHeight,
                                      const CapsuleBrush& brush, const float* surfaceTransform) const {
    if (instanceCount == 0 || firstInstance >= m_instanceCount) return;
    const size_t clampedCount = std::min(instanceCount, m_instanceCount - firstInstance);
    glUseProgram(m_program);
    glUniform4f(m_locTransform, transform.panX, transform.panY, transform.zoom, 0.0f);
    glUniform2f(m_locViewport, static_cast<float>(vpWidth), static_cast<float>(vpHeight));
    glUniform2f(m_locGrain, brush.grain, brush.grainScale);

    if (m_locSurfaceTransform >= 0) {
        if (surfaceTransform != nullptr) {
            glUniformMatrix4fv(m_locSurfaceTransform, 1, GL_FALSE, surfaceTransform);
        } else {
            static const float identity[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            glUniformMatrix4fv(m_locSurfaceTransform, 1, GL_FALSE, identity);
        }
    }

    glBindVertexArray(m_vao);
    if (firstInstance > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        const size_t baseBytes = firstInstance * sizeof(CapsuleInstance);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(CapsuleInstance, pointA)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(CapsuleInstance, pointB)));
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(CapsuleInstance, radii)));
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(CapsuleInstance, colorRgba)));
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(CapsuleInstance, colorRgbaEnd)));
    }
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(clampedCount));
    if (firstInstance > 0) {
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(offsetof(CapsuleInstance, pointA)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(offsetof(CapsuleInstance, pointB)));
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(offsetof(CapsuleInstance, radii)));
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(offsetof(CapsuleInstance, colorRgba)));
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CapsuleInstance),
                              reinterpret_cast<const void*>(offsetof(CapsuleInstance, colorRgbaEnd)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glBindVertexArray(0);
}

void GlCapsuleRenderer::Render(const CanvasTransform& transform, int vpWidth, int vpHeight,
                               const CapsuleBrush& brush, bool singlePassWet,
                               const float* surfaceTransform) {
    RenderRange(0, m_instanceCount, transform, vpWidth, vpHeight, brush, singlePassWet, surfaceTransform);
}

void GlCapsuleRenderer::RenderRange(size_t firstInstance, size_t instanceCount,
                                    const CanvasTransform& transform, int vpWidth, int vpHeight,
                                    const CapsuleBrush& brush, bool singlePassWet,
                                    const float* surfaceTransform) {
    if (!m_program || !m_vao || instanceCount == 0 || firstInstance >= m_instanceCount || vpWidth <= 0 || vpHeight <= 0) return;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    // Live wet stroke or direct solid pass: bypass layer FBO allocations, clears and blits
    if (singlePassWet || (!brush.unionOverlap && brush.opacity >= 0.999f)) {
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DrawInstances(firstInstance, instanceCount, transform, vpWidth, vpHeight, brush, surfaceTransform);
        glUseProgram(0);
        return;
    }

    // Coat / stroke with union overlap: offscreen MAX union pass then composite
    GLint prevFbo = 0;
    GLint vp[4] = {0, 0, vpWidth, vpHeight};
    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint prevScissor[4] = {0, 0, vpWidth, vpHeight};
    if (scissorWasEnabled) {
        glGetIntegerv(GL_SCISSOR_BOX, prevScissor);
    }
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    EnsureLayer(vp[2], vp[3]);

    glBindFramebuffer(GL_FRAMEBUFFER, m_layerFbo);
    glViewport(0, 0, vp[2], vp[3]);
    if (scissorWasEnabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
    }
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);
    DrawInstances(firstInstance, instanceCount, transform, vpWidth, vpHeight, brush, surfaceTransform);
    glBlendEquation(GL_FUNC_ADD);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (scissorWasEnabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
    }
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_compositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_layerTex);
    glUniform1i(m_locCompLayer, 0);
    glUniform1f(m_locCompOpacity, brush.opacity);
    glBindVertexArray(m_compositeVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void GlCapsuleRenderer::RenderStroke(std::span<const StrokePoint> points,
                                     uint32_t color,
                                     const CanvasTransform& transform,
                                     int vpWidth,
                                     int vpHeight,
                                     const CapsuleBrush& brush,
                                     bool singlePassWet,
                                     const float* surfaceTransform) {
    UploadStroke(points, color);
    Render(transform, vpWidth, vpHeight, brush, singlePassWet, surfaceTransform);
}

#if defined(FOLIO_BACKENDS_HAS_IMGUI)
void GlCapsuleRenderer::RenderBoundingQuadsWireframe(ImDrawList* drawList,
                                                     const CanvasTransform& transform) const {
    if (!drawList || m_instances.empty()) return;

    const float zoom = std::max(transform.zoom, 1e-4f);
    const float padWorld = 2.0f / zoom;
    const ImU32 quadCol = IM_COL32(80, 220, 255, 160);
    const ImU32 diagCol = IM_COL32(80, 220, 255, 75);

    for (const auto& inst : m_instances) {
        float dx = inst.pointB.x - inst.pointA.x;
        float dy = inst.pointB.y - inst.pointA.y;
        float segLen = std::hypot(dx, dy);
        float ux = (segLen > 1e-5f) ? (dx / segLen) : 1.0f;
        float uy = (segLen > 1e-5f) ? (dy / segLen) : 0.0f;
        float nx = -uy;
        float ny =  ux;

        float maxR = std::max(inst.radii.x, inst.radii.y) + padWorld;
        float midX = 0.5f * (inst.pointA.x + inst.pointB.x);
        float midY = 0.5f * (inst.pointA.y + inst.pointB.y);
        float halfSpanU = 0.5f * segLen + maxR;

        ImVec2 c0 = transform.WorldToScreen(midX - ux * halfSpanU - nx * maxR,
                                            midY - uy * halfSpanU - ny * maxR);
        ImVec2 c1 = transform.WorldToScreen(midX + ux * halfSpanU - nx * maxR,
                                            midY + uy * halfSpanU - ny * maxR);
        ImVec2 c2 = transform.WorldToScreen(midX + ux * halfSpanU + nx * maxR,
                                            midY + uy * halfSpanU + ny * maxR);
        ImVec2 c3 = transform.WorldToScreen(midX - ux * halfSpanU + nx * maxR,
                                            midY - uy * halfSpanU + ny * maxR);

        drawList->AddQuad(c0, c1, c2, c3, quadCol, 1.0f);
        drawList->AddLine(c0, c2, diagCol, 1.0f);
    }
}
#endif

double GlCapsuleRenderer::BenchmarkCpuIngestionMs(size_t pointCount, int iterations) {
    if (pointCount == 0 || iterations <= 0) return 0.0;

    std::vector<StrokePoint> syntheticPts(pointCount);
    for (size_t i = 0; i < pointCount; ++i) {
        double t = static_cast<double>(i) * 0.05;
        syntheticPts[i].x = 200.0 + t * 12.0 + std::sin(t) * 25.0;
        syntheticPts[i].y = 200.0 + std::cos(t * 1.3) * 25.0;
        syntheticPts[i].r = 3.5f + 1.5f * static_cast<float>(std::sin(t * 0.7));
    }

    std::vector<CapsuleInstance> outInstances;
    outInstances.reserve(pointCount);

    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iterations; ++it) {
        CapsuleBuilder::BuildCapsuleInstances(syntheticPts, 0xFF41A5FFu, outInstances);
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / static_cast<double>(iterations);
}

unsigned int GlCapsuleRenderer::CompileShader(unsigned int type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[GlCapsuleRenderer] Shader compile error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace Folio::Backends
