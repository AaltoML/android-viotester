#include <jni.h>
#include <memory>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <Eigen/Dense>
#include "logging.hpp"
#include <nlohmann/json.hpp>
#include "opengl/gpu_camera_adapter.hpp"
#include "opengl/camera_renderer.hpp"
#include "algorithm_module.hpp"
#include <fstream>
#include "jniutil.hpp"

using nlohmann::json;

namespace {
    // auxiliary stuff for visualizations and preprocessing
    int width;
    int height;
    cv::Mat colorFrame, grayFrame;

    struct PendingFrame {
        bool pending = false;
        long timeNanos;
        cv::Mat colorFrame, grayFrame;
        float focalLength, ppx, ppy;
        int cameraInd;
    } pendingFrame;

    bool recordExternalPoses = false;
    bool visualizationEnabled = false;
    int frameStride = 1;
    int frameNumber = 0;
    std::mutex algorithmMutex, frameMutex;

    std::unique_ptr<GpuCameraAdapter> gpuAdapter;
    std::unique_ptr<GpuCameraAdapter::TextureAdapter> rgbaTexture, grayTexture;

    json settingsJson;
    std::unique_ptr<AlgorithmModule> algorithm;
    class Clock {
    public:
        double update(int64_t tNanos) {
            if (t0 == 0) t0 = tNanos;
            return (tNanos - t0) * 1e-9 + MARGIN;
        }

        Clock() : t0(0) {}

    private:
        /**
         * Time in seconds to add to the beginning to avoid negative timestamps
         * caused by possible unordered samples in the beginning
         */
        static constexpr double MARGIN = 0.01;
        int64_t t0;
    };

    Clock doubleClock;

    void renderToPendingFrame(long timeNanos, int cameraInd, float focalLength, float px, float py) {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!gpuAdapter || !grayTexture) {
            log_warn("render after teardown: skipping");
            return;
        }

        grayTexture->render();
        grayTexture->readPixels(grayFrame.data);

        if (rgbaTexture) {
            assert(!colorFrame.empty());
            rgbaTexture->render();
            rgbaTexture->readPixels(colorFrame.data);
        }

        // for debugging
        //cv::cvtColor(colorFrame, grayFrame, cv::COLOR_BGRA2GRAY);
        //cv::cvtColor(grayFrame, colorFrame, cv::COLOR_GRAY2BGRA);

        colorFrame.copyTo(pendingFrame.colorFrame);
        grayFrame.copyTo(pendingFrame.grayFrame);
        pendingFrame.timeNanos = timeNanos;
        pendingFrame.focalLength = focalLength;
        pendingFrame.ppx = px;
        pendingFrame.ppy = py;
        pendingFrame.pending = true;
        pendingFrame.cameraInd = cameraInd;
    }

    void consumePendingFrame() {
        // note: this should work fine if addFrame is a light-weight queuing operation.
        // if not, it might make sense to copy the image data to a third buffer first
        std::lock_guard<std::mutex> lock(frameMutex);
        if (pendingFrame.pending) {
            pendingFrame.pending = false;
            algorithm->addFrame(
                    doubleClock.update(pendingFrame.timeNanos), grayFrame, visualizationEnabled ? &colorFrame : nullptr,
                    pendingFrame.cameraInd, pendingFrame.focalLength, pendingFrame.ppx, pendingFrame.ppy);
        }
    }
}

