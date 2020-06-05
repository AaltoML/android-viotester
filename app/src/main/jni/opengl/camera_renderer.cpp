#include "camera_renderer.hpp"
#include "util.hpp"
#include <GLES2/gl2.h>
#include <vector>

namespace {
constexpr char vertexShaderSource[] = R"(
attribute vec4 vertex;
attribute vec2 textureCoords;
varying vec2 v_textureCoords;
void main() {
  v_textureCoords = textureCoords;
  gl_Position = vertex;
})";

constexpr char fragmentShaderSource[] = R"(
precision mediump float;
uniform sampler2D texture;
varying vec2 v_textureCoords;
void main() {
  vec4 color = texture2D(texture, v_textureCoords);
  gl_FragColor = vec4(color.zyx, 1.0); // NOTE: BRG -> RGB flip
})";
}

class CameraRendererImplementation : public CameraRenderer {
public:
    CameraRendererImplementation(int w, int h) : width(w), height(h) {
        log_debug("CameraRenderer::CameraRenderer %dx%d", width, height);
        shaderProgram = glUtil::createProgram(vertexShaderSource, fragmentShaderSource);
        assert(shaderProgram && "error compiling shaders");

        // GLSL variables
        uniformTexture = static_cast<GLuint>(glGetUniformLocation(shaderProgram, "texture"));
        attributeVertices = static_cast<GLuint>(glGetAttribLocation(shaderProgram, "vertex"));
        attributeTexCoords = static_cast<GLuint>(glGetAttribLocation(shaderProgram,
                                                                     "textureCoords"));
        glUtil::checkError("CameraRenderer GLSL setup");

        // allocate GL texture for the camera image but don't write any data
        textureId = 0; // avoid linter complaints, not really needed
        textureDataChanged = false;
        textureData = nullptr;
        textureWidth = 0;
        textureHeight = 0;
    }

    void render() final {
        if (textureId == 0) {
            if (textureData == nullptr) return;

            assert(textureWidth > 0 && textureHeight > 0);
            log_debug("Allocating texture of size %d x %d", textureWidth, textureHeight);
            glGenTextures(1, &textureId);
            glBindTexture(GL_TEXTURE_2D, textureId);
            // NOTE: do not try to use GL_RGB as the internal format (not supported)
            // Another note: The texture data is actually in BRGA format, but for some reason,
            // which is highly unclear to me, it is still shown correctly even though formally
            // input as "RGBA"
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, textureData);
            glUtil::checkError("Renderer texture allocation");
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUtil::checkError("CameraRenderer texture setup");
        } else if (textureDataChanged) {
            textureDataChanged = false;
            assert(textureData != nullptr && textureId != 0);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, textureHeight, GL_RGBA,
                            GL_UNSIGNED_BYTE, textureData);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUtil::checkError("CameraRenderer texture update");
        }

        glClear(GL_COLOR_BUFFER_BIT); // clear to avoid showing garbage in letterboxing

        const int x0 = (width - activeWidth) / 2;
        const int y0 = (height - activeHeight) / 2;
        glViewport(x0, y0, activeWidth, activeHeight);

        glUseProgram(shaderProgram);

        glUniform1i(uniformTexture, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textureId);

        glEnableVertexAttribArray(attributeVertices);
        glVertexAttribPointer(attributeVertices, 3, GL_FLOAT, GL_FALSE, 0, screenVertices.data());

        glEnableVertexAttribArray(attributeTexCoords);
        glVertexAttribPointer(attributeTexCoords, 2, GL_FLOAT, GL_FALSE, 0,
                              glUtil::screenQuadTextureCoordinates);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(GL_TEXTURE_2D, 0);

        glUseProgram(0);
        glDisableVertexAttribArray(attributeVertices);
        glDisableVertexAttribArray(attributeTexCoords);
        glUtil::checkError("CameraRenderer::draw");
    }

    void setTextureData(int texWidth, int texHeight, const void *buffer, AspectFixMethod aspectFix) final {
        if (textureId == 0) {
            // note xScale <-> yScale flipped due to rotated image
            const double yScale = width / double(texWidth);
            const double xScale = height / double(texHeight);

            const double scale = (aspectFix == AspectFixMethod::CROP)
                    ? std::min(xScale, yScale)
                    : std::max(xScale, yScale);

            const auto relXScale = float(xScale / scale);
            const auto relYScale = float(yScale / scale);

            if (aspectFix == AspectFixMethod::CROP) {
                activeWidth = width;
                activeHeight = height;

                screenVertices = {
                        -relXScale, -relYScale, 0,
                        +relXScale, -relYScale, 0,
                        -relXScale, +relYScale, 0,
                        +relXScale, +relYScale, 0,
                };
            } else {
                // changes viewport size
                activeWidth = int(width*relXScale);
                activeHeight = int(height*relYScale);

                screenVertices = {
                        -1, -1, 0,
                        +1, -1, 0,
                        -1, +1, 0,
                        +1, +1, 0,
                };
            }
            log_debug("screen %dx%d, camera %dx%d, window %dx%d, scale (%g, %g)",
                    width, height, texWidth, texHeight, activeWidth, activeHeight, relXScale, relYScale);
        } else {
            assert(texWidth == textureWidth && texHeight == textureHeight);
        }
        textureDataChanged = true;
        textureData = buffer;
        textureWidth = texWidth;
        textureHeight = texHeight;
    }

    bool matchDimensions(int w, int h) const final {
        return w == width && h == height;
    }

    int getActiveWidth() const final {
        return activeWidth;
    }

    int getActiveHeight() const final {
        return activeHeight;
    }

private:
    const int width;
    const int height;
    int activeWidth = 0;
    int activeHeight = 0;

    int textureWidth;
    int textureHeight;
    bool textureDataChanged;
    const void *textureData;

    std::vector<GLfloat> screenVertices;

    GLuint shaderProgram;
    GLuint textureId;

    GLuint attributeVertices;
    GLuint attributeTexCoords;
    GLuint uniformTexture;
};

std::unique_ptr<CameraRenderer> CameraRenderer::build(int w, int h) {
    return std::unique_ptr<CameraRenderer>(new CameraRendererImplementation(w, h));
}