#include "../algorithm_module.hpp"
#include "logging.hpp"

#include <accelerated-arrays/standard_ops.hpp>
#include <accelerated-arrays/opengl/image.hpp>
#include <accelerated-arrays/opengl/operations.hpp>
#include <sstream>

namespace {
const auto BORDER_TYPE = accelerated::Image::Border::MIRROR;
typedef accelerated::FixedPoint<std::uint8_t> Ufixed8;

namespace pipeline {
struct Gray {
    accelerated::Image &cameraImage;
    accelerated::operations::Function cameraToGray;

    std::shared_ptr<accelerated::Image> buffer;
    Gray(accelerated::Image &cameraImage,
        accelerated::Image::Factory &images,
        accelerated::opengl::operations::Factory &ops)
    :
        cameraImage(cameraImage)
    {
        // Note: careful with createLike, as cameraImage has a different storage type (EXTERNAL_OES)
        buffer = images.create<Ufixed8, 1>(
            cameraImage.width,
            cameraImage.height);

        cameraToGray = ops.pixelwiseAffine({{ 0,1,0,0 }}).build(cameraImage, *buffer);
    }

    void call() const {
        accelerated::operations::callUnary(cameraToGray, cameraImage, *buffer);
    }
};

struct Sobel {
    typedef std::int16_t Type;
    static constexpr float scale = 100, bias = 0;

    Gray input;
    accelerated::operations::Function opX, opY;
    std::shared_ptr<accelerated::Image> bufX, bufY;

    Sobel(Gray grayInput,
          accelerated::Image::Factory &images,
          accelerated::opengl::operations::Factory &ops)
    :
        input(grayInput)
    {
        bufX = images.create<Type, 1>(input.buffer->width, input.buffer->height);
        bufY = images.createLike(*bufX);

        // note: signs don't matter here that much
        opX = ops.fixedConvolution2D({
                 {-1, 0, 1},
                 {-2, 0, 2},
                 {-1, 0, 1}
             })
            .scaleKernelValues(scale)
            .setBias(bias)
            .setBorder(BORDER_TYPE)
            .build(*input.buffer, *bufX);

        opY = ops.fixedConvolution2D({
                 {-1,-2,-1 },
                 { 0, 0, 0 },
                 { 1, 2, 1 }
             })
            .scaleKernelValues(scale)
            .setBias(bias)
            .setBorder(BORDER_TYPE)
            .build(*input.buffer, *bufY);
    }

    void call() const {
        input.call();
        accelerated::operations::callUnary(opX, *input.buffer, *bufX);
        accelerated::operations::callUnary(opY, *input.buffer, *bufY);
    }

    std::function<void()> renderer(
            accelerated::opengl::operations::Factory &ops,
            std::shared_ptr<accelerated::Image> screenBuffer)
    {
        const float renderBias = -bias / scale + 0.5f;
        auto renderOp = ops.affineCombination()
                .addLinearPart({ {1}, {0}, {0}, {0} })
                .addLinearPart({ {0}, {1}, {0}, {0} })
                .setBias({ renderBias, renderBias, 0.5, 1 })
                .build(*bufX, *screenBuffer);

        Sobel sobel = *this;
        return [sobel, renderOp, screenBuffer]() {
            sobel.call();
            accelerated::operations::callBinary(renderOp, *sobel.bufX, *sobel.bufY, *screenBuffer);
        };
    }
};

struct StructureMatrix {
    typedef std::int16_t Type;
    static constexpr float scale = Sobel::scale, bias = Sobel::bias;
    static constexpr const char * glslType = "ivec4"; // could expose these in accelerated-arrays

    Sobel sobel;
    accelerated::operations::Function op;
    std::shared_ptr<accelerated::Image> buffer;

    static std::string shaderBody() {
        std::ostringstream oss;
        oss << "const float inScaleInv = float(" << (1.0 / Sobel::scale) << ");\n"
            << "const float inBias = float(" << Sobel::bias << ");\n"
            << "const float outScale = float(" << scale << ");\n"
            << "const float outBias = float(" << bias << ");\n"
            << R"(
            void main() {
                ivec2 coord = ivec2(v_texCoord * vec2(u_outSize));
                vec2 der = (vec2(
                    float(texelFetch(u_texture1, coord, 0).r),
                    float(texelFetch(u_texture2, coord, 0).r)) - inBias) * inScaleInv;
                vec2 d2 = der*der;
                float dxdy = der.x*der.y;
                outValue = )" << glslType << R"((vec4(
                    d2.x, dxdy,
                    dxdy, d2.y
                ) * outScale + outBias);
            }
            )";
        return oss.str();
    }

    StructureMatrix(Sobel sobelInput,
        accelerated::Image::Factory &images,
        accelerated::opengl::operations::Factory &ops)
    :
        sobel(sobelInput)
    {
        buffer = images.create<Type, 4>(sobel.bufX->width, sobel.bufX->height);
        op = ops.wrapShader(shaderBody(), { *sobel.bufX, *sobel.bufY }, *buffer);
    }

    void call() const {
        sobel.call();
        accelerated::operations::callBinary(op, *sobel.bufX, *sobel.bufY, *buffer);
    }

    std::function<void()> renderer(
            accelerated::opengl::operations::Factory &ops,
            std::shared_ptr<accelerated::Image> screenBuffer)
    {
        const float renderBias = -bias / scale + 0.5f;
        auto renderOp = ops.pixelwiseAffine({
                    {1, 0, 0, 0},
                    {0, 1, 0, 0},
                    {0, 0, 1, 0},
                    {0, 0, 0, 0}
                })
                .scaleLinearValues(1.0 / scale)
                .setBias({ renderBias, renderBias, renderBias, 1 })
                .build(*buffer, *screenBuffer);

        StructureMatrix structure = *this;
        return [structure, renderOp, screenBuffer]() {
            structure.call();
            accelerated::operations::callUnary(renderOp, *structure.buffer, *screenBuffer);
        };
    }
};

struct FilteredStructureMatrix {
    StructureMatrix input;
    accelerated::operations::Function filterX, filterY;
    std::shared_ptr<accelerated::Image> buffer;

