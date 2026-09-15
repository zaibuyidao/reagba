#pragma once
#include "video/VideoTypes.h"
#include "video/BuiltinShaders.h"
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <epoxy/gl.h>
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace reagba {
class GLRenderer {
    GLuint program_ = 0, texture_ = 0, vao_ = 0;
    GLint sourceSize_ = -1, outputSize_ = -1, shaderPreset_ = -1;
    static GLuint Shader(GLenum type, const char *source) {
        auto shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(log);
        }
        return shader;
    }

  public:
    GLRenderer() {
        const auto fragment = shaders::GLSLFragment();
        auto vs = Shader(GL_VERTEX_SHADER, shaders::GLSLVertex);
        auto ps = Shader(GL_FRAGMENT_SHADER, fragment.c_str());
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, ps);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(ps);
        GLint ok;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
            throw std::runtime_error("OpenGL program link failed");
        sourceSize_ = glGetUniformLocation(program_, "sourceSize");
        outputSize_ = glGetUniformLocation(program_, "outputSize");
        shaderPreset_ = glGetUniformLocation(program_, "shaderPreset");
        glGenVertexArrays(1, &vao_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        Frame blank{};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ~GLRenderer() {
        glDeleteTextures(1, &texture_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(program_);
    }
    void Upload(const Frame &frame) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Width, Height, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
    }
    void Draw(int width, int height, VideoSettings s, int yOffset = 0) {
        if (width <= 0 || height <= 0) return;
        glViewport(0, 0, width, height);
        glClearColor(.027f, .039f, .055f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        float scale = std::min(float(width) / Width, float(height) / Height);
        if (s.integerScaling && scale >= 1)
            scale = std::floor(scale);
        int w = int(Width * scale), h = int(Height * scale);
        glViewport((width - w) / 2, (height - h) / 2 + yOffset, w, h);
        glUseProgram(program_);
        glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glUniform1i(glGetUniformLocation(program_, "frame"), 0);
        glUniform2f(sourceSize_, float(Width), float(Height));
        glUniform2f(outputSize_, float(w), float(h));
        glUniform1i(shaderPreset_, int(s.shader));
        const auto filter = s.linear && s.shader == ShaderPreset::None ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
};
} // namespace reagba
