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
            /**
             * Single-channel gray texture. Only useful if further processed on the GPU
             * since it's not possible to read this to CPU memory as a single-channel image
             * in OpenGL ES
             */
            GRAY,
            /**
             * Grayscale image where groups of consecutive 4 pixels have been encoded
             * into the R,G,B,A channels of the texture whose width is 1/4 of the original.
             * Can be read as a single-channel gray image to the CPU, but not as useful on
             * the GPU
             */
            GRAY_COMPRESSED
        };

        virtual int render(bool toFrameBuffer = true) = 0;

        /**
         * Read cpuSize() pixels to a 4-channel image. Note that OpenGL ES, one can only copy
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