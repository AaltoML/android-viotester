#include "gpu_camera_adapter.hpp"
#include <memory>
#include <accelerated-arrays/standard_ops.hpp>
#include <accelerated-arrays/opengl/image.hpp>
#include <accelerated-arrays/opengl/operations.hpp>

namespace {
struct GpuCameraAdapterImplementation : GpuCameraAdapter {
    const int width, height;
    const int textureId;

    std::unique_ptr<accelerated::Queue> processor;
    std::unique_ptr<accelerated::opengl::Image::Factory> imageFactory;
    std::unique_ptr<accelerated::operations::StandardFactory> opsFactory;
    std::shared_ptr<accelerated::Image> cameraImage;
    std::shared_ptr<accelerated::Image> screen;

    GpuCameraAdapterImplementation(int w, int h, int tid)
    :
        width(w),
        height(h),
        textureId(tid)
    {
        processor = accelerated::Processor::createQueue();
        imageFactory = accelerated::opengl::Image::createFactory(*processor);
        opsFactory = accelerated::opengl::operations::createFactory(*processor);
        cameraImage = imageFactory->wrapTexture<accelerated::FixedPoint<std::uint8_t>, 4>(tid, w, h, accelerated::ImageTypeSpec::StorageType::GPU_OPENGL_EXTERNAL);
        screen = imageFactory->wrapScreen(w, h);
    }

    struct TextureWrapper : TextureAdapter {
        GpuCameraAdapterImplementation &parent;
        accelerated::operations::Function function;
        std::shared_ptr<accelerated::Image> image;

        TextureWrapper(GpuCameraAdapterImplementation &parent, std::shared_ptr<accelerated::Image> img) :
                TextureAdapter(img->width, img->height),
                parent(parent),
                image(img)
        {}

        void readPixels(uint8_t *pixels) final {
            image->readRaw(pixels);
            // the preferred way to do synchronous operations in the GL thread
            parent.processor->processAll();
        }

        std::size_t readPixelsSize() const final {
            return image->size();
        }

        void render(bool bindFrameBuffer) final {
            if (function) {
                if (bindFrameBuffer) {
                    accelerated::operations::callUnary(function, *parent.cameraImage, *image);
                } else {
                    accelerated::operations::callUnary(function, *parent.cameraImage, *parent.screen);
                }
            }
            parent.processor->processAll();
        }
    };

    template <int N>
    std::unique_ptr<accelerated::Image> newBuffer() {
        return imageFactory->create<accelerated::FixedPoint<std::uint8_t>, N>(width,height);
    }

    std::unique_ptr<TextureAdapter> createTextureAdapter(TextureAdapter::Type type) final {
        switch (type) {
            case TextureAdapter::Type::RGBA:
                return std::unique_ptr<TextureAdapter>(new TextureWrapper(*this, cameraImage));
            case TextureAdapter::Type::BGRA:
            {
                auto r = new TextureWrapper(*this, newBuffer<4>());
                r->function = opsFactory->pixelwiseAffine({
                            {0,0,1,0},
                            {0,1,0,0},
                            {1,0,0,0},
                            {0,0,0,1}
                        }).build(*cameraImage, *r->image);

                return std::unique_ptr<TextureAdapter>(r);
            }
            case TextureAdapter::Type::GRAY_COMPRESSED:
            case TextureAdapter::Type::GRAY:
            {
                auto r = new TextureWrapper(*this, newBuffer<1>());
                r->function = opsFactory->pixelwiseAffine({{0,1,0,0}}) // green channel
                        .build(*cameraImage, *r->image);

                return std::unique_ptr<TextureAdapter>(r);
            }
            default:
                assert(false);
        }
    }
};
}

GpuCameraAdapter::TextureAdapter::TextureAdapter(int w, int h) : width(w), height(h) {}
GpuCameraAdapter::TextureAdapter::~TextureAdapter() = default;
GpuCameraAdapter::~GpuCameraAdapter() = default;

std::unique_ptr<GpuCameraAdapter> GpuCameraAdapter::create(int w, int h, int tid) {
    return std::unique_ptr<GpuCameraAdapter>(new GpuCameraAdapterImplementation(w, h, tid));
}