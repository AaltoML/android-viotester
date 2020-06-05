#include "util.hpp"
#include <cstdlib>
#include <cassert>
#include <vector>

namespace glUtil {
const GLfloat screenQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, +1.0f, -1.0f, 0.0f,
    -1.0f, +1.0f, 0.0f, +1.0f, +1.0f, 0.0f,
};

const GLfloat screenQuadTextureCoordinates[] = {
    0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
};

void checkError(const char* op) {
    GLint error;
    bool any = false;
    while((error = glGetError())) {
        any = true;
        log_error("operation %s produced glError (0x%x)\n", op, error);
    }
    if (any) {
        abort();
    }
}

static GLuint loadShader(GLenum shaderType, const char* shaderSource) {
    const GLuint shader = glCreateShader(shaderType);
    assert(shader);

    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLint len = 0;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        assert(len);

        std::vector<char> buf(static_cast<std::size_t>(len));
        glGetShaderInfoLog(shader, len, nullptr, buf.data());
        log_error("Error compiling shader %d:\n%s\n", shaderType, buf.data());
        glDeleteShader(shader);
        assert(false);
    }

    return shader;
}

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint program = glCreateProgram();
    assert(program);
    glAttachShader(program, vertexShader);
    checkError("glAttachShader");
    glAttachShader(program, fragmentShader);
    checkError("glAttachShader");
    glLinkProgram(program);
    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
        if (bufLength) {
            std::vector<char> buf(static_cast<std::size_t>(bufLength));
            glGetProgramInfoLog(program, bufLength, nullptr, buf.data());
            log_error("Could not link program:\n%s\n", buf.data());
        }
        glDeleteProgram(program);
        assert(false);
    }
    return program;
}
}