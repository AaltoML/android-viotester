#include "../opengl/algorithm_renderer.hpp"
#include "../algorithm_module.hpp"
#include "jsonl-recorder/recorder.hpp"
#include "logging.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

static std::unique_ptr<cv::VideoWriter> buildVideoWriter(const std::string &path, float fps, const cv::Mat &modelFrame) {
    assert(fps > 0.0);
    assert(!path.empty());
    const auto codec = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    // This is the only thing we can write without FFMPEG on Android
    // The path name should end with .avi
    const auto backend = cv::CAP_OPENCV_MJPEG;
    const bool isColor = modelFrame.channels() > 1;
    {
        // OpenCV writer gives no errors even if it is unable to open file and
        // write frames to it, so test writing by ourselves.
        std::ofstream video(path);
        assert(video.is_open() && "unable to open video file for writing");
    }
    log_info("recording %s video stream to %s", isColor ? "color" : "gray", path.c_str());
    auto writer = std::make_unique<cv::VideoWriter>(
            path, backend, codec, fps, modelFrame.size(), isColor);
    assert(writer && "failed to create video writer");
    // TODO: set video quality
    return writer;
}

struct RecordingModule : public AlgorithmModule {
    bool recordCamera;
    std::unique_ptr<recorder::Recorder> recorder;
    std::unique_ptr<cv::VideoWriter> videoWriter;
    std::string videoRecordingPath;
    cv::Mat rgbaOutputFrame;
    int w, h;

    float videoRecordingFps = 30;
    bool recordSensors = false;

    RecordingModule(int width, int height, const json &settings) {
        w = width;
        h = height;

        log_info("recording only mode");
        recordSensors = settings.at("recordSensors").get<bool>();
        recordCamera = settings.at("recordCamera").get<bool>();

        auto recName = settings.at("recordingFileName");
        recorder = recorder::Recorder::build(recName.is_null() ? "" : recName.get<std::string>());
        auto videoRecName = settings.at("videoRecordingFileName");
        videoRecordingPath = videoRecName.is_null() ? "" : videoRecName.get<std::string>();
    }

    void addGyro(double t, const recorder::Vector3d &val) final {
        // api->addGyro(t, val); // TODO
    }

    void addAcc(double t, const recorder::Vector3d &val) final {
        // api->addAcc(t, val); // TODO
    }

    recorder::Pose addFrame(double t, const cv::Mat &grayFrame, cv::Mat *colorFrame) final {
        if (recordCamera) {
            assert(colorFrame != nullptr);
            defaultOpenGLRenderer::setBgraCameraData(colorFrame->cols, colorFrame->rows, colorFrame->data);

            assert(!videoRecordingPath.empty());
            // lazy-initialize recorder so it gets the frame in the correct format
            if (!videoWriter) {
                videoWriter = buildVideoWriter(
                        videoRecordingPath,
                        videoRecordingFps,
                        *colorFrame
                );
            }
            if (colorFrame->channels() == 4) {
                // rgbaOutputFrame is a "workspace variable" that is also
                // used elsewhere
                cv::cvtColor(*colorFrame, rgbaOutputFrame, cv::COLOR_BGRA2BGR);
                // This took a while to debug: if the image has 3 channels, the
                // default channel order assumed by OpenCV image IO functions
                // is BGR (which everybody on the internet warns you about).
                // However, if there are 4 channels, at least this particular
                // function (on Android) assumes the color order RGBA
                videoWriter->write(rgbaOutputFrame);
            } else {
                videoWriter->write(*colorFrame);
            }
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
        // api->appendPoseHistoryGps(t, gps.latitude, gps.longitude, gps.accuracy, gps.altitude); TODO
    }

    void addJsonData(const json &json) final {
        // api->recordJson(json); TODO
    }

    std::string status() const final {
        return "recording...";
    }
};

std::unique_ptr<AlgorithmModule> buildRecorder(int w, int h, const AlgorithmModule::json &settings) {
    return std::unique_ptr<AlgorithmModule>(new RecordingModule(w, h, settings));
}