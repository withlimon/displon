#include "ui/gl/Renderer.h"

#include <glad/gl.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {

const char* kVert = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aShape;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
out vec4 vShape;
void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vShape = aShape;
}
)";

const char* kFrag = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in vec4 vShape;   // rounded rect: halfW, halfH, radius, border (halfW = 0: plain colour)
uniform sampler2D uTex;
uniform int uUseTex;
out vec4 frag;
void main() {
    if (uUseTex == 1) {
        float a = texture(uTex, vUV).r;  // alpha atlas
        frag = vec4(vColor.rgb, vColor.a * a);
    } else if (uUseTex == 2) {
        vec4 t = texture(uTex, vUV);  // full-color RGBA (app logo); tint only fades it
        frag = vec4(t.rgb, t.a * vColor.a);
    } else {
        frag = vColor;
        if (vShape.x > 0.0) {
            // vUV = pixel position relative to the rect centre; signed distance to a rounded box.
            vec2 q = abs(vUV) - (vShape.xy - vec2(vShape.z));
            float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vShape.z;
            float a = clamp(0.5 - sd, 0.0, 1.0);
            if (vShape.w > 0.0) a *= clamp(0.5 + sd + vShape.w, 0.0, 1.0);   // ring only
            frag = vec4(vColor.rgb, vColor.a * a);
        }
    }
}
)";

std::uint32_t Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

bool Renderer::Init() {
    GLuint vs = Compile(GL_VERTEX_SHADER, kVert);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    uProj_ = glGetUniformLocation(program_, "uProj");
    uTex_ = glGetUniformLocation(program_, "uTex");
    uUseTex_ = glGetUniformLocation(program_, "uUseTex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, hw));
    glBindVertexArray(0);
    return true;
}

void Renderer::Shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    vbo_ = vao_ = program_ = 0;
}

void Renderer::BeginFrame(int fbWidth, int fbHeight, const carbon::Color& clear) {
    fbW_ = fbWidth;
    fbH_ = fbHeight;
    batches_.clear();
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    ClearScissor();
}

void Renderer::EndFrame() {
    Flush();
}

Renderer::Batch& Renderer::BatchFor(std::uint32_t tex, bool color) {
    if (!batches_.empty() && batches_.back().texture == tex && batches_.back().color == color)
        return batches_.back();
    batches_.push_back({});
    batches_.back().texture = tex;
    batches_.back().color = color;
    return batches_.back();
}

void Renderer::FillRect(const Rect& r, const carbon::Color& c) {
    auto& b = BatchFor(0);
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    auto push = [&](float x, float y) {
        b.verts.push_back({x, y, 0, 0, c.r, c.g, c.b, c.a, 0, 0, 0, 0});
    };
    // two triangles
    push(x0, y0); push(x1, y0); push(x1, y1);
    push(x0, y0); push(x1, y1); push(x0, y1);
}

void Renderer::StrokeRect(const Rect& r, const carbon::Color& c, float thickness) {
    // top
    FillRect({r.x, r.y, r.w, thickness}, c);
    // bottom
    FillRect({r.x, r.y + r.h - thickness, r.w, thickness}, c);
    // left
    FillRect({r.x, r.y, thickness, r.h}, c);
    // right
    FillRect({r.x + r.w - thickness, r.y, thickness, r.h}, c);
}

void Renderer::FocusRing(const Rect& r, const carbon::Color& c) {
    StrokeRect({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, c, 2.f);
}

void Renderer::PushRound(const Rect& r, float radius, const carbon::Color& c, float border) {
    if (r.w <= 0 || r.h <= 0) return;
    auto& b = BatchFor(0);
    const float hw = r.w * 0.5f, hh = r.h * 0.5f;
    const float rad = std::max(0.f, std::min(radius, std::min(hw, hh)));
    const float cx = r.x + hw, cy = r.y + hh;
    // The quad is 1 px larger than the rect so the anti-aliased outer edge is not clipped.
    const float x0 = r.x - 1, y0 = r.y - 1, x1 = r.x + r.w + 1, y1 = r.y + r.h + 1;
    auto push = [&](float x, float y) {
        b.verts.push_back({x, y, x - cx, y - cy, c.r, c.g, c.b, c.a, hw, hh, rad, border});
    };
    push(x0, y0); push(x1, y0); push(x1, y1);
    push(x0, y0); push(x1, y1); push(x0, y1);
}

void Renderer::FillRoundRect(const Rect& r, float radius, const carbon::Color& c) {
    PushRound(r, radius, c, 0.f);
}

void Renderer::StrokeRoundRect(const Rect& r, float radius, const carbon::Color& c, float thickness) {
    PushRound(r, radius, c, thickness > 0.f ? thickness : 1.f);
}

void Renderer::TexturedQuad(const Rect& r, float u0, float v0, float u1, float v1,
                            std::uint32_t texture, const carbon::Color& tint, bool colorTexture) {
    auto& b = BatchFor(texture, colorTexture);
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    auto push = [&](float x, float y, float u, float v) {
        b.verts.push_back({x, y, u, v, tint.r, tint.g, tint.b, tint.a, 0, 0, 0, 0});
    };
    push(x0, y0, u0, v0); push(x1, y0, u1, v0); push(x1, y1, u1, v1);
    push(x0, y0, u0, v0); push(x1, y1, u1, v1); push(x0, y1, u0, v1);
}

void Renderer::SetScissor(const Rect& r) {
    Flush();
    glEnable(GL_SCISSOR_TEST);
    // GL scissor origin is bottom-left
    glScissor(static_cast<int>(r.x), fbH_ - static_cast<int>(r.y + r.h),
              static_cast<int>(r.w), static_cast<int>(r.h));
    scissorOn_ = true;
}

void Renderer::ClearScissor() {
    if (scissorOn_) {
        Flush();
        glDisable(GL_SCISSOR_TEST);
        scissorOn_ = false;
    }
}

void Renderer::Flush() {
    if (batches_.empty()) return;
    glUseProgram(program_);
    // Ortho: origin top-left, y down (UI space)
    float proj[16] = {
        2.f / fbW_, 0, 0, 0,
        0, -2.f / fbH_, 0, 0,
        0, 0, -1, 0,
        -1, 1, 0, 1};
    glUniformMatrix4fv(uProj_, 1, GL_FALSE, proj);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    for (auto& b : batches_) {
        if (b.verts.empty()) continue;
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(b.verts.size() * sizeof(Vertex)),
                     b.verts.data(), GL_STREAM_DRAW);
        if (b.texture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, b.texture);
            glUniform1i(uTex_, 0);
            glUniform1i(uUseTex_, b.color ? 2 : 1);
        } else {
            glUniform1i(uUseTex_, 0);
        }
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(b.verts.size()));
    }
    batches_.clear();
    glBindVertexArray(0);
}
