/**
 * =========================================================================================
 * @file folio_backends/common/gl_raster_dab_renderer.cpp
 * @brief OpenGL ES 3.0 / Core 3.3 Instanced Procreate-Style Raster Dab & Material Renderer
 * =========================================================================================
 */

#include "folio_backends/common/gl_raster_dab_renderer.hpp"
#include "folio_backends/common/display_surface_transform.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#include "inspectorV2.0/gl_loader.hpp"
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <iostream>
#include <string>

namespace Folio::Backends {

namespace {

GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[GlRasterDabRenderer] Shader compile failed: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

GlRasterDabRenderer::GlRasterDabRenderer() = default;
GlRasterDabRenderer::~GlRasterDabRenderer() {
    Shutdown();
}

bool GlRasterDabRenderer::Init() {
#if defined(__ANDROID__)
    const char* glslHeader = "#version 300 es\nprecision highp float;\n";
#else
    const char* glslHeader = "#version 330 core\n";
#endif

    std::string vSrc = std::string(glslHeader) +
        GetGlslNdcSnippet() +
        "layout(location = 0) in vec2 aQuadPos;\n"
        "layout(location = 1) in vec2 aCenter;\n"
        "layout(location = 2) in vec2 aRadiusAngle;\n" // (radius, angleRad)
        "layout(location = 3) in vec4 aColor;\n"
        "layout(location = 4) in vec3 aBrushAttrs;\n"  // (hardness, grainDepth, smudgePull)
        "uniform vec4 uTransform; // (panX, panY, zoom, unused)\n"
        "uniform vec2 uViewport;\n"
        "uniform mat4 uSurfaceTransform;\n"
        "uniform vec4 uBrushParams; // (tipShape, aspectRatio, 0, 0)\n"
        "out vec2 vWorldPos;\n"
        "out vec2 vLocalUv;\n"
        "flat out vec4 vColor;\n"
        "flat out vec3 vAttrs;\n"
        "flat out float vRadius;\n"
        "void main() {\n"
        "    float zoom = max(uTransform.z, 1e-4);\n"
        "    float padWorld = 1.5 / zoom;\n"
        "    float rx = max(aRadiusAngle.x, 0.01);\n"
        "    float ry = rx * clamp(uBrushParams.y, 0.1, 1.0);\n"
        "    vec2 halfExt = vec2(rx + padWorld, ry + padWorld);\n"
        "    vec2 localPos = aQuadPos * halfExt;\n"
        "    vLocalUv = localPos / vec2(rx, ry);\n"
        "    float c = cos(aRadiusAngle.y);\n"
        "    float s = sin(aRadiusAngle.y);\n"
        "    vec2 rotated = vec2(localPos.x * c - localPos.y * s, localPos.x * s + localPos.y * c);\n"
        "    vec2 worldPos = aCenter + rotated;\n"
        "    vec2 screenPos = worldPos * zoom + uTransform.xy;\n"
        "    gl_Position = ComputeNdc(screenPos, uViewport, uSurfaceTransform);\n"
        "    vWorldPos = worldPos;\n"
        "    vColor = aColor;\n"
        "    vAttrs = aBrushAttrs;\n"
        "    vRadius = rx;\n"
        "}\n";

    std::string fSrc = std::string(glslHeader) +
        "in vec2 vWorldPos;\n"
        "in vec2 vLocalUv;\n"
        "flat in vec4 vColor;\n"
        "flat in vec3 vAttrs; // (hardness, grainDepth, smudgePull)\n"
        "flat in float vRadius;\n"
        "uniform vec4 uTransform;\n"
        "uniform vec4 uBrushParams;    // (tipShape, aspectRatio, 0, 0)\n"
        "uniform vec4 uMaterialParams; // (pattern, toothDepth, toothScaleMm, absorbency)\n"
        "out vec4 fragColor;\n"
        "\n"
        "float hash21(vec2 p) {\n"
        "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
        "    p3 += dot(p3, p3.yzx + 33.33);\n"
        "    return fract((p3.x + p3.y) * p3.z);\n"
        "}\n"
        "\n"
        "float vnoise(vec2 p) {\n"
        "    vec2 i = floor(p);\n"
        "    vec2 f = fract(p);\n"
        "    f = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),\n"
        "               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "\n"
        "// Evaluates world-space procedural surface tooth height H in [0.0, 1.0]\n"
        "float EvaluateMaterialHeight(vec2 worldPos, int pattern, float scaleMm) {\n"
        "    vec2 gp = worldPos / max(scaleMm, 0.05);\n"
        "    if (pattern == 0) { // SmoothPaper\n"
        "        return mix(0.88, 1.0, vnoise(gp * 2.5));\n"
        "    } else if (pattern == 1) { // ColdPressPaper\n"
        "        float n1 = vnoise(gp * 1.4);\n"
        "        float n2 = vnoise(gp * 3.1 + vec2(7.2, 13.7));\n"
        "        float n3 = vnoise(gp * 6.5 + vec2(21.1, 4.3));\n"
        "        return 0.50 * n1 + 0.35 * n2 + 0.15 * n3;\n"
        "    } else if (pattern == 2) { // RoughPastelTooth\n"
        "        float n1 = vnoise(gp * vec2(1.8, 3.8));\n"
        "        float n2 = vnoise(gp * 5.2 + vec2(11.3, 2.9));\n"
        "        float grit = hash21(floor(gp * 4.0));\n"
        "        return 0.45 * n1 + 0.35 * n2 + 0.20 * grit;\n"
        "    } else if (pattern == 3) { // WovenCanvas\n"
        "        float wx = 0.5 + 0.5 * sin(gp.x * 6.2831853);\n"
        "        float wy = 0.5 + 0.5 * sin(gp.y * 6.2831853);\n"
        "        float weave = 0.5 * (wx + wy);\n"
        "        float micro = vnoise(gp * 4.0);\n"
        "        return 0.72 * weave + 0.28 * micro;\n"
        "    } else { // ConcreteWall\n"
        "        float n1 = vnoise(gp * 1.2);\n"
        "        float pits = hash21(floor(gp * 4.5));\n"
        "        return 0.55 * n1 + 0.45 * pits;\n"
        "    }\n"
        "}\n"
        "\n"
        "// Evaluates procedural stamp tip silhouette alpha in [0.0, 1.0]\n"
        "float EvaluateTipMask(vec2 uv, int tipShape, float hardness) {\n"
        "    float r = length(uv);\n"
        "    if (tipShape == 2) { // FlatBristle\n"
        "        vec2 d = abs(uv) - vec2(0.88, 0.85);\n"
        "        float boxDist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
        "        float feather = mix(0.28, 0.06, hardness);\n"
        "        float shape = 1.0 - smoothstep(-feather, 0.12, boxDist);\n"
        "        // Bristle strand grooves running along the X stroke axis\n"
        "        float strandWave = 0.5 + 0.5 * sin(uv.y * 28.0 + sin(uv.y * 11.0) * 1.7);\n"
        "        float microGap = 0.45 + 0.55 * smoothstep(0.18, 0.85, strandWave);\n"
        "        // Individual bristle tips taper slightly near the front/back edges\n"
        "        float tipTaper = 1.0 - 0.35 * smoothstep(0.55, 1.0, abs(uv.x)) * (1.0 - strandWave);\n"
        "        return shape * microGap * tipTaper;\n"
        "    }\n"
        "    if (r >= 1.0) return 0.0;\n"
        "    float inner = mix(0.0, 0.90, hardness);\n"
        "    float baseRadial = 1.0 - smoothstep(inner, 1.0, r);\n"
        "    if (tipShape == 3) { // ChalkSpeckle\n"
        "        float speck = 0.55 * vnoise(uv * 8.5 + vec2(3.1, 7.4)) + 0.45 * hash21(floor(vWorldPos * 10.0));\n"
        "        return baseRadial * smoothstep(0.22, 0.72, speck);\n"
        "    } else if (tipShape == 4) { // CharcoalGrain\n"
        "        float edgeNoise = vnoise(uv * 5.0 + vec2(1.7, 9.3));\n"
        "        float erodedR = r + 0.25 * (edgeNoise - 0.5);\n"
        "        float roughMask = 1.0 - smoothstep(mix(0.15, 0.82, hardness), 1.0, erodedR);\n"
        "        float coreGrit = vnoise(vWorldPos * 3.5);\n"
        "        return roughMask * mix(0.55, 1.0, coreGrit);\n"
        "    }\n"
        "    // RoundSoft (0), RoundHard (1), CustomMask fallback (5)\n"
        "    return baseRadial;\n"
        "}\n"
        "\n"
        "void main() {\n"
        "    int tipShape = int(uBrushParams.x + 0.5);\n"
        "    float tipAlpha = EvaluateTipMask(vLocalUv, tipShape, vAttrs.x);\n"
        "    if (tipAlpha <= 0.002) {\n"
        "        discard;\n"
        "    }\n"
        "\n"
        "    float tooth = 1.0;\n"
        "    float grainStrength = clamp(max(vAttrs.y, uMaterialParams.y), 0.0, 1.0);\n"
        "    if (grainStrength > 0.001) {\n"
        "        int pattern = int(uMaterialParams.x + 0.5);\n"
        "        float H = EvaluateMaterialHeight(vWorldPos, pattern, uMaterialParams.z);\n"
        "        float threshold = mix(0.08, 0.62, grainStrength);\n"
        "        float peakMask = smoothstep(threshold - 0.22, threshold + 0.22, H);\n"
        "        tooth = mix(1.0, peakMask, grainStrength);\n"
        "    }\n"
        "\n"
        "    // Watercolor wet-edge pigment pooling modulated by substrate absorbency\n"
        "    float wetEdge = clamp(uBrushParams.z, 0.0, 1.0);\n"
        "    float wetScale = 1.0;\n"
        "    if (wetEdge > 0.001) {\n"
        "        float radial = clamp(length(vLocalUv), 0.0, 1.0);\n"
        "        float rimPool = smoothstep(0.45, 0.92, radial) * (1.0 - smoothstep(0.92, 1.0, radial));\n"
        "        float absorbBoost = mix(0.65, 1.35, clamp(uMaterialParams.w, 0.0, 1.0));\n"
        "        wetScale = mix(1.0, 0.52 + 1.15 * rimPool * absorbBoost, wetEdge);\n"
        "    }\n"
        "    float coverage = tipAlpha * tooth * wetScale;\n"
        "    if (coverage <= 0.002) {\n"
        "        discard;\n"
        "    }\n"
        "    fragColor = vColor * coverage;\n"
        "}\n";

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
        std::cerr << "[GlRasterDabRenderer] Program link failed: " << log << std::endl;
        return false;
    }

