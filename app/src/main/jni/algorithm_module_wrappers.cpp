#include <cassert>
#include <opencv2/core.hpp>

#include "opengl/gpu_camera_adapter.hpp"
#include "opengl/camera_renderer.hpp"
#include "algorithm_module.hpp"
#include "logging.hpp"

struct CpuAlgorithmModule::impl {
    cv::Mat colorFrame, grayFrame, visualization;
    std::mutex mutex, renderMutex;
    std::unique_ptr<GpuCameraAdapter> gpuAdapter;
    std::unique_ptr<GpuCameraAdapter::TextureAdapter> rgbaTexture, grayTexture;
    std::unique_ptr<CameraRenderer> renderer;
};

void GpuCameraAdapter::readChecked(GpuCameraAdapter::TextureAdapter &adapter, cv::Mat &mat) {
    adapter.render();
    assert(adapter.readPixelsSize() == mat.total() * mat.elemSize());
    adapter.readPixels(mat.data);
}

CpuAlgorithmModule::~CpuAlgorithmModule() = default;

CpuAlgorithmModule::CpuAlgorithmModule(int textureId, int width, int height) : pimpl(new impl) {
    log_debug("setting up CPU frames %d x %d (tex ID %d)", width, height, textureId);
    pimpl->colorFrame = cv::Mat(cv::Size(width, height), CV_8UC4);
    pimpl->grayFrame = cv::Mat(cv::Size(width, height), CV_8UC1);

    pimpl->gpuAdapter = GpuCameraAdapter::create(width, height, textureId);
    pimpl->grayTexture = pimpl->gpuAdapter->createTextureAdapter(
            GpuCameraAdapter::TextureAdapter::Type::GRAY_COMPRESSED);
}

void CpuAlgorithmModule::setupRendering(int visuWidth, int visuHeight) {
    if (visualizationEnabled) {
        std::lock_guard<std::mutex> lock(pimpl->mutex);
        pimpl->rgbaTexture = pimpl->gpuAdapter->createTextureAdapter(GpuCameraAdapter::TextureAdapter::Type::BGRA);
        log_debug("screen size size set to %dx%d", visuWidth, visuHeight);
        pimpl->renderer = CameraRenderer::build(visuWidth, visuHeight);
    }
}

void CpuAlgorithmModule::addFrame(double t, int cameraInd, double focalLength, double px, double py) {
    std::lock_guard<std::mutex> lock(pimpl->mutex);
    GpuCameraAdapter::readChecked(*pimpl->grayTexture, pimpl->grayFrame);

    if (pimpl->rgbaTexture) {
        assert(!pimpl->colorFrame.empty());
        GpuCameraAdapter::readChecked(*pimpl->rgbaTexture, pimpl->colorFrame);
    }

    addFrame(t, pimpl->grayFrame, visualizationEnabled ? &pimpl->colorFrame : nullptr,
             cameraInd, focalLength, px, py, pimpl->visualization);

    if (visualizationEnabled) {
        assert(pimpl->renderer);
        std::lock_guard<std::mutex> lock(pimpl->renderMutex);
        pimpl->renderer->setTextureData(pimpl->visualization.cols, pimpl->visualization.rows,
                pimpl->visualization.data, CameraRenderer::AspectFixMethod::CROP);
    }
}

void CpuAlgorithmModule::render() {
    if (!visualizationEnabled) return;
    std::lock_guard<std::mutex> lock(pimpl->renderMutex);
    pimpl->renderer->render();
}

class MutexLockedImplementation : public AlgorithmModule {
public:
    typedef std::lock_guard<std::mutex> Lock;
    void addGyro(double t, const Vector3d &val) final {
        Lock lock(m);
        p->addGyro(t, val);
    }

    void addAcc(double t, const Vector3d &val) final {
        Lock lock(m);
        p->addAcc(t, val);
    }

    void addGps(double t, const Gps &gps) final {
        Lock lock(m);
        p->addGps(t, gps);
    }

    void addJsonData(const json &json) final {
        Lock lock(m);
        p->addJsonData(json);
    }

    std::string status() const final {
        Lock lock(const_cast<MutexLockedImplementation*>(this)->statusLock);
        return statusStruct.textStatus;
    }

    int trackingStatus() final {
        Lock lock(statusLock);
        return statusStruct.trackingStatus;
    }

    void addFrame(double t, int cameraInd, double focalLength, double px, double py) final {
        Status tmpStatus;
        {
            Lock lock(m);
            p->addFrame(t, cameraInd, focalLength, px, py);
            tmpStatus = {
                .textStatus = p->status(),
                .trackingStatus = p->trackingStatus()
            };
        }

        {
            Lock lock(statusLock);
            statusStruct = tmpStatus;
        }
    }

    void setupRendering(int width, int height) final {
        Lock lock(m);
        return p->setupRendering(width, height);
    }

    void render() final {
        Lock lock(m);
        p->render();
    }

    MutexLockedImplementation(AlgorithmModule *nonThreadSafe) : p(nonThreadSafe) {}

private:
    struct Status {
        std::string textStatus = "";
        int trackingStatus = -1;
    } statusStruct;

    std::mutex m, statusLock;
    std::unique_ptr<AlgorithmModule> p;
};

std::unique_ptr<AlgorithmModule> AlgorithmModule::makeThreadSafe(AlgorithmModule *nonThreadSafe) {
    return std::unique_ptr<AlgorithmModule>(new MutexLockedImplementation(nonThreadSafe));
}