extern "C" {

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_nativeStop(
        JNIEnv *env, jobject) {
    log_debug("nativeStop");
    {
        std::lock_guard<std::mutex> lock(algorithmMutex);
        algorithm.reset();
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        rgbaTexture.reset();
        grayTexture.reset();
        gpuAdapter.reset();
        pendingFrame.pending = false;
    }
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_configureVisualization(
        JNIEnv *env, jobject,
        jint visuWidth, jint visuHeight) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) {
        // happens on external AR. TODO: hacky
        log_warn("no algorithm, ignoring configureVisualization");
        return;
    }

    visualizationEnabled = algorithm->setupRendering(visuWidth, visuHeight);
    if (visualizationEnabled) {
        assert(gpuAdapter);
        std::lock_guard<std::mutex> frameLock(frameMutex);
        rgbaTexture = gpuAdapter->createTextureAdapter(GpuCameraAdapter::TextureAdapter::Type::BGRA);
        log_debug("screen size size set to %dx%d", visuWidth, visuHeight);
    }
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_configure(
        JNIEnv *env, jobject,
        jint widthJint,
        jint heightJint,
        jint textureId,
        jint frameStrideJint,
        jboolean recordExternalPosesJboolean,
        jstring moduleNameJava,
        jstring moduleSettingsJson) {
    {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    algorithm.reset();
    doubleClock = {};

    width = static_cast<int>(widthJint);
    height = static_cast<int>(heightJint);
    frameStride = static_cast<int>(frameStrideJint);
    recordExternalPoses = recordExternalPosesJboolean;

    assert(width >= height);

    const std::string moduleName = getStringOrEmpty(env, moduleNameJava);
    log_info("initializing %s", moduleName.c_str());
    json *settingsJsonPtr = nullptr;
    const std::string settingsString = getStringOrEmpty(env, moduleSettingsJson);
    if (!settingsString.empty()) {
        settingsJson = json::parse(settingsString);
        log_debug("json settings\n%s", settingsJson.dump(2).c_str());
        settingsJsonPtr = &settingsJson;
    }
    algorithm = AlgorithmModule::build(width, height, moduleName, settingsJsonPtr);
    }

    {
    std::lock_guard<std::mutex> lock(frameMutex);
    gpuAdapter = GpuCameraAdapter::create(width, height, textureId);
    grayTexture = gpuAdapter->createTextureAdapter(
            GpuCameraAdapter::TextureAdapter::Type::GRAY_COMPRESSED);

    log_debug("setting up color frames");
    colorFrame = cv::Mat(cv::Size(width, height), CV_8UC4);
    grayFrame = cv::Mat(cv::Size(width, height), CV_8UC1);
    }
}

JNIEXPORT jboolean JNICALL Java_org_example_viotester_AlgorithmWorker_processFrame(
        JNIEnv *env, jobject,
        jlong timeNanos,
        jint cameraInd,
        jfloat focalLength,
        jfloat px,
        jfloat py) {

    if ((frameNumber++ % frameStride) != 0) return false;

    renderToPendingFrame(timeNanos, cameraInd, focalLength, px, py);
    return true;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGyroSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (algorithm) {
        algorithm->addGyro(doubleClock.update(timeNanos), { x, y, z });
        consumePendingFrame();
    }
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processAccSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (algorithm) algorithm->addAcc(doubleClock.update(timeNanos), { x, y, z });
}

JNIEXPORT jstring JNICALL Java_org_example_viotester_AlgorithmWorker_getStatsString(
        JNIEnv *env, jobject) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (algorithm) return env->NewStringUTF(algorithm->status().c_str());
    return nullptr;
}

JNIEXPORT jint JNICALL Java_org_example_viotester_AlgorithmWorker_getTrackingStatus(
        JNIEnv *env, jobject) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (algorithm) return algorithm->trackingStatus();
    return -1;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_drawVisualization(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) return;
    algorithm->render();
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGpsLocation(JNIEnv *, jobject, jlong timeNanos, jdouble lat, jdouble lon, jdouble alt, jfloat acc) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) return;
    algorithm->addGps(doubleClock.update(timeNanos), AlgorithmModule::Gps {
            .latitude = lat,
            .longitude = lon,
            .altitude = alt,
            .accuracy = acc
    });
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_writeInfoFile(
        JNIEnv* env, jobject,
        jstring mode, jstring device) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) return;

    std::string infoFile = !settingsJson.at("infoFileName").is_null() ? settingsJson.at("infoFileName") : "";
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
    infoJson["camera mode"] = getStringOrEmpty(env, mode);
    infoJson["device"] = getStringOrEmpty(env, device);
    std::ofstream fileOutput(infoFile);
    fileOutput << infoJson.dump() << std::endl;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_writeParamsFile(
        JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) return;

    std::string paramsFile = !settingsJson.at("parametersFileName").is_null() ? settingsJson.at("parametersFileName") : "";
    if (paramsFile.empty())
        return;
    std::ofstream fileOutput(paramsFile);
    fileOutput << "imuToCameraMatrix -0,-1,0,-1,0,0,0,0,-1;" << std::endl;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processExternalImage(JNIEnv *env, jobject thiz,
        jlong timeNs, jlong frameNumber, jint cameraInd, jfloat focalLength, jfloat ppx, jfloat ppy) {
    renderToPendingFrame(timeNs, cameraInd, focalLength, ppx, ppy);
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_recordPoseMatrix(JNIEnv *env, jobject thiz, jlong timeNs, jfloatArray pose, jstring tag) {
    (void)thiz;
    std::lock_guard<std::mutex> lock(algorithmMutex);
    if (!algorithm) return;

    consumePendingFrame();

    if (!recordExternalPoses) return;

    auto *poseArr = env->GetFloatArrayElements(pose, nullptr);
    Eigen::Matrix4f viewMatrix = Eigen::Map<Eigen::Matrix4f>(poseArr);
    env->ReleaseFloatArrayElements(pose, poseArr, JNI_ABORT);

    const double t = doubleClock.update(timeNs);

    const Eigen::Matrix3f R = viewMatrix.block<3, 3>(0, 0);
    const Eigen::Vector3f p = -R.transpose() * viewMatrix.block<3, 1>(0, 3);
    const Eigen::Quaternionf q(R);


    algorithm->addJsonData(
            AlgorithmModule::json {
                    // Android Studio thinks this indentation is pretty and refuses to change
                    // so let's keep it that way
                    { "time", t },
                    { getStringOrEmpty(env, tag).c_str(),
                              {
                                      { "position",
                                              {
                                                      { "x", p.x() },
                                                      { "y", p.y() },
                                                      { "z", p.z() }
                                              }
                                      },
                                      { "orientation",
                                              {
                                                      { "w", q.w() },
                                                      { "x", q.x() },
                                                      { "y", q.y() },
                                                      { "z", q.z() }
                                              }
                                      }
                              }
                    }
            });
}
}
