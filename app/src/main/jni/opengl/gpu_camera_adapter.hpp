#ifndef GPU_CAMERA_ADAPTER_HPP
#define GPU_CAMERA_ADAPTER_HPP

#include <memory>
#include <vector>

struct GpuCameraAdapter {
    struct TextureAdapter {
        const int width;
        const int height;

        enum class Type {
            RGBA,
            BGRA,
            GRAY
        };

        virtual int render(bool toFrameBuffer = true) = 0;

        /**
         * Read cpuSize() pixels to a 4-channel image. Note that OpenGL ES, one can only cop
         * data to the CPU side as GL_RGBA so we always have 4 bytes per pixel.
         */
        virtual void readPixels(uint8_t *pixels) = 0;
        inline int readPixelsSize() const {
            constexpr int BYTES_PER_PIXEL = 4;
            return width * height * BYTES_PER_PIXEL;
        }

        TextureAdapter(int w, int h);
        virtual ~TextureAdapter();
    };

    static std::unique_ptr<GpuCameraAdapter> create(int width, int height, int textureId);
    virtual std::unique_ptr<TextureAdapter> createTextureAdapter(TextureAdapter::Type type) = 0;

    virtual ~GpuCameraAdapter();
};

#endif