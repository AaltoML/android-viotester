#include <cassert>
#include <jni.h>
#include "logging.hpp"
#include "jniutil.hpp"
#include "native_camera_session.hpp"

#define MY_JNI_FUNC(ret, x) JNIEXPORT ret JNICALL Java_org_example_viotester_CameraWorker_ ## x

namespace {
    std::unique_ptr<NativeCameraSession> cameraSession;
}

extern "C" {
MY_JNI_FUNC(void, stopCameraSession)(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    cameraSession.reset();
}

MY_JNI_FUNC(void, startCameraSession)(JNIEnv *env, jobject thiz, jstring cameraId, jint targetFps, jobject surface) {
    (void)thiz;
    cameraSession.reset();
    cameraSession = NativeCameraSession::create(getStringOrEmpty(env, cameraId), targetFps);
    assert(cameraSession);
    cameraSession->initCameraSurface(env, surface);
}
}