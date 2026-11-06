/**
 * =========================================================================================
 * @file src/debug/gl_shape_renderer.cpp
 * @brief GPU-Driven Analytic Signed Distance Field (SDF) Instanced Shape Renderer
 * =========================================================================================
 */

#include "inspector/gl_shape_renderer.hpp"
#include "inspector/gl_loader.hpp"

#include <iostream>
#include <string>

namespace Folio {

bool GlShapeRenderer::Init() {
#if defined(__ANDROID__)
    const char* glslHeader = "#version 300 es\nprecision highp float;\n";
#else
    const char* glslHeader = "#version 330 core\n";
#endif

    // Vertex Shader:
    // Expands unit quad [-1, 1]^2 by halfSize + AA padding in world coordinates
    std::string vSrc = std::string(glslHeader) +
        "layout(location = 0) in vec2 aQuadPos;\n"
        "layout(location = 1) in vec2 aCenter;\n"
        "layout(location = 2) in vec2 aSize;\n"
        "layout(location = 3) in vec2 aParams; // (cornerRadius, strokeWidth)\n"
        "layout(location = 4) in vec4 aFillColor;\n"
        "layout(location = 5) in vec4 aStrokeColor;\n"
        "layout(location = 6) in float aKind;\n"
        "uniform vec4 uTransform; // (panX, panY, zoom, unused)\n"
        "uniform vec2 uViewport;  // (width, height)\n"
        "out vec2 vLocalPos;\n"
        "flat out vec2 vHalfSize;\n"
        "flat out float vCornerRadius;\n"
        "flat out float vStrokeWidth;\n"
        "flat out vec4 vFillColor;\n"
        "flat out vec4 vStrokeColor;\n"
        "flat out int vKind;\n"
        "void main() {\n"
        "    float zoom = max(uTransform.z, 1e-4);\n"
        "    float padWorld = 2.0 / zoom;\n"
        "    vec2 halfSize = 0.5 * aSize;\n"
        "    vec2 extents = halfSize + vec2(padWorld);\n"
        "    vec2 worldPos = aCenter + aQuadPos * extents;\n"
        "    vec2 screenPos = worldPos * zoom + uTransform.xy;\n"
        "    vec2 ndc = vec2(\n"
        "        (screenPos.x / uViewport.x) * 2.0 - 1.0,\n"
        "        1.0 - (screenPos.y / uViewport.y) * 2.0\n"
        "    );\n"
        "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
        "    vLocalPos = aQuadPos * extents;\n"
        "    vHalfSize = halfSize;\n"
        "    vCornerRadius = aParams.x;\n"
        "    vStrokeWidth = aParams.y;\n"
        "    vFillColor = aFillColor;\n"
        "    vStrokeColor = aStrokeColor;\n"
        "    vKind = int(aKind + 0.5);\n"
        "}\n";

    // Fragment Shader:
    // Evaluates analytic 2D Signed Distance Field (Box, Rounded Box, Circle, Ellipse)
    std::string fSrc = std::string(glslHeader) +
        "in vec2 vLocalPos;\n"
        "flat in vec2 vHalfSize;\n"
        "flat in float vCornerRadius;\n"
        "flat in float vStrokeWidth;\n"
        "flat in vec4 vFillColor;\n"
        "flat in vec4 vStrokeColor;\n"
        "flat in int vKind;\n"
        "uniform vec4 uTransform;\n"
        "out vec4 fragColor;\n"
        "float sdBox(vec2 p, vec2 b, float r) {\n"
        "    vec2 q = abs(p) - b + vec2(r);\n"
        "    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;\n"
        "}\n"
        "void main() {\n"
        "    float dist = 0.0;\n"
        "    if (vKind == 2) {\n"
        "        // Circle\n"
        "        dist = length(vLocalPos) - vHalfSize.x;\n"
        "    } else if (vKind == 3) {\n"
        "        // Ellipse (approximate analytic distance)\n"
        "        vec2 p = abs(vLocalPos);\n"
        "        vec2 ab = max(vHalfSize, vec2(1e-4));\n"
        "        dist = (length(p / ab) - 1.0) * min(ab.x, ab.y);\n"
        "    } else {\n"
        "        // Rectangle or RoundedRectangle\n"
        "        float r = clamp(vCornerRadius, 0.0, min(vHalfSize.x, vHalfSize.y));\n"
        "        dist = sdBox(vLocalPos, vHalfSize, r);\n"
        "    }\n"
        "    float zoom = max(uTransform.z, 1e-4);\n"
        "    float fw = fwidth(dist);\n"
        "    float aa = (fw > 1e-6 && fw < 4.0 / zoom) ? fw : (1.0 / zoom);\n"
        "    float shapeAlpha = smoothstep(0.5 * aa, -0.5 * aa, dist);\n"
        "    if (shapeAlpha <= 0.0) discard;\n"
        "    vec4 col = vFillColor;\n"
        "    if (vStrokeWidth > 0.0) {\n"
        "        float borderDist = abs(dist + 0.5 * vStrokeWidth) - 0.5 * vStrokeWidth;\n"
        "        float borderAlpha = smoothstep(0.5 * aa, -0.5 * aa, borderDist);\n"
        "        col = mix(vFillColor, vStrokeColor, borderAlpha);\n"
        "    }\n"
        "    fragColor = col * shapeAlpha;\n"
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
        std::cerr << "[GlShapeRenderer] Program link failed: " << log << std::endl;
        return false;
    }

