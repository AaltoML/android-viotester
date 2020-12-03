#include <opencv2/core.hpp>
#include "../algorithm_module.hpp"
#include "jsonl-recorder/recorder.hpp"
#include "logging.hpp"
#include <nlohmann/json.hpp>
#include <accelerated-arrays/future.hpp>

struct RecordingModule : public CpuAlgorithmModule {
    bool recordCamera;
    std::unique_ptr<recorder::Recorder> recorder;
    int w, h;
    bool recordSensors;
    std::unique_ptr<accelerated::Processor> recorderThread;

    RecordingModule(int textureId, int width, int height, const json &settings) : CpuAlgorithmModule(textureId, width, height) {
        w = width;
        h = height;
        recorderThread = accelerated::Processor::createThreadPool(1);
        recordSensors = settings.at("recordSensors").get<bool>();
        recordCamera = settings.at("recordCamera").get<bool>();
        visualizationEnabled = true;

        auto recName = settings.at("recordingFileName");
        auto videoRecName = settings.at("videoRecordingFileName");
        std::string outputPath = recName.is_null() ? "" : recName.get<std::string>();
        recorder = recorder::Recorder::build(outputPath,
                (videoRecName.is_null() || !recordCamera) ? "" : videoRecName.get<std::string>());

        recorder->setVideoRecordingFps(settings.at("targetFps").get<float>());

        log_info("Recorder started, output %s", outputPath.c_str());
    }

    void addGyro(double t, const recorder::Vector3d &val) final {
        if (recordSensors)
            recorderThread->enqueue([this, t, val]() {
                recorder->addGyroscope(t, val.x, val.y, val.z);
            });
    }

    void addAcc(double t, const recorder::Vector3d &val) final {
        if (recordSensors)
            recorderThread->enqueue([this, t, val]() {
                recorder->addAccelerometer(t, val.x, val.y, val.z);
            });
    }

    void addFrame(double t, const cv::Mat &grayFrame, cv::Mat *colorFrame,
                            const CameraIntrinsics &cam,
                            cv::Mat &outputColorFrame) final {
        if (recordCamera) {
            assert(colorFrame != nullptr);
            // TODO: render GPU texture directly
            auto frameData  = recorder::FrameData {
                    .t = t,
                    .cameraInd = cam.cameraIndex,
                    .focalLengthX = cam.focalLengthX,
                    .focalLengthY = cam.focalLengthY,
                    .px = cam.principalPointX,
                    .py = cam.principalPointY,
                    .frameData = nullptr
            };
            recorderThread->enqueue([this, frameData, colorFrame]() {
                auto f = frameData;
                f.frameData = colorFrame;
                recorder->addFrame(f);
            });

            if (visualizationEnabled) {
                outputColorFrame = *colorFrame;
            }
        }
    }

    void addGps(double t, const AlgorithmModule::Gps &gps) final {
        recorderThread->enqueue([this, t, gps]() {
            recorder->addGps(t, gps.latitude, gps.longitude, gps.accuracy, gps.altitude);
        });
    }

    void addJsonData(const json &json) final {
        recorderThread->enqueue([this, json]() {
            recorder->addJson(json);
        });
    }

    std::string status() const final {
        return "recording...";
    }
};

std::unique_ptr<AlgorithmModule> buildRecorder(int textureId, int w, int h, const AlgorithmModule::json &settings) {
    return std::make_unique<RecordingModule>(textureId, w, h, settings);
}