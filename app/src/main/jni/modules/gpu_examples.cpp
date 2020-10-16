#include "../algorithm_module.hpp"
#include "logging.hpp"

#include <accelerated-arrays/standard_ops.hpp>
#include <accelerated-arrays/opengl/image.hpp>
#include <accelerated-arrays/opengl/operations.hpp>
#include <sstream>

//#define RENDER_SOBEL
//#define RENDER_UNFILTERED_STRUCTURE
//#define RENDER_STRUCTURE

namespace {
std::pair<std::function<void()>, std::shared_ptr<accelerated::Image>> createFilter(
        accelerated::Image::Factory &images,
        accelerated::opengl::operations::Factory &ops,
        accelerated::Image &cameraImage,
        const accelerated::Image &screen) {
    (void)screen;

    typedef accelerated::FixedPoint<std::uint8_t> Ufixed8;

    std::shared_ptr<accelerated::Image> screenBuffer = images.create<Ufixed8, 4>(
            cameraImage.width,
            cameraImage.height);

    // Note: careful with createLike, as cameraImage has a different storage type (EXTERNAL_OES)
    std::shared_ptr<accelerated::Image> grayBuf = images.create<Ufixed8, 1>(
        cameraImage.width,
        cameraImage.height);

    auto cameraToGray = ops.pixelwiseAffine({{ 0,1,0,0 }}).build(cameraImage, *grayBuf);

    //typedef Ufixed8 SobelType;
    //double sobelScale = 1, sobelBias = 0.5;

    typedef std::int16_t SobelType;
    float sobelScale = 100, sobelBias = 0;

    auto borderType = accelerated::Image::Border::MIRROR;

    std::shared_ptr<accelerated::Image> sobelXBuf = images.create<SobelType, 1>(grayBuf->width, grayBuf->height);
    std::shared_ptr<accelerated::Image> sobelYBuf = images.createLike(*sobelXBuf);

    // note: signs don't matter here that much
    auto sobelX = ops.fixedConvolution2D({
            {-1, 0, 1},
            {-2, 0, 2},
            {-1, 0, 1}
        })
        .scaleKernelValues(sobelScale)
        .setBias(sobelBias)
        .setBorder(borderType)
        .build(*grayBuf, *sobelXBuf);

    auto sobelY = ops.fixedConvolution2D({
            {-1,-2,-1 },
            { 0, 0, 0 },
            { 1, 2, 1 }
        })
        .scaleKernelValues(sobelScale)
        .setBias(sobelBias)
        .setBorder(borderType)
        .build(*grayBuf, *sobelYBuf);

#ifdef RENDER_SOBEL
    const float renderBias = -sobelBias / sobelScale + 0.5f;
    auto renderOp = ops.affineCombination()
            .addLinearPart({ {1}, {0}, {0}, {0} })
            .addLinearPart({ {0}, {1}, {0}, {0} })
            .setBias({ renderBias, renderBias, 0.5, 1 })
            .build(*sobelXBuf, *screenBuffer);

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
#else
    typedef std::int16_t StructureType;
    const float structureScale = sobelScale, structureBias = sobelBias;
    // NOTE: does not seem to work with 3 channels, debug why
    std::shared_ptr<accelerated::Image> structureBuf = images.create<StructureType, 4>(grayBuf->width, grayBuf->height);
    const char *structureGlslType = "ivec4"; // could expose these in accelerated-arrays

    std::string structShaderBody;
    {
        std::ostringstream oss;
        oss << "const float inScaleInv = float(" << (1.0 / sobelScale) << ");\n"
            << "const float inBias = float(" << sobelBias << ");\n"
            << "const float outScale = float(" << structureScale << ");\n"
            << "const float outBias = float(" << structureBias << ");\n"
            << R"(
            void main() {
                ivec2 coord = ivec2(v_texCoord * vec2(u_outSize));
                vec2 der = (vec2(
                    float(texelFetch(u_texture1, coord, 0).r),
                    float(texelFetch(u_texture2, coord, 0).r)) - inBias) * inScaleInv;
                vec2 d2 = der*der;
                float dxdy = der.x*der.y;
                outValue = )" << structureGlslType << R"((vec4(
                    d2.x, dxdy,
                    dxdy, d2.y
                ) * outScale + outBias);
            }
            )";
        structShaderBody = oss.str();
    }
    auto structureOp = ops.wrapShader(structShaderBody, { *sobelXBuf, *sobelYBuf }, *structureBuf);