    m_locTransform = glGetUniformLocation(m_program, "uTransform");
    m_locViewport  = glGetUniformLocation(m_program, "uViewport");

    static constexpr float kUnitQuad[12] = {
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

    // Quad VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuad), kUnitQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glVertexAttribDivisor(0, 0);

    // Instance VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);

    // 1. vec2 center
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, center)));
    glVertexAttribDivisor(1, 1);

    // 2. vec2 size
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, size)));
    glVertexAttribDivisor(1, 1);

    // 3. vec2 params (cornerRadius, strokeWidth)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, cornerRadius)));
    glVertexAttribDivisor(1, 1);

    // 4. vec4 fillColor (normalized bytes)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, fillColor)));
    glVertexAttribDivisor(1, 1);

    // 5. vec4 strokeColor (normalized bytes)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, strokeColor)));
    glVertexAttribDivisor(1, 1);

    // 6. float kind
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(GpuShapeInstance),
                          reinterpret_cast<const void*>(offsetof(GpuShapeInstance, kind)));
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return true;
}

void GlShapeRenderer::Shutdown() {
    if (m_instanceVbo) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_quadVbo)     { glDeleteBuffers(1, &m_quadVbo);     m_quadVbo = 0; }
    if (m_vao)         { glDeleteVertexArrays(1, &m_vao);    m_vao = 0; }
    if (m_program)     { glDeleteProgram(m_program);         m_program = 0; }
}

void GlShapeRenderer::UploadShapes(std::span<const GpuShapeInstance> shapes) {
    m_shapes.assign(shapes.begin(), shapes.end());
    if (!m_instanceVbo || m_shapes.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_shapes.size() * sizeof(GpuShapeInstance)),
                 m_shapes.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlShapeRenderer::Render(const Debug::CanvasTransform& transform, int vpWidth, int vpHeight) {
    if (!m_program || !m_vao || m_shapes.empty() || vpWidth <= 0 || vpHeight <= 0) return;

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_program);
    glUniform4f(m_locTransform, transform.panX, transform.panY, transform.zoom, 0.0f);
    glUniform2f(m_locViewport, static_cast<float>(vpWidth), static_cast<float>(vpHeight));

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(m_shapes.size()));
    glBindVertexArray(0);
    glUseProgram(0);
}

unsigned int GlShapeRenderer::CompileShader(unsigned int type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[GlShapeRenderer] Shader compile error: " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace Folio