    FilteredStructureMatrix(StructureMatrix input,
        accelerated::Image::Factory &images,
        accelerated::opengl::operations::Factory &ops)
    :
            input(input)
    {
        buffer = images.createLike(*input.buffer);

        // 3x3 box filter (separable)
        filterX = ops.fixedConvolution2D({{ 1, 1, 1 }})
                .scaleKernelValues(1 / 3.0)
                .setBorder(BORDER_TYPE)
                .build(*buffer);

        filterY = ops.fixedConvolution2D({ {1}, {1}, {1} })
                .scaleKernelValues(1 / 3.0)
                .setBorder(BORDER_TYPE)
                .build(*buffer);
    }

    accelerated::Image &getOutputBuffer() const {
        return *input.buffer;
    }

    void call() const {
        input.call();
        accelerated::operations::callUnary(filterX, *input.buffer, *buffer);
        accelerated::operations::callUnary(filterY, *buffer, *input.buffer);
    }

    std::function<void()> renderer(
            accelerated::opengl::operations::Factory &ops,
            std::shared_ptr<accelerated::Image> screenBuffer)
    {
        // note: reuses the buffer in non-filtered structure matrix, therefore this works
        FilteredStructureMatrix filtered = *this;
        auto renderOp = input.renderer(ops, screenBuffer);
        return [filtered, renderOp]() {
            filtered.call();
            renderOp();
        };
    }
};

struct CornerResponse {
    typedef Ufixed8 Type;
    static constexpr float scale = 3.0, bias = 0.5;

    FilteredStructureMatrix input;
    accelerated::operations::Function op;
    std::shared_ptr<accelerated::Image> buffer;

    static std::string shaderBody() {
        std::ostringstream oss;
        oss << "const float inScaleInv = float(" << (1.0 / StructureMatrix::scale) << ");\n"
            << "const float inBias = float(" << StructureMatrix::bias << ");\n"
            << "const float outBias = float(" << bias << ");\n"
            << "const float outScale = float(" << scale << ");\n"
            << "const float k = float(0.04);\n"
            << R"(
            void main() {
                ivec2 coord = ivec2(v_texCoord * vec2(u_outSize));
                vec4 m = (vec4(texelFetch(u_texture, coord, 0)) - inBias) * inScaleInv;
                float det = m.x * m.a - m.y * m.z;
                float tr = m.x + m.y;
                float result = det - k * tr;
                outValue = result*outScale + outBias;
            }
            )";
        return oss.str();
    }

    CornerResponse(FilteredStructureMatrix input,
                    accelerated::Image::Factory &images,
                    accelerated::opengl::operations::Factory &ops)
    :
        input(input)
    {
        auto &inBuf = input.getOutputBuffer();
        buffer = images.create<Type, 1>(inBuf.width, inBuf.height);
        op = ops.wrapShader(shaderBody(), { inBuf }, *buffer);
    }

    void call() const {
        input.call();
        accelerated::operations::callUnary(op, input.getOutputBuffer(), *buffer);
    }

    std::function<void()> renderer(
            accelerated::opengl::operations::Factory &ops,
            std::shared_ptr<accelerated::Image> screenBuffer)
    {
        auto renderOp = ops.pixelwiseAffine({ {1}, {1}, {1}, {0} })
                .setBias({ 0, 0, 0, 1 })
                .build(*buffer, *screenBuffer);

        CornerResponse cornerResponse = *this;
        return [cornerResponse, renderOp, screenBuffer]() {
            cornerResponse.call();
            accelerated::operations::callUnary(renderOp, *cornerResponse.buffer, *screenBuffer);
        };
    }
};


std::pair<std::function<void()>, std::shared_ptr<accelerated::Image>> create(
        accelerated::Image::Factory &images,
        accelerated::opengl::operations::Factory &ops,
        accelerated::Image &cameraImage,
        const accelerated::Image &screen) {
    (void)screen;

    Gray gray(cameraImage, images, ops);

    std::shared_ptr<accelerated::Image> screenBuffer = images.create<Ufixed8, 4>(
            cameraImage.width,
            cameraImage.height);

    Sobel sobel(gray, images, ops);
    // auto processor = sobel.renderer(ops, screenBuffer);
    StructureMatrix structureMatrix(sobel, images, ops);
    // auto processor = structureMatrix.renderer(ops, screenBuffer);
    FilteredStructureMatrix filteredStructureMatrix(structureMatrix, images, ops);
    // auto processor = filteredStructureMatrix.renderer(ops, screenBuffer);
    CornerResponse cornerResponse(filteredStructureMatrix, images, ops);
    auto processor = cornerResponse.renderer(ops, screenBuffer);

    return std::make_pair(processor, screenBuffer);
}
}

struct GpuExampleModule : public AlgorithmModule {
    std::unique_ptr<accelerated::Queue> processor;
    std::unique_ptr<accelerated::opengl::Image::Factory> imageFactory;
    std::unique_ptr<accelerated::opengl::operations::Factory> opsFactory;
    std::unique_ptr<accelerated::Image> camera, screen;
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
        auto pair = pipeline::create(*imageFactory, *opsFactory, *camera, *screen);
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
    return std::unique_ptr<AlgorithmModule>(new GpuExampleModule(textureId, w, h));
}