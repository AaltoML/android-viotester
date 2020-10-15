#include "../algorithm_module.hpp"
#include "logging.hpp"

#include <accelerated-arrays/standard_ops.hpp>
#include <accelerated-arrays/opengl/image.hpp>
#include <accelerated-arrays/opengl/operations.hpp>

namespace {
std::pair<std::function<void()>, std::shared_ptr<accelerated::Image>> createFilter(
        accelerated::Image::Factory &images,
        accelerated::operations::StandardFactory &ops,
        accelerated::Image &cameraImage,
        const accelerated::Image &screen) {
    (void)screen;

    typedef accelerated::FixedPoint<std::uint8_t> Ufixed8;

    // Note: careful with createLike, as cameraImage has a different storage type (EXTERNAL_OES)
    std::shared_ptr<accelerated::Image> grayBuf = images.create<Ufixed8, 1>(
        cameraImage.width,
        cameraImage.height);

    std::shared_ptr<accelerated::Image>
        sobelXBuf = images.createLike(*grayBuf),
        sobelYBuf = images.createLike(*grayBuf);

    auto cameraToGray = ops.pixelwiseAffine({{ 0,1,0,0 }}).build(cameraImage, *grayBuf);

    // note: signs don't matter here that much
    auto sobelX = ops.fixedConvolution2D({
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    }).setBias(0.5).setBorder(accelerated::Image::Border::MIRROR).build(*grayBuf);

    auto sobelY = ops.fixedConvolution2D({
        {-1,-2,-1 },
        { 0, 0, 0 },
        { 1, 2, 1 }
    }).setBias(0.5).setBorder(accelerated::Image::Border::MIRROR).build(*grayBuf);

    std::shared_ptr<accelerated::Image> screenBuffer = images.create<Ufixed8, 4>(
            cameraImage.width,
            cameraImage.height);

    auto renderOp = ops.affineCombination()
            .addLinearPart({ {1}, {0}, {0}, {0} })
            .addLinearPart({ {0}, {1}, {0}, {0} })
            .setBias({ 0, 0, 0.5, 1 })
            .build(*grayBuf, *screenBuffer);

    auto cameraProcessor = [
            &cameraImage,
            cameraToGray, grayBuf,
            sobelX, sobelXBuf,
            sobelY, sobelYBuf,
            renderOp, screenBuffer]() {
        accelerated::operations::callUnary(cameraToGray, cameraImage, *grayBuf);
        accelerated::operations::callUnary(sobelX, *grayBuf, *sobelXBuf);
        accelerated::operations::callUnary(sobelY, *grayBuf, *sobelYBuf);
        accelerated::operations::callBinary(renderOp, *sobelXBuf, *sobelYBuf, *screenBuffer);
    };

    return std::make_pair(cameraProcessor, screenBuffer);
}

struct GpuExampleModule : public AlgorithmModule {
    std::unique_ptr<accelerated::Image> camera, screen;

    std::unique_ptr<accelerated::Queue> processor;
    std::unique_ptr<accelerated::opengl::Image::Factory> imageFactory;
    std::unique_ptr<accelerated::operations::StandardFactory> opsFactory;
    std::function<void()> cameraProcessor, renderer;


    GpuExampleModule(int textureId, int width, int height) {
        processor = accelerated::Processor::createQueue();
        imageFactory = accelerated::opengl::Image::createFactory(*processor);
        opsFactory = accelerated::opengl::operations::createFactory(*processor);
        camera = imageFactory->wrapTexture<accelerated::FixedPoint<std::uint8_t>, 4>(
                textureId, width, height,
                accelerated::ImageTypeSpec::StorageType::GPU_OPENGL_EXTERNAL);
    }

    void setupRendering(int screenWidth, int screenHeight) final {
        screen = imageFactory->wrapScreen(screenWidth, screenHeight);
        auto pair = createFilter(*imageFactory, *opsFactory, *camera, *screen);
        cameraProcessor = pair.first;
        auto screenBuffer = pair.second;

        // keep aspect ratio
        double xScale = screenWidth / double(camera->width);
        double yScale = screenHeight / double(camera->height);

        const bool LETTERBOX = false;
        const double scale = LETTERBOX
            ? std::min(xScale, yScale)
            : std::max(xScale, yScale);
        const auto relXScale = float(xScale / scale);
        const auto relYScale = float(yScale / scale);

        auto renderOp = opsFactory->rescale(relXScale, -relYScale)
                .setTranslation(
                        (1.0 - relXScale) * 0.5,
                        relYScale + (1.0 - relYScale) * 0.5)
                .setBorder(accelerated::Image::Border::CLAMP)
                .setInterpolation(accelerated::Image::Interpolation::LINEAR)
                .build(*screenBuffer, *screen);

        renderer = [this, renderOp, screenBuffer]() {
            accelerated::operations::callUnary(renderOp, *screenBuffer, *screen);
        };
    }

    void addFrame(double t, int cameraInd, double focalLength, double px, double py) final {
        (void)t; (void)cameraInd; (void)focalLength; (void)px; (void)py;
        if (cameraProcessor) cameraProcessor();
        processor->processAll();
    }

    void render() final {
        if (renderer) renderer();
        processor->processAll();
    }

    void addGyro(double t, const recorder::Vector3d &val) final { (void)t; (void)val; }
    void addAcc(double t, const recorder::Vector3d &val) final { (void)t; (void)val; }
    // std::string status() const final { return "..."; }
};

}

std::unique_ptr<AlgorithmModule> buildGpuExample(int textureId, int w, int h, const AlgorithmModule::json &settings) {
    (void)settings;
    return AlgorithmModule::makeThreadSafe(new GpuExampleModule(textureId, w, h));
}