    m_locTransform        = glGetUniformLocation(m_program, "uTransform");
    m_locViewport         = glGetUniformLocation(m_program, "uViewport");
    m_locSurfaceTransform = glGetUniformLocation(m_program, "uSurfaceTransform");
    m_locBrushParams      = glGetUniformLocation(m_program, "uBrushParams");
    m_locMaterialParams   = glGetUniformLocation(m_program, "uMaterialParams");

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

    // 1. Static unit quad VBO (divisor 0)
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuadVertices), kUnitQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    // 2. Persistent Dynamic Instance VBO (divisor 1)
    m_vboCapacity = 8192;
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vboCapacity * sizeof(RasterDabInstance)),
                 nullptr, GL_DYNAMIC_DRAW);

    // Attribute 1: vec2 center
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                          reinterpret_cast<const void*>(offsetof(RasterDabInstance, center)));
    glVertexAttribDivisor(1, 1);

    // Attribute 2: vec2 (radius, angleRad)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                          reinterpret_cast<const void*>(offsetof(RasterDabInstance, radius)));
    glVertexAttribDivisor(2, 1);

    // Attribute 3: vec4 colorRgba (normalized GL_UNSIGNED_BYTE)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RasterDabInstance),
                          reinterpret_cast<const void*>(offsetof(RasterDabInstance, colorRgba)));
    glVertexAttribDivisor(3, 1);

    // Attribute 4: vec3 (hardness, grainDepth, smudgePull)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                          reinterpret_cast<const void*>(offsetof(RasterDabInstance, hardness)));
    glVertexAttribDivisor(4, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void GlRasterDabRenderer::Shutdown() {
    if (m_instanceVbo) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_quadVbo)     { glDeleteBuffers(1, &m_quadVbo);     m_quadVbo = 0; }
    if (m_vao)         { glDeleteVertexArrays(1, &m_vao);    m_vao = 0; }
    if (m_program)     { glDeleteProgram(m_program);         m_program = 0; }
    m_vboCapacity = 0;
    m_dabCount = 0;
}

