#include <opencv2/core.hpp>
#include "../algorithm_module.hpp"
#include "jsonl-recorder/recorder.hpp"
#include "../opengl/camera_renderer.hpp"
#include "logging.hpp"
#include <nlohmann/json.hpp>

struct RecordingModule : public AlgorithmModule {
    bool recordCamera;
    std::unique_ptr<recorder::Recorder> recorder;
    int w, h;
    bool recordSensors;
    bool previewCamera;
    std::unique_ptr<CameraRenderer> cameraRenderer;

    RecordingModule(int width, int height, const json &settings) {
        w = width;
        h = height;

        log_info("recording only mode");
        recordSensors = settings.at("recordSensors").get<bool>();
        recordCamera = settings.at("recordCamera").get<bool>();
        previewCamera = settings.at("previewCamera").get<bool>();

        auto recName = settings.at("recordingFileName");
        auto videoRecName = settings.at("videoRecordingFileName");
        recorder = recorder::Recorder::build(
                recName.is_null() ? "" : recName.get<std::string>(),
                (videoRecName.is_null() || !recordCamera) ? "" : videoRecName.get<std::string>());

        recorder->setVideoRecordingFps(settings.at("targetFps").get<float>());
    }

    void addGyro(double t, const recorder::Vector3d &val) final {
        if (recordSensors) recorder->addGyroscope(t, val.x, val.y, val.z);
    }

    void addAcc(double t, const recorder::Vector3d &val) final {
        if (recordSensors) recorder->addAccelerometer(t, val.x, val.y, val.z);
    }

    recorder::Pose addFrame(double t, const cv::Mat &grayFrame, cv::Mat *colorFrame,
                            int cameraInd, double focalLength, double px, double py) final {
        if (recordCamera) {
            assert(colorFrame != nullptr);
            // TODO: render GPU texture directly
            cameraRenderer->setTextureData(colorFrame->cols, colorFrame->rows, colorFrame->data, CameraRenderer::AspectFixMethod::LETTERBOX);
            recorder->addFrame(recorder::FrameData {
                    .t = t,
                    .cameraInd = cameraInd,
                    .focalLength = focalLength,
                    .px = px,
                    .py = py,
                    .frameData = colorFrame
            });
        }
        return {};
    }

    bool setupRendering(int visuWidth, int visuHeight) final {
        if (recordCamera) {
            cameraRenderer = CameraRenderer::build(visuWidth, visuHeight);
            return true;
        }
        return false;
    }

    void render() final {
        if (previewCamera && cameraRenderer) {
            cameraRenderer->render();
        }
    }

    void addGps(double t, const AlgorithmModule::Gps &gps) final {
        recorder->addGps(t, gps.latitude, gps.longitude, gps.accuracy, gps.altitude);
    }

    void addJsonData(const json &json) final {
        recorder->addJson(json);
    }

    std::string status() const final {
        return "recording...";
    }
};

std::unique_ptr<AlgorithmModule> buildRecorder(int w, int h, const AlgorithmModule::json &settings) {
    return std::unique_ptr<AlgorithmModule>(new RecordingModule(w, h, settings));
}