#include <jni.h>
#include <memory>
#include <opencv2/core.hpp>
#include <Eigen/Dense>
#include "logging.hpp"
#include <nlohmann/json.hpp>
#include "opengl/algorithm_renderer.hpp"
#include "algorithm_module.hpp"
#include <fstream>

using nlohmann::json;

namespace {
    // auxiliary stuff for visualizations and preprocessing
    int width;
    int height;
    cv::Mat colorFrameMat, visualizationMat;
    std::vector<uint32_t> outputBuffers[2];
    int outputBufferIndex;

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

    std::string getStringOrEmpty(JNIEnv *env, jstring s) {
        if (s == nullptr) {
            return "";
        }
        const char *cstr = env->GetStringUTFChars(s, nullptr);
        std::string result(cstr);
        env->ReleaseStringUTFChars(s, cstr);
        return result;
    }
}

extern "C" {

JNIEXPORT jboolean JNICALL Java_org_example_viotester_AlgorithmWorker_configure(
        JNIEnv *env, jobject,
        jint widthJint,
        jint heightJint,
        jstring moduleNameJava,
        jstring moduleSettingsJson) {

    algorithm.reset();
    doubleClock = {};

    width = static_cast<int>(widthJint);
    height = static_cast<int>(heightJint);

    assert(width >= height);
    int visuWidth = width;
    int visuHeight = height;

    {
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

    const bool processColorFrames = algorithm->setupRendering(visualizationMat);
    if (processColorFrames) {
        log_debug("setting up color frames");
        colorFrameMat = cv::Mat(cv::Size(width, height), CV_8UC4);
    }

    if (processColorFrames) {
        log_debug("videoVisualization size set to %dx%d", visuWidth, visuHeight);
        visualizationMat = cv::Mat(cv::Size(visuWidth, visuHeight), CV_8UC4);

        // double buffer for GL rendering
        outputBufferIndex = 0;
        for (auto &buf : outputBuffers) {
            buf.resize(static_cast<unsigned>(visuWidth * visuHeight));
        }
    }

    return static_cast<jboolean>(processColorFrames);
}

static inline uint8_t clamp255(int x) {
    if (x > 255) return 255;
    if (x < 0) return 0;
    return static_cast<uint8_t>(x);
}

JNIEXPORT jlong JNICALL Java_org_example_viotester_AlgorithmWorker_processFrame(
        JNIEnv *env, jobject,
        jlong timeNanos,
        jint rowStride,
        jint chromaPixelStride,
        jint grayImageSize,
        jint chromaPlaneSize,
        jbyteArray yuvDataJava,
        jboolean hasColorFrameJBoolean,
        jdoubleArray outputPoseDataJava,
        jint cameraInd,
        jfloat focalLength,
        jfloat px,
        jfloat py) {

    auto *yuvBuffer = reinterpret_cast<uint8_t*>(env->GetByteArrayElements(yuvDataJava, nullptr));
    const bool processColorFrames = hasColorFrameJBoolean;

    if (processColorFrames) {
        // prepare RGB data for videoVisualization
        assert(chromaPixelStride <= 2);
        assert((width % 2) == 0 && (height % 2) == 0);
        assert(hasColorFrameJBoolean);

        // manual YUV -> BGR conversion was the easiest way to handle all
        // possible combinations of alignments, sizes and pixel strides
        const uint8_t *ySrc = yuvBuffer;
        const uint8_t *uSrc = yuvBuffer + grayImageSize;
        const uint8_t *vSrc = yuvBuffer + grayImageSize + chromaPlaneSize;
        const auto srcRowStride = rowStride;
        const auto dstPixelStride = 4;
        const auto dstRowStride = width*dstPixelStride;

        for (int y=0; y<height; ++y) {
            for (int x=0; x<width; ++x) {
                const auto yVal = ySrc[y*srcRowStride + x];
                const auto uVal = uSrc[(y/2)*srcRowStride + (x/2) * chromaPixelStride];
                const auto vVal = vSrc[(y/2)*srcRowStride + (x/2) * chromaPixelStride];

                // https://en.wikipedia.org/wiki/YUV#Y%E2%80%B2UV420sp_(NV21)_to_RGB_conversion_(Android)
                auto r = static_cast<int>(yVal + (1.370705 * (vVal-128)));
                auto g = static_cast<int>(yVal - (0.698001 * (vVal-128)) - (0.337633 * (uVal-128)));
                auto b = static_cast<int>(yVal + (1.732446 * (uVal-128)));

                uint8_t *dstPixel = colorFrameMat.data + (y*dstRowStride + x*dstPixelStride);
                // BGR format for OpenCV
                dstPixel[0] = clamp255(b);
                dstPixel[1] = clamp255(g);
                dstPixel[2] = clamp255(r);
                dstPixel[3] = 0xff; // alpha (not used)
            }
        }
    }

    jlong ret = -1;
    cv::Mat grayMatrix(cv::Size(width, height), CV_8UC1, yuvBuffer, rowStride);

    const auto pose = algorithm->addFrame(
            doubleClock.update(timeNanos), grayMatrix, processColorFrames ? &colorFrameMat : nullptr,
            cameraInd, focalLength, px, py);
    assert(env->GetArrayLength(outputPoseDataJava) == 7);
    auto *outData = env->GetDoubleArrayElements(outputPoseDataJava, nullptr);
    outData[0] = pose.position.x;
    outData[1] = pose.position.y;
    outData[2] = pose.position.z;
    outData[3] = pose.orientation.w;
    outData[4] = pose.orientation.x;
    outData[5] = pose.orientation.y;
    outData[6] = pose.orientation.z;
    env->ReleaseDoubleArrayElements(outputPoseDataJava, outData, 0);
    ret = static_cast<jlong>(pose.time * 1e9); // convert to nanoseconds
    env->ReleaseByteArrayElements(yuvDataJava, reinterpret_cast<jbyte*>(yuvBuffer), JNI_ABORT);
    return ret;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGyroSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    if (algorithm) algorithm->addGyro(doubleClock.update(timeNanos), { x, y, z });
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processAccSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    if (algorithm) algorithm->addAcc(doubleClock.update(timeNanos), { x, y, z });
}

JNIEXPORT jstring JNICALL Java_org_example_viotester_AlgorithmWorker_getStatsString(
        JNIEnv *env, jobject) {
    if (algorithm) return env->NewStringUTF(algorithm->status().c_str());
    return nullptr;
}

JNIEXPORT jint JNICALL Java_org_example_viotester_AlgorithmWorker_getTrackingStatus(
        JNIEnv *env, jobject) {
    if (algorithm) return algorithm->trackingStatus();
    return -1;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_drawVisualization(JNIEnv *, jobject) {
    if (!algorithm) return;

    assert(outputBufferIndex >= 0 && outputBufferIndex <= 1);
    uint32_t *rgbaBuffer = outputBuffers[outputBufferIndex].data();
    cv::Mat outMat(visualizationMat.size(), CV_8UC4, rgbaBuffer);

    bool changed = algorithm->render(outMat);

    if (changed) {
        outputBufferIndex = (outputBufferIndex + 1) % 2; // double-buffer flip
    }
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGpsLocation(JNIEnv *, jobject, jlong timeNanos, jdouble lat, jdouble lon, jdouble alt, jfloat acc) {
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
    std::string paramsFile = !settingsJson.at("parametersFileName").is_null() ? settingsJson.at("parametersFileName") : "";
    if (paramsFile.empty())
        return;
    std::ofstream fileOutput(paramsFile);
    fileOutput << "imuToCameraMatrix -0,-1,0,-1,0,0,0,0,-1;" << std::endl;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_recordPoseMatrix(JNIEnv *env, jobject thiz, jlong timeNs, jfloatArray pose, jstring tag) {
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