void GlRasterDabRenderer::EnsureCapacity(size_t requiredCount) {
    if (requiredCount <= m_vboCapacity) return;
    m_vboCapacity = std::max(m_vboCapacity * 2, requiredCount + 2048);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vboCapacity * sizeof(RasterDabInstance)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlRasterDabRenderer::UploadDabs(std::span<const RasterDabInstance> dabs) {
    m_dabCount = dabs.size();
    if (m_dabCount == 0 || !m_instanceVbo) return;

    EnsureCapacity(m_dabCount);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(m_dabCount * sizeof(RasterDabInstance)),
                    dabs.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlRasterDabRenderer::AppendDabs(std::span<const RasterDabInstance> newDabs, size_t fromIndex) {
    if (newDabs.empty() || !m_instanceVbo) return;
    const size_t total = fromIndex + newDabs.size();
    EnsureCapacity(total);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(fromIndex * sizeof(RasterDabInstance)),
                    static_cast<GLsizeiptr>(newDabs.size() * sizeof(RasterDabInstance)),
                    newDabs.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_dabCount = total;
}

void GlRasterDabRenderer::Render(const CanvasTransform& transform,
                                 int vpWidth,
                                 int vpHeight,
                                 const RasterBrushConfig& brushConfig,
                                 const CanvasMaterial& material,
                                 const float* surfaceTransform) {
    RenderRange(0, m_dabCount, transform, vpWidth, vpHeight, brushConfig, material, surfaceTransform);
}

void GlRasterDabRenderer::RenderRange(size_t firstDab,
                                      size_t dabCount,
                                      const CanvasTransform& transform,
                                      int vpWidth,
                                      int vpHeight,
                                      const RasterBrushConfig& brushConfig,
                                      const CanvasMaterial& material,
                                      const float* surfaceTransform) {
    if (!m_program || !m_vao || dabCount == 0 || firstDab >= m_dabCount || vpWidth <= 0 || vpHeight <= 0) {
        return;
    }

    const size_t clampedCount = std::min(dabCount, m_dabCount - firstDab);
    if (clampedCount == 0) return;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_program);
    glUniform4f(m_locTransform, transform.panX, transform.panY, transform.zoom, 0.0f);
    glUniform2f(m_locViewport, static_cast<float>(vpWidth), static_cast<float>(vpHeight));
    static constexpr float kIdentity4x4[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    glUniformMatrix4fv(m_locSurfaceTransform, 1, GL_FALSE,
                       surfaceTransform ? surfaceTransform : kIdentity4x4);
    glUniform4f(m_locBrushParams,
                static_cast<float>(static_cast<uint8_t>(brushConfig.tipShape)),
                std::clamp(brushConfig.aspectRatio, 0.1f, 1.0f),
                std::clamp(brushConfig.wetEdge, 0.0f, 1.0f),
                0.0f);
    glUniform4f(m_locMaterialParams,
                static_cast<float>(static_cast<uint8_t>(material.pattern)),
                std::clamp(material.toothDepth, 0.0f, 1.0f),
                std::max(material.toothScaleMm, 0.05f),
                std::clamp(material.absorbency, 0.0f, 1.0f));

    glBindVertexArray(m_vao);
    if (firstDab > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        const size_t baseBytes = firstDab * sizeof(RasterDabInstance);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(RasterDabInstance, center)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(RasterDabInstance, radius)));
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(RasterDabInstance, colorRgba)));
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(baseBytes + offsetof(RasterDabInstance, hardness)));
    }

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(clampedCount));

    if (firstDab > 0) {
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(offsetof(RasterDabInstance, center)));
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(offsetof(RasterDabInstance, radius)));
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(offsetof(RasterDabInstance, colorRgba)));
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(RasterDabInstance),
                              reinterpret_cast<const void*>(offsetof(RasterDabInstance, hardness)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace Folio::Backends
