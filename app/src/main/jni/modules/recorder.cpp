#include <opencv2/core.hpp>
#include "../opengl/algorithm_renderer.hpp"
#include "../algorithm_module.hpp"
#include "jsonl-recorder/recorder.hpp"
#include "logging.hpp"
#include <nlohmann/json.hpp>

struct RecordingModule : public AlgorithmModule {
    bool recordCamera;
    std::unique_ptr<recorder::Recorder> recorder;
    int w, h;
    bool recordSensors = false;
    std::string infoFile;
    std::string paramsFile;

    RecordingModule(int width, int height, const json &settings) {
        w = width;
        h = height;

        log_info("recording only mode");
        recordSensors = settings.at("recordSensors").get<bool>();
        recordCamera = settings.at("recordCamera").get<bool>();

        auto recName = settings.at("recordingFileName");
        auto videoRecName = settings.at("videoRecordingFileName");
        recorder = recorder::Recorder::build(
                recName.is_null() ? "" : recName.get<std::string>(),
                (videoRecName.is_null() || !recordCamera) ? "" : videoRecName.get<std::string>());

        recorder->setVideoRecordingFps(settings.at("targetFps").get<float>());

        infoFile = !settings.at("infoFileName").is_null() ? settings.at("infoFileName") : "";
        paramsFile = !settings.at("parametersFileName").is_null() ? settings.at("parametersFileName") : "";
    }
    
    void writeInfoFile(const std::string &mode, const std::string &device) {
        // TODO: Move implementation to recorder?
        if (infoFile.empty())
            return;
        json infoJson = R"(
            {
                "camera mode": "",
                "device": "",
                "tags": [
                    "android"
                ]
            }
        )"_json;
        infoJson["camera mode"] = mode;
        infoJson["device"] = device;
        std::ofstream fileOutput(infoFile);
        std::ostream &output(fileOutput);
        output << infoJson.dump() << std::endl;
    }

    void writeParamsFile(float focalLength) {
        // TODO: Move implementation to recorder?
        if (paramsFile.empty())
            return;
        std::ofstream fileOutput(paramsFile);
        std::ostream &output(fileOutput);
        output << "imuToCameraMatrix -0,-1,0,-1,0,0,0,0,-1;focalLength "
               << std::to_string(focalLength)
               << ";"
               << std::endl;
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
            defaultOpenGLRenderer::setBgraCameraData(colorFrame->cols, colorFrame->rows, colorFrame->data);
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

    bool setupRendering(cv::Mat &out) {
        if (recordCamera) {
            out = cv::Mat(h, w, CV_8UC4);
            return true;
        }
        return false;
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