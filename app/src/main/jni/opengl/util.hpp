#ifndef GL_UTIL_H_
#define GL_UTIL_H_

#include <GLES2/gl2.h>
#include "logging.hpp"

namespace glUtil {

void checkError(const char* op);
GLuint createProgram(const char* vertexShaderSource, const char* fragmentShaderSource);

extern const GLfloat screenQuadVertices[];
extern const GLfloat screenQuadTextureCoordinates[];
}

#endif