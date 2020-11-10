#ifndef NATIVE_CAMERA_SESSION_HPP
#define NATIVE_CAMERA_SESSION_HPP

#include <jni.h>
#include <memory>

struct NativeCameraSession {
    static std::unique_ptr<NativeCameraSession> create(std::string cameraId, int targetFps);
    virtual ~NativeCameraSession();
    virtual void initCameraSurface(JNIEnv* env, jobject surface) = 0;
};

#endif