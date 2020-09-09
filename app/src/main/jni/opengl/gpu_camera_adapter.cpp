#include "gpu_camera_adapter.hpp"
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
// NDK bug workaround: https://stackoverflow.com/a/31025110
#define __gl2_h_
#include <GLES2/gl2ext.h>
#include "util.hpp"
#include <memory>
#include <string>
#include <sstream>

namespace {
struct TextureSpec {
    int width;
    int height;
    GLint bindType = GL_TEXTURE_EXTERNAL_OES;
    GLint internalFormat = GL_RGBA;
    GLint format = GL_RGBA;

    std::string fragmentShaderBody = R"(
        void main()
        {
            gl_FragColor = texture2D(uTexture, v_texCoord);
        }
    )";
};

struct GpuCameraAdapterImplementation : GpuCameraAdapter {
    const int width, height;
    const int textureId;
    GLuint vertexBuffer, vertexIndexBuffer;

    const std::string vertexShaderSource = R"(
        precision highp float;
        attribute vec4 a_vertexPos;
        attribute vec2 a_texCoord;
        varying vec2 v_texCoord;
        void main()
        {
            v_texCoord = a_texCoord;
            gl_Position = a_vertexPos;
        }
    )";

    struct Texture : TextureAdapter {
        const GpuCameraAdapterImplementation &parent;
        TextureSpec spec;

        GLuint textureId;
        GLuint frameBufferId;

        GLuint shaderProgram;
        GLuint aVertexPos, aTexCoords;
        GLuint uTexture;

        Texture(const GpuCameraAdapterImplementation &parent, const TextureSpec &spec) :
                TextureAdapter(spec.width, spec.height),
                parent(parent),
                spec(spec)
        {

            GLuint one[1];
            glGenFramebuffers(1, one);
            frameBufferId = one[0];
            glGenTextures(1, one);
            textureId = one[0];

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textureId);

            glTexImage2D(GL_TEXTURE_2D, 0, spec.internalFormat, width, height, 0, spec.format, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            std::string fragmentShaderSource;
            {
                std::stringstream oss;
                std::string texUniformType = "sampler2D";
                if (spec.bindType == GL_TEXTURE_EXTERNAL_OES) {
                    oss << "#extension GL_OES_EGL_image_external : require\n";
                    texUniformType = "samplerExternalOES";
                }
                oss << "precision mediump float;\n";
                oss << "uniform " << texUniformType << " uTexture;";
                oss << "varying vec2 v_texCoord;\n";
                oss << spec.fragmentShaderBody;
                oss << std::endl;
                fragmentShaderSource = oss.str();
            }

            log_debug("fragment shader source:\n%s", fragmentShaderSource.c_str());

            shaderProgram = glUtil::createProgram(parent.vertexShaderSource.c_str(), fragmentShaderSource.c_str());
            assert(shaderProgram != 0);
            aVertexPos = glGetAttribLocation(shaderProgram, "a_vertexPos");
            aTexCoords = glGetAttribLocation(shaderProgram, "a_texCoord");
            uTexture = glGetUniformLocation(shaderProgram, "uTexture");

            glUtil::checkError("Texture::Texture");
        }

        ~Texture() final {
            // TODO: destroy the OpenGL objects, now leaks memory
        }

        void readPixels(uint8_t *pixels) final {
            glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId);
            // OpenGL ES only supports GL_RGBA / GL_UNSIGNED_BYTE (in practice)
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glUtil::checkError("Texture::readPixels");
        }

        int render(bool bindFrameBuffer) final {
            glUseProgram(shaderProgram);
            glUtil::checkError("Texture::render: glUseProgram");
            glDepthMask(GL_FALSE);
            // glDisable(GL_BLEND);

            if (bindFrameBuffer) glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId);
            glUtil::checkError("Texture::render: glBindFramebuffer");

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(spec.bindType, parent.textureId);
            glUtil::checkError("Texture::render: glBindTexture");
            glUniform1i(uTexture, 0);
            glUtil::checkError("Texture::render: glUniform1i");
            glTexParameteri(spec.bindType, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(spec.bindType, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(spec.bindType, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(spec.bindType, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            glBindBuffer(GL_ARRAY_BUFFER, parent.vertexBuffer);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, parent.vertexIndexBuffer);
            glUtil::checkError("Texture::render: glBindBuffer");

            glEnableVertexAttribArray(aVertexPos);
            glVertexAttribPointer(aVertexPos, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, nullptr);
            glUtil::checkError("Texture::render: glVertexAttribPointer");

            glEnableVertexAttribArray(aTexCoords);
            glVertexAttribPointer(aTexCoords, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5, (void *)(3 * sizeof(float))); // ???
            glUtil::checkError("Texture::render: glVertexAttribPointer");

            if (bindFrameBuffer) glViewport(0, 0, width, height);
            glUtil::checkError("Texture::render: glViewport");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);;
            glUtil::checkError("Texture::render: glDrawElements");

            glDisableVertexAttribArray(aVertexPos);
            glDisableVertexAttribArray(aTexCoords);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(spec.bindType, 0);
            glUseProgram(0);
            if (bindFrameBuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glUtil::checkError("Texture::render: unbind");

            return textureId;
        }
    };

    GpuCameraAdapterImplementation(int w, int h, int tid)
    :
        width(w),
        height(h),
        textureId(tid)
    {
        GLuint buf[2];
        glGenBuffers(2, buf);
        vertexBuffer = buf[0];
        vertexIndexBuffer = buf[1];

        // Set up vertices
        float vertices[] {
                // x, y, z, u, v
                -1, -1, 0, 0, 0,
                -1, 1, 0, 0, 1,
                1, 1, 0, 1, 1,
                1, -1, 0, 1, 0
        };
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Set up indices
        GLuint indices[] { 2, 1, 0, 0, 3, 2 };
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    std::unique_ptr<TextureAdapter> createTextureAdapter(TextureAdapter::Type type) final {
        TextureSpec spec;
        spec.width = width;
        spec.height = height;
        switch (type) {
            case TextureAdapter::Type::RGBA:
                break;
            case TextureAdapter::Type::BGRA:
                spec.fragmentShaderBody = R"(
                    void main()
                    {
                        gl_FragColor = texture2D(uTexture, v_texCoord).bgra;
                    }
                )";
                break;
            case TextureAdapter::Type::GRAY:
                spec.internalFormat = GL_R8;
                spec.format = GL_RED;
                break;
            case TextureAdapter::Type::GRAY_COMPRESSED: {
                assert((width % 4) == 0);
                spec.width = width / 4;
                // https://stackoverflow.com/a/37484800/1426569
                // pix = v_texCoord.x * WIDTH - 0.5; // pixel 0-based index
                // pix_other = pix * WIDTH_OTHER / WIDTH;
                // pix_next_n = pix_other + n
                // tex_coord_next_n = (pix_next_n + 0.5) / WIDTH_OTHER
                //  = ((v_texCoord.x * WIDTH - 0.5) * WIDTH_OTHER / WIDTH + n + 0.5) / WIDTH_OTHER
                //  = ((v_texCoord.x - 0.5 / WIDTH) * WIDTH_OTHER + n + 0.5) / WIDTH_OTHER
                //  = v_texCoord.x - 0.5 / WIDTH + (n + 0.5) / WIDTH_OTHER
                //  = v_texCoord.x + n / WIDTH_OTHER + 0.5 * (1/WIDTH_OTHER - 1/WIDTH)
                spec.fragmentShaderBody = R"(
                    void main()
                    {
                        const int WIDTH = )" + std::to_string(spec.width) + R"(;
                        const int WIDTH_ORIG = 4 * WIDTH;
                        const float offset = 0.5 * (1.0/float(WIDTH_ORIG) - 1.0/float(WIDTH));
                        vec2 delta = vec2(1.0 / float(WIDTH_ORIG), 0);
                        vec2 coord = v_texCoord + vec2(offset, 0);
                        gl_FragColor = vec4(
                            texture2D(uTexture, coord).g,
                            texture2D(uTexture, coord + delta).g,
                            texture2D(uTexture, coord + 2.0 * delta).g,
                            texture2D(uTexture, coord + 3.0 * delta).g
                        );
                    }
                )";
                }
                break;
            default:
                assert(false);
        }

        return std::unique_ptr<TextureAdapter>(new Texture(*this, spec));
    }
};
}

GpuCameraAdapter::TextureAdapter::TextureAdapter(int w, int h) : width(w), height(h) {}
GpuCameraAdapter::TextureAdapter::~TextureAdapter() = default;
GpuCameraAdapter::~GpuCameraAdapter() = default;

std::unique_ptr<GpuCameraAdapter> GpuCameraAdapter::create(int w, int h, int tid) {
    return std::unique_ptr<GpuCameraAdapter>(new GpuCameraAdapterImplementation(w, h, tid));
}