    std::shared_ptr<accelerated::Image> structureBuf2 = images.createLike(*structureBuf);

#ifdef RENDER_STRUCTURE
    const float renderBias = -structureBias / structureScale + 0.5f;
    auto renderOp = ops.pixelwiseAffine({
                {1, 0, 0, 0},
                {0, 1, 0, 0},
                {0, 0, 1, 0},
                {0, 0, 0, 0}
            })
            .scaleLinearValues(1.0 / structureScale)
            .setBias({ renderBias, renderBias, renderBias, 1 })
            .build(*structureBuf, *screenBuffer);
#endif

#ifdef RENDER_UNFILTERED_STRUCTURE
    auto cameraProcessor = [
            &cameraImage,
            cameraToGray, grayBuf,
            sobelX, sobelXBuf,
            sobelY, sobelYBuf,
            structureOp, structureBuf,
            renderOp, screenBuffer]() {
        accelerated::operations::callUnary(cameraToGray, cameraImage, *grayBuf);
        accelerated::operations::callUnary(sobelX, *grayBuf, *sobelXBuf);
        accelerated::operations::callUnary(sobelY, *grayBuf, *sobelYBuf);
        accelerated::operations::callBinary(structureOp, *sobelXBuf, *sobelYBuf, *structureBuf);
        accelerated::operations::callUnary(renderOp, *structureBuf, *screenBuffer);
    };
#else
    // 3x3 box filter (separable)
    auto boxFilterX = ops.fixedConvolution2D({{ 1, 1, 1 }})
            .scaleKernelValues(1 / 3.0)
            .setBorder(borderType)
            .build(*structureBuf);

    auto boxFilterY = ops.fixedConvolution2D({ {1}, {1}, {1} })
            .scaleKernelValues(1 / 3.0)
            .setBorder(borderType)
            .build(*structureBuf);

#ifndef RENDER_STRUCTURE
    std::string cornerShaderBody;
    {
        std::ostringstream oss;
        oss << "const float inScaleInv = float(" << (1.0 / structureScale) << ");\n"
            << "const float inBias = float(" << structureBias << ");\n"
            << "const float outBias = float(0.5);\n"
            << "const float outScale = float(3.0);\n"
            << "const float k = float(0.04);\n"
            << R"(
            void main() {
                ivec2 coord = ivec2(v_texCoord * vec2(u_outSize));
                vec4 m = (vec4(texelFetch(u_texture, coord, 0)) - inBias) * inScaleInv;
                float det = m.x * m.a - m.y * m.z;
                float tr = m.x + m.y;
                float result = det - k * tr;

                outValue = vec4(vec3(result)*outScale + outBias, 1.0);
            }
            )";
        cornerShaderBody = oss.str();
    }

    // corner-op
    auto renderOp = ops.wrapShader(cornerShaderBody, { *structureBuf }, *screenBuffer);
#endif
    auto cameraProcessor = [
            &cameraImage,
            cameraToGray, grayBuf,
            sobelX, sobelXBuf,
            sobelY, sobelYBuf,
            structureOp, structureBuf,
            boxFilterX, boxFilterY, structureBuf2,
            renderOp, screenBuffer]() {
        accelerated::operations::callUnary(cameraToGray, cameraImage, *grayBuf);
        accelerated::operations::callUnary(sobelX, *grayBuf, *sobelXBuf);
        accelerated::operations::callUnary(sobelY, *grayBuf, *sobelYBuf);
        accelerated::operations::callBinary(structureOp, *sobelXBuf, *sobelYBuf, *structureBuf);
        accelerated::operations::callUnary(boxFilterX, *structureBuf, *structureBuf2);
        accelerated::operations::callUnary(boxFilterY, *structureBuf2, *structureBuf);
        accelerated::operations::callUnary(renderOp, *structureBuf, *screenBuffer);
    };
#endif
#endif

    return std::make_pair(cameraProcessor, screenBuffer);
}

struct GpuExampleModule : public AlgorithmModule {
    std::unique_ptr<accelerated::Image> camera, screen;

    std::unique_ptr<accelerated::Queue> processor;
    std::unique_ptr<accelerated::opengl::Image::Factory> imageFactory;
    std::unique_ptr<accelerated::opengl::operations::Factory> opsFactory;
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