#include <jni.h>
#include <memory>
#include <atomic>

#include <Eigen/Dense>
#include "logging.hpp"
#include <nlohmann/json.hpp>
#include "algorithm_module.hpp"
#include <fstream>
#include "jniutil.hpp"

using nlohmann::json;

namespace {
    bool recordExternalPoses = false;
    int frameStride = 1;
    int frameNumber = 0;

    json settingsJson;
    std::shared_ptr<AlgorithmModule> algorithmPtr;

    class Clock {
    public:
        double convert(int64_t tNanos) const {
            return (tNanos - t0) * 1e-9 + MARGIN;
        }

        Clock(int64_t t = 0) : t0(t) {}

    private:
        /**
         * Time in seconds to add to the beginning to avoid negative timestamps
         * caused by possible unordered samples in the beginning
         */
        static constexpr double MARGIN = 0.01;
        const int64_t t0;
    };

    std::unique_ptr<Clock> doubleClock;
}

extern "C" {

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_nativeStop(JNIEnv *, jobject) {
    log_debug("nativeStop");
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) {
        log_warn("Expected algorithm to exist at this point");
        return;
    }
    algorithm->stop();
    std::atomic_store(&algorithmPtr, std::shared_ptr<AlgorithmModule>(nullptr));
    // dtor may be called here
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_configureVisualization(
        JNIEnv *, jobject,
        jint visuWidth, jint visuHeight) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) {
        // happens on external AR. TODO: hacky
        log_warn("no algorithm, ignoring configureVisualization");
        return;
    }
    log_debug("configureVisualization(%d, %d)", visuWidth, visuHeight);
    algorithm->setupRendering(visuWidth, visuHeight);
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_configure(
        JNIEnv *env, jobject,
        jlong timeNanos,
        jint width,
        jint height,
        jint textureId,
        jint frameStrideJint,
        jboolean recordExternalPosesJboolean,
        jstring moduleNameJava,
        jstring moduleSettingsJson) {

    std::atomic_store(&algorithmPtr, std::shared_ptr<AlgorithmModule>(nullptr));
    doubleClock = std::make_unique<Clock>(timeNanos);

    frameNumber = 0;
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

    auto ptr = AlgorithmModule::build(textureId, width, height, moduleName, settingsJsonPtr);
    std::atomic_store(&algorithmPtr, std::shared_ptr<AlgorithmModule>(std::move(ptr)));
}

JNIEXPORT jboolean JNICALL Java_org_example_viotester_AlgorithmWorker_processFrame(
        JNIEnv *, jobject,
        jlong timeNanos,
        jint cameraInd,
        jfloat fx,
        jfloat fy,
        jfloat px,
        jfloat py) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return false;

    if ((frameNumber++ % frameStride) != 0) return false;

    AlgorithmModule::CameraIntrinsics cam {
        .cameraIndex = cameraInd,
        .focalLengthX = fx,
        .focalLengthY = fy,
        .principalPointX = px,
        .principalPointY = py
    };
    algorithm->addFrame(doubleClock->convert(timeNanos), cam);
    return true;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGyroSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;

    algorithm->addGyro(doubleClock->convert(timeNanos), { x, y, z });
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processAccSample(
        JNIEnv*, jobject,
        jlong timeNanos, jfloat x, jfloat y, jfloat z) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    algorithm->addAcc(doubleClock->convert(timeNanos), { x, y, z });
}

JNIEXPORT jstring JNICALL Java_org_example_viotester_AlgorithmWorker_getStatsString(
        JNIEnv *env, jobject) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (algorithm) return env->NewStringUTF(algorithm->status().c_str());
    return nullptr;
}

JNIEXPORT jint JNICALL Java_org_example_viotester_AlgorithmWorker_getTrackingStatus(
        JNIEnv *, jobject) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (algorithm) return algorithm->trackingStatus();
    return -1;
}

JNIEXPORT jdoubleArray JNICALL Java_org_example_viotester_AlgorithmWorker_getPose(
        JNIEnv *env, jobject) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (algorithm) {
        recorder::Pose pose;
        bool hasPose = algorithm->pose(pose);
        if (!hasPose) {
            return NULL;
        }
        jdoubleArray result = env->NewDoubleArray(8);
        jdouble buf[8];
        buf[0] = pose.time;
        buf[1] = pose.position.x;
        buf[2] = pose.position.y;
        buf[3] = pose.position.z;
        buf[4] = pose.orientation.x;
        buf[5] = pose.orientation.y;
        buf[6] = pose.orientation.z;
        buf[7] = pose.orientation.w;
        env->SetDoubleArrayRegion(result, 0, 8, buf);
        return result;
    }
    return NULL;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_drawVisualization(JNIEnv *, jobject, jlong timeNanos) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    algorithm->render(doubleClock->convert(timeNanos));
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGpsLocation(JNIEnv *, jobject, jlong timeNanos, jdouble lat, jdouble lon, jdouble alt, jfloat acc) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    algorithm->addGps(doubleClock->convert(timeNanos), AlgorithmModule::Gps {
            .latitude = lat,
            .longitude = lon,
            .altitude = alt,
            .accuracy = acc
    });
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processGpsTime(JNIEnv *, jobject, jlong timeNanos, jdouble gpsTimeUtcSeconds) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    const double t = doubleClock->convert(timeNanos);
    algorithm->addJsonData(
            AlgorithmModule::json {
                    { "time", t },
                    { "gpsTime",
                              {
                                      { "utcSeconds", gpsTimeUtcSeconds }
                              }
                    }
            });
}

JNIEXPORT jdouble JNICALL Java_org_example_viotester_AlgorithmWorker_convertTime(JNIEnv *, jobject, jlong timeNanos) {
    return doubleClock->convert(timeNanos);
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_writeInfoFile(
        JNIEnv* env, jobject,
        jstring mode, jstring device) {
    auto algorithm = std::atomic_load(&algorithmPtr);
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
    std::string paramsFile = !settingsJson.at("parametersFileName").is_null() ? settingsJson.at("parametersFileName") : "";
    if (paramsFile.empty())
        return;
    std::ofstream fileOutput(paramsFile);
    fileOutput << "imuToCameraMatrix -0,-1,0,-1,0,0,0,0,-1;" << std::endl;
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_processExternalImage(JNIEnv *, jobject,
        jlong timeNs, jlong frameNumber, jint cameraInd, jfloat fx, jfloat fy, jfloat ppx, jfloat ppy) {
    (void)frameNumber;
    AlgorithmModule::CameraIntrinsics cam {
            .cameraIndex = cameraInd,
            .focalLengthX = fx,
            .focalLengthY = fy,
            .principalPointX = ppx,
            .principalPointY = ppy
    };
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    algorithm->addFrame(doubleClock->convert(timeNs), cam);
}

JNIEXPORT void JNICALL Java_org_example_viotester_AlgorithmWorker_recordPoseMatrix(JNIEnv *env, jobject, jlong timeNs, jfloatArray pose, jstring tag) {
    auto algorithm = std::atomic_load(&algorithmPtr);
    if (!algorithm) return;
    if (!recordExternalPoses) return;

    auto *poseArr = env->GetFloatArrayElements(pose, nullptr);
    Eigen::Matrix4f viewMatrix = Eigen::Map<Eigen::Matrix4f>(poseArr);
    env->ReleaseFloatArrayElements(pose, poseArr, JNI_ABORT);

    const double t = doubleClock->convert(timeNs);

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
